#include "Components/NPCMentalStateComponent.h"
#include "Components/NPCStateComponent.h"
#include "Components/NPCNeedsComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UNPCMentalStateComponent::UNPCMentalStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UNPCMentalStateComponent::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	// Run simulation on server
	if (World && GetOwner() && GetOwner()->HasAuthority())
	{
		World->GetTimerManager().SetTimer(MentalTickTimerHandle, this, &UNPCMentalStateComponent::MentalTick, 1.0f, true);
		PreviousDopamine = MentalState.Dopamine;
		RecalculateAffect();
	}
}

void UNPCMentalStateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(MentalTickTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UNPCMentalStateComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UNPCMentalStateComponent, MentalState);
	DOREPLIFETIME(UNPCMentalStateComponent, CurrentMoodTier);
}

void UNPCMentalStateComponent::GrantDopamine(float Amount)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	MentalState.Dopamine = FMath::Clamp(MentalState.Dopamine + Amount, -100.0f, 100.0f);
	RecalculateAffect();
}

void UNPCMentalStateComponent::AddStress(float Amount)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	MentalState.Stress = FMath::Clamp(MentalState.Stress + Amount, 0.0f, 100.0f);
	RecalculateAffect();
}

void UNPCMentalStateComponent::RelieveStress(float Amount)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	MentalState.Stress = FMath::Clamp(MentalState.Stress - Amount, 0.0f, 100.0f);
	RecalculateAffect();
}

void UNPCMentalStateComponent::TriggerDopamineCrash(float Severity)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	MentalState.DopamineCrash = FMath::Clamp(MentalState.DopamineCrash + Severity, 0.0f, 100.0f);
	MentalState.Dopamine = FMath::Clamp(MentalState.Dopamine - Severity * 0.7f, -100.0f, 100.0f);
	AddStress(Severity * 0.5f);
	OnDopamineCrash.Broadcast(Severity);
	RecalculateAffect();
}

void UNPCMentalStateComponent::OnRep_MentalState()
{
	// Client hook
}

void UNPCMentalStateComponent::OnRep_CurrentMoodTier()
{
	OnMoodTierChanged.Broadcast(CurrentMoodTier, PreviousTierClient);
	if (CurrentMoodTier == ENPCMoodTier::Enraged)
	{
		OnNPCEnraged.Broadcast();
	}
	PreviousTierClient = CurrentMoodTier;
}

void UNPCMentalStateComponent::MentalTick()
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	// 1. Dopamine natural baseline decay toward 50.0
	if (MentalState.Dopamine > 50.0f)
	{
		MentalState.Dopamine = FMath::Max(50.0f, MentalState.Dopamine - (DopamineDecayRate * 1.0f));
	}
	else if (MentalState.Dopamine < 50.0f)
	{
		MentalState.Dopamine = FMath::Min(50.0f, MentalState.Dopamine + (DopamineDecayRate * 0.5f));
	}

	// 2. Check for sudden dopamine drop crash
	if (PreviousDopamine >= 80.0f && MentalState.Dopamine < 55.0f)
	{
		TriggerDopamineCrash(PreviousDopamine - MentalState.Dopamine);
	}
	PreviousDopamine = MentalState.Dopamine;

	// 3. Dopamine crash recovery
	if (MentalState.DopamineCrash > 0.0f)
	{
		MentalState.DopamineCrash = FMath::Max(0.0f, MentalState.DopamineCrash - 0.5f);
	}

	// 4. Stress decay
	if (MentalState.Stress > 0.0f)
	{
		MentalState.Stress = FMath::Max(0.0f, MentalState.Stress - (StressRecoveryRate * 1.0f));
	}

	// 5. Update Valence & Arousal
	RecalculateAffect();
}

