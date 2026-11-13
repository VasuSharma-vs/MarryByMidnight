#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PropBatchUpdateComponent.h"
#include "InteractablePropSenseSightComponent.generated.h"

class APropManager;

/**
 * Operations for observer PID management (Section 4 Group 2).
 */
UENUM(BlueprintType)
enum class EObserverPIDOperation : uint8
{
	RegisterPID        UMETA(DisplayName = "Register PID"),
	UnregisterPID      UMETA(DisplayName = "Unregister PID"),
	SetInstanceLimit   UMETA(DisplayName = "Set Instance Limit")
};

/**
 * Result details for a perceived prop in sight.
 */
USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FPropSightPerceptionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	int32 ISID = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	int32 ProductID = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	float Distance = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	EPropRepresentation Representation = EPropRepresentation::ISM;

	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	EPropState State = EPropState::AtRest;

	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	TWeakObjectPtr<AActor> ActiveActor = nullptr;
};

/**
 * Single detected prop item containing ISID and Location as specified in Section 2.
 */
USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FPropSightDetectedItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	int32 ISID = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	int32 ProductID = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	float Distance = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	EPropRepresentation Representation = EPropRepresentation::ISM;

	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	EPropState State = EPropState::AtRest;

	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	TWeakObjectPtr<AActor> ActiveActor = nullptr;
};

/**
 * Wrapper struct holding detected props information in sight.
 * Provides both ISID-based tracking and ProductID (PID) based identification for Blueprint AI logic.
 */
USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FPropSightDetectedProps
{
	GENERATED_BODY()

	/** Map of [ISID -> World Location] for all detected props in sight (legacy compatibility) */
	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	TMap<int32, FVector> Props;

	/** Unique list of ProductIDs (PIDs) currently detected in sight (matches the PIDs registered on continuous sight) */
	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	TArray<int32> DetectedProductIDs;

	/** Map of [ISID -> ProductID] for every detected prop in sight */
	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	TMap<int32, int32> ISIDToProductID;

	/** Full detailed list of all detected props in sight (each entry has ISID, ProductID, WorldLocation, Distance, etc.) */
	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	TArray<FPropSightPerceptionResult> DetectedPropsList;
};

/**
 * Delegate called when a one-shot sight query completes its full round-robin sweep over world props.
 */
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnPropSightQueryCompleted, bool, bFoundAny, const FPropSightDetectedProps&, DetectedProps, const FPropSightPerceptionResult&, ClosestProp);

/**
 * Blueprint event dispatcher for continuous sight updates or query completions.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnPropSightPerceptionUpdated, AActor*, ObserverActor, bool, bFoundAny, const FPropSightDetectedProps&, DetectedProps, const FPropSightPerceptionResult&, ClosestProp);

/**
 * Continuous Observer Data structure as specified in Section 2.A.
 */
USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FContinuousObserverData
{
	GENERATED_BODY()

	/** Observer Actor Reference */
	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	TWeakObjectPtr<AActor> ObserverActor = nullptr;

	/** Structure containing ISID and Location */
	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	TArray<FPropSightDetectedItem> DetectedItems;

	/** Fast lookup index map of [ISID -> Index in DetectedItems] */
	TMap<int32, int32> ISIDToIndexMap;

	/** Observer unique debug color */
	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	FColor DebugColor = FColor::Yellow;

	/** Vision range and other observation settings */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	float SightRadius = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	float PeripheralVisionHalfAngle = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	TArray<int32> TargetProductIDs;

	/** Per-PID requested instance limits [PID -> Max Count] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	TMap<int32, int32> PIDInstanceLimits;

	/** Per-PID selected items, each sorted in ascending distance order (closest at index 0, farthest at last index) */
	TMap<int32, TArray<FPropSightDetectedItem>> PerPIDDetectedItems;

	/** When true, only perceives products listed in TargetProductIDs. If false, perceives all products. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	bool bFilterByPID = false;

	/** When true, allows observing all product types without restriction (bypasses PID filter). Default is false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	bool bObserveAllPIDs = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	bool bIgnoreBlocked = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	bool bRequireGrabbable = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	bool bDrawDebugCone = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	bool bDrawDebugVisibleObjects = false;

	FVector SightLocationOverride = FVector::ZeroVector;
	FRotator SightRotationOverride = FRotator::ZeroRotator;
	bool bHasManualTransformOverride = false;

	/** Last evaluated origin used for distance calculations */
	FVector LastEvaluatedLocation = FVector(MAX_flt, MAX_flt, MAX_flt);

	/** True if detected items were added, modified, or removed and need sorting */
	bool bNeedsSort = false;

	TSet<int32> IgnoredISIDs;

	FPropSightPerceptionResult BestClosestProp;
	float BestDistanceSq = MAX_FLT;
};

