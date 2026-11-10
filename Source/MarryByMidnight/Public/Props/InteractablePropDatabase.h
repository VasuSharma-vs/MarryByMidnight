#pragma once

#include "CoreMinimal.h"
#include "Shop/SharedWalletTypes.h"
#include "Shop/ShopCheckoutTypes.h"
#include "Props/LiquidReleaseTypes.h"
#include "Engine/DataAsset.h"
#include "Engine/NetSerialization.h"
#include "NPCEnumsAndTypes.h"
#include "GameFramework/SaveGame.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "InteractablePropDatabase.generated.h"

class UStaticMesh;
class USkeletalMesh;
class UMaterialInterface;
class UCurveFloat;
class UInteractablePropDataAsset;
class AActivePropActor;
class UInteractableISMComponent;
class UFragmentAssemblyDataAsset;

/**
 * Physical representation mode of a logical prop (ISID).
 * Rule 1 & 2: ISM is the persistent resting representation; ASM is the temporary active representation.
 */
UENUM(BlueprintType)
enum class EPropRepresentation : uint8
{
	ISM UMETA(DisplayName = "ISM (Resting Instanced Representation)"),
	ASM UMETA(DisplayName = "ASM (Active Static Mesh Actor)"),
	CompositeFragment UMETA(DisplayName = "Original Fragment Component")
};

/**
 * Authoritative logical state of a prop instance (Section 6 & 18).
 */
UENUM(BlueprintType)
enum class EPropState : uint8
{
	AtRest    UMETA(DisplayName = "At Rest"),
	InMotion  UMETA(DisplayName = "In Motion"),
	Held      UMETA(DisplayName = "Held"),
	Settling  UMETA(DisplayName = "Settling"),
	Unsettled UMETA(DisplayName = "Unsettled")
};

/**
 * Reason for promoting an ISM instance to an active ASM actor (Section 21).
 */
UENUM(BlueprintType)
enum class EPropActivationReason : uint8
{
	None           UMETA(DisplayName = "None"),
	Grab           UMETA(DisplayName = "Grab / Carry"),
	Drop           UMETA(DisplayName = "Drop"),
	Throw          UMETA(DisplayName = "Throw"),
	PhysicsImpulse UMETA(DisplayName = "Physics Impulse / Hit"),
	Damage         UMETA(DisplayName = "Damage Reaction"),
	Interaction    UMETA(DisplayName = "Complex Interaction"),
	Movement       UMETA(DisplayName = "Movement / Rotation"),
	Animation      UMETA(DisplayName = "Animation"),
	LoadRestore    UMETA(DisplayName = "Save/Load Restore")
};

/**
 * Physics simulation mode for a prop (Section 12).
 */
UENUM(BlueprintType)
enum class EPropPhysicsMode : uint8
{
	Disabled  UMETA(DisplayName = "Disabled / Kinematic"),
	Simulated UMETA(DisplayName = "Simulated Physics"),
	Settling  UMETA(DisplayName = "Settling Physics"),
	Held      UMETA(DisplayName = "Held / Attached")
};

/**
 * Discrete interaction types supported by the Prop Manager (Sections 10, 11, 13, 14).
 */
UENUM(BlueprintType)
enum class EPropInteractionType : uint8
{
	None       UMETA(DisplayName = "None"),
	Grab       UMETA(DisplayName = "Grab"),
	Release    UMETA(DisplayName = "Release"),
	Drop       UMETA(DisplayName = "Drop"),
	Throw      UMETA(DisplayName = "Throw"),
	PickUp     UMETA(DisplayName = "Pick Up"),
	Use        UMETA(DisplayName = "Use"),
	Consume    UMETA(DisplayName = "Consume"),
	Open       UMETA(DisplayName = "Open"),
	Close      UMETA(DisplayName = "Close"),
	Toggle     UMETA(DisplayName = "Toggle"),
	Activate   UMETA(DisplayName = "Activate"),
	Deactivate UMETA(DisplayName = "Deactivate"),
	Break      UMETA(DisplayName = "Break"),
	Repair     UMETA(DisplayName = "Repair"),
	Damage     UMETA(DisplayName = "Damage"),
	Inspect    UMETA(DisplayName = "Inspect"),
	Combine    UMETA(DisplayName = "Combine"),
	Split      UMETA(DisplayName = "Split"),
	Move       UMETA(DisplayName = "Move"),
	Rotate     UMETA(DisplayName = "Rotate")
};

/**
 * Bitmask flags for interaction capabilities (Section 14).
 */
namespace EPropInteractionCapFlags
{
	constexpr int32 None       = 0;
	constexpr int32 Grab       = 1 << 0;
	constexpr int32 Use        = 1 << 1;
	constexpr int32 Consume    = 1 << 2;
	constexpr int32 Open       = 1 << 3;
	constexpr int32 Close      = 1 << 4;
	constexpr int32 Toggle     = 1 << 5;
	constexpr int32 Activate   = 1 << 6;
	constexpr int32 Break      = 1 << 7;
	constexpr int32 Repair     = 1 << 8;
	constexpr int32 Damage     = 1 << 9;
	constexpr int32 Inspect    = 1 << 10;
	constexpr int32 Combine    = 1 << 11;
	constexpr int32 Split      = 1 << 12;
	constexpr int32 Throw      = 1 << 13;
	constexpr int32 PickUp     = 1 << 14;
	constexpr int32 Drop       = 1 << 15;
	constexpr int32 Move       = 1 << 16;
	constexpr int32 Rotate     = 1 << 17;
	constexpr int32 Deactivate = 1 << 18;
}

/**
 * Ownership / interaction lock state for a prop instance (Section 26).
 */
UENUM(BlueprintType)
enum class EPropInteractionLockState : uint8
{
	Free        UMETA(DisplayName = "Free"),
	Reserved    UMETA(DisplayName = "Reserved"),
	Held        UMETA(DisplayName = "Exclusively Held"),
	Interacting UMETA(DisplayName = "Interacting"),
	Locked      UMETA(DisplayName = "Locked")
};

/**
 * Dirty flags tracking what changed on an ISID for bulk delta replication (Section 6 & 34).
 */
namespace EPropDirtyFlags
{
	constexpr int32 Clean           = 0;
	constexpr int32 Transform       = 1 << 0;
	constexpr int32 State           = 1 << 1;
	constexpr int32 RuntimeData     = 1 << 2;
	constexpr int32 Representation  = 1 << 3;
	constexpr int32 Created         = 1 << 4;
	constexpr int32 Deleted         = 1 << 5;
	constexpr int32 ReleasedToEmpty = 1 << 6;
	constexpr int32 All             = Transform | State | RuntimeData | Representation | Created;
}

/**
 * Loading and synchronization lifecycle state of the Prop Manager (Section 59).
 */
UENUM(BlueprintType)
enum class EPropManagerLoadState : uint8
{
	Uninitialized        UMETA(DisplayName = "Uninitialized"),
	LoadingComponents    UMETA(DisplayName = "Loading Components"),
	LoadingDatabase      UMETA(DisplayName = "Loading Database"),
	RestoringInstances   UMETA(DisplayName = "Restoring Instances"),
	SynchronizingClients UMETA(DisplayName = "Synchronizing Clients"),
	Ready                UMETA(DisplayName = "Ready"),
	Failed               UMETA(DisplayName = "Failed")
};

/**
 * Status of an independent Prop Manager task (Section 35).
 */
