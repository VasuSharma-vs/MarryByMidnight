#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractablePropManager.h"
#include "Characters/NPCCharacter.h"
#include "Components/NPCPersonalityComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

UInteractableInstancedStaticMeshComponent::UInteractableInstancedStaticMeshComponent()
{
	SetIsReplicatedByDefault(true);
}

void UInteractableInstancedStaticMeshComponent::BeginPlay()
{
	Super::BeginPlay();

	SyncDatabaseWithInstanceCount();

	if (AInteractablePropManager* Manager = AInteractablePropManager::Get(this))
	{
		Manager->RegisterISMComponent(this);
	}
}

void UInteractableInstancedStaticMeshComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AInteractablePropManager* Manager = AInteractablePropManager::Get(this))
	{
		Manager->UnregisterISMComponent(this);
	}

	Super::EndPlay(EndPlayReason);
}

void UInteractableInstancedStaticMeshComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UInteractableInstancedStaticMeshComponent, PropDatabase);
}

void UInteractableInstancedStaticMeshComponent::OnRep_PropDatabase()
{
	// Client hook when prop database replicates
}

int32 UInteractableInstancedStaticMeshComponent::AddInteractableInstance(const FTransform& InstanceTransform, const FInteractablePropData& InitialData)
{
	const int32 NewIndex = AddInstance(InstanceTransform);
	if (NewIndex != INDEX_NONE)
	{
		FInteractablePropData DataCopy = InitialData;
		DataCopy.InstanceId = NewIndex;
		if (NewIndex < PropDatabase.Num())
		{
			PropDatabase[NewIndex] = DataCopy;
		}
		else
		{
			PropDatabase.Add(DataCopy);
		}
	}
	return NewIndex;
}

bool UInteractableInstancedStaticMeshComponent::RemoveInteractableInstance(int32 InstanceIndex)
{
	if (!IsValidInstance(InstanceIndex)) return false;

	const bool bRemoved = RemoveInstance(InstanceIndex);
	if (bRemoved && PropDatabase.IsValidIndex(InstanceIndex))
	{
		PropDatabase.RemoveAt(InstanceIndex);
		// Re-index remaining entries
		for (int32 i = InstanceIndex; i < PropDatabase.Num(); ++i)
		{
			PropDatabase[i].InstanceId = i;
		}
	}
	return bRemoved;
}

bool UInteractableInstancedStaticMeshComponent::GetPropDataForInstance(int32 InstanceIndex, FInteractablePropData& OutData) const
{
	if (PropDatabase.IsValidIndex(InstanceIndex))
	{
		OutData = PropDatabase[InstanceIndex];
		return true;
	}
	return false;
}

bool UInteractableInstancedStaticMeshComponent::SetPropDataForInstance(int32 InstanceIndex, const FInteractablePropData& NewData)
{
	if (PropDatabase.IsValidIndex(InstanceIndex))
	{
		PropDatabase[InstanceIndex] = NewData;
		return true;
	}
	return false;
}

bool UInteractableInstancedStaticMeshComponent::ConsumeInstancePortions(int32 InstanceIndex, int32 SipsToConsume, AActor* ConsumerActor, int32& OutSipsConsumed)
{
	OutSipsConsumed = 0;
	if (!PropDatabase.IsValidIndex(InstanceIndex)) return false;

	FInteractablePropData& Data = PropDatabase[InstanceIndex];
	if (Data.IsEmpty()) return false;

	OutSipsConsumed = Data.ConsumePortions(SipsToConsume);
	if (OutSipsConsumed <= 0) return false;

	const float Ratio = (Data.MaxPortions > 0) ? static_cast<float>(OutSipsConsumed) / static_cast<float>(Data.MaxPortions) : 1.0f;
	const bool bItemExpired = Data.IsExpired();

	if (ANPCCharacter* NPC = Cast<ANPCCharacter>(ConsumerActor))
	{
		if (bItemExpired)
		{
			// Expired: add sickness, subtract dopamine (depression), add stress
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::Sickness, Data.SicknessLevel * Ratio);
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::Dopamine, -Data.ExpiredDopaminePenalty * Ratio);
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::Stress, 25.0f * Ratio);
		}
		else
		{
			// Temperature closeness check
			const float TempDiff = FMath::Abs(Data.CurrentTemperature - Data.ConsumableTemperature);
			if (TempDiff <= Data.TemperatureTolerance)
			{
				const float Closeness = 1.0f - (TempDiff / FMath::Max(0.1f, Data.TemperatureTolerance));
				const float Bonus = Data.TemperatureDopamineBonus * Closeness * Ratio;
				NPC->ApplySimulationStatDelta(ENPCSimulationStat::Dopamine, Bonus);
			}

			// Stimuli modifiers
			for (const auto& Mod : Data.StimuliPerPortion)
			{
				NPC->ApplySimulationStatDelta(Mod.Stat, Mod.ChangeRate * Ratio);
			}

			// Energy boost
			if (Data.EnergyLevel > 0.0f)
			{
				NPC->ApplySimulationStatDelta(ENPCSimulationStat::Power, Data.EnergyLevel * Ratio);
			}

			// Taste compatibility
			if (NPC->PersonalityComponent)
			{
				const float Compatibility = NPC->PersonalityComponent->EvaluateTaste(Data.TasteProfile);
				if (Compatibility >= 0.65f)
				{
					NPC->ApplySimulationStatDelta(ENPCSimulationStat::Dopamine, 10.0f * Compatibility * Ratio);
				}
				else if (Compatibility <= 0.30f)
				{
					NPC->ApplySimulationStatDelta(ENPCSimulationStat::Stress, 8.0f * (1.0f - Compatibility) * Ratio);
				}
			}
		}

		// Body temperature effect
		if (Data.BodyTemperatureEffect != 0.0f)
		{
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::BodyTemperature, Data.BodyTemperatureEffect * Ratio);
		}
	}

	OnInstancePortionConsumed.Broadcast(InstanceIndex, Data.RemainingPortions, ConsumerActor);
	return true;
}