/**
 * One-Time Observer Data structure as specified in Section 2.B.
 */
USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FOneTimeObserverData
{
	GENERATED_BODY()

	int64 QueryID = 0;

	/** Observer Actor Reference */
	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	TWeakObjectPtr<AActor> ObserverActor = nullptr;

	/** Structure containing ISID and Location */
	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	TArray<FPropSightDetectedItem> DetectedItems;

	/** Fast lookup index map of [ISID -> Index in DetectedItems] */
	TMap<int32, int32> ISIDToIndexMap;

	/** Observer unique debug color */
	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	FColor DebugColor = FColor::Cyan;

	/** Vision range and other observation settings */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	float SightRadius = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	float PeripheralVisionHalfAngle = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	TArray<int32> TargetProductIDs;

	/** Per-PID requested instance limits [PID -> Max Count] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	TMap<int32, int32> PIDInstanceLimits;

	/** Per-PID selected items, each sorted in ascending distance order (closest at index 0, farthest at last index) */
	TMap<int32, TArray<FPropSightDetectedItem>> PerPIDDetectedItems;

	/** When true, only perceives products listed in TargetProductIDs. If false, perceives all products. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	bool bFilterByPID = false;

	/** When true, allows observing all product types without restriction (bypasses PID filter). Default is false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	bool bObserveAllPIDs = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	bool bIgnoreBlocked = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	bool bRequireGrabbable = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	bool bDrawDebugCone = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Perception")
	bool bDrawDebugVisibleObjects = false;

	FVector SightLocationOverride = FVector::ZeroVector;
	FRotator SightRotationOverride = FRotator::ZeroRotator;
	bool bHasManualTransformOverride = false;

	/** Last evaluated origin used for distance calculations */
	FVector LastEvaluatedLocation = FVector(MAX_flt, MAX_flt, MAX_flt);

	/** True if detected items were added, modified, or removed and need sorting */
	bool bNeedsSort = false;

	/** Register Index identifying the iteration point at which results should be received */
	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	int32 RegisterIndex = 0;

	/** Registered Loop Size recording the number of product instances being processed when registered */
	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	int32 RegisteredLoopSize = 0;

	/** Total items evaluated since registration */
	int32 TotalItemsProcessed = 0;

	/** Tracks if batch iteration has wrapped around index 0 */
	bool bHasWrappedOnce = false;

	FPropSightPerceptionResult BestClosestProp;
	float BestDistanceSq = MAX_FLT;

	/** ISIDs explicitly ignored by this query */
	TSet<int32> IgnoredISIDs;

	FOnPropSightQueryCompleted OnQueryCompleted;
};

/**
 * Component providing time-sliced AI vision perception for interactable props (ISM and ASM).
 * Plugs into UPropBatchUpdateComponent to inspect props using cone-of-vision and line-of-sight checks.
 */