UENUM(BlueprintType)
enum class EPropTaskStatus : uint8
{
	Pending    UMETA(DisplayName = "Pending"),
	InProgress UMETA(DisplayName = "In Progress"),
	Completed  UMETA(DisplayName = "Completed"),
	Failed     UMETA(DisplayName = "Failed"),
	Cancelled  UMETA(DisplayName = "Cancelled")
};

/**
 * Type of asynchronous/batched task in the Prop Manager task system (Section 35).
 */
UENUM(BlueprintType)
enum class EPropTaskType : uint8
{
	GenerateProps  UMETA(DisplayName = "Generate Props"),
	UpdateInstance UMETA(DisplayName = "Update ISID"),
	RemoveProp     UMETA(DisplayName = "Remove Prop"),
	BulkSync       UMETA(DisplayName = "Bulk Client Sync"),
	Custom         UMETA(DisplayName = "Custom Task")
};

/**
 * Product conversion behavior when changing a prop's ProductID at runtime.
 */
UENUM(BlueprintType)
enum class EPropProductResetMode : uint8
{
	PreserveUniqueState UMETA(DisplayName = "Preserve Unique State (Keep Content, Strength, Temp, CreationTime)"),
	ContentReset        UMETA(DisplayName = "Content Reset (Zero Content & Cap; Keep Strength, Temp, CreationTime)"),
	FullReset           UMETA(DisplayName = "Full Reset (Restore Product Defaults & Fresh CreationTime)")
};

/**
 * Dirty mask bitflags for discrete Category 2 Event Update Data replication.
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EPropEventDirtyMask : uint8
{
	None         = 0       UMETA(Hidden),
	ContentLevel = 1 << 0  UMETA(DisplayName = "Content Level"),
	ContentID    = 1 << 1  UMETA(DisplayName = "Content ID"),
	Strength     = 1 << 2  UMETA(DisplayName = "Strength"),
	CapSealed    = 1 << 3  UMETA(DisplayName = "Cap Sealed"),
	CreationTime = 1 << 4  UMETA(DisplayName = "Creation Time"),
	Grabbable    = 1 << 5  UMETA(DisplayName = "Is Grabbable"),
	ProductID    = 1 << 6  UMETA(DisplayName = "Product ID"),
	CapType      = 1 << 7  UMETA(DisplayName = "Cap Type")
};
ENUM_CLASS_FLAGS(EPropEventDirtyMask);

namespace EPropExtendedEventMask
{
	constexpr uint8 Clean = 1 << 0;
	constexpr uint8 ContentCreationTime = 1 << 1;
	constexpr uint8 AttachedCap = 1 << 2;
	constexpr uint8 AttachedBottle = 1 << 3;
}

/**
 * Transformation topology triggered by structural, thermal, or overpressure failure.
 */
UENUM(BlueprintType)
enum class EPropTransformationType : uint8
{
	None              UMETA(DisplayName = "None"),
	BreakageFracture  UMETA(DisplayName = "Breakage Fracture (1-to-Many Fragments)"),
	ThermalMelt       UMETA(DisplayName = "Thermal Melt (1-to-1)"),
	OverpressureBurst UMETA(DisplayName = "Overpressure Burst (1-to-1)")
};

/**
 * Physical shell material classification for container deformation and breakage rules.
 */
UENUM(BlueprintType)
enum class EBottleMaterialType : uint8
{
	Glass          UMETA(DisplayName = "Glass (Cracks + Breakage Fracture)"),
	Plastic        UMETA(DisplayName = "Plastic (WPO Dents + Inflation)"),
	HeavyMetal     UMETA(DisplayName = "Heavy Metal"),
	EnchantedStone UMETA(DisplayName = "Enchanted Stone (Legacy)"),
	Diamond UMETA(DisplayName = "Diamond"),
	Concrete UMETA(DisplayName = "Concrete")
};

/**
 * Attached bottle cap mechanism identifier packed into FPropEventUpdateData::CapType.
 */
UENUM(BlueprintType)
enum class EBottleCapType : uint8
{
	None     = 0 UMETA(DisplayName = "None"),
	Twist    = 1 UMETA(DisplayName = "Twist Cap"),
	Cork     = 2 UMETA(DisplayName = "Cork"),
	BoltLock = 3 UMETA(DisplayName = "Bolt Lock"),
	RuneSeal = 4 UMETA(DisplayName = "Rune Seal")
};

/**
 * Bitflags for configuring which bottle cap types a product supports.
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EBottleCapMaskFlags : uint8
{
	None     = 0      UMETA(Hidden),
	Twist    = 1 << 0 UMETA(DisplayName = "Twist"),
	Cork     = 1 << 1 UMETA(DisplayName = "Cork"),
	BoltLock = 1 << 2 UMETA(DisplayName = "Bolt Lock"),
	RuneSeal = 1 << 3 UMETA(DisplayName = "Rune Seal")
};
ENUM_CLASS_FLAGS(EBottleCapMaskFlags);

inline bool IsBottleCapTypeSupportedInMask(uint8 SupportedMask, uint8 CapTypeValue)
{
	if (CapTypeValue == 0)
	{
		return false;
	}
	const uint8 BitIndex = CapTypeValue - 1;
	return (BitIndex < 8) && ((SupportedMask & static_cast<uint8>(1u << BitIndex)) != 0);
}

/**
 * Transient ASM state machine for 4-stage bottle cap installation and removal.
 */
UENUM(BlueprintType)
enum class EBottleCapState : uint8
{
	None      UMETA(DisplayName = "Not Installed"),
	Validated UMETA(DisplayName = "Validated"),
	Attached  UMETA(DisplayName = "Attached"),
	Closing   UMETA(DisplayName = "Closing"),
	Closed    UMETA(DisplayName = "Closed"),
	Sealed    UMETA(DisplayName = "Sealed"),
	Failed    UMETA(DisplayName = "Failed Seal"),
	Removed   UMETA(DisplayName = "Removed"),
	Broken    UMETA(DisplayName = "Broken")
};

/**
 * Removal method supplied to RemoveCap.
 */
UENUM(BlueprintType)
enum class EBottleCapRemovalType : uint8
{
	Twist UMETA(DisplayName = "Twist"),
	Pop   UMETA(DisplayName = "Pop"),
	Break UMETA(DisplayName = "Break"),
	Pull  UMETA(DisplayName = "Pull"),
	Cut   UMETA(DisplayName = "Cut")
};

/**
 * Containment hazard payload released on catastrophic bottle burst or breakage.
 */
UENUM(BlueprintType)
enum class EColaHazardType : uint8
{
	FoamRocket  UMETA(DisplayName = "Foam Rocket"),
	FirePatch   UMETA(DisplayName = "Fire Patch"),
	ToxicCloud  UMETA(DisplayName = "Toxic Cloud"),
	SpiderSwarm UMETA(DisplayName = "Spider Swarm"),
	StickySlow  UMETA(DisplayName = "Sticky Slow"),
	Radiation   UMETA(DisplayName = "Radiation")
};

/**
 * Containment failure classification ordered by priority in Containment Failure Manager.
 */
UENUM(BlueprintType)
enum class EContainmentFailureType : uint8
{
	None              UMETA(DisplayName = "None"),
	BottleBreakage    UMETA(DisplayName = "Bottle Breakage (Priority 1)"),
	OverpressureBurst UMETA(DisplayName = "Overpressure Burst (Priority 2)"),
	ThermalFailure    UMETA(DisplayName = "Thermal Failure (Priority 3)"),
	PressureRelease   UMETA(DisplayName = "Pressure Release (Priority 4)"),
	CapFailure        UMETA(DisplayName = "Cap Failure (Priority 5)"),
	Other             UMETA(DisplayName = "Other (Priority 6)")
};

