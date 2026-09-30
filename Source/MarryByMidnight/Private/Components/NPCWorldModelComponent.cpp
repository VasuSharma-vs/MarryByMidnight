#include "Components/NPCWorldModelComponent.h"
#include "Subsystems/NPCKnowledgeSubsystem.h"
#include "Environment/NPCOperableObject.h"
#include "Props/NPCConsumableProp.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"


UNPCWorldModelComponent::UNPCWorldModelComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UNPCWorldModelComponent::RecordDiscovery(FName ObjectId, FGameplayTag InputTag, FGameplayTag OutputTag, FVector Location)
{
	FDiscoveredObjectInfo& Info = DiscoveredObjects.FindOrAdd(ObjectId);
	Info.ObjectId = ObjectId;
	if (InputTag.IsValid()) Info.KnownInputs.AddTag(InputTag);
	if (OutputTag.IsValid()) Info.KnownOutputs.AddTag(OutputTag);
	Info.DiscoveredLocations.AddUnique(Location);
	Info.TimesInteracted++;

	OnNewObjectDiscovered.Broadcast(ObjectId, InputTag, Location);

	// Notify global cultural subsystem
	UWorld* World = GetWorld();
	if (World)
	{
		UNPCKnowledgeSubsystem* Subsystem = World->GetSubsystem<UNPCKnowledgeSubsystem>();
		if (Subsystem)
		{
			Subsystem->RecordDiscovery(ObjectId, InputTag, OutputTag, Location, GetOwner());
		}
	}
}

bool UNPCWorldModelComponent::IsObjectKnown(FName ObjectId) const
{
	return DiscoveredObjects.Contains(ObjectId);
}

bool UNPCWorldModelComponent::GetKnownLocations(FName ObjectId, TArray<FVector>& OutLocations) const
{
	const FDiscoveredObjectInfo* Found = DiscoveredObjects.Find(ObjectId);
	if (Found && Found->DiscoveredLocations.Num() > 0)
	{
		OutLocations = Found->DiscoveredLocations;
		return true;
	}
	return false;
}

void UNPCWorldModelComponent::RecordPlaceEffect(FName PlaceId, ENPCSimulationStat Stat, float ChangeRate, FVector Location)
{
	if (PlaceId.IsNone())
	{
		PlaceId = FName(*FString::Printf(TEXT("Place_%s"), *UEnum::GetDisplayValueAsText(Stat).ToString()));
	}

	FDiscoveredPlaceInfo& Place = DiscoveredPlaces.FindOrAdd(PlaceId);
	Place.PlaceId = PlaceId;
	Place.AffectedStat = Stat;
	Place.ChangeRate = ChangeRate;
	Place.Location = Location;
	Place.VisitCount++;
}

bool UNPCWorldModelComponent::FindBestKnownPlaceForStat(ENPCSimulationStat Stat, bool bSeekingIncrease, FVector& OutLocation) const
{
	const FDiscoveredPlaceInfo* BestPlace = nullptr;
	float BestRate = 0.0f;

	for (const auto& Pair : DiscoveredPlaces)
	{
		const FDiscoveredPlaceInfo& Place = Pair.Value;
		if (Place.AffectedStat == Stat)
		{
			if (bSeekingIncrease && Place.ChangeRate > 0.0f)
			{
				if (!BestPlace || Place.ChangeRate > BestRate)
				{
					BestRate = Place.ChangeRate;
					BestPlace = &Place;
				}
			}
			else if (!bSeekingIncrease && Place.ChangeRate < 0.0f)
			{
				if (!BestPlace || Place.ChangeRate < BestRate)
				{
					BestRate = Place.ChangeRate;
					BestPlace = &Place;
				}
			}
		}
	}

	if (BestPlace)
	{
		OutLocation = BestPlace->Location;
		return true;
	}

	return false;
}

void UNPCWorldModelComponent::RememberOperableMachine(ANPCOperableObject* Machine)
{
	if (!Machine) return;

	const FName MachineId = Machine->GetFName();
	FDiscoveredMachineMemory& Memory = RememberedMachines.FindOrAdd(MachineId);
	Memory.MachineId = MachineId;
	Memory.DisplayName = Machine->MachineDisplayName;
	Memory.WorldLocation = Machine->GetActorLocation();
	Memory.MachineActor = Machine;
	Memory.KnownOfferings = Machine->AvailableOfferings;

	UWorld* World = GetWorld();
	Memory.LastSeenTime = World ? World->GetTimeSeconds() : 0.0f;
}

