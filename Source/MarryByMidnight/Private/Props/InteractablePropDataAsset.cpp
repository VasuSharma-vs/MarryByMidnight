#include "Props/InteractablePropDataAsset.h"
#include "Engine/StaticMesh.h"

UInteractablePropDataAsset::UInteractablePropDataAsset()
{
	ConsumableType = EConsumablePropType::Drink;
	Price = 2.0f;
	MaxPortions = 4;
	EnergyLevel = 10.0f;
	ConsumableTemperature = 4.0f;
	bEnableTemperature = false;
	TemperatureTolerance = 5.0f;
	TemperatureDopamineBonus = 10.0f;
	BodyTemperatureEffect = -0.5f;
	ThermalExchangeRate = 0.015f;

	bHasExpiry = true;
	ExpiryLifetimeMinutes = 60.0f;
	SicknessLevel = 35.0f;
	ExpiredDopaminePenalty = 25.0f;

	// Default chooseable stimuli (Index stats are chooseable)
	StimuliPerPortion.Empty();
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Thirst, -25.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Hunger, -5.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Power, 10.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Dopamine, 5.0f));

	// Default chooseable taste entries (Index taste types are chooseable)
	TasteEntries.Empty();
	TasteEntries.Add(FNPCTasteEntry(ENPCTasteType::Sweet, 0.85f));
	TasteEntries.Add(FNPCTasteEntry(ENPCTasteType::Spicy, 0.20f));

	// Default expired taste entries (sour and bitter dominate when expired)
	ExpiredTasteEntries.Empty();
	ExpiredTasteEntries.Add(FNPCTasteEntry(ENPCTasteType::Sour, 0.90f));
	ExpiredTasteEntries.Add(FNPCTasteEntry(ENPCTasteType::Bitter, 0.80f));
	ExpiredTasteEntries.Add(FNPCTasteEntry(ENPCTasteType::Sweet, 0.05f));
}

FNPCTasteVector UInteractablePropDataAsset::BuildTasteVector(bool bIsExpired) const
{
	FNPCTasteVector Result;
	// Zero out baseline so only specified taste entries define the flavor profile
	Result.Sweet = 0.0f;
	Result.Spicy = 0.0f;
	Result.Salty = 0.0f;
	Result.Sour = 0.0f;
	Result.Bitter = 0.0f;
	Result.Meat = 0.0f;
	Result.Vegetarian = 0.0f;
	Result.Healthy = 0.0f;
	Result.Luxury = 0.0f;

	const TArray<FNPCTasteEntry>& SourceEntries = (bIsExpired && ExpiredTasteEntries.Num() > 0) ? ExpiredTasteEntries : TasteEntries;

	for (const FNPCTasteEntry& Entry : SourceEntries)
	{
		switch (Entry.TasteType)
		{
		case ENPCTasteType::Sweet:      Result.Sweet = Entry.Intensity; break;
		case ENPCTasteType::Spicy:      Result.Spicy = Entry.Intensity; break;
		case ENPCTasteType::Salty:      Result.Salty = Entry.Intensity; break;
		case ENPCTasteType::Sour:       Result.Sour = Entry.Intensity; break;
		case ENPCTasteType::Bitter:     Result.Bitter = Entry.Intensity; break;
		case ENPCTasteType::Meat:       Result.Meat = Entry.Intensity; break;
		case ENPCTasteType::Vegetarian: Result.Vegetarian = Entry.Intensity; break;
		case ENPCTasteType::Healthy:    Result.Healthy = Entry.Intensity; break;
		case ENPCTasteType::Luxury:     Result.Luxury = Entry.Intensity; break;
		}
	}

	if (bIsExpired && ExpiredTasteEntries.Num() == 0)
	{
		Result.Sour = 0.85f;
		Result.Bitter = 0.75f;
		Result.Sweet = 0.05f;
	}

	return Result;
}