/**
 * Execution lifecycle state of Containment Failure Manager on an ASM bottle.
 */
UENUM(BlueprintType)
enum class EContainmentFailureState : uint8
{
	Normal         UMETA(DisplayName = "Normal"),
	PendingFailure UMETA(DisplayName = "Pending Failure"),
	Failing        UMETA(DisplayName = "Failing"),
	Broken         UMETA(DisplayName = "Broken")
};

/**
 * Pressure venting modes distinguishing agitation bleed from full headspace depressurization.
 */
UENUM(BlueprintType)
enum class EPressureReleaseMode : uint8
{
	ReleaseInteractionPressure UMETA(DisplayName = "Release Interaction Pressure"),
	FullPressureVent           UMETA(DisplayName = "Full Pressure Vent")
};

/**
 * Closing validation trigger strategy for Stage 4 of AddCap.
 */
UENUM(BlueprintType)
enum class ECapValidationMode : uint8
{
	AutoTrigger  UMETA(DisplayName = "Auto Trigger (Retriggerable Delay)"),
	EventTrigger UMETA(DisplayName = "Event Trigger (External Signal)")
};

/**
 * Converts between EPropState and EPhysicsSimulationState.
 */
inline EPropState ConvertPhysicsStateToPropState(EPhysicsSimulationState InState)
{
	switch (InState)
	{
	case EPhysicsSimulationState::AtRest:    return EPropState::AtRest;
	case EPhysicsSimulationState::InMotion:  return EPropState::InMotion;
	case EPhysicsSimulationState::Held:      return EPropState::Held;
	case EPhysicsSimulationState::Settling:  return EPropState::Settling;
	case EPhysicsSimulationState::Unsettled: return EPropState::Unsettled;
	default:                                 return EPropState::AtRest;
	}
}

inline EPhysicsSimulationState ConvertPropStateToPhysicsState(EPropState InState)
{
	switch (InState)
	{
	case EPropState::AtRest:    return EPhysicsSimulationState::AtRest;
	case EPropState::InMotion:  return EPhysicsSimulationState::InMotion;
	case EPropState::Held:      return EPhysicsSimulationState::Held;
	case EPropState::Settling:  return EPhysicsSimulationState::Settling;
	case EPropState::Unsettled: return EPhysicsSimulationState::Unsettled;
	default:                    return EPhysicsSimulationState::AtRest;
	}
}

/**
 * Interaction capability profile defining what interactions a product supports (Section 14).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropInteractionProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanGrab = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanUse = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanConsume = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanOpen = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanClose = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanToggle = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanActivate = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanBreak = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanRepair = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanDamage = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanInspect = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanCombine = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanSplit = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanThrow = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanPickUp = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanDrop = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanMove = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	bool bCanRotate = true;

	/** Maximum distance in cm from which a player/NPC can interact with this prop */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile", meta = (ClampMin = "10.0"))
	float MaxInteractionDistance = 350.0f;

	/** Minimum cooldown in seconds between interactions */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile", meta = (ClampMin = "0.0"))
	float InteractionCooldown = 0.1f;

	/** Optional required gameplay tag on the interactor for permission checks */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Profile")
	FGameplayTag RequiredPermissionTag;

	/** Builds the compact integer bitmask of supported capabilities */
	int32 ToBitmask() const;

	/** Populates boolean capability flags from a bitmask */
	void FromBitmask(int32 InBitmask);

	/** Evaluates whether a specific interaction type is supported by this profile */
	bool SupportsInteraction(EPropInteractionType InteractionType) const;
};

/**
 * Physics settings shared by all instances of a product (Section 5 & 12).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropPhysicsProfile
{
	GENERATED_BODY()

	/** Linear speed threshold (cm/s) below which a settling prop is considered settled */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Profile", meta = (ClampMin = "0.1"))
	float LinearVelocityTolerance = 10.0f;

	/** Angular speed threshold (deg/s) below which a settling prop is considered settled */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Profile", meta = (ClampMin = "0.1"))
	float AngularVelocityTolerance = 15.0f;

	/** Delay in seconds in Unsettled state before transitioning to Settling */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Profile", meta = (ClampMin = "0.0"))
	float UnsettledDelay = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Profile")
	float LinearDamping = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Profile")
	float AngularDamping = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Profile")
	bool bEnableGravity = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Profile")
	FName DefaultCollisionProfile = FName(TEXT("BlockAllDynamic"));
};

/**
 * Consumable profile shared by all instances of a product (Section 5).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropConsumableProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable Profile")
	EConsumablePropType ConsumableType = EConsumablePropType::Drink;

	/** Total number of portions (sips or bites) this product contains when full */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable Profile", meta = (DisplayName = "Num of Portions", ClampMin = "0"))
	int32 NumOfPortions = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable Profile")
	float Price = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable Profile")
	bool bDestroyWhenEmpty = true;

	/** Portion size fraction calculated as 1.0 / NumOfPortions */
	float GetPortionSize() const { return (NumOfPortions > 0) ? (1.0f / static_cast<float>(NumOfPortions)) : 0.0f; }
};

/**
 * Temperature profile shared by all instances of a product (Section 5).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropTemperatureProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature Profile", meta = (DisplayName = "Enable Temperature Update"))
	bool bEnableTemperatureUpdate = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature Profile", meta = (Units = "Celsius"))
	float ConsumableTemperature = 4.0f;

	/** Max dopamine given per portion when current temperature matches consumable temperature */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature Profile")
	float TemperatureDopamineBonus = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature Profile")
	float ThermalExchangeRate = 0.015f;
};

/**
 * Expiry profile shared by all instances of a product (Section 5).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropExpiryProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expiry Profile")
	bool bHasExpiry = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expiry Profile", meta = (EditCondition = "bHasExpiry", ClampMin = "0.5"))
	float ExpiryLifetimeMinutes = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expiry Profile", meta = (EditCondition = "bHasExpiry"))
	float SicknessLevel = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expiry Profile", meta = (EditCondition = "bHasExpiry"))
	float ExpiredDopaminePenalty = 25.0f;

	/** Biological / psychological stimuli applied per portion when consumed expired */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expiry Profile", meta = (EditCondition = "bHasExpiry", DisplayName = "Expiry Stimulus Profile"))
	TArray<FNPCSimulationStatModifier> ExpiryStimulusProfile;
};

/**
 * Compact per-instance runtime data with explicit deterministic bit-level packing (Section 32).
 * Does not rely on compiler-dependent C++ bitfield layout.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropPackedRuntimeData
{
	GENERATED_BODY()

	/** World minute when this instance was created (0..65535) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packed Runtime Data")
	int32 CreationMinute = 0;

	/** World minute when this instance was last updated (0..65535) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packed Runtime Data")
	int32 LastUpdatedMinute = 0;

	/** Remaining consumable portions (0..255) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packed Runtime Data")
	int32 RemainingPortions = 4;

	/** Current physical temperature in Celsius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packed Runtime Data")
	float Temperature = 21.0f;

	/** Whether this instance can currently be grabbed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packed Runtime Data")
	bool bIsGrabbable = true;

	/** Schema and extension flags (8 bits packed into 64-bit word) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Packed Runtime Data")
	int32 SchemaFlags = 0;

	/**
	 * Explicitly packs fields into a deterministic 64-bit integer:
	 * Bits [0..15]  : CreationMinute (uint16)
	 * Bits [16..31] : LastUpdatedMinute (uint16)
	 * Bits [32..39] : RemainingPortions (uint8)
	 * Bits [40..51] : Quantized Temperature in 0.1C steps from -100.0C (12 bits, range -100.0C..+309.5C)
	 * Bit  [52]     : bIsGrabbable (1 bit)
	 * Bits [56..63] : SchemaFlags (8 bits)
	 */
	uint64 PackToUint64() const;

	/** Explicitly unpacks fields from a deterministic 64-bit integer */
	void UnpackFromUint64(uint64 PackedBits);

	/** Explicit archive serialization */
	void SerializeExplicit(FArchive& Ar);
};