void UNPCMentalStateComponent::RecalculateAffect()
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	float SicknessVal = 0.0f;
	float PowerVal = 75.0f;
	float SubstanceVal = 0.0f;
	float TempDeviation = 0.0f;

	float HungerVal = 20.0f;
	float ThirstVal = 20.0f;
	float SleepinessVal = 10.0f;
	float ComfortVal = 80.0f;

	AActor* OwnerActor = GetOwner();
	if (OwnerActor)
	{
		UNPCStateComponent* StateComp = OwnerActor->FindComponentByClass<UNPCStateComponent>();
		if (StateComp)
		{
			SicknessVal = StateComp->PhysicalState.Sickness;
			PowerVal = StateComp->PhysicalState.Power;
			SubstanceVal = StateComp->PhysicalState.AlcoholLevel + StateComp->PhysicalState.DrugLevel;
			TempDeviation = FMath::Abs(StateComp->PhysicalState.BodyTemperature - 37.0f);
		}

		UNPCNeedsComponent* NeedsComp = OwnerActor->FindComponentByClass<UNPCNeedsComponent>();
		if (NeedsComp)
		{
			HungerVal = NeedsComp->Needs.Hunger;
			ThirstVal = NeedsComp->Needs.Thirst;
			SleepinessVal = NeedsComp->Needs.Sleepiness;
			ComfortVal = NeedsComp->Needs.Comfort;
		}
	}

	// Non-linear penalties for extreme hunger and thirst (> 70)
	const float HungerPenalty = (HungerVal > 70.0f) ? (HungerVal - 70.0f) * 1.5f : 0.0f;
	const float ThirstPenalty = (ThirstVal > 70.0f) ? (ThirstVal - 70.0f) * 1.8f : 0.0f;

	// Calculate Valence (0 = Miserable, 100 = Ecstatic)
	const float RawValence = 50.0f 
		+ (0.35f * MentalState.Dopamine)
		- (0.35f * MentalState.Stress)
		- (0.25f * SicknessVal)
		- (0.25f * MentalState.DopamineCrash)
		- (0.20f * HungerPenalty)
		- (0.25f * ThirstPenalty)
		+ (0.15f * (ComfortVal - 50.0f));

	MentalState.Valence = FMath::Clamp(RawValence, 0.0f, 100.0f);

	// Calculate Arousal (0 = Calm / Depleted, 100 = Hyper / Agitated Energy)
	const float RawArousal = (0.35f * PowerVal)
		+ (0.45f * MentalState.Stress)
		+ (0.25f * SubstanceVal)
		- (0.35f * SleepinessVal)
		+ (TempDeviation * 3.0f);

	MentalState.Arousal = FMath::Clamp(RawArousal, 0.0f, 100.0f);

	// Evaluate discrete tier
	const ENPCMoodTier NewTier = EvaluateMoodTier(MentalState.Valence, MentalState.Arousal);
	if (NewTier != CurrentMoodTier)
	{
		const ENPCMoodTier OldTier = CurrentMoodTier;
		CurrentMoodTier = NewTier;
		OnMoodTierChanged.Broadcast(NewTier, OldTier);

		if (NewTier == ENPCMoodTier::Enraged)
		{
			OnNPCEnraged.Broadcast();
		}
	}
}

ENPCMoodTier UNPCMentalStateComponent::EvaluateMoodTier(float Valence, float Arousal)
{
	if (Valence < 35.0f && Arousal >= 65.0f)
	{
		return ENPCMoodTier::Enraged;
	}

	if (Valence >= 85.0f)
	{
		return ENPCMoodTier::Ecstatic;
	}
	if (Valence >= 70.0f)
	{
		return ENPCMoodTier::Happy;
	}
	if (Valence >= 55.0f)
	{
		return ENPCMoodTier::Content;
	}
	if (Valence >= 40.0f)
	{
		return ENPCMoodTier::Neutral;
	}
	if (Valence >= 25.0f)
	{
		return ENPCMoodTier::Discontent;
	}

	return ENPCMoodTier::Frustrated;
}
