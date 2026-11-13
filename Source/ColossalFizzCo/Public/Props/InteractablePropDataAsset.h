#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NPCEnumsAndTypes.h"
#include "Props/InteractablePropDatabase.h"
#include "InteractablePropDataAsset.generated.h"

class UStaticMesh;
class USkeletalMesh;
class AActivePropActor;
class UMaterialInterface;
class UCurveFloat;

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
struct COLOSSALFIZZCO_API FNPCTasteEntry
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
class COLOSSALFIZZCO_API UInteractablePropDataAsset : public UPrimaryDataAsset
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

	/** Optional material overrides for the mesh (applies to StaticMesh or SkeletalMesh) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TArray<TObjectPtr<UMaterialInterface>> MaterialOverrides;

	/** Optional custom Blueprint or C++ AActivePropActor subclass spawned when this prop is promoted from ISM to ASM */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual", meta = (DisplayName = "Custom Active Prop Actor Class (BP)"))
	TSubclassOf<AActivePropActor> CustomActiveActorClass;

	/** Optional skeletal mesh model used by this prop in ASM (if null, falls back to StaticMesh) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<USkeletalMesh> SkeletalMesh;

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

	/** Gameplay mass is independent of giant visual dimensions and thermal mass. Zero preserves authored physics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics", meta=(ClampMin="0"))
	float GameplayMassKg = 0;
	/** Empty bottle mass, independent of liquid and giant mesh scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics|Bottle", meta=(ClampMin="0.001", Units="kg"))
	float BottleMass = .05f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics|Bottle", meta=(ClampMin="0.001"))
	float PortionVolumeMl = 20.f;
	/** Used when registering this product's liquid content definition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics|Bottle", meta=(ClampMin="0.001"))
	float LiquidDensityGPerMl = 1.04f;

	/** Total number of portions (e.g. 4 sips or bites) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quantity", meta = (DisplayName = "Num of Portions", ClampMin = "0"))
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
	// Category 1 Feature Toggles & Unique Defaults
	// -------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unique Data")
	bool bEnableInstanceUniqueData = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unique Data", meta = (EditCondition = "bEnableInstanceUniqueData"))
	bool bEnableContinuousUpdateData = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unique Data|Demotion")
	bool bAllowUncappedFilledISMDemotion = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unique Data|Thermal")
	bool bPromoteISMOnContinuousTempFailure = false;

	/** Default initial normalized ContentLevel when spawning new instances */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unique Data", meta = (ClampMin = "0.0"))
	float DefaultContentLevel = 1.0f;

	/** Default liquid ContentID when spawning new instances (0 = None / Empty) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unique Data")
	int32 DefaultContentID = 0;

	// -------------------------------------------------------------
	// Bottle Containment Capacity, Shell & Transformations
	// -------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Capacity", meta = (ClampMin = "0.01"))
	float NormalCapacity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Capacity", meta = (ClampMin = "1.0"))
	float OverfillWarningThreshold = 1.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Capacity", meta = (ClampMin = "1.0"))
	float MaxOverfillLevel = 1.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Capacity")
	TObjectPtr<UCurveFloat> FillSlowdownCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Capacity")
	bool bEnableOverfillFailure = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Shell")
	EBottleMaterialType BottleMaterialType = EBottleMaterialType::Glass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Shell", meta = (ClampMin = "1.0"))
	float MaxStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Shell", meta = (ClampMin = "0.0"))
	float BreakageFailureThreshold = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Pressure", meta = (ClampMin = "0.01"))
	float PressureResistanceMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Pressure", meta = (ClampMin = "0.0"))
	float PressureDamageThreshold = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Pressure", meta = (ClampMin = "0.0"))
	float PressureFailureThreshold = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Pressure", meta = (ClampMin = "0.0"))
	float StrengthDamageRate = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Deformation")
	bool bCanInflate = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Deformation")
	bool bCanBend = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Deformation")
	bool bCanDent = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Deformation")
	bool bCanCrack = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Deformation")
	bool bCanRecover = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Deformation")
	bool bSupportsRepair = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Thermal", meta = (ClampMin = "0.01"))
	float ThermalMass = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Thermal", meta = (ClampMin = "0.0"))
	float ThermalConductivity = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Thermal", meta = (ClampMin = "0.01"))
	float HeatingRateMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Thermal", meta = (ClampMin = "0.01"))
	float CoolingRateMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Thermal", meta = (Units = "Celsius"))
	float LowTempDamageThreshold = -5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Thermal", meta = (Units = "Celsius"))
	float HighTempDamageThreshold = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Thermal", meta = (Units = "Celsius"))
	float MinTemperatureThreshold = -20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Thermal", meta = (Units = "Celsius"))
	float MaxTemperatureThreshold = 85.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Thermal", meta = (ClampMin = "0.0"))
	float ThermalDamageRate = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Thermal", meta = (ClampMin = "0.01"))
	float ThermalEquilibriumEpsilon = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap", meta = (Bitmask, BitmaskEnum = "/Script/ColossalFizzCo.EBottleCapMaskFlags"))
	uint8 SupportedCapMask = 0xFF;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bottle|Cap") bool bIsBottleCap = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bottle|Cap") FTransform CapAttachmentTransform = FTransform(FVector(0,0,170));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap")
	EBottleCapType DefaultCapType = EBottleCapType::Twist;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap")
	TObjectPtr<UStaticMesh> DefaultCapMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultCapSealed = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap", meta = (ClampMin = "0.0"))
	float CapReleasePressure = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap", meta = (ClampMin = "0.0"))
	float SafeOpenPressureThreshold = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TorqueSweetSpotMin = 0.41f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TorqueSweetSpotMax = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	TArray<int32> SupportedContentIDs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	int32 EmptyBottleProductID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	TMap<int32, int32> FilledProductIDByContentID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	int32 BrokenProductID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	int32 MeltedProductID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	int32 BurstBrokenProductID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	bool bPersistBrokenHullAfterFracture = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	TArray<FPropFractureFragmentEntry> FractureFragments;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	TObjectPtr<UFragmentAssemblyDataAsset> FragmentAssembly;

	// -------------------------------------------------------------
	// Helper Methods
	// -------------------------------------------------------------
	/** Converts chooseable taste entries into a composite FNPCTasteVector */
	UFUNCTION(BlueprintPure, Category = "Taste")
	FNPCTasteVector BuildTasteVector(bool bIsExpired = false) const;
};