/**
 * Category 2.1: Continuous Update Data (16 bytes).
 * Mutated in background batches over time without direct interaction (Temperature & LastUpdatedTime only).
 * Never overwrites discrete Event Update Data such as CreationTime or ContentLevel.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropContinuousUpdateData
{
	GENERATED_BODY()

	/** Double-precision WorldElapsedSeconds at the last thermal integration step */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Continuous")
	double LastUpdatedTime = 0.0;

	/** Authoritative equilibrium shell temperature in Celsius (drives GPU Slot 2) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Continuous", meta = (Units = "Celsius"))
	float Temperature = 21.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Continuous", meta = (ClampMin = "0"))
	float SealedPressure = 0.0f;
};

/**
 * Category 2.2: Event Update Data (32 bytes).
 * Mutated ONLY at spawn or on discrete gameplay events (Fill, Consume, Cap, Damage, Repair, PID Swap).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropEventUpdateData
{
	GENERATED_BODY()

	/** Immutable creation timestamp in WorldElapsedSeconds (only set at spawn or FullReset) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event")
	double CreationTime = 0.0;

	/** Content age restarts on an eligible empty refill; container creation remains unchanged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event")
	double ContentCreationTime = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event")
	int32 AttachedCapISID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event")
	int32 AttachedBottleISID = 0;

	/** Normalized fill volume (0.0 = Empty, 1.0 = NormalCapacity, > 1.0 = Overfill; resting BasePressure) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event", meta = (ClampMin = "0.0"))
	float ContentLevel = 0.0f;

	/** Shared ContentID keying APropManager::ContentDefinitions (0 = None / Empty) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event")
	int32 ContentID = 0;

	/** Unified structural integrity (0.0..MaxStrength); replaces PermanentDeformation, CrackProgression, ThermalDamageLevel */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event", meta = (ClampMin = "0.0"))
	float Strength = 1.0f;

	/** Normalized cap seal state (0.0 = None/Removed/Broken, 0.01..0.99 = Loose/Compromised, 1.0 = Sealed) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CapSealed = 0.0f;

	/** Monotonically incremented event counter guarding against out-of-order network updates */
	UPROPERTY(EditAnywhere, SaveGame, Category = "Unique Data|Event")
	uint32 EventSequence = 1;

	/** Attached EBottleCapType byte packed alongside bIsGrabbable */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event")
	uint8 CapType = 0;

	/** Whether this instance can currently be picked up / grabbed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event")
	bool bIsGrabbable = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Unique Data|Event") bool bClean = true;
};

/**
 * Sparse per-ISID Category 2 container allocated ONLY when ProductDef->bEnableInstanceUniqueData == true.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropInstanceUniqueData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data")
	FPropContinuousUpdateData ContinuousData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data")
	FPropEventUpdateData EventData;
};

/**
 * Category 1B: Shared liquid / content definition keyed by ContentID in APropManager::ContentDefinitions.
 * Prevents duplicating liquid arrays, materials, prices, and expiry settings per bottle instance.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropContentDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
	int32 ContentID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
	FText DisplayName = FText::FromString(TEXT("None"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
	TObjectPtr<UMaterialInterface> LiquidMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
	FLinearColor LiquidColor = FLinearColor(0.25f, 0.08f, 0.02f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition", meta = (ClampMin = "0.01"))
	float PressureStrengthMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition", meta = (ClampMin = "0.0"))
	float BasePortionPrice = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition", meta = (Units = "Celsius"))
	float IdealServingTemperature = 4.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Content Definition", meta=(ClampMin="0.01"))
	float TemperaturePenaltyRange = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition", meta = (ClampMin = "0.01"))
	float SpecificHeatCapacity = 4.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition", meta = (ClampMin = "0.01"))
	float Viscosity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
	EColaHazardType HazardType = EColaHazardType::FoamRocket;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
	TArray<FNPCSimulationStatModifier> StimulusProfile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
	FNPCTasteVector TasteProfile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
	FNPCTasteVector ExpiredTasteProfile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
	FPropExpiryProfile ExpiryProfile;

	bool IsValidDefinition() const { return ContentID > 0; }
};

/**
 * Fracture shard recipe entry mapping a broken bottle product into independent Glass Fragment 1..N child products.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropFractureFragmentEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fracture")
	int32 FragmentProductID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fracture")
	FTransform LocalSpawnTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fracture")
	FVector LocalScatterDirection = FVector(0.0f, 0.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fracture", meta = (ClampMin = "0.0"))
	float MinImpulseSpeed = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fracture", meta = (ClampMin = "0.0"))
	float MaxImpulseSpeed = 380.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fracture", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VelocityInheritance = 0.65f;
};

/**
 * Context passed from a failing ASM bottle to APropManager & ABrokenBottlePropActor during 1-to-Many fracture or 1-to-1 transform.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropTransformationContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transformation")
	int32 SourceISID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transformation")
	int32 SourceProductID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transformation")
	EPropTransformationType TransformationType = EPropTransformationType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transformation")
	EBottleMaterialType SourceMaterialType = EBottleMaterialType::Glass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transformation")
	FTransform WorldTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transformation")
	FVector ImpactPoint = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transformation")
	FVector ImpactNormal = FVector::UpVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transformation")
	FVector LinearVelocity = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transformation")
	FVector AngularVelocity = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transformation")
	int32 FractureSeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transformation")
	FPropInstanceUniqueData InheritedUniqueData;
};

/**
 * Standardized failure request routed into AColaPropActor's Containment Failure Manager.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FContainmentFailureRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Failure")
	int32 FailureEventID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Failure")
	EContainmentFailureType FailureType = EContainmentFailureType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Failure", meta = (ClampMin = "0.0"))
	float Severity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Failure")
	FVector FailureLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Failure")
	FVector FailureDirection = FVector::UpVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Failure")
	bool bReleaseContent = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Failure")
	bool bReleasePressure = true;
};

/**
 * Active heating or cooling source registered against a bottle during multi-source ASM thermal simulation.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropThermalSource
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thermal")
	FName SourceID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thermal", meta = (Units = "Celsius"))
	float TargetTemperature = 21.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thermal", meta = (ClampMin = "0.0"))
	float Conductance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thermal")
	float DirectHeatPower = 0.0f;
};

/**
 * Channel A: Compact unreliable Continuous sync item sent inside Multicast_SyncContinuousBatch.
 */
USTRUCT()
struct MARRYBYMIDNIGHT_API FPropContinuousSyncItem
{
	GENERATED_BODY()

	UPROPERTY()
	int32 ISID = 0;

	UPROPERTY()
	float Temperature = 21.0f;

	UPROPERTY() float SealedPressure = 0.0f;

	UPROPERTY()
	float RelativeTimeOffset = 0.0f;

	UPROPERTY()
	float ThermalStrength = 1.0f;