UCLASS(ClassGroup = (Props), meta = (BlueprintSpawnableComponent))
class COLOSSALFIZZCO_API UInteractablePropSenseSightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractablePropSenseSightComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// -------------------------------------------------------------------------
	// One-Shot Sight Query API
	// -------------------------------------------------------------------------

	/**
	 * Requests an asynchronous, time-sliced sight search over the world's props for the closest product.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense")
	int64 RequestClosestProductInSight(
		const TArray<int32>& TargetProductIDs,
		float SightRadius,
		float PeripheralVisionHalfAngle,
		AActor* ObserverActor,
		const FOnPropSightQueryCompleted& OnCompleted,
		bool bIgnoreBlocked = false,
		FVector SightLocationOverride = FVector::ZeroVector,
		FRotator SightRotationOverride = FRotator::ZeroRotator,
		ECollisionChannel TraceChannel = ECC_Visibility,
		bool bInDrawDebugCone = false,
		bool bInDrawDebugVisibleObjects = false,
		bool bInRequireGrabbable = false
	);

	/** Cancels an active one-shot sight query by ID */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense")
	bool CancelSightQuery(int64 QueryID);

	/**
	 * Requests an asynchronous, time-sliced sight search over the world's props with per-PID instance limits (Section 1).
	 *
	 * @param InPIDInstanceLimits Map of [PID -> Max Requested Instance Count].
	 * @param SightRadius Vision range radius in cm.
	 * @param PeripheralVisionHalfAngle Vision cone half-angle in degrees.
	 * @param ObserverActor The actor doing the observing.
	 * @param OnCompleted Delegate executed when the query completes.
	 * @param bIgnoreBlocked When true, skips line-of-sight obstruction raycasts (sees through walls).
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Query")
	int64 RequestClosestProductInSightWithLimits(
		const TMap<int32, int32>& InPIDInstanceLimits,
		float SightRadius,
		float PeripheralVisionHalfAngle,
		AActor* ObserverActor,
		const FOnPropSightQueryCompleted& OnCompleted,
		bool bIgnoreBlocked = false,
		FVector SightLocationOverride = FVector::ZeroVector,
		FRotator SightRotationOverride = FRotator::ZeroRotator,
		ECollisionChannel TraceChannel = ECC_Visibility,
		bool bInDrawDebugCone = false,
		bool bInDrawDebugVisibleObjects = false,
		bool bInRequireGrabbable = false
	);

	// -------------------------------------------------------------------------
	// Continuous Observer Registration API
	// -------------------------------------------------------------------------

	/**
	 * Registers an actor (AI or player) for continuous perceptual tracking of props within sight.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense")
	void RegisterObserverForContinuousSight(
		AActor* ObserverActor,
		const TArray<int32>& TargetProductIDs,
		float SightRadius = 2500.0f,
		float PeripheralVisionHalfAngle = 60.0f,
		bool bIgnoreBlocked = false,
		FVector SightLocationOverride = FVector::ZeroVector,
		FRotator SightRotationOverride = FRotator::ZeroRotator,
		ECollisionChannel TraceChannel = ECC_Visibility,
		bool bInDrawDebugCone = false,
		bool bInDrawDebugVisibleObjects = false,
		bool bInRequireGrabbable = false
	);

	/**
	 * Group 1: Registers or updates an observer's sight parameters.
	 * Keeps sight settings separate from PID requirements and debug options.
	 * If the observer is already registered, updates its sight parameters without resetting PID limits.
	 *
	 * Note on bIgnoreBlocked:
	 * When true, line-of-sight raycast obstruction traces are ignored/skipped (props are seen through walls).
	 * When false, line-of-sight raycast traces must be unobstructed between observer and prop.
	 *
	 * @param ObserverActor The observer actor to configure.
	 * @param SightRadius Vision radius in cm.
	 * @param PeripheralVisionHalfAngle Vision cone half-angle in degrees.
	 * @param bIgnoreBlocked True to ignore line-of-sight obstruction raycasts.
	 * @param bRequireGrabbable True if props must be grabbable to be seen.
	 * @param TraceChannel Collision channel used for line-of-sight raycasts.
	 * @param SightLocationOverride Custom eye location override.
	 * @param SightRotationOverride Custom eye rotation override.
	 * @return True if registered or updated successfully; false if ObserverActor is invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Registration")
	bool RegisterObserverSightSettings(
		AActor* ObserverActor,
		float SightRadius = 2500.0f,
		float PeripheralVisionHalfAngle = 60.0f,
		bool bIgnoreBlocked = false,
		bool bRequireGrabbable = false,
		ECollisionChannel TraceChannel = ECC_Visibility,
		FVector SightLocationOverride = FVector::ZeroVector,
		FRotator SightRotationOverride = FRotator::ZeroRotator
	);

	/**
	 * Registers an observer for continuous sight with per-PID instance limits (Section 1).
	 *
	 * @param ObserverActor The actor doing the observing.
	 * @param InPIDInstanceLimits Map of [PID -> Requested Maximum Instance Count].
	 * @param SightRadius Vision range radius in cm.
	 * @param PeripheralVisionHalfAngle Half-angle for vision cone.
	 * @param bIgnoreBlocked True to ignore line-of-sight obstruction raycasts.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Registration")
	void RegisterObserverWithPIDLimits(
		AActor* ObserverActor,
		const TMap<int32, int32>& InPIDInstanceLimits,
		float SightRadius = 2500.0f,
		float PeripheralVisionHalfAngle = 60.0f,
		bool bIgnoreBlocked = false,
		FVector SightLocationOverride = FVector::ZeroVector,
		FRotator SightRotationOverride = FRotator::ZeroRotator,
		ECollisionChannel TraceChannel = ECC_Visibility,
		bool bInDrawDebugCone = false,
		bool bInDrawDebugVisibleObjects = false,
		bool bInRequireGrabbable = false
	);

	// -------------------------------------------------------------------------
	// Section 12: Observer Unregistration Functions
	// -------------------------------------------------------------------------

	/**
	 * Unregisters an observer from one-time observation.
	 * Cancels all pending one-time queries for this observer and prevents callbacks from firing.
	 *
	 * @param ObserverActor The observer actor (or its controller/pawn) to unregister.
	 * @return True if one or more one-time registrations were found and removed; false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Unregistration")
	bool UnregisterOneTimeObserver(AActor* ObserverActor);

	/**
	 * Unregisters an observer from continuous sight perception.
	 * Clears observation results and prevents future continuous perception updates.
	 *
	 * @param ObserverActor The observer actor (or its controller/pawn) to unregister.
	 * @return True if the continuous registration was found and removed; false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Unregistration")
	bool UnregisterContinuousObserver(AActor* ObserverActor);

	/**
	 * Combined unregistration function.
	 * Removes all registrations belonging to that observer from both continuous and one-time systems.
	 * Cancels pending one-time requests and stops continuous perception updates.
	 *
	 * @param ObserverActor The observer actor (or its controller/pawn) to unregister.
	 * @return True if the observer was unregistered from either or both systems; false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Unregistration")
	bool UnregisterObserver(AActor* ObserverActor);

	/** Updates the sight location and rotation overrides dynamically for an observer */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense")
	void UpdateObserverSightTransform(AActor* ObserverActor, FVector NewSightLocation, FRotator NewSightRotation);

	/** Clears custom sight location and rotation overrides, restoring automatic dynamic tracking */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense")
	void ClearObserverSightOverrides(AActor* ObserverActor);

	/** Toggles whether this observer requires props to be grabbable to perceive them */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense")
	void SetObserverRequireGrabbable(AActor* ObserverActor, bool bInRequireGrabbable);

	/** Immediately evaluates all active props in the world for this observer */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense")
	void ForceEvaluateObserver(AActor* ObserverActor);

	/** Instantly retrieves the cached map of [ISID -> Location] currently in sight */
	UFUNCTION(BlueprintPure, Category = "Prop Sight Sense")
	bool GetVisiblePropsForObserver(AActor* ObserverActor, TMap<int32, FVector>& OutVisibleProps) const;

	/** Instantly retrieves the unique list of ProductIDs (PIDs) currently visible */
	UFUNCTION(BlueprintPure, Category = "Prop Sight Sense")
	bool GetVisibleProductIDsForObserver(AActor* ObserverActor, TArray<int32>& OutProductIDs) const;

	/** Instantly retrieves the full detailed list of perceived props currently visible */
	UFUNCTION(BlueprintPure, Category = "Prop Sight Sense")
	bool GetDetectedPropsListForObserver(AActor* ObserverActor, TArray<FPropSightPerceptionResult>& OutProps) const;

	/** Instantly retrieves the closest visible prop for a registered observer */
	UFUNCTION(BlueprintPure, Category = "Prop Sight Sense")
	bool GetClosestVisiblePropForObserver(AActor* ObserverActor, FPropSightPerceptionResult& OutClosestProp) const;

	// -------------------------------------------------------------------------
	// Section 10: Find Closest Detected Product by PID
	// -------------------------------------------------------------------------

	/**
	 * Finds the closest detected product instance for a specified Product ID (PID)
	 * from a list of detected props.
	 *
	 * @param PID The Product ID used to identify the product type to search for.
	 * @param DetectedPropsList An array of detected prop structures containing the ISID and location of each detected instance.
	 * @param OutClosestISID The ISID of the closest matching detected product instance.
	 * @param OutClosestLocation The location of that instance.
	 * @param ObserverActor Optional observer actor used as the distance origin.
	 * @return True if a matching detected instance was found; false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Query")
	bool FindClosestDetectedProductByPID(
		int32 PID,
		const TArray<FPropSightPerceptionResult>& DetectedPropsList,
		int32& OutClosestISID,
		FVector& OutClosestLocation,
		AActor* ObserverActor = nullptr
	) const;

	/**
	 * Finds the closest detected product instance for a specified PID
	 * directly from an FPropSightDetectedProps wrapper struct.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Query")
	bool FindClosestDetectedProductInPropsByPID(
		int32 PID,
		const FPropSightDetectedProps& DetectedProps,
		int32& OutClosestISID,
		FVector& OutClosestLocation,
		AActor* ObserverActor = nullptr
	) const;

	/**
	 * Directly finds the closest detected product for a registered observer by PID.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Query")
	bool FindClosestDetectedProductForObserver(
		int32 PID,
		AActor* ObserverActor,
		int32& OutClosestISID,
		FVector& OutClosestLocation
	) const;

	// -------------------------------------------------------------------------
	// Combine and Sort Detected Props Functions
	// -------------------------------------------------------------------------

	/**
	 * Takes two arrays of detected props, combines them (deduplicating by ISID),
	 * recalculates distance to the specified observer (or location override),
	 * and sorts the combined array in ascending order of distance (closest at index 0, farthest at last index).
	 *
	 * @param ArrayA First array of detected props.
	 * @param ArrayB Second array of detected props.
	 * @param ObserverActor The observer actor used as distance origin (uses Pawn view location / eyes if available).
	 * @param ObserverLocationOverride Optional location override used if ObserverActor is null or as explicit origin.
	 * @return Single sorted array containing all combined detected props.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Query")
	TArray<FPropSightPerceptionResult> CombineAndSortDetectedProps(
		const TArray<FPropSightPerceptionResult>& ArrayA,
		const TArray<FPropSightPerceptionResult>& ArrayB,
		AActor* ObserverActor = nullptr,
		FVector ObserverLocationOverride = FVector::ZeroVector
	) const;

	/**
	 * Takes two FPropSightDetectedProps wrappers, combines their detected items (deduplicating by ISID),
	 * recalculates distance to the observer, and returns a single sorted wrapper.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Query")
	FPropSightDetectedProps CombineAndSortDetectedPropsWrapper(
		const FPropSightDetectedProps& PropsA,
		const FPropSightDetectedProps& PropsB,
		AActor* ObserverActor = nullptr,
		FVector ObserverLocationOverride = FVector::ZeroVector
	) const;

	/**
	 * Takes two arrays of FPropSightDetectedItem, combines them (deduplicating by ISID),
	 * recalculates distance to the observer, and sorts the combined array ascending by distance.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Query")
	TArray<FPropSightDetectedItem> CombineAndSortDetectedItems(
		const TArray<FPropSightDetectedItem>& ArrayA,
		const TArray<FPropSightDetectedItem>& ArrayB,
		AActor* ObserverActor = nullptr,
		FVector ObserverLocationOverride = FVector::ZeroVector
	) const;

	// -------------------------------------------------------------------------
	// Section 11.A: ISID Ignore List Management
	// -------------------------------------------------------------------------

	/**
	 * Adds the specified ISID to that observer's ignore list, or updates its state.
	 * Ignored ISIDs are excluded from continuous and one-time results and FindClosestDetectedProductByPID.
	 *
	 * @param ObserverActor The observer to configure.
	 * @param InISID The ISID to ignore.
	 * @return True if successfully added/updated; false if observer or ISID is invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Ignore")
	bool SetISIDToIgnore(AActor* ObserverActor, int32 InISID);

	/**
	 * Removes the specified ISID from that observer's ignore list so it can be observed again.
	 *
	 * @param ObserverActor The observer to configure.
	 * @param InISID The ISID to unignore.
	 * @return True if removed; false if not found or invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Ignore")
	bool UnsetISIDToIgnore(AActor* ObserverActor, int32 InISID);

	/**
	 * Clears all ignored ISIDs belonging to that observer.
	 *
	 * @param ObserverActor The observer to configure.
	 * @return True if the ignore list was cleared; false if observer is invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Ignore")
	bool RemoveAllISIDsToIgnore(AActor* ObserverActor);

	/** Backwards-compatible alias for SetISIDToIgnore */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Ignore")
	void IgnoreISIDForObserver(AActor* ObserverActor, int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Ignore")
	void IgnoreISIDsForObserver(AActor* ObserverActor, const TArray<int32>& InISIDs);

	/** Backwards-compatible alias for UnsetISIDToIgnore */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Ignore")
	void UnignoreISIDForObserver(AActor* ObserverActor, int32 InISID);

	/** Backwards-compatible alias for RemoveAllISIDsToIgnore */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Ignore")
	void ClearIgnoredISIDsForObserver(AActor* ObserverActor);

	UFUNCTION(BlueprintPure, Category = "Prop Sight Sense|Ignore")
	bool IsISIDIgnoredForObserver(AActor* ObserverActor, int32 InISID) const;

	// -------------------------------------------------------------------------
	// Section 11.B: PID Observation Management
	// -------------------------------------------------------------------------

	/**
	 * Adds the specified PID to the observer's observation allowlist.
	 *
	 * @param ObserverActor The observer to configure.
	 * @param InPID The Product ID to allow.
	 * @return True if the PID was added; false if observer or PID is invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Products")
	bool AddPIDToObservation(AActor* ObserverActor, int32 InPID);

	/**
	 * Removes the specified PID from the observer's observation allowlist.
	 * Immediately purges any existing detected results belonging to this PID.
	 *
	 * @param ObserverActor The observer to configure.
	 * @param InPID The Product ID to remove.
	 * @return True if the PID was removed; false if not found or invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Products")
	bool RemovePIDFromObservation(AActor* ObserverActor, int32 InPID);

	/**
	 * Clears the observer's configured PID observation allowlist.
	 * When cleared while filtering is enabled, the observer will observe no products.
	 *
	 * @param ObserverActor The observer to configure.
	 * @return True if cleared; false if observer is invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Products")
	bool RemoveAllPIDsFromObservation(AActor* ObserverActor);

	/**
	 * Explicitly sets whether an observer observes all PIDs without filtering.
	 * If set to true, bFilterByPID is disabled and all product types can be seen.
	 * If set to false, only PIDs explicitly in the allowlist can be seen.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Products")
	bool SetObserveAllPIDs(AActor* ObserverActor, bool bObserveAll = true);

	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Products")
	void SetTargetProductIDsForObserver(AActor* ObserverActor, const TArray<int32>& NewProductIDs);

	/** Backwards-compatible alias for AddPIDToObservation */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Products")
	void AddTargetProductIDForObserver(AActor* ObserverActor, int32 InProductID);

	/** Backwards-compatible alias for RemovePIDFromObservation */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Products")
	void RemoveTargetProductIDForObserver(AActor* ObserverActor, int32 InProductID);

	UFUNCTION(BlueprintPure, Category = "Prop Sight Sense|Products")
	bool GetTargetProductIDsForObserver(AActor* ObserverActor, TArray<int32>& OutProductIDs) const;

	// -------------------------------------------------------------------------
	// Group 2: Observer PID Observation Management (Section 4 Group 2)
	// -------------------------------------------------------------------------

	/**
	 * Manages an observer's PID observation configuration (Section 4 Group 2).
	 * Supports RegisterPID, UnregisterPID, and SetInstanceLimit.
	 *
	 * @param ObserverActor The observer actor to configure.
	 * @param Operation RegisterPID, UnregisterPID, or SetInstanceLimit.
	 * @param PID The Product ID.
	 * @param InstanceLimit The requested instance limit (used for RegisterPID and SetInstanceLimit, default 1).
	 * @return True if operation was successful; false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Products")
	bool ManageObserverPIDObservation(
		AActor* ObserverActor,
		EObserverPIDOperation Operation,
		int32 PID,
		int32 InstanceLimit = 1
	);

	// -------------------------------------------------------------------------
	// Section 3: Functions to Update Instance Limits
	// -------------------------------------------------------------------------

	/**
	 * Increases the instance limit for a specified PID on an observer (Section 3).
	 * Automatically expands capacity and re-evaluates world props for this PID.
	 *
	 * @param ObserverActor The observer actor to configure.
	 * @param PID The Product ID.
	 * @param IncreaseBy Amount to increase the limit by.
	 * @return True if limit was increased; false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Products")
	bool IncreasePIDInstanceLimit(AActor* ObserverActor, int32 PID, int32 IncreaseBy = 1);

	/**
	 * Decreases the instance limit for a specified PID on an observer (Section 3).
	 * If the new limit is lower than the current number of observed instances, immediately
	 * trims the excess instances from the observer's result array, removing farthest first.
	 *
	 * @param ObserverActor The observer actor to configure.
	 * @param PID The Product ID.
	 * @param DecreaseBy Amount to decrease the limit by.
	 * @return True if limit was decreased; false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Products")
	bool DecreasePIDInstanceLimit(AActor* ObserverActor, int32 PID, int32 DecreaseBy = 1);

	/**
	 * Directly sets the instance limit for a specified PID on an observer (Section 3).
	 * Trims excess farthest instances if the new limit is lower than current count.
	 *
	 * @param ObserverActor The observer actor to configure.
	 * @param PID The Product ID.
	 * @param NewLimit The new maximum count of instances to maintain (0 or less unregisters PID).
	 * @return True if limit was updated; false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Products")
	bool SetPIDInstanceLimit(AActor* ObserverActor, int32 PID, int32 NewLimit);

	// -------------------------------------------------------------------------
	// Group 4: PID Observation Queries (Section 4 Group 4)
	// -------------------------------------------------------------------------

	/**
	 * Queries the observed instance count and requested count for a specific PID on an observer (Section 4 Group 4).
	 *
	 * @param ObserverActor The observer actor to query.
	 * @param PID The Product ID to check.
	 * @param OutPID Output confirming the queried Product ID.
	 * @param OutRequestedCount Output maximum instance limit requested for this PID.
	 * @param OutCurrentObservedCount Output number of instances currently detected and maintained.
	 * @return True if the observer is registered and has requested this PID; false otherwise.
	 */
	UFUNCTION(BlueprintPure, Category = "Prop Sight Sense|Query")
	bool GetObservedInstanceCountByPID(
		AActor* ObserverActor,
		int32 PID,
		int32& OutPID,
		int32& OutRequestedCount,
		int32& OutCurrentObservedCount
	) const;

	// -------------------------------------------------------------------------
	// Debug Visualization API
	// -------------------------------------------------------------------------

	/** When true, draws debug cones, gaze rays, and vision range spheres */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Sense|Debug")
	bool bDrawDebugCone = false;

	/** When true, draws debug shapes on all visible objects in vision range */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Sight Sense|Debug")
	bool bDrawDebugVisibleObjects = false;

	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Debug")
	void SetDebugDrawOptions(bool bInDrawCone, bool bInDrawVisibleObjects);

	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Debug")
	void SetObserverDebugDrawOptions(AActor* ObserverActor, bool bInDrawCone, bool bInDrawVisibleObjects);

	// -------------------------------------------------------------------------
	// Group 3: Observer Debug Settings (Section 4 Group 3)
	// -------------------------------------------------------------------------

	/**
	 * Enables debug visualization for a specific observer (Section 4 Group 3).
	 *
	 * @param ObserverActor The observer actor.
	 * @param bInDrawCone True to draw vision cone, range sphere, and gaze ray.
	 * @param bInDrawVisibleObjects True to draw shapes and lines to detected props.
	 * @return True if observer was found and updated; false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Debug")
	bool EnableObserverDebug(AActor* ObserverActor, bool bInDrawCone = true, bool bInDrawVisibleObjects = true);

	/**
	 * Disables debug visualization for a specific observer (Section 4 Group 3).
	 *
	 * @param ObserverActor The observer actor.
	 * @return True if observer was found and updated; false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Debug")
	bool DisableObserverDebug(AActor* ObserverActor);

	/**
	 * Updates debug visualization settings and assigns a custom debug color for a specific observer.
	 *
	 * @param ObserverActor The observer actor.
	 * @param bInDrawCone True to draw vision cone.
	 * @param bInDrawVisibleObjects True to draw detected shapes.
	 * @param NewDebugColor The new color to use for this observer's visualizations and text.
	 * @return True if observer was found and updated; false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Debug")
	bool UpdateObserverDebug(AActor* ObserverActor, bool bInDrawCone, bool bInDrawVisibleObjects, FColor NewDebugColor);

	// -------------------------------------------------------------------------
	// Event Hook
	// -------------------------------------------------------------------------

	/** Called every frame when UPropBatchUpdateComponent broadcasts an evaluated batch */
	UFUNCTION()
	void HandleBatchEvaluated(const TArray<FPropBatchEvaluationItem>& Batch, const FPropBatchMetadata& Metadata);

	/** Event broadcast when an observer's continuous perception cycle updates */
	UPROPERTY(BlueprintAssignable, Category = "Prop Sight Sense")
	FOnPropSightPerceptionUpdated OnPropSightPerceptionUpdated;

protected:
	/** Section 2.B: One-Time Observer Data array */
	UPROPERTY(Transient)
	TArray<FOneTimeObserverData> OneTimeObservers;

	/** Section 2.A: Continuous Observer Data array */
	UPROPERTY(Transient)
	TArray<FContinuousObserverData> ContinuousObservers;

	/** Next unique QueryID */
	int64 NextQueryID = 1;

	/** Counter for unique debug color assignment */
	int32 NextColorIndex = 0;

	/** Cached pointer to owning PropManager */
	UPROPERTY(Transient)
	TWeakObjectPtr<APropManager> PropManagerRef;

	/** Cached pointer to sibling BatchUpdateComponent */
	UPROPERTY(Transient)
	TWeakObjectPtr<UPropBatchUpdateComponent> BatchUpdateComponentRef;

	/** Section 12.E: Persistent ignore lists stored per observer actor, surviving unregistration and re-registration */
	TMap<TWeakObjectPtr<AActor>, TSet<int32>> PersistentIgnoredISIDs;

	/** Section 12.E: Persistent PID allowlists stored per observer actor, surviving unregistration and re-registration */
	TMap<TWeakObjectPtr<AActor>, TArray<int32>> PersistentTargetProductIDs;
	TMap<TWeakObjectPtr<AActor>, bool> PersistentFilterByPID;

	/** Persistent PID limits stored per observer actor, surviving unregistration and re-registration */
	TMap<TWeakObjectPtr<AActor>, TMap<int32, int32>> PersistentPIDInstanceLimits;

	/** Section 1: Assigns a unique debug color for each registered observer */
	FColor AllocateUniqueObserverColor();

	/** Section 4: Dedicated function to process continuous observers */
	void ProcessContinuousObservers(const TArray<FPropBatchEvaluationItem>& Batch, const FPropBatchMetadata& Metadata);

	/** Section 5: Dedicated function to process one-time observers */
	void ProcessOneTimeObservers(const TArray<FPropBatchEvaluationItem>& Batch, const FPropBatchMetadata& Metadata);

	/** Helper to add or update an item in an observer's detected items array */
	static void AddOrUpdateDetectedItem(TArray<FPropSightDetectedItem>& Items, TMap<int32, int32>& IndexMap, const FPropSightDetectedItem& NewItem);

	/** Helper to remove an item from an observer's detected items array */
	static void RemoveDetectedItem(TArray<FPropSightDetectedItem>& Items, TMap<int32, int32>& IndexMap, int32 InISID);

	/** Internal helper to find continuous observer data by Actor (with Controller <-> Pawn fallback) */
	const FContinuousObserverData* FindContinuousObserver(AActor* ObserverActor) const;
	FContinuousObserverData* FindContinuousObserverMutable(AActor* ObserverActor);

	/** Internal helpers for visibility testing */
	bool IsItemVisible(const FPropBatchEvaluationItem& Item, const FContinuousObserverData& Obs, float& OutDistSq, FVector& OutTargetLoc) const;
	bool IsItemVisible(const FPropBatchEvaluationItem& Item, const FOneTimeObserverData& Obs, float& OutDistSq, FVector& OutTargetLoc) const;

	/** Checks if a specific ProductID is allowed for this observer based on its PIDs and limits */
	bool IsProductAllowedForObserver(const FContinuousObserverData& Obs, int32 ProductID) const;
	bool IsProductAllowedForObserver(const FOneTimeObserverData& Obs, int32 ProductID) const;

	/** Checks whether an observer has any PIDs configured (TargetProductIDs, PIDInstanceLimits, or bObserveAllPIDs) */
	bool HasObserverConfiguredPIDs(const FContinuousObserverData& Obs) const;
	bool HasObserverConfiguredPIDs(const FOneTimeObserverData& Obs) const;

	/** Resolves effective sight origin and rotation */
	void ResolveSightTransform(const FContinuousObserverData& Config, FVector& OutLocation, FRotator& OutRotation) const;
	void ResolveSightTransform(const FOneTimeObserverData& Config, FVector& OutLocation, FRotator& OutRotation) const;

	/** Resolves distance calculation origin for an observer actor or location override */
	FVector ResolveDistanceOrigin(AActor* ObserverActor, const FVector& LocationOverride = FVector::ZeroVector) const;

	/** Line of sight multi-point raycast test */
	bool CheckMultiPointLineOfSight(const FVector& ViewOrigin, const FPropBatchEvaluationItem& Item, ECollisionChannel TraceChannel, AActor* ObserverActor) const;

	/** Section 1: Draws range sphere, cone, gaze ray, and detected shapes using observer's unique color */
	void DrawDebugForObserver(UWorld* World, const FContinuousObserverData& Config) const;
	void DrawDebugForObserver(UWorld* World, const FOneTimeObserverData& Config) const;

	/** Evaluates all world props for an observer */
	void EvaluateAllPropsForContinuous(FContinuousObserverData& Config);
	void EvaluateAllPropsForOneTime(FOneTimeObserverData& Config);

	/** Evaluates world props for a specific PID and observer to immediately fill capacity (Section 3 & Section 4) */
	void EvaluatePropsForPID(FContinuousObserverData& Obs, int32 PID);
	void EvaluatePropsForPID(FOneTimeObserverData& Obs, int32 PID);

	/** Internal helper to process a batch item against an observer's per-PID limits and distance replacement (Section 2) */
	bool ProcessItemForObserver(FContinuousObserverData& Obs, const FPropBatchEvaluationItem& Item, bool bVisible, float DistSq, const FVector& TargetLoc);
	bool ProcessItemForObserver(FOneTimeObserverData& Obs, const FPropBatchEvaluationItem& Item, bool bVisible, float DistSq, const FVector& TargetLoc);

	/** Synchronizes DetectedItems and ISIDToIndexMap from PerPIDDetectedItems */
	void SyncDetectedItemsFromPerPID(FContinuousObserverData& Obs);
	void SyncDetectedItemsFromPerPID(FOneTimeObserverData& Obs);

	/** Section 9: Sorts observer's detected items in ascending order of distance (closest at index 0, farthest at last index) */
	void SortObserverResults(FContinuousObserverData& Obs, const FVector& ObserverLocation);
	void SortObserverResults(FOneTimeObserverData& Obs, const FVector& ObserverLocation);

	/** Packages detected items into FPropSightDetectedProps for Blueprint delegates */
	FPropSightDetectedProps BuildDetectedPropsWrapper(const TArray<FPropSightDetectedItem>& Items) const;
};