void UNPCWorldModelComponent::RememberConsumableProp(ANPCConsumableProp* Prop)
{
	if (!Prop) return;

	const FName PropId = Prop->GetFName();
	FDiscoveredPropMemory& Memory = RememberedProps.FindOrAdd(PropId);
	Memory.PropId = PropId;
	Memory.ItemName = Prop->ItemDisplayName;
	Memory.ConsumableType = Prop->ConsumableType;
	Memory.WorldLocation = Prop->GetActorLocation();
	Memory.PropActor = Prop;
	Memory.Price = Prop->Price;
	Memory.QuantityLevel = Prop->QuantityLevel;
	Memory.RemainingPortions = Prop->RemainingPortions;
	Memory.ExpiryFreshness = Prop->ExpiryFreshness;
	Memory.KnownStimuli = Prop->StimuliPerPortion;

	UWorld* World = GetWorld();
	Memory.LastSeenTime = World ? World->GetTimeSeconds() : 0.0f;
}

bool UNPCWorldModelComponent::FindBestKnownSourceForStat(
	ENPCSimulationStat Stat,
	bool bSeekingIncrease,
	float AvailableCash,
	FVector& OutLocation,
	AActor*& OutTargetActor,
	FName& OutOfferingId) const
{
	float BestUtilityScore = -1.0f;
	bool bFound = false;

	AActor* OwnerActor = GetOwner();
	const FVector MyLoc = OwnerActor ? OwnerActor->GetActorLocation() : FVector::ZeroVector;

	// 1. Search Remembered Props (e.g. food or drinks in the world)
	for (const auto& Pair : RememberedProps)
	{
		const FDiscoveredPropMemory& Mem = Pair.Value;
		if (!Mem.PropActor.IsValid()) continue;

		ANPCConsumableProp* Prop = Cast<ANPCConsumableProp>(Mem.PropActor.Get());
		if (!Prop || Prop->IsEmpty()) continue;

		// Can we afford it?
		if (Mem.Price > AvailableCash) continue;

		for (const auto& Mod : Mem.KnownStimuli)
		{
			if (Mod.Stat == Stat)
			{
				const bool bFavorable = bSeekingIncrease ? (Mod.ChangeRate > 0.0f) : (Mod.ChangeRate < 0.0f);
				if (bFavorable)
				{
					const float Magnitude = FMath::Abs(Mod.ChangeRate);
					const float Dist = FVector::Dist(MyLoc, Mem.WorldLocation);
					// Score: High magnitude, penalized by distance and price
					const float Score = (Magnitude * 2.0f) - (Dist * 0.005f) - (Mem.Price * 3.0f);

					if (Score > BestUtilityScore)
					{
						BestUtilityScore = Score;
						OutLocation = Mem.WorldLocation;
						OutTargetActor = Prop;
						OutOfferingId = FName("ConsumePortion");
						bFound = true;
					}
				}
			}
		}
	}

	// 2. Search Remembered Machines (e.g. Vending Machines, Food Counters, Massage Sofas)
	for (const auto& Pair : RememberedMachines)
	{
		const FDiscoveredMachineMemory& Mem = Pair.Value;
		if (!Mem.MachineActor.IsValid()) continue;

		ANPCOperableObject* Machine = Cast<ANPCOperableObject>(Mem.MachineActor.Get());
		if (!Machine) continue;

		for (const auto& Offering : Mem.KnownOfferings)
		{
			if (Offering.MoneyCost > AvailableCash) continue;

			for (const auto& Mod : Offering.OfferedStimuli)
			{
				if (Mod.Stat == Stat)
				{
					const bool bFavorable = bSeekingIncrease ? (Mod.ChangeRate > 0.0f) : (Mod.ChangeRate < 0.0f);
					if (bFavorable)
					{
						const float Magnitude = FMath::Abs(Mod.ChangeRate);
						const float Dist = FVector::Dist(MyLoc, Mem.WorldLocation);
						const float Score = (Magnitude * 2.0f) - (Dist * 0.005f) - (Offering.MoneyCost * 3.0f);

						if (Score > BestUtilityScore)
						{
							BestUtilityScore = Score;
							OutLocation = Mem.WorldLocation;
							OutTargetActor = Machine;
							OutOfferingId = Offering.OfferingId;
							bFound = true;
						}
					}
				}
			}
		}
	}

	return bFound;
}

bool UNPCWorldModelComponent::FindOpportunisticScavengeProp(
	float AvailableCash,
	FVector& OutLocation,
	ANPCConsumableProp*& OutProp) const
{
	for (const auto& Pair : RememberedProps)
	{
		const FDiscoveredPropMemory& Mem = Pair.Value;
		if (!Mem.PropActor.IsValid()) continue;

		ANPCConsumableProp* Prop = Cast<ANPCConsumableProp>(Mem.PropActor.Get());
		if (!Prop || Prop->IsEmpty()) continue;

		if (Mem.Price > AvailableCash) continue;

		// Check if half full or near getting low/expiring
		const bool bGettingLow = Mem.QuantityLevel <= 0.65f || Mem.RemainingPortions <= 2;
		const bool bExpiringSoon = Mem.ExpiryFreshness <= 0.60f && Mem.ExpiryFreshness > 0.25f;

		if (bGettingLow || bExpiringSoon)
		{
			OutLocation = Mem.WorldLocation;
			OutProp = Prop;
			return true;
		}
	}

	return false;
}