	UPROPERTY()
	bool bHasThermalStrengthUpdate = false;

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FPropContinuousSyncItem> : public TStructOpsTypeTraitsBase2<FPropContinuousSyncItem>
{
	enum
	{
		WithNetSerializer = true,
	};
};

/**
 * Channel B: Reliable per-ISID discrete Event sync delta containing only mutated fields via NetSerialize.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropEventSyncDelta
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	int32 ISID = 0;

	UPROPERTY()
	uint32 EventSequence = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	uint8 DirtyMask = 0;

	UPROPERTY() uint8 ExtendedDirtyMask = 0;
	UPROPERTY() double ContentCreationTime = 0;
	UPROPERTY() int32 AttachedCapISID = 0;
	UPROPERTY() int32 AttachedBottleISID = 0;
	UPROPERTY() bool bClean = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	float ContentLevel = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	int32 ContentID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	float Strength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	float CapSealed = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	double CreationTime = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	bool bIsGrabbable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	uint8 CapType = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	int32 NewProductID = 0;

	bool HasFlag(EPropEventDirtyMask Flag) const
	{
		return (DirtyMask & static_cast<uint8>(Flag)) != 0;
	}

	void SetFlag(EPropEventDirtyMask Flag)
	{
		DirtyMask |= static_cast<uint8>(Flag);
	}

	void ClearFlag(EPropEventDirtyMask Flag)
	{
		DirtyMask &= ~static_cast<uint8>(Flag);
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FPropEventSyncDelta> : public TStructOpsTypeTraitsBase2<FPropEventSyncDelta>
{
	enum
	{
		WithNetSerializer = true,
	};
};

/**
 * Snapshot structure bundling an instance's identity, slot index, ProductID, and runtime defaults.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FISMInstanceRuntimeInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime Info")
	int32 ISID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime Info")
	int32 ISMI = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime Info")
	int32 ProductID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime Info")
	FPropPackedRuntimeData RuntimeDefaults;
};

/**
 * Lightweight network sync packet for batched thermal/runtime data updates.
 */
USTRUCT()
struct MARRYBYMIDNIGHT_API FPropRuntimeSyncItem
{
	GENERATED_BODY()

	UPROPERTY()
	int32 ISID = 0;

	UPROPERTY()
	float Temperature = 21.0f;

	UPROPERTY()
	int32 RemainingPortions = 0;

	UPROPERTY()
	int32 LastUpdatedMinute = 0;
};

/**
 * Dirty mask bitflags for fine-grained runtime field replication.
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EPropRuntimeDirtyMask : uint8
{
	None        = 0       UMETA(Hidden),
	Temperature = 1 << 0  UMETA(DisplayName = "Temperature"),
	Portions    = 1 << 1  UMETA(DisplayName = "Portions"),
	State       = 1 << 2  UMETA(DisplayName = "State"),
	Flags       = 1 << 3  UMETA(DisplayName = "Flags")
};
ENUM_CLASS_FLAGS(EPropRuntimeDirtyMask);

/**
 * Fine-grained per-ISID runtime field delta containing only modified properties.
 * Sent over the network when specific fields change (e.g. only portions when drinking, only temperature in fridge).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropRuntimeFieldDelta
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	int32 ISID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	uint8 DirtyFlags = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	float Temperature = 21.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	int32 RemainingPortions = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	EPropState State = EPropState::AtRest;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	int32 LastUpdatedMinute = 0;

	bool HasFlag(EPropRuntimeDirtyMask Flag) const
	{
		return (DirtyFlags & static_cast<uint8>(Flag)) != 0;
	}

	void SetFlag(EPropRuntimeDirtyMask Flag)
	{
		DirtyFlags |= static_cast<uint8>(Flag);
	}

	void ClearFlag(EPropRuntimeDirtyMask Flag)
	{
		DirtyFlags &= ~static_cast<uint8>(Flag);
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FPropRuntimeFieldDelta> : public TStructOpsTypeTraitsBase2<FPropRuntimeFieldDelta>
{
	enum
	{
		WithNetSerializer = true,
	};
};

/**
 * Per-instance context exposed in UPropBatchUpdateComponent's frame batch evaluation event.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropBatchEvaluationItem
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Batch Evaluation")
	int32 ISID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Batch Evaluation")
	int32 ProductID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Batch Evaluation")
	EPropRepresentation Representation = EPropRepresentation::ISM;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Batch Evaluation")
	EPropState State = EPropState::AtRest;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Batch Evaluation")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Batch Evaluation")
	FTransform WorldTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Batch Evaluation")
	float CurrentTemperature = 21.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Batch Evaluation")
	int32 RemainingPortions = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Batch Evaluation")
	float AgeMinutes = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Batch Evaluation")
	TWeakObjectPtr<AActor> ActiveActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Batch Evaluation")
	bool bIsGrabbable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Batch Evaluation")
	int32 IISMComponentID = 0;
};

/**
 * Extended interaction-specific runtime data for an ISID (Sections 26 & 33).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropInteractionRuntimeData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Runtime")
	TWeakObjectPtr<AActor> CurrentInteractor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Runtime")
	TWeakObjectPtr<AActor> InteractionOwner = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Runtime")
	EPropInteractionLockState InteractionState = EPropInteractionLockState::Free;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Runtime")
	int32 InteractionRequestID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Runtime")
	bool bInteractionLock = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Runtime")
	FName AttachSocketName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Runtime", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OpenAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Runtime")
	bool bActivated = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Runtime")
	float FuseTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Runtime")
	float Damage = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Runtime")
	float Durability = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Runtime")
	float Temperature = 21.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Runtime")
	float RemainingFuel = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Runtime")
	int32 RemainingPortions = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Runtime")
	int32 CustomFlags = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction Runtime")
	float LastInteractionTime = -1000.0f;
};

/**
 * Partial patch structure for updating specific fields of an ISID record (Section 17 & 20).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropInstancePatch
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	bool bModifyState = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	EPropState NewState = EPropState::AtRest;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	bool bModifyGrabbable = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	bool bNewIsGrabbable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	bool bModifyPortions = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	int32 NewRemainingPortions = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	bool bModifyTemperature = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	float NewTemperature = 21.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	bool bModifyOpenAmount = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	float NewOpenAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	bool bModifyActivated = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	bool bNewActivated = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	bool bModifyDamage = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	float NewDamage = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	bool bModifyDurability = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	float NewDurability = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	bool bModifyCustomFlags = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	int32 NewCustomFlags = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	bool bModifyTransform = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	FTransform NewTransform = FTransform::Identity;
};

/**
 * IISMDB — Shared, immutable Product Definition (Section 5).
 * Contains common data shared by all instances of the same ProductID.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropProductDefinition
{
	GENERATED_BODY()

	/** Unique Product Identifier (PID) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product Definition")
	int32 ProductID = 0;

	/** Static Mesh Identifier (SMID) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product Definition")
	int32 StaticMeshID = 0;

	/** Human-readable product name */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product Definition")
	FText DisplayName = FText::FromString(TEXT("Interactable Prop"));

	/** Visual static mesh asset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product Definition")
	TObjectPtr<UStaticMesh> StaticMesh = nullptr;

	/** Optional material overrides */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product Definition")
	TArray<TObjectPtr<UMaterialInterface>> MaterialOverrides;

	/** Optional source data asset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product Definition")
	TObjectPtr<UInteractablePropDataAsset> SourceDataAsset = nullptr;

	/** Optional custom Blueprint or C++ AActivePropActor subclass used when promoting this product from ISM to ASM */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product Definition", meta = (DisplayName = "Custom Active Prop Actor Class (BP)"))
	TSubclassOf<AActivePropActor> CustomActiveActorClass;

	/** Visual skeletal mesh asset used in ASM (if null, falls back to StaticMesh) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product Definition")
	TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;

	/** Interaction capability profile */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product Definition")
	FPropInteractionProfile InteractionProfile;

