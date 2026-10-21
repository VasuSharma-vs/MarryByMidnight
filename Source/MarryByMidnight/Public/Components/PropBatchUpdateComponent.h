#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Props/InteractablePropDatabase.h"
#include "PropBatchUpdateComponent.generated.h"

class APropManager;
class UInteractableISMComponent;
class AActivePropActor;

/**
 * Progress and index metadata broadcast alongside the frame's evaluated batch.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropBatchMetadata
{
	GENERATED_BODY()

	/** Starting ISID index evaluated in this frame's batch */
	UPROPERTY(BlueprintReadOnly, Category = "Prop Batch Update")
	int32 BatchStartIndex = 0;

	/** Ending ISID index evaluated in this frame's batch */
	UPROPERTY(BlueprintReadOnly, Category = "Prop Batch Update")
	int32 BatchEndIndex = 0;

	/** Total number of active registered ISIDs in the world */
	UPROPERTY(BlueprintReadOnly, Category = "Prop Batch Update")
	int32 TotalActiveISIDs = 0;

	/** True if this frame's batch wrapped around, completing a full sweep over all world props */
	UPROPERTY(BlueprintReadOnly, Category = "Prop Batch Update")
	bool bCycleCompleted = false;
};

/**
 * Event broadcast each frame containing a batch of 10 to 50 evaluated props (ISM and ASM) with progress metadata.
 * Subscribed systems (weather, cooking/cooling zones, AI perception) can inspect spatial data
 * or call mutation functions to update temperature, portions, or states.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPropBatchEvaluate, const TArray<FPropBatchEvaluationItem>&, Batch, const FPropBatchMetadata&, Metadata);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPropBatchEvaluateNative, const TArray<FPropBatchEvaluationItem>&, const FPropBatchMetadata&);

/**
 * Component that decouples real-time prop inspection, spatial queries, and environmental simulation
 * from APropManager into a time-sliced, event-driven batch pipeline.
 */
UCLASS(ClassGroup = (Props), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UPropBatchUpdateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPropBatchUpdateComponent();

	virtual void BeginPlay() override;

	/**
	 * Main processing function called once per frame (e.g. from APropManager::Tick on the server).
	 * Loops over BatchSize (10 to 50) ISIDs round-robin, gathers their spatial and runtime info,
	 * broadcasts the batch to listeners, and replicates any modified field deltas.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Batch Update")
	void ProcessBatchUpdate(float DeltaSeconds);

	/**
	 * Broadcast each frame when a batch of 10 to 50 props is evaluated.
	 * External actors (weather, cooking/cooling zones, AI perception) can bind to this event
	 * to read data or apply updates.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Prop Batch Update")
	FOnPropBatchEvaluate OnBatchEvaluateProps;

	/** Native C++ delegate for high-performance systems */
	FOnPropBatchEvaluateNative OnBatchEvaluatePropsNative;

	// -------------------------------------------------------------------------
	// Configuration & Settings
	// -------------------------------------------------------------------------

	/** Number of ISIDs to evaluate per frame (clamped between 5 and 50) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Batch Update|Performance", meta = (ClampMin = "5", ClampMax = "50"))
	int32 BatchSize = 25;

	// -------------------------------------------------------------------------
	// Mutation API (Used by external systems or listeners during/after evaluation)
	// -------------------------------------------------------------------------

	/**
	 * Sets the temperature for a specific ISID.
	 * Marks ONLY the Temperature dirty flag for replication if changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Batch Update|Mutation")
	bool SetInstanceTemperature(int32 InISID, float NewTemperature);

	/**
	 * Sets the remaining consumable portions for a specific ISID.
	 * Marks ONLY the Portions dirty flag for replication if changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Batch Update|Mutation")
	bool SetInstancePortions(int32 InISID, int32 NewPortions);

	/**
	 * Sets the state for a specific ISID.
	 * Marks ONLY the State dirty flag for replication if changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Batch Update|Mutation")
	bool SetInstanceState(int32 InISID, EPropState NewState);

	/**
	 * Applies a full or partial field delta to an instance.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Batch Update|Mutation")
	bool ApplyFieldDelta(const FPropRuntimeFieldDelta& Delta);

	// -------------------------------------------------------------------------
	// Query & Search Helpers (For external actors inspecting the batch)
	// -------------------------------------------------------------------------

	/**
	 * Finds the closest prop in the evaluated batch to a specified origin.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Batch Update|Query")
	static bool FindClosestItemInBatch(const TArray<FPropBatchEvaluationItem>& Batch, const FVector& Origin, FPropBatchEvaluationItem& OutClosestItem, float MaxDistance = 5000.0f);

	/**
	 * Filters items in the batch that match a specific ProductID.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Batch Update|Query")
	static void FilterItemsByProduct(const TArray<FPropBatchEvaluationItem>& Batch, int32 InProductID, TArray<FPropBatchEvaluationItem>& OutItems);

	/**
	 * Filters items in the batch that fall inside a world bounding box (e.g. inside a fridge or room).
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Batch Update|Query")
	static void FilterItemsInBox(const TArray<FPropBatchEvaluationItem>& Batch, const FBox& WorldBounds, TArray<FPropBatchEvaluationItem>& OutItems);

	// -------------------------------------------------------------------------
	// Network Replication
	// -------------------------------------------------------------------------

	/**
	 * Flushes all pending field deltas accumulated this frame across the network to remote clients.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Batch Update|Replication")
	void FlushFieldDeltas();

	/**
	 * Reliable NetMulticast sending fine-grained field deltas to clients.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SyncFieldDeltas(const TArray<FPropRuntimeFieldDelta>& Deltas);

	/** Applies received field deltas on client */
	void ApplyFieldDeltasOnClient(const TArray<FPropRuntimeFieldDelta>& Deltas);

	/** Returns cached or resolved owning APropManager */
	UFUNCTION(BlueprintPure, Category = "Prop Batch Update")
	APropManager* GetPropManager();

	/** Returns current round-robin batch evaluation index */
	UFUNCTION(BlueprintPure, Category = "Prop Batch Update")
	int32 GetNextBatchIndex() const { return NextBatchIndex; }

protected:
	/** Round-robin index through active ISIDs */
	UPROPERTY(Transient)
	int32 NextBatchIndex = 0;

	/** Pending dirty field deltas accumulated during the frame */
	TMap<int32, FPropRuntimeFieldDelta> PendingFieldDeltas;

	/** Cached pointer to owning PropManager */
	UPROPERTY(Transient)
	TWeakObjectPtr<APropManager> PropManagerRef;
};
