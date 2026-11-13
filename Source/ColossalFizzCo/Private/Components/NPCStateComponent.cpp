#include "Components/NPCStateComponent.h"
#include "Characters/NPCCharacter.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UNPCStateComponent::UNPCStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UNPCStateComponent::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	// Only run simulation on authoritative server
	if (World && GetOwner() && GetOwner()->HasAuthority())
	{
		// 10 Hz (every 0.1s): Fast survival and power calculations
		World->GetTimerManager().SetTimer(FastUpdateTimerHandle, this, &UNPCStateComponent::FastTickUpdate, 0.1f, true);

		// 2 Hz (every 0.5s): Thermal equilibration and metabolic substance burns
		World->GetTimerManager().SetTimer(SlowUpdateTimerHandle, this, &UNPCStateComponent::SlowTickUpdate, 0.5f, true);
	}
}

void UNPCStateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(FastUpdateTimerHandle);
		World->GetTimerManager().ClearTimer(SlowUpdateTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UNPCStateComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UNPCStateComponent, PhysicalState);
	DOREPLIFETIME(UNPCStateComponent, CurrentDizzinessTier);
	DOREPLIFETIME(UNPCStateComponent, bWasCritical);
}

void UNPCStateComponent::ModifyPower(float DeltaAmount)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	PhysicalState.Power = FMath::Clamp(PhysicalState.Power + DeltaAmount, 0.0f, 100.0f);
	RecalculateDizziness();
	EvaluateCriticalState();
}

void UNPCStateComponent::SetPowerChangeRate(float NewRate)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;
	PhysicalState.PowerChangeRate = NewRate;
}

void UNPCStateComponent::ConsumeWater(float Amount)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	PhysicalState.Water = FMath::Clamp(PhysicalState.Water - Amount, 0.0f, 100.0f);
	RecalculateDizziness();
	EvaluateCriticalState();
}

void UNPCStateComponent::Hydrate(float Amount, float TemperatureDelta)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	PhysicalState.Water = FMath::Clamp(PhysicalState.Water + Amount, 0.0f, 100.0f);
	if (!FMath::IsNearlyZero(TemperatureDelta))
	{
		PhysicalState.BodyTemperature = FMath::Clamp(PhysicalState.BodyTemperature + TemperatureDelta, 30.0f, 43.0f);
	}
	RecalculateDizziness();
	EvaluateCriticalState();
}

void UNPCStateComponent::IngestAlcohol(float Amount)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	PhysicalState.AlcoholLevel = FMath::Clamp(PhysicalState.AlcoholLevel + Amount, 0.0f, 100.0f);
	RecalculateDizziness();
}

void UNPCStateComponent::IngestDrug(float Amount)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	PhysicalState.DrugLevel = FMath::Clamp(PhysicalState.DrugLevel + Amount, 0.0f, 100.0f);
	RecalculateDizziness();
}

void UNPCStateComponent::InflictSickness(float Amount)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	PhysicalState.Sickness = FMath::Clamp(PhysicalState.Sickness + Amount, 0.0f, 100.0f);
	RecalculateDizziness();
	EvaluateCriticalState();
}

void UNPCStateComponent::HealSickness(float Amount)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	PhysicalState.Sickness = FMath::Clamp(PhysicalState.Sickness - Amount, 0.0f, 100.0f);
	RecalculateDizziness();
	EvaluateCriticalState();
}

void UNPCStateComponent::SetAmbientTemperature(float NewAmbientTemp)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;
	PhysicalState.AmbientTemperature = NewAmbientTemp;
}

bool UNPCStateComponent::IsInCriticalCondition() const
{
	return PhysicalState.Power <= 0.0f 
		|| PhysicalState.Water <= 0.0f 
		|| PhysicalState.BodyTemperature <= 32.0f 
		|| PhysicalState.BodyTemperature >= 42.0f 
		|| PhysicalState.Sickness >= 100.0f;
}

void UNPCStateComponent::OnRep_PhysicalState()
{
	// Client-side visual and state updates
	EvaluateCriticalState();
}

void UNPCStateComponent::OnRep_DizzinessTier()
{
	OnDizzinessTierChanged.Broadcast(CurrentDizzinessTier);
	if (CurrentDizzinessTier == EDizzinessTier::PassedOut)
	{
		OnNPCPassedOut.Broadcast();
	}
}

void UNPCStateComponent::FastTickUpdate()
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	// 1. Update power based on movement exertion, baseline rate, and physics simulation resting
	float PowerDelta = PhysicalState.PowerChangeRate * 0.1f;
	ANPCCharacter* NPCChar = Cast<ANPCCharacter>(GetOwner());

	if (NPCChar && NPCChar->IsSimulatingPhysics())
	{
		// NPC is collapsed/resting on the floor: gradually restore power (+3.0 power/sec)
		PowerDelta = 0.30f;
		if (PhysicalState.Power >= 35.0f)
		{
			NPCChar->StopPhysicsSimulation();
		}
	}
	else if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		const float MovementSpeed = OwnerChar->GetVelocity().Size2D();
		if (MovementSpeed > 10.0f)
		{
			// Walking/running drains power proportionally to speed (~0.6 power/sec at 300 walk speed)
			const float ExertionDrain = (MovementSpeed / 300.0f) * 0.6f * 0.1f;
			PowerDelta -= ExertionDrain;
		}
	}

	if (!FMath::IsNearlyZero(PowerDelta))
	{
		PhysicalState.Power = FMath::Clamp(PhysicalState.Power + PowerDelta, 0.0f, 100.0f);
	}

	// Trigger physical collapse if power hits 0
	if (PhysicalState.Power <= 0.0f && NPCChar && !NPCChar->IsSimulatingPhysics())
	{
		NPCChar->StartPhysicsSimulation();
	}

	// 2. Continuous passive water consumption
	PhysicalState.Water = FMath::Clamp(PhysicalState.Water - (0.015f * 0.1f), 0.0f, 100.0f);

	// 3. Recalculate derived conditions
	RecalculateDizziness();
	EvaluateCriticalState();
}