	/** Physics simulation and settling profile */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product Definition")
	FPropPhysicsProfile PhysicsProfile;

	/** Consumable portions and economy profile */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product Definition")
	FPropConsumableProfile ConsumableProfile;

	/** Thermal exchange and optimal serving temperature profile */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product Definition")
	FPropTemperatureProfile TemperatureProfile;

	/** Biological / psychological stimuli applied per portion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product Definition")
	TArray<FNPCSimulationStatModifier> StimulusProfile;

	/** Multi-dimensional flavor vector */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product Definition")
	FNPCTasteVector TasteProfile;

	/** Flavor vector when expired */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product Definition")
	FNPCTasteVector ExpiredTasteProfile;

	/** Expiration and spoilage profile */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product Definition")
	FPropExpiryProfile ExpiryProfile;

	/** Default packed runtime values for newly created instances of this product */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Product Definition")
	FPropPackedRuntimeData RuntimeDefaults;

	// -------------------------------------------------------------
	// Category 1 Feature Toggles (Zero-Copy Unique Data & Continuous Ticks)
	// -------------------------------------------------------------
	/** Enables allocating, replicating, and saving FPropInstanceUniqueData per ISID (false = 0 B static overhead) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unique Data")
	bool bEnableInstanceUniqueData = false;

	/** Enables UPropBatchUpdateComponent continuous ticks (Temperature, thermal Strength drain, GPU Slots 1..3) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unique Data", meta = (EditCondition = "bEnableInstanceUniqueData"))
	bool bEnableContinuousUpdateData = false;

	/** Allows settled uncapped partial/open bottles to demote to resting ISM keeping ContentLevel and CapSealed = 0 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unique Data|Demotion")
	bool bAllowUncappedFilledISMDemotion = true;

	/** If true, continuous thermal breach promotes resting ISM to ASM for 3D shatter/VFX; if false, converts silently in ISM */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unique Data|Thermal")
	bool bPromoteISMOnContinuousTempFailure = false;

	/** Default initial Category 2 UniqueData copied onto newly spawned instances when bEnableInstanceUniqueData is true */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unique Data", meta = (EditCondition = "bEnableInstanceUniqueData"))
	FPropInstanceUniqueData DefaultUniqueData;

	// -------------------------------------------------------------
	// Bottle Containment Capacity & Overfill Rules
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

	// -------------------------------------------------------------
	// Bottle Shell Material, Structural Strength & Deformation
	// -------------------------------------------------------------
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

	// -------------------------------------------------------------
	// Bottle Thermal Physics & Thresholds
	// -------------------------------------------------------------
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

	// -------------------------------------------------------------
	// Cap Hardware, Compatible Contents & Product Transformations
	// -------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap", meta = (Bitmask, BitmaskEnum = "/Script/MarryByMidnight.EBottleCapMaskFlags"))
	uint8 SupportedCapMask = 0xFF;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap")
	EBottleCapType DefaultCapType = EBottleCapType::Twist;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap")
	TObjectPtr<UStaticMesh> DefaultCapMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap", meta = (ClampMin = "0.0"))
	float CapReleasePressure = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap", meta = (ClampMin = "0.0"))
	float SafeOpenPressureThreshold = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TorqueSweetSpotMin = 0.41f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TorqueSweetSpotMax = 0.75f;

	/** Compatible liquid ContentIDs for SetBottleContentType (empty = accepts all registered ContentIDs) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	TArray<int32> SupportedContentIDs;

	/** Target ProductID when bottle becomes empty (ContentReset); 0 = single-use or no auto-conversion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	int32 EmptyBottleProductID = 0;

	/** Optional mapping ContentID -> Filled resting ProductID for flavor-specific ISM meshes/labels */
	UPROPERTY(NotReplicated, EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	TMap<int32, int32> FilledProductIDByContentID;
	/** Parallel key/value wire representation; local map stays available to existing callers. */
	UPROPERTY() TArray<int32> FilledContentIDs;
	UPROPERTY() TArray<int32> FilledProductIDs;

	/** Target BrokenGlass orchestrator ProductID for 1-to-Many breakage transformation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	int32 BrokenProductID = 0;

	/** Target Melted ProductID for 1-to-1 extreme heat transformation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	int32 MeltedProductID = 0;

	/** Target Burst ProductID for 1-to-1 overpressure rupture transformation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	int32 BurstBrokenProductID = 0;

	/** If false (Pure Fragments), BrokenGlass orchestrator despawns cleanly once shards spawn; if true, retains broken hull ISM */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	bool bPersistBrokenHullAfterFracture = false;

	/** Independent child fragment recipes spawned by BreakageFracture */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	TArray<FPropFractureFragmentEntry> FractureFragments;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Transformations")
	TObjectPtr<UFragmentAssemblyDataAsset> FragmentAssembly;

	/** Populates a product definition from a UInteractablePropDataAsset */
	static FPropProductDefinition FromDataAsset(const UInteractablePropDataAsset* InAsset, int32 InProductID, int32 InStaticMeshID = 0);

	/** Returns true if this product definition has valid ID and mesh configuration */
	bool IsValidDefinition() const { return ProductID > 0; }
};

/**
 * Authoritative Per-Instance Record / Runtime Index mapped by ISID (Sections 6 & 30).
 * Rule 3 & 5: ISID is the permanent identity; runtime information belongs to ISID.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FISIDRuntimeIndex
{
	GENERATED_BODY()

	/** Permanent logical instance identity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISID Record")
	int32 ISID = 0;

	/** Product definition identifier (PID) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISID Record")
	int32 ProductID = 0;

	/** Owning IISM component identifier (ICID) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISID Record")
	int32 IISMComponentID = 0;

	/** Local ISM instance slot index inside the IISM component (never treated as permanent identity) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISID Record")
	int32 ISMI = INDEX_NONE;

	/** Current physical representation (ISM resting vs ASM active) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISID Record")
	EPropRepresentation Representation = EPropRepresentation::ISM;

	/** Authoritative prop state */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISID Record")
	EPropState State = EPropState::AtRest;

	/** Monotonically increasing version counter to detect stale updates */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISID Record")
	int32 Version = 1;

	/** Active ASM unit identifier (ASMID) when Representation == EPropRepresentation::ASM (0 when resting in ISM) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISID Record")
	int32 ASMID = 0;

	/** Active ASM actor when Representation == EPropRepresentation::ASM */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISID Record")
	TObjectPtr<AActivePropActor> ActiveActor = nullptr;

	/** Original destruction carrier; no pooled ASMID is assigned during this stage. */
	UPROPERTY() FGuid FragmentGroupID;
	UPROPERTY() int32 FragmentPieceIndex = INDEX_NONE;
	UPROPERTY() double FragmentPoseTime = 0;

	/** Direct reference to the owning IISM component */
	UPROPERTY(BlueprintReadOnly, Category = "ISID Record")
	TWeakObjectPtr<UInteractableISMComponent> ComponentRef = nullptr;

	/** Compact packed runtime data */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISID Record")
	FPropPackedRuntimeData PackedRuntimeData;

	/** Extended interaction and dynamic runtime data */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISID Record")
	FPropInteractionRuntimeData RuntimeData;

	/** Authoritative gameplay world transform of the prop */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISID Record")
	FTransform ActiveTransform = FTransform::Identity;

