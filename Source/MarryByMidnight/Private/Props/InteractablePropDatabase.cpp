#include "Props/InteractablePropDatabase.h"
#include "Props/InteractablePropDataAsset.h"

FInteractablePropData::FInteractablePropData()
{
	ConsumableTemperature = 21.0f;
	CurrentTemperature = 21.0f;
	bEnableTemperature = false;
	TemperatureTolerance = 5.0f;
	TemperatureDopamineBonus = 10.0f;
	BodyTemperatureEffect = -0.5f;
	ThermalExchangeRate = 0.015f;

	MaxPortions = 4;
	RemainingPortions = 4;
	QuantityLevel = 1.0f;
	Price = 2.0f;
	EnergyLevel = 10.0f;

	bIsGrabbable = true;
	SimulationState = EPhysicsSimulationState::AtRest;
	VelocityTolerance = 10.0f;

	bHasExpiry = true;
	ExpiryLifetimeMinutes = 60.0f;
	AgeMinutes = 0.0f;
	SicknessLevel = 35.0f;
	ExpiredDopaminePenalty = 25.0f;

	// Default stimuli
	StimuliPerPortion.Empty();
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Power, 10.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Hunger, -5.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Thirst, -25.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Dopamine, 5.0f));

	// Default taste profile
	TasteProfile.Sweet = 0.85f;
	TasteProfile.Healthy = 0.3f;
	TasteProfile.Luxury = 0.2f;
}

int32 FInteractablePropData::ConsumePortions(int32 PortionsToConsume)
{
	if (RemainingPortions <= 0) return 0;

	const int32 ActualConsumed = FMath::Clamp(PortionsToConsume, 1, RemainingPortions);
	RemainingPortions = FMath::Max(0, RemainingPortions - ActualConsumed);
	QuantityLevel = (MaxPortions > 0) ? static_cast<float>(RemainingPortions) / static_cast<float>(MaxPortions) : 0.0f;
	return ActualConsumed;
}

void FInteractablePropData::UpdateTemperature(float AmbientTemperature, float DeltaSeconds)
{
	if (!bEnableTemperature) return;

	if (ThermalExchangeRate > 0.0f && FMath::Abs(CurrentTemperature - AmbientTemperature) > 0.05f)
	{
		const float TempDelta = (AmbientTemperature - CurrentTemperature) * ThermalExchangeRate * DeltaSeconds;
		CurrentTemperature += TempDelta;
	}

	if (bHasExpiry)
	{
		AgeMinutes += (DeltaSeconds / 60.0f);
	}
}
