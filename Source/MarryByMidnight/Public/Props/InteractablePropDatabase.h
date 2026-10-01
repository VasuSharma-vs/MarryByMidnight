#pragma once

#include "CoreMinimal.h"
#include "NPCEnumsAndTypes.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "InteractablePropDatabase.generated.h"

class UStaticMesh;
class UInteractablePropDataAsset;

/**
 * Universal database entry for an interactable prop or instanced static mesh.
 * Stores consumable temperature, current temperature, portions, grabbability,
 * simulation state, velocity tolerance, expiry, stimuli, and taste profile.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FInteractablePropData
{
	GENERATED_BODY()

	/** Unique Instance or Item Identifier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Data")
	int32 InstanceId = INDEX_NONE;

	/** In-game display name */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FText ItemDisplayName = FText::FromString(TEXT("Interactable Prop"));

	/** Consumable category (Drink, Food, Alcohol, Medicine, Snack, Other) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	EConsumablePropType ConsumableType = EConsumablePropType::Drink;

	/** Static mesh model */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<UStaticMesh> StaticMesh = nullptr;

	/** Optional primary Data Asset reference for base stats */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Data")
	TObjectPtr<UInteractablePropDataAsset> DataAsset = nullptr;

	// -------------------------------------------------------------
	// Temperature (Consumable & Current)
	// -------------------------------------------------------------
	/** Option to enable temperature simulation. If false, prop only has ConsumableTemperature and does not tick adjust to environment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature")
	bool bEnableTemperature = false;

	/** Ideal serving temperature in Celsius (e.g. 72.0°C hot, 4.0°C cold) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature", meta = (Units = "Celsius"))
	float ConsumableTemperature = 21.0f;

	/** Current physical temperature in Celsius (slowly adjusts to environment if enabled) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature", meta = (Units = "Celsius"))
	float CurrentTemperature = 21.0f;

	/** Temperature tolerance in Celsius: if CurrentTemperature is within this range of ConsumableTemperature, awards dopamine bonus */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature", meta = (ClampMin = "0.5", ClampMax = "20.0", Units = "Celsius"))
	float TemperatureTolerance = 5.0f;

	/** Dopamine bonus awarded when consumed near optimal temperature */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature")
	float TemperatureDopamineBonus = 10.0f;

	/** Body temperature change applied to consumer (e.g. -0.5 for chilled drink, +0.8 for hot soup) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature", meta = (Units = "Celsius"))
	float BodyTemperatureEffect = -0.5f;

	/** Rate at which current temp normalizes towards environment ambient temp */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature")
	float ThermalExchangeRate = 0.015f;

	// -------------------------------------------------------------
	// Economy & Portions
	// -------------------------------------------------------------
	/** Monetary purchase price or value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	float Price = 2.0f;

	/** Maximum total portions (e.g. 4 sips or bites) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portions", meta = (ClampMin = "1"))
	int32 MaxPortions = 4;

	/** Current remaining portions (e.g. player drinks 2 sips, 2 left) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portions", meta = (ClampMin = "0"))
	int32 RemainingPortions = 4;

	/** Quantity level fraction [0.0 = Empty, 1.0 = Full] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portions", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float QuantityLevel = 1.0f;

	/** Caloric energy value boosting Power stat */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physical")
	float EnergyLevel = 10.0f;

	// -------------------------------------------------------------
	// Physics Simulation & Grabbability
	// -------------------------------------------------------------
	/** Whether this prop is currently grabbable by player or NPC */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	bool bIsGrabbable = true;

	/** Current physics simulation state */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	EPhysicsSimulationState SimulationState = EPhysicsSimulationState::AtRest;

	/** Simulation stop tolerance level (cm/s). When speed falls below this in Settling, transitions to At Rest */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	float VelocityTolerance = 10.0f;

	// -------------------------------------------------------------
	// Expiry & Freshness
	// -------------------------------------------------------------
	/** Whether this prop has an expiry date */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freshness")
	bool bHasExpiry = true;

	/** Lifetime in minutes before expiring */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freshness")
	float ExpiryLifetimeMinutes = 60.0f;

	/** Age in minutes since creation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freshness")
	float AgeMinutes = 0.0f;

	/** Sickness level added when consumed expired */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freshness")
	float SicknessLevel = 35.0f;

	/** Dopamine subtracted when consumed expired */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freshness")
	float ExpiredDopaminePenalty = 25.0f;

	// -------------------------------------------------------------
	// Stimuli & Taste Profile
	// -------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stimuli")
	TArray<FNPCSimulationStatModifier> StimuliPerPortion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taste")
	FNPCTasteVector TasteProfile;

	FInteractablePropData();

	bool IsExpired() const { return bHasExpiry && (AgeMinutes >= ExpiryLifetimeMinutes); }
	bool IsEmpty() const { return RemainingPortions <= 0 || QuantityLevel <= 0.0f; }
	bool IsAtIdealTemperature() const { return FMath::Abs(CurrentTemperature - ConsumableTemperature) <= TemperatureTolerance; }

	/** Consumes portions (e.g. 2 sips). Returns actual portions consumed */
	int32 ConsumePortions(int32 PortionsToConsume = 1);

	/** Updates temperature toward ambient */
	void UpdateTemperature(float AmbientTemperature, float DeltaSeconds);
};