void UInteractableInstancedStaticMeshComponent::UpdateInstanceTemperature(int32 InstanceIndex, float AmbientTemperature, float DeltaSeconds)
{
	if (PropDatabase.IsValidIndex(InstanceIndex))
	{
		PropDatabase[InstanceIndex].UpdateTemperature(AmbientTemperature, DeltaSeconds);
	}
}

void UInteractableInstancedStaticMeshComponent::SetInstanceSimulationState(int32 InstanceIndex, EPhysicsSimulationState NewState)
{
	if (PropDatabase.IsValidIndex(InstanceIndex))
	{
		PropDatabase[InstanceIndex].SimulationState = NewState;
	}
}

void UInteractableInstancedStaticMeshComponent::SetInstanceGrabbable(int32 InstanceIndex, bool bNewGrabbable)
{
	if (PropDatabase.IsValidIndex(InstanceIndex))
	{
		PropDatabase[InstanceIndex].bIsGrabbable = bNewGrabbable;
	}
}

void UInteractableInstancedStaticMeshComponent::SyncDatabaseWithInstanceCount()
{
	const int32 NumInstances = GetInstanceCount();
	if (PropDatabase.Num() < NumInstances)
	{
		const int32 PrevNum = PropDatabase.Num();
		PropDatabase.SetNum(NumInstances);
		for (int32 i = PrevNum; i < NumInstances; ++i)
		{
			if (DefaultDataAsset)
			{
				PropDatabase[i] = CreatePropDataFromAsset(DefaultDataAsset, i);
			}
			else
			{
				PropDatabase[i].InstanceId = i;
			}
		}
	}
}

FInteractablePropData UInteractableInstancedStaticMeshComponent::CreatePropDataFromAsset(const UInteractablePropDataAsset* InAsset, int32 InInstanceId) const
{
	FInteractablePropData NewData;
	NewData.InstanceId = InInstanceId;

	if (InAsset)
	{
		NewData.ItemDisplayName = InAsset->ItemDisplayName;
		NewData.ConsumableType = InAsset->ConsumableType;
		NewData.StaticMesh = InAsset->StaticMesh;
		NewData.DataAsset = const_cast<UInteractablePropDataAsset*>(InAsset);
		NewData.bEnableTemperature = InAsset->bEnableTemperature;
		NewData.ConsumableTemperature = InAsset->ConsumableTemperature;
		NewData.CurrentTemperature = InAsset->ConsumableTemperature;
		NewData.TemperatureTolerance = InAsset->TemperatureTolerance;
		NewData.TemperatureDopamineBonus = InAsset->TemperatureDopamineBonus;
		NewData.BodyTemperatureEffect = InAsset->BodyTemperatureEffect;
		NewData.ThermalExchangeRate = InAsset->ThermalExchangeRate;
		NewData.Price = InAsset->Price;
		NewData.MaxPortions = InAsset->MaxPortions;
		NewData.RemainingPortions = InAsset->MaxPortions;
		NewData.QuantityLevel = 1.0f;
		NewData.EnergyLevel = InAsset->EnergyLevel;
		NewData.bHasExpiry = InAsset->bHasExpiry;
		NewData.ExpiryLifetimeMinutes = InAsset->ExpiryLifetimeMinutes;
		NewData.SicknessLevel = InAsset->SicknessLevel;
		NewData.ExpiredDopaminePenalty = InAsset->ExpiredDopaminePenalty;
		NewData.StimuliPerPortion = InAsset->StimuliPerPortion;
		NewData.TasteProfile = InAsset->BuildTasteVector(false);
	}

	return NewData;
}
