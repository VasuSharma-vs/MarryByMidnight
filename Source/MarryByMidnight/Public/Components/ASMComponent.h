#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Props/InteractablePropDatabase.h"
#include "ASMComponent.generated.h"

class AActivePropActor;
class APropManager;
class UStaticMeshPhysicsSimulationComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnASMSettlingObjectRested, UStaticMeshPhysicsSimulationComponent*, SimComp, AActor*, OwnerActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnASMPoolScaled, int32, TotalASMCount, int32, ActiveASMCount, int32, EmptyASMCount);

/**
 * ASM Component — Dedicated component that manages all Active Static Mesh (AActivePropActor) units,
 * dynamic pool scaling (Min 100 to Max 500 ASM actors, expanding automatically at 90% utilization),
 * EmptyASMIDs tracking, ISID <-> ASMID bindings, distributed ASM temperature updates, and physics settling checks.
 */
UCLASS(ClassGroup = (Custom, Common), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UASMComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UASMComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// =====================================================================
	// Dynamic ASM Pool Configuration (100 to 500 Active Physics & Interactable Objects)
	// =====================================================================

	/** Minimum number of ASM actors maintained in the pool (default: 100) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASM Component|Pool Scaling", meta = (ClampMin = "1", ClampMax = "500"))
	int32 MinASMCount = 100;

	/** Maximum number of ASM actors allowed in the world/pool (default: 500) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASM Component|Pool Scaling", meta = (ClampMin = "1", ClampMax = "5000"))
	int32 MaxASMCount = 500;

	/** Pool utilization threshold (0.0 - 1.0) at which more ASM actors are automatically spawned (default: 0.90 = 90% full) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASM Component|Pool Scaling", meta = (ClampMin = "0.50", ClampMax = "1.0"))
	float ExpandThresholdRatio = 0.90f;

	/** Pool utilization threshold (0.0 - 1.0) below which excess empty ASM actors above MinASMCount can be removed (default: 0.25 = 25%) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASM Component|Pool Scaling", meta = (ClampMin = "0.05", ClampMax = "0.80"))
	float ShrinkThresholdRatio = 0.25f;

	/** Number of ASM actors to add in a batch when pool utilization reaches ExpandThresholdRatio (90%) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASM Component|Pool Scaling", meta = (ClampMin = "1", ClampMax = "100"))
	int32 ExpandBatchSize = 25;

	/** Whether to automatically remove excess empty ASM actors down toward MinASMCount when utilization drops below ShrinkThresholdRatio */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASM Component|Pool Scaling")
	bool bAutoShrinkExcessASMs = true;

	/**
	 * Custom Blueprint or C++ Actor class (child of AActivePropActor) spawned for active ASM units.
	 * Create a Blueprint from ActivePropActor (e.g. BP_ActivePropActor) to add custom logic and assign it here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASM Component|Custom ASM Actor", meta = (DisplayName = "Custom Active Prop Actor Class (BP)"))
	TSubclassOf<AActivePropActor> ActivePropActorClass;

	/** Optional per-ProductID custom Blueprint Actor class overrides (if a ProductID is in this map, it uses that BP class; otherwise uses ActivePropActorClass) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASM Component|Custom ASM Actor", meta = (DisplayName = "Custom Actor Class By Product ID"))
	TMap<int32, TSubclassOf<AActivePropActor>> CustomActorClassByProductID;

	/** Storage/parking transforms for inactive ASM representations */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASM Component|Parking")
	TArray<FTransform> ResetTransforms;

	/** Default parking/reset transform used when ResetTransforms is empty */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASM Component|Parking")
	FTransform DefaultResetTransform;

	// =====================================================================
	// ASM Arrays, Empty [ASMID] Tracking, & Mappings
	// =====================================================================

	/** All managed AActivePropActor units (active + empty/pooled, 100 to 500) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ASM Component|Registry")
	TArray<TObjectPtr<AActivePropActor>> ASMActors;

	/** Empty / inactive [ASMID] unit IDs currently parked at ResetTransform and available for reuse */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ASM Component|Empty ASMs")
	TArray<int32> EmptyASMIDs;

	/** Lookup of all spawned ASM actors by their unique physical ASMID */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ASM Component|Mapping")
	TMap<int32, TObjectPtr<AActivePropActor>> ActiveActorByASMID;

	/** Lookup of currently active (promoted) ASM actors by logical ISID */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ASM Component|Mapping")
	TMap<int32, TObjectPtr<AActivePropActor>> ActiveActorByISID;

	/** Bidirectional mapping: Logical ISID -> Physical ASMID */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ASM Component|Mapping")
	TMap<int32, int32> ISIDToASMID;

	/** Bidirectional mapping: Physical ASMID -> Logical ISID */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ASM Component|Mapping")
	TMap<int32, int32> ASMIDToISID;

	/** Next monotonically allocated physical ASMID (starts at 1) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ASM Component|Identity")
	int32 NextASMID = 1;

	/** Owner PropManager reference */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ASM Component")
	TObjectPtr<APropManager> PropManager = nullptr;

	/** Fired when a settling ASM physics component comes to rest */
	UPROPERTY(BlueprintAssignable, Category = "ASM Component|Events")
	FOnASMSettlingObjectRested OnSettlingObjectRested;

	/** Fired when ASM pool expands or shrinks */
	UPROPERTY(BlueprintAssignable, Category = "ASM Component|Events")
	FOnASMPoolScaled OnASMPoolScaled;

	// =====================================================================
	// Custom ASM Actor Class & Dynamic Pool Scaling API
	// =====================================================================

	/** Sets the custom Blueprint / C++ ActivePropActor class to use for ASM units and optionally respawns empty pooled actors with the new class */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Custom ASM Actor")
	void SetCustomActivePropActorClass(TSubclassOf<AActivePropActor> NewActorClass, bool bRebuildPoolNow = true);

	/** Sets a custom Blueprint / C++ ActivePropActor class for a specific ProductID */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Custom ASM Actor")
	void SetCustomActorClassForProduct(int32 InProductID, TSubclassOf<AActivePropActor> NewActorClass);

	/** Resolves the effective AActivePropActor class to spawn for a given ProductID */
	UFUNCTION(BlueprintPure, Category = "ASM Component|Custom ASM Actor")
	TSubclassOf<AActivePropActor> GetEffectiveActivePropActorClass(int32 InProductID = 0) const;

	/** Initializes the ASM pool, pre-spawning MinASMCount (100) parked ASM units and populating EmptyASMIDs */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Pool")
	void InitializeASMPool(int32 InitialCount = -1);

	/** Evaluates current pool usage: if >= 90% full (ExpandThresholdRatio), adds more ASMs up to MaxASMCount (500); if low, trims excess down to MinASMCount (100) */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Pool")
	void EvaluatePoolCapacity();

	/** Spawns a single new AActivePropActor unit, assigns it an ASMID, parks it at ResetTransform, and adds its ASMID to EmptyASMIDs */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Pool")
	AActivePropActor* SpawnPooledASM(TSubclassOf<AActivePropActor> OverrideClass = nullptr);

	/** Spawns Count new AActivePropActor units (clamped to MaxASMCount) and adds their ASMIDs to EmptyASMIDs */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Pool")
	int32 AddASMUnits(int32 Count);

	/** Removes and destroys up to MaxToRemove empty ASM actors while keeping at least MinASMCount (100) */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Pool")
	int32 RemoveExcessEmptyASMs(int32 MaxToRemove = 0);

	/** Permanently removes and destroys a specific ASM actor by ASMID */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Pool")
	bool RemoveASMByASMID(int32 InASMID);

	/** Returns the current pool utilization ratio (ActiveASMCount / TotalASMCount) in [0.0, 1.0] */
	UFUNCTION(BlueprintPure, Category = "ASM Component|Pool")
	float GetPoolUtilizationRatio() const;

	/** Returns the total number of spawned ASM actors (active + empty) */
	UFUNCTION(BlueprintPure, Category = "ASM Component|Pool")
	int32 GetTotalASMCount() const { return ASMActors.Num(); }

	/** Returns the number of currently active (promoted) ASM actors */
	UFUNCTION(BlueprintPure, Category = "ASM Component|Pool")
	int32 GetActiveASMCount() const { return ActiveActorByISID.Num(); }

	/** Computes a non-overlapping parking transform for a given pool slot index */
	UFUNCTION(BlueprintPure, Category = "ASM Component|Parking")
	FTransform GetResetTransformForSlot(int32 SlotIndex) const;

	// =====================================================================
	// Empty [ASMID] Management Functions
	// =====================================================================

	/** Marks an ASMID as empty, unbinding any ISID, parking the actor at ResetTransform, and adding InASMID to EmptyASMIDs */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Empty ASMs")
	bool AddEmptyASMID(int32 InASMID);

	/** Removes InASMID from the EmptyASMIDs array */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Empty ASMs")
	bool RemoveEmptyASMID(int32 InASMID);

	/** Returns true if InASMID is currently in EmptyASMIDs */
	UFUNCTION(BlueprintPure, Category = "ASM Component|Empty ASMs")
	bool IsEmptyASMID(int32 InASMID) const;

	/** Returns the number of available empty [ASMID] units in the pool */
	UFUNCTION(BlueprintPure, Category = "ASM Component|Empty ASMs")
	int32 GetEmptyASMIDCount() const { return EmptyASMIDs.Num(); }

	/** Returns the Empty ASMID at Index (or 0 if out of bounds) */
	UFUNCTION(BlueprintPure, Category = "ASM Component|Empty ASMs")
	int32 GetEmptyASMID(int32 Index = 0) const;

	/** Pops and returns the next available Empty ASMID for reuse (or 0 if none available) */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Empty ASMs")
	int32 ConsumeEmptyASMID();

	/** Clears EmptyASMIDs; optionally destroys the physical empty ASM actors */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Empty ASMs")
	void ClearEmptyASMIDs(bool bDestroyPhysicalActors = false);

	/** Rebuilds EmptyASMIDs by finding all valid ASMActors that are not currently bound to an active ISID */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Empty ASMs")
	void RebuildEmptyASMIDs();

	// =====================================================================
	// Identity, Lookup & Representation Conversion (ISM <-> ASM)
	// =====================================================================

	/** Allocates a unique physical ASMID (> 0) */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Identity")
	int32 AllocateASMID();

	/** Finds an ASM actor by its physical ASMID */
	UFUNCTION(BlueprintPure, Category = "ASM Component|Mapping")
	AActivePropActor* GetActiveActorFromASMID(int32 InASMID) const;

	/** Finds an active ASM actor by its logical ISID */
	UFUNCTION(BlueprintPure, Category = "ASM Component|Mapping")
	AActivePropActor* GetActiveActorFromISID(int32 InISID) const;

	/** Returns the physical ASMID bound to InISID (0 if none) */
	UFUNCTION(BlueprintPure, Category = "ASM Component|Mapping")
	int32 GetASMIDFromISID(int32 InISID) const;

	/** Returns the logical ISID bound to InASMID (0 if empty/unassigned) */
	UFUNCTION(BlueprintPure, Category = "ASM Component|Mapping")
	int32 GetISIDFromASMID(int32 InASMID) const;

	/** Binds a logical ISID to a physical ASM actor and removes its ASMID from EmptyASMIDs */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Mapping")
	void RegisterActiveActor(int32 InISID, AActivePropActor* InActor);

	/** Unbinds a logical ISID from its active ASM actor */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Mapping")
	void UnregisterActiveActor(int32 InISID);

	/** Acquires an available AActivePropActor from EmptyASMIDs (or spawns if needed up to MaxASMCount) and checks the 90% expansion threshold */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Representation")
	AActivePropActor* AcquireActiveActor(int32 InProductID = 0);

	/** Deactivates an ASM actor, parks it at ResetTransform, and adds its ASMID to EmptyASMIDs */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Representation")
	void ReturnActiveActorToPool(AActivePropActor* InActor);

	/** Promotes a logical prop record from ISM to an active ASM actor */
	bool PromoteRecordToASM(
		FISIDRuntimeIndex& Record,
		const FPropProductDefinition* ProductDef,
		const FTransform& GameplayTransform,
		EPropActivationReason Reason,
		bool bEnableReplication,
		AActivePropActor*& OutActor);

	/** Demotes an active ASM actor back to ISM state and returns the ASM unit to EmptyASMIDs */
	bool DemoteRecordFromASM(
		FISIDRuntimeIndex& Record,
		EPropState NewState,
		bool bPoolActor,
		bool bWriteBackTransform);

	/** Returns all active ASMs to the pool and resets active ISID bindings for level reload */
	UFUNCTION(BlueprintCallable, Category = "ASM Component|Representation")
	void ResetActiveASMsToPool();

	// =====================================================================
	// Registration, Distributed Temperature Updates & Physics Settling
	// =====================================================================

	UFUNCTION(BlueprintCallable, Category = "ASM Component|Registration")
	void RegisterProp(AActivePropActor* Prop);

	UFUNCTION(BlueprintCallable, Category = "ASM Component|Registration")
	void UnregisterProp(AActivePropActor* Prop);

	UFUNCTION(BlueprintCallable, Category = "ASM Component|Settling")
	void RegisterSettlingComponent(UStaticMeshPhysicsSimulationComponent* SimComp);

	UFUNCTION(BlueprintCallable, Category = "ASM Component|Settling")
	void UnregisterSettlingComponent(UStaticMeshPhysicsSimulationComponent* SimComp);

	UFUNCTION(BlueprintPure, Category = "ASM Component|Stats")
	int32 GetRegisteredPropsCount() const { return ASMActors.Num(); }

	UFUNCTION(BlueprintPure, Category = "ASM Component|Stats")
	int32 GetActiveSettlingCount() const { return ActiveSettlingComponents.Num(); }

	/** Updates temperature on active ASM actors in a distributed round-robin loop */
	void UpdateDistributedASMs(float DeltaSeconds, float AmbientTemperature, int32 PropsUpdatedPerFrame);

	/** Evaluates settling ASM physics components (synchronously or on an async background task) */
	void CheckSettlingQueue(bool bUseAsyncSettlingCheck);

	/** Flushes all active ASM actor transforms and runtime data into their authoritative ISID records */
	void FlushActiveActorState();

	/** Syncs the parent PropManager's inspectable ASMActors and EmptyASMIDs arrays */
	void SyncManagerArrays();

protected:
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UStaticMeshPhysicsSimulationComponent>> ActiveSettlingComponents;

	int32 CurrentASMUpdateIndex = 0;

	void ProcessSettlingQueueAsync();
};
