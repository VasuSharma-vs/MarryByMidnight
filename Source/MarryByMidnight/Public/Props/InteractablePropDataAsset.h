#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NPCEnumsAndTypes.h"
#include "InteractablePropDataAsset.generated.h"

class UStaticMesh;

/**
 * Taste types that can be chosen per index in the Data Asset
 */
UENUM(BlueprintType)
enum class ENPCTasteType : uint8
{
	Sweet        UMETA(DisplayName = "Sweet"),
	Spicy        UMETA(DisplayName = "Spicy"),
	Salty        UMETA(DisplayName = "Salty"),
	Sour         UMETA(DisplayName = "Sour"),
	Bitter       UMETA(DisplayName = "Bitter"),
	Meat         UMETA(DisplayName = "Meat"),
	Vegetarian   UMETA(DisplayName = "Vegetarian"),
	Healthy      UMETA(DisplayName = "Healthy"),
	Luxury       UMETA(DisplayName = "Luxury")
};

/**
 * Individual chooseable taste entry (e.g. Index 0: Sweet, Index 1: Spicy)
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FNPCTasteEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taste")
	ENPCTasteType TasteType = ENPCTasteType::Sweet;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"), Category = "Taste")
	float Intensity = 0.5f;

	FNPCTasteEntry() = default;
	FNPCTasteEntry(ENPCTasteType InType, float InIntensity) : TasteType(InType), Intensity(InIntensity) {}
};

/**
 * Primary Data Asset defining the configuration for an Interactable Prop.
 * Can be assigned to any AInteractableProp instance or Blueprint.
 */
UCLASS(BlueprintType)
class MARRYBYMIDNIGHT_API UInteractablePropDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UInteractablePropDataAsset();

	// -------------------------------------------------------------
	// Mesh & Presentation
	// -------------------------------------------------------------
	/** Static mesh model used by this prop */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<UStaticMesh> StaticMesh;

	/** In-game display name */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FText ItemDisplayName = FText::FromString(TEXT("Interactable Prop"));

	/** Category of consumable (Drink, Food, Alcohol, Medicine, Snack, Other) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	EConsumablePropType ConsumableType = EConsumablePropType::Drink;

	// -------------------------------------------------------------
	// Economy & Portions
	// -------------------------------------------------------------
	/** Monetary purchase price or value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	float Price = 2.0f;

	/** Maximum total portions (e.g. 4 sips or bites) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quantity", meta = (ClampMin = "1"))
	int32 MaxPortions = 4;

	/** Caloric energy value directly boosting internal Power stat */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physical")
	float EnergyLevel = 10.0f;

	// -------------------------------------------------------------
	// Temperature Configuration
	// -------------------------------------------------------------
	/** Option to enable temperature simulation. If false, prop only has ConsumableTemperature and does not adjust to environment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature", meta = (DisplayName = "Enable Prop Temperature"))
	bool bEnableTemperature = false;

	/** Ideal serving temperature in Celsius (e.g. 4.0 for cold soda, 65.0 for hot coffee) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature", meta = (Units = "Celsius"))
	float ConsumableTemperature = 4.0f;

	/** Tolerance range in Celsius. If current temp is within this range of ConsumableTemperature, awards dopamine bonus */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature", meta = (ClampMin = "0.5", ClampMax = "20.0", Units = "Celsius"))
	float TemperatureTolerance = 5.0f;

	/** Dopamine bonus awarded when consumed near optimal temperature */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature")
	float TemperatureDopamineBonus = 10.0f;

	/** Body temperature change applied to consumer (e.g. -0.5 for chilled drink, +0.8 for hot soup) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature", meta = (Units = "Celsius"))
	float BodyTemperatureEffect = -0.5f;

	/** Rate at which temperature normalizes towards ambient room temp per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature")
	float ThermalExchangeRate = 0.015f;

	// -------------------------------------------------------------
	// Expiry & Freshness Configuration
	// -------------------------------------------------------------
	/** Whether this prop has an expiry date */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freshness & Expiry")
	bool bHasExpiry = true;

	/** Total lifetime in minutes before item expires */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freshness & Expiry", meta = (EditCondition = "bHasExpiry", ClampMin = "0.5"))
	float ExpiryLifetimeMinutes = 60.0f;

	/** Sickness level added to consumer when expired */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freshness & Expiry", meta = (EditCondition = "bHasExpiry"))
	float SicknessLevel = 35.0f;

	/** Dopamine subtracted when consumed expired (pushes into depression level) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freshness & Expiry", meta = (EditCondition = "bHasExpiry"))
	float ExpiredDopaminePenalty = 25.0f;

	// -------------------------------------------------------------
	// Stimuli Modifiers (Index stats are chooseable)
	// -------------------------------------------------------------
	/** Chooseable stat modifiers applied per portion consumed (Index 0: Thirst -25, Index 1: Hunger -5, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stimuli")
	TArray<FNPCSimulationStatModifier> StimuliPerPortion;

	// -------------------------------------------------------------
	// Taste Profile (Index taste types are chooseable)
	// -------------------------------------------------------------
	/** Chooseable taste entries (e.g. Index 0: Sweet = 0.85, Index 1: Spicy = 0.40) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taste")
	TArray<FNPCTasteEntry> TasteEntries;

	/** Custom taste profile when expired (e.g. high sour and bitter) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taste", meta = (EditCondition = "bHasExpiry"))
	TArray<FNPCTasteEntry> ExpiredTasteEntries;

	// -------------------------------------------------------------
	// Helper Methods
	// -------------------------------------------------------------
	/** Converts chooseable taste entries into a composite FNPCTasteVector */
	UFUNCTION(BlueprintPure, Category = "Taste")
	FNPCTasteVector BuildTasteVector(bool bIsExpired = false) const;
};