	/** Bitmask of EPropDirtyFlags indicating pending replication changes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISID Record")
	int32 DirtyFlags = 0;

	/** Server/local WorldElapsedSeconds timestamp of latest reliable Event Update (guards stale Continuous sync packets) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISID Record")
	double LastDiscreteEventTime = 0.0;

	bool IsValidRecord() const { return ISID > 0; }

	/** Mirrors key derived fields into legacy PackedRuntimeData for backwards compatibility */
	void SyncLegacyPackedFromUniqueData(const FPropInstanceUniqueData& Unique, const FPropProductDefinition* ProductDef = nullptr)
	{
		PackedRuntimeData.CreationMinute = FMath::Max(0, FMath::FloorToInt(Unique.EventData.ContentCreationTime / 60.0));
		PackedRuntimeData.LastUpdatedMinute = FMath::Max(0, FMath::FloorToInt(Unique.ContinuousData.LastUpdatedTime / 60.0));
		PackedRuntimeData.Temperature = Unique.ContinuousData.Temperature;
		PackedRuntimeData.bIsGrabbable = Unique.EventData.bIsGrabbable;
		const int32 MaxPortions = (ProductDef && ProductDef->ConsumableProfile.NumOfPortions > 0)
			? ProductDef->ConsumableProfile.NumOfPortions
			: 4;
		PackedRuntimeData.RemainingPortions = FMath::Clamp(FMath::RoundToInt(Unique.EventData.ContentLevel * static_cast<float>(MaxPortions)), 0, 255);
	}
};

/** Alias matching Section 6 (FPropInstanceRecord) and Section 30 (FISIDRuntimeIndex) */
using FPropInstanceRecord = FISIDRuntimeIndex;

/**
 * Runtime settings for a UInteractableISMComponent (Section 31).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FISMComponentRuntimeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Settings")
	bool bEnableCollisionWhenResting = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Settings")
	FName RestingCollisionProfile = FName(TEXT("BlockAll"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Settings")
	bool bAutoRegisterWithPropManager = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Settings")
	float CullDistanceMax = 0.0f;
};

/**
 * Compact network delta record for a single ISID in bulk replication (Section 34).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropInstanceDelta
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	int32 ISID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	int32 ProductID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	int32 IISMComponentID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	int32 ISMI = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	int32 DirtyFlags = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	EPropRepresentation Representation = EPropRepresentation::ISM;

	UPROPERTY() FGuid FragmentGroupID;
	UPROPERTY() int32 FragmentPieceIndex = INDEX_NONE;
	UPROPERTY() double FragmentPoseTime = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	EPropState State = EPropState::AtRest;

	/** Deterministic 64-bit packed runtime state */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	int64 PackedRuntimeBits = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	FTransform Transform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	float OpenAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	bool bActivated = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	float Damage = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	float Durability = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	int32 CustomFlags = 0;

	/** True when this delta carries full sparse Category 2 UniqueData (initial snapshot / creation / full sync) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	bool bHasUniqueData = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	FPropInstanceUniqueData UniqueData;
};

/**
 * Lightweight entry for batch ISM -> ASM conversion sent on fast replication at end of frame (max 25 per frame).
 * Contains only the logical ISID and the target ASMID (8 bytes total, highly optimal for MTU budget).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropASMConversionEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Conversion")
	int32 ISID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Conversion")
	int32 ASMID = 0;
};

/**
 * Bounded batch of dirty ISID deltas sent to clients (Sections 34, 58, 66).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropBulkUpdateBatch
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	int32 BatchSequence = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	int32 TaskID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	bool bIsFullSnapshot = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	bool bIsFirstInitBatch = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	bool bIsFinalInitBatch = false;

	/** Product definitions required to generate products and IISM components on the client side (populated ONLY in initial snapshot) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	TArray<FPropProductDefinition> ProductDefinitions;

	/** Shared liquid ContentDefinitions synced alongside initial snapshot */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	TArray<FPropContentDefinition> ContentDefinitions;

	/** Server monotonic clock snapshot aligned with initial batch */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	double SnapshotWorldElapsedSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	TArray<FPropInstanceDelta> Records;

	/** Logical ISIDs released to EmptyInstances so clients reuse their physical ISM slots */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	TArray<int32> ReleasedToEmptyISIDs;

	/** Logical ISIDs permanently destroyed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	TArray<int32> DeletedISIDs;

	int32 GetEstimatedByteSize() const
	{
		return sizeof(FPropBulkUpdateBatch)
			+ (ProductDefinitions.Num() * sizeof(FPropProductDefinition))
			+ (ContentDefinitions.Num() * sizeof(FPropContentDefinition))
			+ (Records.Num() * sizeof(FPropInstanceDelta))
			+ (ReleasedToEmptyISIDs.Num() * sizeof(int32))
			+ (DeletedISIDs.Num() * sizeof(int32));
	}
};

/**
 * Lightweight frame-budgeted sync packet for an actively simulating/moving prop.
 * Streamed at up to 10-20 props per frame to steer client physics without teleportation or bandwidth saturation.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FActivePropPhysicsSync
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Sync")
	int32 ISID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Sync")
	int32 ASMID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Sync")
	FVector_NetQuantize10 Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Sync")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Sync")
	FVector_NetQuantize10 LinearVelocity = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Sync")
	FVector_NetQuantize10 AngularVelocity = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Sync")
	EPropState State = EPropState::Unsettled;
};

/**
 * Per-client synchronization state and delivery stream (Sections 30, 34, 59).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropClientSyncState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Client Sync")
	TWeakObjectPtr<APlayerController> ClientController = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Client Sync")
	int32 LastSentBatchSequence = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Client Sync")
	int32 LastAckedBatchSequence = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Client Sync")
	int32 BytesSentThisFrame = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Client Sync")
	int32 InstancesSentThisFrame = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Client Sync")
	bool bNeedsFullSnapshot = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Client Sync")
	bool bClientSyncReady = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Client Sync")
	float LastSnapshotSendTime = -1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Client Sync")
	int32 SnapshotSendCount = 0;

	/** Pending unacknowledged batches for retry if needed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Client Sync")
	TArray<FPropBulkUpdateBatch> PendingBatches;

	/** Queued dirty ISIDs waiting for this client's bandwidth budget */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Client Sync")
	TArray<int32> QueuedISIDs;
};

/**
 * Independent task record managed by the Prop Manager Task System (Section 35).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropTaskRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task System")
	int32 TaskID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task System")
	int32 RequestID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task System")
	int32 ISID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task System")
	int32 TargetVersion = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task System")
	EPropTaskType TaskType = EPropTaskType::UpdateInstance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task System")
	EPropTaskStatus Status = EPropTaskStatus::Pending;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task System")
	TArray<TObjectPtr<APlayerController>> TargetClients;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task System")
	int32 RetryCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task System")
	float CreatedTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task System")
	float LastSendTime = 0.0f;

	/** Optional batch generation parameters (e.g. Task 100: Generate 500 Props) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task System")
	int32 ProductID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task System")
	TArray<FTransform> PendingTransforms;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task System")
	int32 ProcessedCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task System")
	float ProgressRatio = 0.0f;
};

/**
 * Serialized logical prop record for Save/Load persistence (Sections 36, 48, 60, 61).
 * Rule 23 & 39: Save/load uses ISID as logical identity; saved ISMI is repaired on load if physical layout changed.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropSerializedInstance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	int32 ISID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	int32 ProductID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	int32 IISMComponentID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	int32 ISMI = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	EPropState State = EPropState::AtRest;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	EPropRepresentation Representation = EPropRepresentation::ISM;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	FTransform ActiveTransform = FTransform::Identity;

	UPROPERTY(SaveGame) FVector LinearVelocity = FVector::ZeroVector;
	UPROPERTY(SaveGame) FVector AngularVelocity = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	int64 PackedRuntimeBits = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	FPropPackedRuntimeData PackedRuntimeData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	float OpenAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	bool bActivated = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	float Damage = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	float Durability = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	float RemainingFuel = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	int32 CustomFlags = 0;

	/** True when this instance stores sparse Category 2 unique data (SchemaVersion >= 2) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	bool bHasUniqueData = false;

	/** Persisted Continuous + Event Unique Data (SchemaVersion >= 2) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	FPropInstanceUniqueData UniqueData;
};

/**
 * Container for world prop save data (Section 30 & 36).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropWorldSaveData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	int32 SchemaVersion = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	double WorldElapsedSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	int32 NextISID = 10001;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	TArray<FPropSerializedInstance> SavedInstances;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	TArray<int32> DestroyedISIDs;

	UPROPERTY(SaveGame)
	FSharedWalletSaveData SharedWallet;

	UPROPERTY(SaveGame)
	FShopCheckoutSaveData ShopCheckout;

	UPROPERTY(SaveGame)
	TArray<FPropProductDefinition> ProductDefinitions;

	UPROPERTY(SaveGame)
	TArray<FPropContentDefinition> ContentDefinitions;

	UPROPERTY(SaveGame) TArray<FReleasedLiquid> ReleasedLiquids;
};

/**
 * USaveGame object for persisting the Prop Manager database to disk (Section 36).
 */
