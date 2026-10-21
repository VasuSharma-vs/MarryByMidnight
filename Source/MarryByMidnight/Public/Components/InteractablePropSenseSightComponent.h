#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PropBatchUpdateComponent.h"
#include "InteractablePropSenseSightComponent.generated.h"

class APropManager;

/**
 * Result details for a perceived prop in sight.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropSightPerceptionResult
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
 * Wrapper struct holding a map of [ISID -> World Location] for all detected props in sight.
 * Ensures clean Blueprint delegate compatibility.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropSightDetectedProps
{
	GENERATED_BODY()

	/** Map of [ISID -> World Location] for all detected props in sight */
	UPROPERTY(BlueprintReadOnly, Category = "Prop Sight Perception")
	TMap<int32, FVector> Props;
};

/**
 * Delegate called when a one-shot sight query completes its full round-robin sweep over world props.
 * DetectedProps returns the Map of [ISID -> Location].
 * ClosestProp provides the single closest perceived prop.
 */
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnPropSightQueryCompleted, bool, bFoundAny, const FPropSightDetectedProps&, DetectedProps, const FPropSightPerceptionResult&, ClosestProp);

/**
 * Blueprint event dispatcher for continuous sight updates or query completions.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnPropSightPerceptionUpdated, AActor*, ObserverActor, bool, bFoundAny, const FPropSightDetectedProps&, DetectedProps, const FPropSightPerceptionResult&, ClosestProp);

/**
 * Internal configuration for an active sight observer or one-shot query.
 */
USTRUCT()
struct FPropSightObserverConfig
{
	GENERATED_BODY()

	int64 QueryID = 0;
	TArray<int32> TargetProductIDs;
	float SightRadius = 2500.0f;
	float PeripheralVisionHalfAngle = 60.0f;
	bool bIgnoreBlocked = false;
	bool bRequireGrabbable = true;

	TWeakObjectPtr<AActor> ObserverActor = nullptr;
	FVector SightLocationOverride = FVector::ZeroVector;
	FRotator SightRotationOverride = FRotator::ZeroRotator;
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	bool bContinuous = false;
	int32 StartISIDIndex = 0;
	bool bHasWrappedOnce = false;

	TMap<int32, FVector> DetectedProps; // ISID -> Location
	FPropSightPerceptionResult BestClosestProp;
	float BestDistanceSq = MAX_FLT;

	TSet<int32> IgnoredISIDs;

	FOnPropSightQueryCompleted OnQueryCompleted;
};

/**
 * Component providing time-sliced AI vision perception for interactable props (ISM and ASM).
 * Plugs into UPropBatchUpdateComponent to inspect props using cone-of-vision and line-of-sight checks.
 */
