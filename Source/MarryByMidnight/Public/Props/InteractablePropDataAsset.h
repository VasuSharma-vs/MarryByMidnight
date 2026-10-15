#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NPCEnumsAndTypes.h"
#include "InteractablePropDataAsset.generated.h"

class UStaticMesh;
class AActivePropActor;
class UMaterialInterface;

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

	/** Optional material overrides for the static mesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TArray<TObjectPtr<UMaterialInterface>> MaterialOverrides;

	/** Optional custom Blueprint or C++ AActivePropActor subclass spawned when this prop is promoted from ISM to ASM */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual", meta = (DisplayName = "Custom Active Prop Actor Class (BP)"))
	TSubclassOf<AActivePropActor> CustomActiveActorClass;

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

	/** Total number of portions (e.g. 4 sips or bites) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quantity", meta = (DisplayName = "Num of Portions", ClampMin = "1"))
	int32 NumOfPortions = 4;

	// -------------------------------------------------------------
	// Temperature Configuration
	// -------------------------------------------------------------
	/** Option to enable temperature simulation update. If false, prop only has ConsumableTemperature and does not adjust to environment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature", meta = (DisplayName = "Enable Temperature Update"))
	bool bEnableTemperatureUpdate = false;

	/** Ideal serving temperature in Celsius (e.g. 4.0 for cold soda, 65.0 for hot coffee) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature", meta = (Units = "Celsius"))
	float ConsumableTemperature = 4.0f;

	/** Max dopamine bonus awarded per portion when consumed at ConsumableTemperature (decreases and goes negative as temperature diverges) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature")
	float TemperatureDopamineBonus = 0.5f;

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

	/** Stimulus profile applied per portion when consumed expired (enabled when bHasExpiry is true) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freshness & Expiry", meta = (EditCondition = "bHasExpiry", DisplayName = "Expiry Stimulus Profile"))
	TArray<FNPCSimulationStatModifier> ExpiryStimulusProfile;

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
	// Interaction Capability & Physics Profiles (IISMDB Section 5 & 14)
	// -------------------------------------------------------------
	/** Whether instances of this product can be grabbed/carried */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanGrab = true;

	/** Whether instances of this product can be used */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanUse = true;

	/** Whether instances of this product can be consumed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanConsume = true;

	/** Whether instances of this product can be thrown */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanThrow = true;

	/** Whether instances of this product can be inspected */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanInspect = true;

	/** Linear velocity threshold (cm/s) for settling evaluation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Profile", meta = (ClampMin = "0.1"))
	float LinearVelocityTolerance = 10.0f;

	/** Angular velocity threshold (deg/s) for settling evaluation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Profile", meta = (ClampMin = "0.1"))
	float AngularVelocityTolerance = 15.0f;

	/** Delay in seconds in Unsettled state before transitioning to Settling */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Profile", meta = (ClampMin = "0.0"))
	float UnsettledDelay = 3.0f;

	// -------------------------------------------------------------
	// Helper Methods
	// -------------------------------------------------------------
	/** Converts chooseable taste entries into a composite FNPCTasteVector */
	UFUNCTION(BlueprintPure, Category = "Taste")
	FNPCTasteVector BuildTasteVector(bool bIsExpired = false) const;
};
