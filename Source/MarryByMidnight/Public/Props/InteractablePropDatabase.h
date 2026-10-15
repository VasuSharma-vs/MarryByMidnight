#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/NetSerialization.h"
#include "NPCEnumsAndTypes.h"
#include "GameFramework/SaveGame.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "InteractablePropDatabase.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UInteractablePropDataAsset;
class AActivePropActor;
class UInteractableISMComponent;

/**
 * Physical representation mode of a logical prop (ISID).
 * Rule 1 & 2: ISM is the persistent resting representation; ASM is the temporary active representation.
 */
UENUM(BlueprintType)
enum class EPropRepresentation : uint8
{
	ISM UMETA(DisplayName = "ISM (Resting Instanced Representation)"),
	ASM UMETA(DisplayName = "ASM (Active Static Mesh Actor)")
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable Profile", meta = (DisplayName = "Num of Portions", ClampMin = "1"))
	int32 NumOfPortions = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable Profile")
	float Price = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable Profile")
	bool bDestroyWhenEmpty = true;

	/** Portion size fraction calculated as 1.0 / NumOfPortions */
	float GetPortionSize() const { return (NumOfPortions > 0) ? (1.0f / static_cast<float>(NumOfPortions)) : 1.0f; }
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

	bool IsValidRecord() const { return ISID > 0; }
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

	/** Product definitions required to generate products and IISM components on the client side */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replication")
	TArray<FPropProductDefinition> ProductDefinitions;

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
};

/**
 * Container for world prop save data (Section 30 & 36).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropWorldSaveData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	int32 SchemaVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	int32 NextISID = 10001;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	TArray<FPropSerializedInstance> SavedInstances;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SaveLoad")
	TArray<int32> DestroyedISIDs;
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portions", meta = (DisplayName = "Num of Portions", ClampMin = "1"))
	int32 NumOfPortions = 4;

	/** Current remaining portions (e.g. player drinks 2 sips, 2 left) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portions", meta = (ClampMin = "0"))
	int32 RemainingPortions = 4;

	/** Quantity level fraction [0.0 = Empty, 1.0 = Full] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portions", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float QuantityLevel = 1.0f;

	/** Portion size fraction calculated as 1.0 / NumOfPortions */
	float GetPortionSize() const { return (NumOfPortions > 0) ? (1.0f / static_cast<float>(NumOfPortions)) : 1.0f; }

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

	/** Synchronizes from an authoritative FISIDRuntimeIndex and FPropProductDefinition */
	void SyncFromISIDRecord(const FISIDRuntimeIndex& Record, const FPropProductDefinition* ProductDef = nullptr);
};