UCLASS(ClassGroup = (Props), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UInteractablePropSenseSightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractablePropSenseSightComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// -------------------------------------------------------------------------
	// One-Shot Sight Query API
	// -------------------------------------------------------------------------

	/**
	 * Requests an asynchronous, time-sliced sight search over the world's props for the closest product.
	 * Evaluates props as batches stream in, and invokes OnCompleted when a full sweep has finished.
	 *
	 * @param TargetProductIDs Array of ProductIDs to search for (empty array matches any ProductID).
	 * @param SightRadius Maximum vision distance in Unreal units (cm).
	 * @param PeripheralVisionHalfAngle Peripheral vision half-angle in degrees (e.g. 60° = 120° total FOV).
	 * @param ObserverActor The actor doing the observing (used for location/rotation if overrides are zero).
	 * @param OnCompleted Callback delegate fired with (bFoundAny, Map<ISID, Location>, ClosestProp) when finished.
	 * @param bIgnoreBlocked If false (default), checks for line-of-sight obstruction. If true, ignores obstacles.
	 * @param SightLocationOverride Custom sight origin. If (0,0,0), uses ObserverActor's eyes or location.
	 * @param SightRotationOverride Custom sight direction. If (0,0,0), uses ObserverActor's rotation.
	 * @param TraceChannel Collision channel used for obstruction testing (default: ECC_Visibility).
	 * @return Unique QueryID for tracking or cancellation.
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
		ECollisionChannel TraceChannel = ECC_Visibility
	);

	/** Cancels an active one-shot sight query by ID */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense")
	bool CancelSightQuery(int64 QueryID);

	// -------------------------------------------------------------------------
	// Continuous Observer Registration API
	// -------------------------------------------------------------------------

	/**
	 * Registers an actor (AI or player) for continuous perceptual tracking of props within sight.
	 * As props stream in every frame, their visibility is evaluated and cached for instant query.
	 *
	 * @param ObserverActor The actor observing props.
	 * @param TargetProductIDs Array of ProductIDs to track (empty matches all).
	 * @param SightRadius Maximum vision distance.
	 * @param PeripheralVisionHalfAngle Peripheral vision half-angle in degrees.
	 * @param bIgnoreBlocked If false (default), checks for line-of-sight obstruction.
	 * @param SightLocationOverride Custom sight location. If (0,0,0), uses ObserverActor's eyes/location.
	 * @param SightRotationOverride Custom sight rotation. If (0,0,0), uses ObserverActor's rotation.
	 * @param TraceChannel Collision channel used for obstruction testing.
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
		ECollisionChannel TraceChannel = ECC_Visibility
	);

	/** Unregisters an observer from continuous sight perception */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense")
	void UnregisterObserver(AActor* ObserverActor);

	/**
	 * Instantly retrieves the cached map of [ISID -> Location] currently in sight for a registered observer.
	 */
	UFUNCTION(BlueprintPure, Category = "Prop Sight Sense")
	bool GetVisiblePropsForObserver(AActor* ObserverActor, TMap<int32, FVector>& OutVisibleProps) const;

	/**
	 * Instantly retrieves the closest visible prop for a registered observer from its cached sight data.
	 */
	UFUNCTION(BlueprintPure, Category = "Prop Sight Sense")
	bool GetClosestVisiblePropForObserver(AActor* ObserverActor, FPropSightPerceptionResult& OutClosestProp) const;

	// -------------------------------------------------------------------------
	// Ignored ISIDs API (e.g. for props placed in carts/inventory)
	// -------------------------------------------------------------------------

	/**
	 * Ignores a specific prop ISID for a registered observer (e.g. when an item is placed in a cart/inventory).
	 * Immediately removes it from the observer's detected props so it is no longer perceived.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Ignore")
	void IgnoreISIDForObserver(AActor* ObserverActor, int32 InISID);

	/**
	 * Ignores multiple prop ISIDs for a registered observer.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Ignore")
	void IgnoreISIDsForObserver(AActor* ObserverActor, const TArray<int32>& InISIDs);

	/**
	 * Removes an ISID from the observer's ignore list, allowing it to be perceived again (e.g. dropped on floor).
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Ignore")
	void UnignoreISIDForObserver(AActor* ObserverActor, int32 InISID);

	/**
	 * Clears all ignored ISIDs for a registered observer.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Ignore")
	void ClearIgnoredISIDsForObserver(AActor* ObserverActor);

	/**
	 * Returns true if an ISID is currently ignored by an observer.
	 */
	UFUNCTION(BlueprintPure, Category = "Prop Sight Sense|Ignore")
	bool IsISIDIgnoredForObserver(AActor* ObserverActor, int32 InISID) const;

	// -------------------------------------------------------------------------
	// Dynamic Target Product IDs API (e.g. for dynamic shopping lists)
	// -------------------------------------------------------------------------

	/**
	 * Sets the target ProductIDs for a registered observer, immediately purging visible props of any non-matching product.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Products")
	void SetTargetProductIDsForObserver(AActor* ObserverActor, const TArray<int32>& NewProductIDs);

	/**
	 * Adds a single ProductID to an observer's target list.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Products")
	void AddTargetProductIDForObserver(AActor* ObserverActor, int32 InProductID);

	/**
	 * Removes a ProductID from an observer's target list (e.g. customer finished buying all required cola bottles)
	 * and immediately purges all detected items of that product from their sight.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Sight Sense|Products")
	void RemoveTargetProductIDForObserver(AActor* ObserverActor, int32 InProductID);

	/**
	 * Returns the current target ProductIDs for a registered observer.
	 */
	UFUNCTION(BlueprintPure, Category = "Prop Sight Sense|Products")
	bool GetTargetProductIDsForObserver(AActor* ObserverActor, TArray<int32>& OutProductIDs) const;

	// -------------------------------------------------------------------------
	// Event Hook
	// -------------------------------------------------------------------------

	/**
	 * Called every frame when UPropBatchUpdateComponent broadcasts an evaluated batch with progress metadata.
	 */
	UFUNCTION()
	void HandleBatchEvaluated(const TArray<FPropBatchEvaluationItem>& Batch, const FPropBatchMetadata& Metadata);

	/** Event broadcast when an observer's continuous perception cycle updates */
	UPROPERTY(BlueprintAssignable, Category = "Prop Sight Sense")
	FOnPropSightPerceptionUpdated OnPropSightPerceptionUpdated;

protected:
	/** Active one-shot queries */
	TMap<int64, FPropSightObserverConfig> ActiveOneShotQueries;

	/** Registered continuous observers keyed by ObserverActor */
	TMap<TWeakObjectPtr<AActor>, FPropSightObserverConfig> ContinuousObservers;

	/** Next unique QueryID */
	int64 NextQueryID = 1;

	/** Cached pointer to owning PropManager */
	UPROPERTY(Transient)
	TWeakObjectPtr<APropManager> PropManagerRef;

	/** Cached pointer to sibling BatchUpdateComponent */
	UPROPERTY(Transient)
	TWeakObjectPtr<UPropBatchUpdateComponent> BatchUpdateComponentRef;

	/** Internal helper to test an item against an observer config */
	bool IsItemVisibleToObserver(
		const FPropBatchEvaluationItem& Item,
		const FPropSightObserverConfig& Config,
		float& OutDistanceSq,
		FVector& OutTargetPoint
	) const;

	/** Resolves effective sight origin and rotation */
	void ResolveSightTransform(
		const FPropSightObserverConfig& Config,
		FVector& OutLocation,
		FRotator& OutRotation
	) const;

	/** Line of sight multi-point raycast test */
	bool CheckMultiPointLineOfSight(
		const FVector& ViewOrigin,
		const FPropBatchEvaluationItem& Item,
		ECollisionChannel TraceChannel,
		AActor* ObserverActor
	) const;
};