UCLASS(BlueprintType)
class MARRYBYMIDNIGHT_API UPropDatabaseSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	FPropWorldSaveData WorldSaveData;
};

/**
 * Shared Product Definition Database asset (IISMDB — Section 4 & 18).
 * Holds shared static product definitions (Mesh, Materials, Collision, Physics, Thermal, Consumable, Interaction profiles)
 * keyed by ProductID (PID).
 */
UCLASS(BlueprintType, Blueprintable)
class MARRYBYMIDNIGHT_API UIISMDB : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Shared product definitions keyed by ProductID (PID) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IISMDB")
	TMap<int32, FPropProductDefinition> ProductDefinitions;
};

/**
 * Per-Instance Runtime Information Database asset/container (PIIDB — Section 4 & 19).
 * Holds unique per-instance runtime records keyed by permanent logical identity (ISID).
 */
UCLASS(BlueprintType, Blueprintable)
class MARRYBYMIDNIGHT_API UPIIDB : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Authoritative per-instance runtime records keyed by ISID */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PIIDB")
	TMap<int32, FISIDRuntimeIndex> InstanceRecords;
};

/**
 * Universal database entry for an interactable prop or instanced static mesh.
 * Preserved and bridged with ISID / ProductID for full compatibility across existing systems.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FInteractablePropData
{
	GENERATED_BODY()

	/** Unique Instance or Item Identifier (ISMI local slot or legacy ID) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Data")
	int32 InstanceId = INDEX_NONE;

	/** Permanent logical instance identity (ISID) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Data")
	int32 ISID = 0;

	/** Product Definition ID (PID) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Data")
	int32 ProductID = 0;

	/** IISM Component ID (ICID) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Data")
	int32 IISMComponentID = 0;

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
	/** Option to enable temperature simulation update. If false, prop only has ConsumableTemperature and does not tick adjust to environment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature", meta = (DisplayName = "Enable Temperature Update"))
	bool bEnableTemperatureUpdate = false;

	/** Ideal serving temperature in Celsius (e.g. 72.0°C hot, 4.0°C cold) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature", meta = (Units = "Celsius"))
	float ConsumableTemperature = 21.0f;

	/** Current physical temperature in Celsius (slowly adjusts to environment if enabled) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature", meta = (Units = "Celsius"))
	float CurrentTemperature = 21.0f;

	/** Max dopamine bonus awarded per portion when CurrentTemperature matches ConsumableTemperature (decreases and goes negative as temperature diverges) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature")
	float TemperatureDopamineBonus = 0.5f;

	/** Rate at which current temp normalizes towards environment ambient temp */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature")
	float ThermalExchangeRate = 0.015f;

	// -------------------------------------------------------------
	// Economy & Portions
	// -------------------------------------------------------------
	/** Monetary purchase price or value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	float Price = 2.0f;

	/** Total number of portions (e.g. 4 sips or bites) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portions", meta = (DisplayName = "Num of Portions", ClampMin = "0"))
	int32 NumOfPortions = 4;

	/** Current remaining portions (e.g. player drinks 2 sips, 2 left) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portions", meta = (ClampMin = "0"))
	int32 RemainingPortions = 4;

	/** Quantity level fraction [0.0 = Empty, 1.0 = Full] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portions", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float QuantityLevel = 1.0f;

	/** Portion size fraction calculated as 1.0 / NumOfPortions */
	float GetPortionSize() const { return (NumOfPortions > 0) ? (1.0f / static_cast<float>(NumOfPortions)) : 0.0f; }

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

	/** Angular velocity stop tolerance level (deg/s). Evaluated alongside linear velocity during Settling */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	float AngularVelocityTolerance = 15.0f;

	// -------------------------------------------------------------
	// Expiry & Freshness
	// -------------------------------------------------------------
	/** Whether this prop has an expiry date */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freshness")
	bool bHasExpiry = true;

	/** Lifetime in minutes before expiring */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freshness", meta = (EditCondition = "bHasExpiry"))
	float ExpiryLifetimeMinutes = 60.0f;

	/** Age in minutes since creation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freshness", meta = (EditCondition = "bHasExpiry"))
	float AgeMinutes = 0.0f;

	/** Sickness level added when consumed expired */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freshness", meta = (EditCondition = "bHasExpiry"))
	float SicknessLevel = 35.0f;

	/** Dopamine subtracted when consumed expired */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freshness", meta = (EditCondition = "bHasExpiry"))
	float ExpiredDopaminePenalty = 25.0f;

	/** Stimuli applied per portion when consumed expired */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freshness", meta = (EditCondition = "bHasExpiry", DisplayName = "Expiry Stimulus Profile"))
	TArray<FNPCSimulationStatModifier> ExpiryStimulusProfile;

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
	bool IsAtIdealTemperature(float Tolerance = 2.0f) const { return FMath::Abs(CurrentTemperature - ConsumableTemperature) <= Tolerance; }

	/** Calculates dopamine delta per portion based on how close CurrentTemp is to IdealTemp (ConsumableTemperature), scaled by consumer tolerance */
	static float CalculateTemperatureDopamine(float CurrentTemp, float IdealTemp, float MaxDopamineBonus, float ConsumerTolerance = 0.5f);

	/** Calculates automatic consumer body temperature delta from item CurrentTemp (hot items warm consumer, cold items cool consumer) */
	static float CalculateBodyTemperatureDelta(float CurrentTemp, float NeutralTemp = 21.0f);

	/** Consumes portions (e.g. 2 sips). Returns actual portions consumed */
	int32 ConsumePortions(int32 PortionsToConsume = 1);

	/** Updates temperature toward ambient */
	void UpdateTemperature(float AmbientTemperature, float DeltaSeconds);

	/** Synchronizes an ephemeral stack snapshot from an authoritative FISIDRuntimeIndex, shared definitions, and Category 2 UniqueData */
	void SyncFromISIDRecord(
		const FISIDRuntimeIndex& Record,
		const FPropProductDefinition* ProductDef = nullptr,
		const FPropContentDefinition* ContentDef = nullptr,
		const FPropInstanceUniqueData* UniqueData = nullptr,
		double CurrentWorldSeconds = 0.0);
};