void UNPCStateComponent::SlowTickUpdate()
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	// 1. Warm-blooded homeostatic thermoregulation:
	// Normal healthy core body temperature is 37.0°C. Sickness causes a fever up to +4.0°C.
	const float BaseHealthyCoreTemp = 37.0f + (PhysicalState.Sickness * 0.04f);

	// Ambient room temperature only causes environmental stress if extreme (< 12°C or > 36°C)
	float EnvironmentalStress = 0.0f;
	if (PhysicalState.AmbientTemperature < 12.0f)
	{
		EnvironmentalStress = (PhysicalState.AmbientTemperature - 12.0f) * 0.15f; // Hypothermia in freezing cold
	}
	else if (PhysicalState.AmbientTemperature > 36.0f)
	{
		EnvironmentalStress = (PhysicalState.AmbientTemperature - 36.0f) * 0.15f; // Heatstroke in extreme heat
	}

	const float TargetBodyTemp = BaseHealthyCoreTemp + EnvironmentalStress;
	// Smoothly maintain healthy core body temperature
	PhysicalState.BodyTemperature = FMath::FInterpTo(PhysicalState.BodyTemperature, TargetBodyTemp, 0.5f, 0.2f);

	// 2. Metabolize alcohol and narcotics
	PhysicalState.AlcoholLevel = FMath::Max(0.0f, PhysicalState.AlcoholLevel - (AlcoholBurnRate * 0.5f));
	PhysicalState.DrugLevel = FMath::Max(0.0f, PhysicalState.DrugLevel - (DrugBurnRate * 0.5f));
}

void UNPCStateComponent::RecalculateDizziness()
{
	const float LowPowerContribution = FMath::Clamp((30.0f - PhysicalState.Power) * 1.2f, 0.0f, 35.0f);
	const float DehydrationContribution = FMath::Clamp((30.0f - PhysicalState.Water) * 1.5f, 0.0f, 40.0f);
	const float TempDeviation = FMath::Abs(PhysicalState.BodyTemperature - 37.0f);
	const float TempContribution = FMath::Clamp(TempDeviation * 5.0f, 0.0f, 30.0f);
	const float SicknessContribution = PhysicalState.Sickness * 0.35f;
	const float AlcoholContribution = PhysicalState.AlcoholLevel * 0.6f;
	const float DrugContribution = PhysicalState.DrugLevel * 0.5f;

	PhysicalState.Dizziness = FMath::Clamp(
		LowPowerContribution + DehydrationContribution + TempContribution + SicknessContribution + AlcoholContribution + DrugContribution,
		0.0f,
		100.0f
	);

	EDizzinessTier NewTier = EDizzinessTier::Normal;
	if (PhysicalState.Dizziness >= 100.0f)
	{
		NewTier = EDizzinessTier::PassedOut;
	}
	else if (PhysicalState.Dizziness >= 75.0f)
	{
		NewTier = EDizzinessTier::Severe;
	}
	else if (PhysicalState.Dizziness >= 50.0f)
	{
		NewTier = EDizzinessTier::Impaired;
	}
	else if (PhysicalState.Dizziness >= 25.0f)
	{
		NewTier = EDizzinessTier::Mild;
	}

	if (NewTier != CurrentDizzinessTier)
	{
		CurrentDizzinessTier = NewTier;
		OnDizzinessTierChanged.Broadcast(NewTier);

		if (NewTier == EDizzinessTier::PassedOut)
		{
			OnNPCPassedOut.Broadcast();
		}
	}
}

void UNPCStateComponent::EvaluateCriticalState()
{
	const bool bIsCurrentlyCritical = IsInCriticalCondition();
	if (bIsCurrentlyCritical != bWasCritical)
	{
		bWasCritical = bIsCurrentlyCritical;

		FString Reason = TEXT("Stable");
		if (PhysicalState.Power <= 0.0f) Reason = TEXT("Complete Exhaustion");
		else if (PhysicalState.Water <= 0.0f) Reason = TEXT("Fatal Dehydration");
		else if (PhysicalState.BodyTemperature <= 32.0f) Reason = TEXT("Severe Hypothermia");
		else if (PhysicalState.BodyTemperature >= 42.0f) Reason = TEXT("Lethal Heatstroke");
		else if (PhysicalState.Sickness >= 100.0f) Reason = TEXT("Fatal Illness / Poisoning");

		OnPhysicalCriticalStateChanged.Broadcast(bIsCurrentlyCritical, Reason);
	}
}
