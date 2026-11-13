#include "Components/NPCNeedsComponent.h"
#include "Components/NPCStateComponent.h"
#include "Characters/NPCCharacter.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UNPCNeedsComponent::UNPCNeedsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UNPCNeedsComponent::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	// Run simulation on server
	if (World && GetOwner() && GetOwner()->HasAuthority())
	{
		World->GetTimerManager().SetTimer(NeedsUpdateTimerHandle, this, &UNPCNeedsComponent::UpdateNeeds, 0.5f, true);
	}
}

void UNPCNeedsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(NeedsUpdateTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UNPCNeedsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UNPCNeedsComponent, Needs);
}

void UNPCNeedsComponent::ConsumeFood(float NutritionalValue, bool bCanInduceFoodComa)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	Needs.Hunger = FMath::Clamp(Needs.Hunger - NutritionalValue, 0.0f, 100.0f);
	Needs.BowelUrgency = FMath::Clamp(Needs.BowelUrgency + (NutritionalValue * FoodToBowelRatio), 0.0f, 100.0f);

	if (bCanInduceFoodComa && NutritionalValue >= 40.0f)
	{
		Needs.Sleepiness = FMath::Clamp(Needs.Sleepiness + 35.0f, 0.0f, 100.0f);
		OnFoodComaEntered.Broadcast();
	}
}

void UNPCNeedsComponent::ConsumeDrink(float HydrationAmount, bool bIsAlcohol)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	Needs.Thirst = FMath::Clamp(Needs.Thirst - HydrationAmount, 0.0f, 100.0f);

	const float BladderMultiplier = bIsAlcohol ? (DrinkToBladderRatio * 1.8f) : DrinkToBladderRatio;
	Needs.BladderUrgency = FMath::Clamp(Needs.BladderUrgency + (HydrationAmount * BladderMultiplier), 0.0f, 100.0f);

	AActor* OwnerActor = GetOwner();
	if (OwnerActor)
	{
		UNPCStateComponent* StateComp = OwnerActor->FindComponentByClass<UNPCStateComponent>();
		if (StateComp)
		{
			StateComp->Hydrate(HydrationAmount * 0.8f);
			if (bIsAlcohol)
			{
				StateComp->IngestAlcohol(HydrationAmount * 0.4f);
			}
		}
	}
}

void UNPCNeedsComponent::RelieveBladder()
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	Needs.BladderUrgency = 0.0f;
	bBladderUrgentFired = false;
}

void UNPCNeedsComponent::RelieveBowel()
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	Needs.BowelUrgency = 0.0f;
	bBowelUrgentFired = false;
}

void UNPCNeedsComponent::Rest(float RestAmount)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	Needs.Sleepiness = FMath::Clamp(Needs.Sleepiness - RestAmount, 0.0f, 100.0f);
	bSleepinessUrgentFired = false;

	// Sleeping anywhere costs hunger level increase!
	Needs.Hunger = FMath::Clamp(Needs.Hunger + (RestAmount * RestHungerCostRatio), 0.0f, 100.0f);

	AActor* OwnerActor = GetOwner();
	if (OwnerActor)
	{
		UNPCStateComponent* StateComp = OwnerActor->FindComponentByClass<UNPCStateComponent>();
		if (StateComp)
		{
			StateComp->ModifyPower(RestAmount * 0.75f);
		}
	}
}

bool UNPCNeedsComponent::HasUrgentNeed() const
{
	return Needs.Hunger >= UrgentThreshold
		|| Needs.Thirst >= UrgentThreshold
		|| Needs.BladderUrgency >= UrgentThreshold
		|| Needs.BowelUrgency >= UrgentThreshold
		|| Needs.Sleepiness >= UrgentThreshold;
}

