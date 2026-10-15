#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Props/InteractablePropDatabase.h"
#include "InteractableInstancedStaticMeshComponent.generated.h"

class UInteractablePropDataAsset;
class ANPCCharacter;
class APawn;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnInstancePortionConsumed, int32, InstanceIndex, int32, RemainingPortions, AActor*, ConsumerActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnISMInstanceHit, int32, ISMI, int32, ISID, AActor*, OtherActor, FVector, NormalImpulse, const FHitResult&, Hit);

/**
 * IISM Component (Sections 4, 23, 24, 31, 65).
 * Focuses on ISM-specific representation management while APropManager handles global orchestration.
 * Maintains bidirectional ISID <-> ISMI local slot mapping, PackedInstanceData, EmptyInstances, and ResetTransform parking for persistent ISM instances.
 */
UCLASS(ClassGroup = (Rendering, Common), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UInteractableISMComponent : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	UInteractableISMComponent();

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	// -----------------------------------------------------------------
	// Section 4 & 31: IISM Component Identity & Configuration
	// -----------------------------------------------------------------
	/** Product Definition ID (PID) associated with this ISM batch */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "IISM Component|Identity")
	int32 ProductID = 0;

	/** Static Mesh ID (SMID) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "IISM Component|Identity")
	int32 StaticMeshID = 0;

	/** Unique IISM Component ID (ICID) assigned by the Prop Manager */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "IISM Component|Identity")
	int32 IISMComponentID = 0;

	/** Shared Product Definition (IISMDB) for instances in this component */
	UPROPERTY(ReplicatedUsing = OnRep_IISMDB, EditAnywhere, BlueprintReadWrite, Category = "IISM Component|Database")
	FPropProductDefinition IISMDB;

	UFUNCTION()
	void OnRep_IISMDB();

	/** Applies a Product Definition (StaticMesh, MaterialOverrides, IDs) to this component */
	void ApplyProductDefinition(const FPropProductDefinition& InDefinition);

	/** Self-reference / target ISM component pointer matching Section 31 layout */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IISM Component")
	TObjectPtr<UInstancedStaticMeshComponent> ISMComponent = nullptr;

	/** Local mapping: Permanent ISID -> Local ISM Slot Index (ISMI) (Section 23 & 31) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IISM Component|Mapping")
	TMap<int32, int32> ISIDToISMI;

	/** Local mapping: Local ISM Slot Index (ISMI) -> Permanent ISID (Section 23 & 31) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IISM Component|Mapping")
	TMap<int32, int32> ISMIToISID;

	/** Unused physical ISM slot indices (ISMI) available for reuse (Sections 40, 65, 70) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IISM Component|Empty Instances")
	TArray<int32> EmptyInstances;

	/** Compact per-instance runtime data indexed by ISMI (Section 31 & 32) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "IISM Component|Runtime Data")
	TArray<FPropPackedRuntimeData> PackedInstanceData;

	/** Reset / Parking transform where persistent ISM instances and Empty Instances are parked (Section 7, 8, 24, 31, 40) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IISM Component|Parking")
	FTransform ResetTransform;

	/** Component runtime settings (Section 31) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IISM Component|Settings")
	FISMComponentRuntimeSettings ComponentRuntimeSettings;

	/** Whether hitting an ISM instance automatically resolves ISMI -> ISID and transitions it into Unsettled state (promoted to ASM) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IISM Component|Settings")
	bool bUnsettleOnInstanceHit = false;

	/** Debug option: when enabled, prints the hit instance's PID, ISID, and ISMI to screen and log */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "IISM Component|Debug")
	bool bDebugPrintInstanceHit = false;

	/** Default Data Asset used to initialize instances that lack custom database entries */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interactable Prop Database")
	TObjectPtr<UInteractablePropDataAsset> DefaultDataAsset;

	/** Replicated per-instance interactable prop database (kept synchronized with PackedInstanceData) */
	UPROPERTY(ReplicatedUsing = OnRep_PropDatabase, EditAnywhere, BlueprintReadWrite, Category = "Interactable Prop Database")
	TArray<FInteractablePropData> PropDatabase;

	UFUNCTION()
	void OnRep_PropDatabase();

	/** Fired when an instance's portions are consumed */
	UPROPERTY(BlueprintAssignable, Category = "Interactable Prop Database|Events")
	FOnInstancePortionConsumed OnInstancePortionConsumed;

	/** Fired when an ISM instance is hit (provides ISMI, resolved ISID, OtherActor, NormalImpulse, and HitResult) */
	UPROPERTY(BlueprintAssignable, Category = "IISM Component|Events")
	FOnISMInstanceHit OnISMInstanceHit;

	// -----------------------------------------------------------------
	// Section 31 & 65: UInteractableISMComponent Core & Empty Instance API
	// -----------------------------------------------------------------
	/** Adds a new ISM instance bound to a permanent ISID and initializes its local mapping and packed data */
	UFUNCTION(BlueprintCallable, Category = "IISM Component")
	int32 AddInstanceWithISID(int32 InISID, const FTransform& InstanceTransform, const FPropPackedRuntimeData& InitialData, bool bWorldSpace = true);

	/** Adds multiple ISM instances bound to permanent ISIDs */
	UFUNCTION(BlueprintCallable, Category = "IISM Component")
	TArray<int32> AddInstancesWithISIDs(const TArray<int32>& InISIDs, const TArray<FTransform>& InstanceTransforms, const FPropPackedRuntimeData& InitialData, bool bWorldSpace = true);

	/** Reuses an Empty Instance (or PreferredISMI if available) or adds a new ISM instance for InISID */
	UFUNCTION(BlueprintCallable, Category = "IISM Component")
	int32 ReuseOrAddInstanceWithISID(int32 InISID, const FTransform& InstanceTransform, const FPropPackedRuntimeData& InitialData, int32 PreferredISMI = -1, bool bWorldSpace = true);

	/** Releases an active logical ISID's physical ISM slot to EmptyInstances, clearing runtime data and moving to ResetTransform (Sections 41 & 64) */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Empty Instances")
	bool ReleaseInstanceByISIDToEmpty(int32 InISID);

	/** Permanently removes an ISM instance by ISID and repairs affected ISMI mappings */
	UFUNCTION(BlueprintCallable, Category = "IISM Component")
	bool RemoveInstanceByISID(int32 InISID);

	/** Removes an ISM instance by ISID and outputs all (ISID -> NewISMI) pairs whose slot index shifted */
	bool RemoveInstanceByISIDWithRepair(int32 InISID, TMap<int32, int32>& OutShiftedISIDToISMI);

	/** Resolves local ISM slot index (ISMI) from permanent ISID; returns INDEX_NONE if not found */
	UFUNCTION(BlueprintPure, Category = "IISM Component|Mapping")
	int32 FindISMI(int32 InISID) const;

	/** Resolves permanent ISID from local ISM slot index (ISMI); returns 0 if not found or if Empty */
	UFUNCTION(BlueprintPure, Category = "IISM Component|Mapping")
	int32 FindISID(int32 InISMI) const;

	/** Resolves permanent ISID from local ISM slot index (ISMI) */
	UFUNCTION(BlueprintPure, Category = "IISM Component|Mapping")
	int32 GetISIDFromISMI(int32 InISMI) const { return FindISID(InISMI); }

	/** Registers or updates a bidirectional ISID <-> ISMI mapping locally */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Mapping")
	void BindISIDToISMI(int32 InISID, int32 InISMI);

	/** Reads the world or local transform of an instance by ISID */
	UFUNCTION(BlueprintPure, Category = "IISM Component|Transform")
	bool GetInstanceTransformByISID(int32 InISID, FTransform& OutTransform, bool bWorldSpace = true) const;

	/** Updates the transform of an instance (Section 31 & 65 exact name) */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Transform")
	bool SetInstanceTransform(int32 InISID, const FTransform& NewTransform, bool bWorldSpace = true, bool bMarkRenderStateDirty = true, bool bTeleport = true);

	/** Updates the transform of an instance by ISID (keeps the same ISMI slot) */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Transform")
	bool SetInstanceTransformByISID(int32 InISID, const FTransform& NewTransform, bool bWorldSpace = true, bool bMarkRenderStateDirty = true, bool bTeleport = true);

	/** Updates the transform of an instance by local ISMI index */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Transform")
	bool SetInstanceTransformByISMI(int32 InISMI, const FTransform& NewTransform, bool bWorldSpace = true, bool bMarkRenderStateDirty = true, bool bTeleport = true);

	/** Returns a guaranteed hidden parking transform (falls back to Z=-100000, Scale=0.0001 if ResetTransform was left at identity) */
	UFUNCTION(BlueprintPure, Category = "IISM Component|Parking")
	FTransform GetEffectiveResetTransform() const;

	/** Returns true if the given physical ISM slot index is currently parked at ResetTransform */
	UFUNCTION(BlueprintPure, Category = "IISM Component|Parking")
	bool IsInstanceParkedAtResetTransform(int32 InISMI) const;

	/** Parks the persistent ISM instance at ResetTransform during ISM -> ASM promotion (Rule 1 & Section 7.2) */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Parking")
	bool MoveInstanceToResetTransform(int32 InISID);

	/** Restores the persistent ISM instance from ResetTransform back to a gameplay transform during ASM -> ISM demotion (Rule 1 & Section 7.3) */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Parking")
	bool RestoreInstanceToGameplayTransform(int32 InISID, const FTransform& GameplayTransform);

	/** Gets packed runtime data for an ISID */
	UFUNCTION(BlueprintPure, Category = "IISM Component|Runtime Data")
	bool GetInstanceData(int32 InISID, FPropPackedRuntimeData& OutData) const;

	/** Sets packed runtime data for an ISID */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Runtime Data")
	bool SetInstanceData(int32 InISID, const FPropPackedRuntimeData& InData);

	/** Gets packed runtime data by local ISMI index */
	UFUNCTION(BlueprintPure, Category = "IISM Component|Runtime Data")
	bool GetInstanceDataByISMI(int32 InISMI, FPropPackedRuntimeData& OutData) const;

	/** Sets packed runtime data by local ISMI index */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Runtime Data")
	bool SetInstanceDataByISMI(int32 InISMI, const FPropPackedRuntimeData& InData);

	/** Returns a complete runtime info snapshot (ISID, ISMI, ProductID, RuntimeDefaults) for an instance slot */
	UFUNCTION(BlueprintPure, Category = "IISM Component|Runtime Data")
	bool GetInstanceRuntimeInfo(int32 InISMI, FISMInstanceRuntimeInfo& OutInfo) const;

	/** Returns a complete runtime info snapshot by permanent ISID */
	UFUNCTION(BlueprintPure, Category = "IISM Component|Runtime Data")
	bool GetInstanceRuntimeInfoByISID(int32 InISID, FISMInstanceRuntimeInfo& OutInfo) const;

	/** Updates runtime defaults for an instance slot and automatically updates the timestamp on Authority */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Runtime Data")
	bool UpdateRuntimeDefaults(int32 InISMI, const FPropPackedRuntimeData& NewData, bool bAutoUpdateTimestamp = true);

	// -----------------------------------------------------------------
	// Section 40 & 65: Empty Instance Functions
	// -----------------------------------------------------------------
	/** Marks an ISMI slot as an Empty Instance, clearing its logical ISID and runtime data and moving it to ResetTransform */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Empty Instances")
	bool AddEmptyInstance(int32 InISMI);

	/** Removes an ISMI slot from the EmptyInstances array */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Empty Instances")
	bool RemoveEmptyInstance(int32 InISMI);

	/** Returns true if InISMI is currently in EmptyInstances */
	UFUNCTION(BlueprintPure, Category = "IISM Component|Empty Instances")
	bool IsEmptyInstance(int32 InISMI) const;

	/** Returns the number of available reusable Empty Instances */
	UFUNCTION(BlueprintPure, Category = "IISM Component|Empty Instances")
	int32 GetEmptyInstanceCount() const;

	/** Returns the Empty ISMI at Index (or INDEX_NONE if out of bounds) */
	UFUNCTION(BlueprintPure, Category = "IISM Component|Empty Instances")
	int32 GetEmptyInstance(int32 Index = 0) const;

	/** Pops and returns the next available Empty ISMI for reuse (or INDEX_NONE if none available) */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Empty Instances")
	int32 ConsumeEmptyInstance();

	/** Clears EmptyInstances tracking array; optionally removes the physical empty ISM instances when session ends */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Empty Instances")
	void ClearEmptyInstances(bool bRemovePhysicalISMInstances = false);

	/** Rebuilds EmptyInstances by identifying all ISM slots without an active logical ISID */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Empty Instances")
	void RebuildEmptyInstances();

	/** Rebuilds ISIDToISMI and ISMIToISID maps after bulk operations or removals */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Mapping")
	void RebuildLocalIndexMap();

	/** Validates consistency between ISM instance count, PackedInstanceData, EmptyInstances, and ISID <-> ISMI mappings */
	UFUNCTION(BlueprintPure, Category = "IISM Component|Validation")
	bool Validate() const;

	// -----------------------------------------------------------------
	// Legacy & Convenience API
	// -----------------------------------------------------------------
	/** Adds an interactable instance with associated prop database entry */
	UFUNCTION(BlueprintCallable, Category = "Interactable Prop Database")
	int32 AddInteractableInstance(const FTransform& InstanceTransform, const FInteractablePropData& InitialData);

	/** Removes an instance and its database entry */
	UFUNCTION(BlueprintCallable, Category = "Interactable Prop Database")
	bool RemoveInteractableInstance(int32 InstanceIndex);

	/** Gets prop data for a specific instance index */
	UFUNCTION(BlueprintPure, Category = "Interactable Prop Database")
	bool GetPropDataForInstance(int32 InstanceIndex, FInteractablePropData& OutData) const;

	/** Updates prop data for a specific instance index */
	UFUNCTION(BlueprintCallable, Category = "Interactable Prop Database")
	bool SetPropDataForInstance(int32 InstanceIndex, const FInteractablePropData& NewData);

	/**
	 * Consumes portions (e.g. player drinks 2 sips out of 4 sips).
	 * Updates the database entry remaining portions, quantity level, and applies stimuli/temperature effects to consumer.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interactable Prop Database")
	bool ConsumeInstancePortions(int32 InstanceIndex, int32 SipsToConsume, AActor* ConsumerActor, int32& OutSipsConsumed);

	/** Updates temperature for a specific instance toward ambient */
	UFUNCTION(BlueprintCallable, Category = "Interactable Prop Database")
	void UpdateInstanceTemperature(int32 InstanceIndex, float AmbientTemperature, float DeltaSeconds);

	/** Updates the simulation state for an instance */
	UFUNCTION(BlueprintCallable, Category = "Interactable Prop Database")
	void SetInstanceSimulationState(int32 InstanceIndex, EPhysicsSimulationState NewState);

	/**
	 * Called automatically when this component receives a physics/collision hit.
	 * Extracts the hit Instance Index (ISMI = Hit.Item), resolves ISID, and sets the prop into Unsettled state.
	 */
	UFUNCTION()
	void HandleComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	/**
	 * Resolves the hit ISM instance index (ISMI) from an FHitResult.
	 * Accounts for FHitResult::GetReversedHit swapping Item and MyItem on swept blocking hits,
	 * and falls back to the closest active instance to ImpactPoint when Item/MyItem are INDEX_NONE.
	 */
	UFUNCTION(BlueprintPure, Category = "IISM Component|Hit & Physics")
	int32 ResolveHitISMI(const FHitResult& Hit, AActor* OtherActor = nullptr, UPrimitiveComponent* OtherComp = nullptr) const;

	/**
	 * Resolves ISID from the hit Instance Index (ISMI) and transitions the instance into Unsettled state (promoted to ASM).
	 * Can be called automatically by OnComponentHit or manually from traces/projectiles/Blueprints.
	 */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Hit & Physics", meta = (AutoCreateRefTerm = "Hit"))
	bool OnInstanceHit(int32 InISMI, AActor* OtherActor = nullptr, UPrimitiveComponent* OtherComp = nullptr, FVector NormalImpulse = FVector::ZeroVector, const FHitResult& Hit = FHitResult());

	/** Resolves ISID from InISMI and transitions the prop into Unsettled state via PropManager */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Hit & Physics")
	bool SetInstanceUnsettledByISMI(int32 InISMI, FVector HitImpulse = FVector::ZeroVector, FVector HitLocation = FVector::ZeroVector);

	/** Sets the instance at InstanceIndex (ISMI) to AtRest (demotes ASM back to ISM at its settled transform) */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Instance State")
	bool SetInstanceToRest(int32 InstanceIndex);

	/** Sets the instance at InstanceIndex (ISMI) to Unsettled (promotes ONLY this instance to ASM, moves ISM slot to ResetTransform, enables physics + unsettled timer) */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Instance State")
	bool SetInstanceToUnsettled(int32 InstanceIndex, FVector HitImpulse = FVector::ZeroVector, FVector ImpactPoint = FVector::ZeroVector, float UnsettledDelay = -1.0f);

	/** Sets the instance at InstanceIndex (ISMI) to Settling (promotes to ASM if needed and monitors velocity until at rest) */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Instance State")
	bool SetInstanceToSettling(int32 InstanceIndex, float VelocityTolerance = -1.0f);

	/** Sets the instance at InstanceIndex (ISMI) to Held (promotes to ASM, moves ISM slot to ResetTransform, disables physics) */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Instance State")
	bool SetInstanceToHeld(int32 InstanceIndex, AActor* HolderActor = nullptr, USceneComponent* InAttachParent = nullptr, FName SocketName = NAME_None);

	/** Sets the instance at InstanceIndex (ISMI) to InMotion (promotes to ASM, moves ISM slot to ResetTransform, enables physics simulation) */
	UFUNCTION(BlueprintCallable, Category = "IISM Component|Instance State")
	bool SetInstanceToInMotion(int32 InstanceIndex, FVector HitImpulse = FVector::ZeroVector, FVector ImpactPoint = FVector::ZeroVector);

	/** Updates the grabbable flag for an instance */
	UFUNCTION(BlueprintCallable, Category = "Interactable Prop Database")
	void SetInstanceGrabbable(int32 InstanceIndex, bool bNewGrabbable);

	/** Ensures PropDatabase and PackedInstanceData arrays match the instance count */
	UFUNCTION(BlueprintCallable, Category = "Interactable Prop Database")
	void SyncDatabaseWithInstanceCount();

	/** Applies a Data Asset to create an initial FInteractablePropData entry */
	UFUNCTION(BlueprintPure, Category = "Interactable Prop Database")
	FInteractablePropData CreatePropDataFromAsset(const UInteractablePropDataAsset* InAsset, int32 InInstanceId = -1) const;

protected:
	/** Cached last gameplay transform per ISID while an ISM slot is parked at ResetTransform */
	UPROPERTY(Transient)
	TMap<int32, FTransform> CachedGameplayTransformsByISID;

	/** Deduplicates OnComponentHit and Actor::NotifyHit occurring on the same frame for the same ISMI */
	uint64 LastProcessedHitFrame = 0;
	int32 LastProcessedHitISMI = INDEX_NONE;
};