FName UNPCNeedsComponent::GetDominantNeed() const
{
	float MaxVal = Needs.Hunger;
	FName Dominant = FName("Hunger");

	if (Needs.Thirst > MaxVal)
	{
		MaxVal = Needs.Thirst;
		Dominant = FName("Thirst");
	}
	if (Needs.BladderUrgency > MaxVal)
	{
		MaxVal = Needs.BladderUrgency;
		Dominant = FName("Bladder");
	}
	if (Needs.BowelUrgency > MaxVal)
	{
		MaxVal = Needs.BowelUrgency;
		Dominant = FName("Bowel");
	}
	if (Needs.Sleepiness > MaxVal)
	{
		MaxVal = Needs.Sleepiness;
		Dominant = FName("Sleepiness");
	}

	return Dominant;
}

void UNPCNeedsComponent::OnRep_Needs()
{
	// Client hook for HUD / overhead updates
}

void UNPCNeedsComponent::UpdateNeeds()
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	ANPCCharacter* NPCChar = Cast<ANPCCharacter>(GetOwner());
	const bool bIsSleepingOnFloor = NPCChar && NPCChar->IsSimulatingPhysics();
	const bool bIsRestingInZone = NPCChar && NPCChar->bIsRestingInSimulationZone;

	// Sleeping anywhere costs hunger level increase (accelerated metabolic expenditure)
	float EffectiveHungerRate = HungerRate;
	if (bIsSleepingOnFloor || bIsRestingInZone)
	{
		EffectiveHungerRate *= SleepingHungerMultiplier;
	}

	// 1. Natural accumulation over time (0.5s step)
	Needs.Hunger = FMath::Clamp(Needs.Hunger + (EffectiveHungerRate * 0.5f), 0.0f, 100.0f);
	Needs.Thirst = FMath::Clamp(Needs.Thirst + (ThirstRate * 0.5f), 0.0f, 100.0f);

	if (bIsSleepingOnFloor)
	{
		Needs.Sleepiness = FMath::Clamp(Needs.Sleepiness - (SleepinessRate * 2.0f * 0.5f), 0.0f, 100.0f);
	}
	else
	{
		Needs.Sleepiness = FMath::Clamp(Needs.Sleepiness + (SleepinessRate * 0.5f), 0.0f, 100.0f);
	}

	// 2. Check emergency soiled accidents
	if (Needs.BladderUrgency >= 100.0f)
	{
		OnToiletEmergencyFailed.Broadcast(true);
		Needs.BladderUrgency = 0.0f; // Empties on floor
		bBladderUrgentFired = false;
	}
	if (Needs.BowelUrgency >= 100.0f)
	{
		OnToiletEmergencyFailed.Broadcast(false);
		Needs.BowelUrgency = 0.0f; // Empties on floor
		bBowelUrgentFired = false;
	}

	// 3. Fire single-shot events when crossing urgent threshold
	if (Needs.Hunger >= UrgentThreshold && !bHungerUrgentFired)
	{
		bHungerUrgentFired = true;
		OnNeedUrgencyTriggered.Broadcast(FName("Hunger"), Needs.Hunger);
	}
	else if (Needs.Hunger < UrgentThreshold)
	{
		bHungerUrgentFired = false;
	}

	if (Needs.Thirst >= UrgentThreshold && !bThirstUrgentFired)
	{
		bThirstUrgentFired = true;
		OnNeedUrgencyTriggered.Broadcast(FName("Thirst"), Needs.Thirst);
	}
	else if (Needs.Thirst < UrgentThreshold)
	{
		bThirstUrgentFired = false;
	}

	if (Needs.BladderUrgency >= UrgentThreshold && !bBladderUrgentFired)
	{
		bBladderUrgentFired = true;
		OnNeedUrgencyTriggered.Broadcast(FName("Bladder"), Needs.BladderUrgency);
	}

	if (Needs.BowelUrgency >= UrgentThreshold && !bBowelUrgentFired)
	{
		bBowelUrgentFired = true;
		OnNeedUrgencyTriggered.Broadcast(FName("Bowel"), Needs.BowelUrgency);
	}

	if (Needs.Sleepiness >= UrgentThreshold && !bSleepinessUrgentFired)
	{
		bSleepinessUrgentFired = true;
		OnNeedUrgencyTriggered.Broadcast(FName("Sleepiness"), Needs.Sleepiness);
	}
	else if (Needs.Sleepiness < UrgentThreshold)
	{
		bSleepinessUrgentFired = false;
	}
}
