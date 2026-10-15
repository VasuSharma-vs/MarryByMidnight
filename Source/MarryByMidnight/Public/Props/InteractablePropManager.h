#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Props/InteractablePropDatabase.h"
#include "InteractablePropManager.generated.h"

class AActivePropActor;
class UASMComponent;
class UInteractableISMComponent;
class UStaticMeshPhysicsSimulationComponent;
class UPrimitiveComponent;
class USceneComponent;
class APlayerController;
class UInteractablePropDataAsset;
class UStaticMesh;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSettlingObjectRested, UStaticMeshPhysicsSimulationComponent*, SimComp, AActor*, OwnerActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPropRepresentationChanged, int32, ISID, EPropRepresentation, NewRepresentation, AActivePropActor*, ActiveActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPropStateChanged, int32, ISID, EPropState, NewState, EPropState, PreviousState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPropInteracted, int32, ISID, EPropInteractionType, InteractionType, AActor*, InstigatorActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPropManagerLoadStateChanged, EPropManagerLoadState, NewLoadState, bool, bServerReady, bool, bClientReady);

/**
 * Central Server-Authoritative Multiplayer Prop Manager (ISM <-> ASM Architecture).
 *
 * Core Principles (Sections 1-70):
 * 1. One logical prop = one permanent ISID; physical ISM slot = reusable ISMI.
 * 2. ISM is the persistent resting representation; ASM (AActivePropActor) is the temporary active representation managed by UASMComponent.
 * 3. Promotion (ISM -> ASM) never deletes the ISM instance; it moves the SAME ISM slot to ResetTransform and places a pooled ASM at the gameplay transform.
 * 4. Demotion (ASM -> ISM) writes ASM state & transform back to ISID, restores the SAME ISM slot to the gameplay transform, disables the ASM, and returns it to the pool at ResetTransform.
 * 5. Consuming or converting a product releases its ISMI to EmptyInstances for reuse; generating new products reuses EmptyInstances first before creating new ISM instances.
 * 6. Server owns all authoritative state and mutations; clients send requests, receive bounded bulk delta / initialization batches, and generate/reuse products and instances locally.
 * 7. Save/Load persists logical props by ISID, repairing ISMI on load if physical layout changed, and clearing EmptyInstances across sessions.
 */
UCLASS(BlueprintType, Blueprintable)
class MARRYBYMIDNIGHT_API APropManager : public AActor
{
	GENERATED_BODY()

public:
	APropManager();

	/** Static helper to obtain the active Prop Manager in the world */
	UFUNCTION(BlueprintPure, Category = "Prop Manager", meta = (WorldContext = "WorldContextObject"))
	static APropManager* Get(const UObject* WorldContextObject);

protected:
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void PreInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, class UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	// =====================================================================
	// Section 30 & 59: Recommended Variable Layout (APropManager)
	// =====================================================================

	/** Dedicated ASM Component that handles all ASM actors, dynamic 100-500 pool scaling (90% threshold), and EmptyASMIDs */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|ASM")
	TObjectPtr<UASMComponent> ASMComponent;

	/** Array of all managed AActivePropActor units (100 to 500, synced from ASMComponent) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|ASM")
	TArray<TObjectPtr<AActivePropActor>> ASMActors;

	/** Array containing empty/available [ASMID] unit IDs parked at ResetTransform (synced from ASMComponent) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|ASM")
	TArray<int32> EmptyASMIDs;

	/** Shared immutable Product Definitions (IISMDB) keyed by ProductID (PID) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Database")
	TMap<int32, FPropProductDefinition> ProductDefinitions;

	/** Optional initial Data Assets to auto-register into ProductDefinitions on InitializeIISMDB */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Database")
	TArray<TObjectPtr<UInteractablePropDataAsset>> DefaultProductDataAssets;

	/** Registered IISM components keyed by IISMComponentID (ICID) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|Registry")
	TMap<int32, TObjectPtr<UInteractableISMComponent>> ComponentsByICID;

	/** Authoritative per-instance database keyed by permanent logical identity (ISID) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|Registry")
	TMap<int32, FISIDRuntimeIndex> ISIDData;

	/** Active ASM actors currently promoted in gameplay, keyed by ISID */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|Registry")
	TMap<int32, TObjectPtr<AActivePropActor>> ActiveActorByISID;

	/** All spawned ASM actors (active or pooled) keyed by their unique physical ASMID */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|Registry")
	TMap<int32, TObjectPtr<AActivePropActor>> ActiveActorByASMID;

	/** Next monotonically allocated permanent ISID (starts at 10001 per Section 23 & 47) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|Identity")
	int32 NextISID = 10001;

	/** Next monotonically allocated physical ASMID for AActivePropActor units (starts at 1) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|Identity")
	int32 NextASMID = 1;

	/** Next monotonically allocated server TaskID (starts at 100 per Section 35) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|Tasks")
	int32 NextTaskID = 100;

	/** Next monotonically allocated client RequestID */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|Identity")
	int32 NextRequestID = 1;

	/** Independent asynchronous/frame-sliced tasks (Section 35) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|Tasks")
	TArray<FPropTaskRecord> PendingTasks;

	/** Queue/Set of dirty ISIDs awaiting bulk delta replication (Section 30 & 34) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|Replication")
	TArray<int32> DirtyISIDs;

	/** Queue of unique ASMIDs of placed level props awaiting batched conversion to ISM */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|Queue")
	TArray<int32> QueuedPlacedASMIDs;

	/** Number of placed ASM objects to convert to ISM per frame (default: 20, clamp 5..60) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Performance", meta = (ClampMin = "5", ClampMax = "60"))
	int32 PlacedConversionsPerFrame = 20;

	/** Number of actively simulating physics props to synchronize per frame (default: 10, clamp: 1..30) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Networking", meta = (ClampMin = "1", ClampMax = "30"))
	int32 PhysicsSyncPropsPerFrame = 10;

	/** Round-robin index for active physics replication */
	int32 NextPhysicsSyncIndex = 0;

	/** Duration in seconds for client-side ASM to smoothly interpolate to the server's rest transform before converting to ISM (default: 0.20s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Networking", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float DemoteRestInterpDuration = 0.20f;

	/** Flat list of all registered ISIDs for round-robin background processing */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|Registry")
	TArray<int32> AllActiveISIDs;

	/** Number of ISIDs to check and update thermal timestamps for per frame (default: 15, clamp 5..50) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Performance", meta = (ClampMin = "5", ClampMax = "50"))
	int32 ThermalUpdatesPerFrame = 15;

	/** Round-robin cursor index for thermal updates */
	int32 NextThermalUpdateIndex = 0;

	/** Queue of ISIDs awaiting batched promotion to ASM */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|Queue")
	TArray<int32> QueuedPromotionISIDs;

	/** Number of ISM->ASM promotions to process per frame */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Performance", meta = (ClampMin = "5", ClampMax = "60"))
	int32 PromotionsPerFrame = 20;

	/** Per-client delivery streams keyed by PlayerController (Section 30 & 34) */
	UPROPERTY(Transient)
	TMap<TObjectPtr<APlayerController>, FPropClientSyncState> ClientDeliveryStreams;

	/** Replicated/inspectable array of client synchronization states (Section 30) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|Replication")
	TArray<FPropClientSyncState> ClientSyncState;

	/** Optional class override when spawning pooled AActivePropActor instances */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Pooling")
	TSubclassOf<AActivePropActor> ActivePropActorClass;

	/** Storage/parking transforms for inactive ISM and ASM representations (Section 8 & 30) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Pooling")
	TArray<FTransform> ResetTransforms;

	/** Default parking/reset transform used when ResetTransforms is empty */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Pooling")
	FTransform DefaultResetTransform;

	/** Returns the specific reset/parking transform for an actor slot */
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Pooling")
	FTransform GetResetTransformForSlot(int32 SlotIndex) const;

	/** Persistent world save data snapshot (Section 30 & 36) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|SaveLoad")
	FPropWorldSaveData WorldSaveData;

	/** Save schema version (Section 30) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|SaveLoad")
	int32 SchemaVersion = 1;

	/** Current level initialization and synchronization state (Section 59) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Prop Manager|Loading")
	EPropManagerLoadState LoadState = EPropManagerLoadState::Uninitialized;

	/** True when the server authoritative Prop Database is initialized and ready (Section 59) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Prop Manager|Loading")
	bool PropManagerReady = false;

	/** True when the local client has received and applied initial prop synchronization (Section 59) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop Manager|Loading")
	bool PropSyncReady = false;

	// =====================================================================
	// Section 34: Bulk Replication Limits & Environment Settings
	// =====================================================================

	/** Bounds instance processing per frame (Section 34) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Replication", meta = (ClampMin = "1"))
	int32 MaxInstancesPerFrame = 64;

	/** Bounds network payload bytes per frame (Section 34) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Replication", meta = (ClampMin = "256"))
	int32 MaxBytesPerFrame = 8192;

	/** Bounds records per bulk update batch (Section 34) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Replication", meta = (ClampMin = "1"))
	int32 MaxRecordsPerBatch = 32;

	/** Ambient environment temperature in Celsius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Environment", meta = (Units = "Celsius"))
	float AmbientTemperature = 21.0f;

	/** Number of props to update per frame in the distributed round-robin loop (default: 1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Performance", meta = (ClampMin = "1"))
	int32 PropsUpdatedPerFrame = 1;

	/** Interval in seconds for checking settling objects (default: 0.05s / 20 Hz) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Performance", meta = (ClampMin = "0.01"))
	float SettlingCheckInterval = 0.05f;

	/** Whether to evaluate settling object velocities on an asynchronous background task */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Performance")
	bool bUseAsyncSettlingCheck = true;

	/** Debug option: when enabled on PropManager, prints hit ISM instance PID, ISID, and ISMI to screen and log across all managed IISM components */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Prop Manager|Debug")
	bool bDebugPrintInstanceHit = false;

	// =====================================================================
	// Delegates / Events
	// =====================================================================
	UPROPERTY(BlueprintAssignable, Category = "Prop Manager|Events")
	FOnSettlingObjectRested OnSettlingObjectRested;

	UPROPERTY(BlueprintAssignable, Category = "Prop Manager|Events")
	FOnPropRepresentationChanged OnPropRepresentationChanged;

	UPROPERTY(BlueprintAssignable, Category = "Prop Manager|Events")
	FOnPropStateChanged OnPropStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Prop Manager|Events")
	FOnPropInteracted OnPropInteracted;

	UPROPERTY(BlueprintAssignable, Category = "Prop Manager|Events")
	FOnPropManagerLoadStateChanged OnPropManagerLoadStateChanged;

	// =====================================================================
	// 37.A & Section 66 — Initialization & Level Initialization Functions
	// =====================================================================
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Initialization")
	void InitializePropManager();

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Initialization")
	void InitializeIISMDB();

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Initialization")
	bool RegisterProductDefinition(const FPropProductDefinition& InDefinition);

	/** Allocates a unique ProductID (>100) not currently used by any registered product definition */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Identity")
	int32 AllocateProductID();

	/** Returns true if the given ProductID is registered in ProductDefinitions */
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Database")
	bool IsProductRegistered(int32 InProductID) const;

	/** Searches registered product definitions for one matching the given Data Asset */
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Database")
	int32 FindProductIDByDataAsset(const UInteractablePropDataAsset* InAsset) const;

	/** Searches registered product definitions for one matching the given Static Mesh */
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Database")
	int32 FindProductIDByStaticMesh(const UStaticMesh* InMesh) const;

	/**
	 * Registers a product definition from an AActivePropActor's PropDataAsset or PropMesh.
	 * If the actor already has a registered ProductID, returns it.
	 * Otherwise allocates or derives a ProductID, registers the definition, and assigns ProductID on the actor.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Database")
	int32 RegisterProductFromActor(AActivePropActor* InActor);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Initialization")
	int32 RegisterIISMComponent(UInteractableISMComponent* InComponent, int32 ExplicitICID = 0);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Initialization")
	UInteractableISMComponent* GetOrCreateIISMComponent(int32 InProductID);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Initialization")
	bool ValidateDefinitions() const;

	/** Section 45, 51, 66, 69: Level Initialization Sequence */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Level Initialization")
	void InitializeLevelProps(const FString& SaveSlotName = TEXT(""), int32 SaveUserIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Level Initialization")
	void InitializeIISMComponents();

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Level Initialization")
	void ClearLevelRuntimeState();

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Level Initialization")
	bool LoadLevelPropDatabase(const FString& SlotName = TEXT("PropManagerSaveSlot"), int32 UserIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Level Initialization")
	void RestoreLevelInstances();

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Level Initialization")
	void RebuildLevelMappings();

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Level Initialization")
	void RebuildLevelEmptyInstances();

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Level Initialization")
	void PrepareClientPropSync();

	// =====================================================================
	// 37.B & Section 22 — Identity & Mapping Functions
	// =====================================================================
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Identity")
	int32 AllocateISID();

	/** Allocates a unique physical ASM Actor Identifier (ASMID > 0) */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Identity")
	int32 AllocateASMID();

	/** C++ direct pointer lookup into ISIDData */
	FISIDRuntimeIndex* FindISIDData(int32 InISID);
	const FISIDRuntimeIndex* FindISIDData(int32 InISID) const;

	/** Blueprint-callable lookup of an ISID record */
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Identity", meta = (DisplayName = "Find ISID Data"))
	bool K2_FindISIDData(int32 InISID, FISIDRuntimeIndex& OutRecord) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Identity")
	bool RegisterISID(const FISIDRuntimeIndex& InRecord);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Identity")
	bool UnregisterISID(int32 InISID);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Identity")
	int32 GetISMI(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Identity")
	int32 GetISMIFromISID(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Identity")
	int32 GetICID(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Identity")
	int32 GetProductID(int32 InISID) const;

	/** Returns the physical ASMID currently bound to InISID (0 if in ISM representation) */
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Identity")
	int32 GetASMID(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Identity")
	bool ValidateISID(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Mapping")
	int32 GetISIDFromISMI(int32 InICID, int32 InISMI) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Mapping")
	UInteractableISMComponent* GetComponentFromISID(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Mapping")
	AActivePropActor* GetActiveActorFromISID(int32 InISID) const;

	/** Looks up an AActivePropActor unit directly by its physical ASMID */
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Mapping")
	AActivePropActor* GetActiveActorFromASMID(int32 InASMID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Mapping")
	AActivePropActor* FindActiveActor(int32 InISID) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Mapping")
	void RegisterActiveActor(int32 InISID, AActivePropActor* InActor);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Mapping")
	void UnregisterActiveActor(int32 InISID);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Mapping")
	UInteractableISMComponent* FindIISMComponent(int32 InICID) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Mapping")
	bool RegisterISMI(int32 InISID, int32 InICID, int32 InISMI);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Mapping")
	bool UpdateISMI(int32 InISID, int32 NewISMI);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Mapping")
	bool RemoveISMI(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Mapping")
	void RebuildISMIMap(int32 InICID);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Mapping")
	bool ValidateISMIMap(int32 InICID) const;

	// =====================================================================
	// 37.C & Sections 15, 42, 43, 52, 53, 66 — Product Generation & Add Functions
	// =====================================================================
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Create")
	void RequestAddProp(int32 InProductID, const FTransform& InTransform, AActor* RequestingActor = nullptr);

	/** C++ overload matching Section 52: RequestAddProp(ProductID, Quantity) */
	void RequestAddProp(int32 InProductID, int32 Quantity, const FTransform& BaseTransform = FTransform::Identity, AActor* RequestingActor = nullptr);

	/** Blueprint-callable quantity request matching Section 52: RequestAddProp(ProductID, Quantity) */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Create", meta = (DisplayName = "Request Add Prop By Quantity", AutoCreateRefTerm = "BaseTransform"))
	void RequestAddPropByQuantity(int32 InProductID, int32 Quantity, const FTransform& BaseTransform, AActor* RequestingActor = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Create")
	void RequestAdd(int32 InProductID, const FTransform& InTransform, AActor* RequestingActor = nullptr);

	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Create")
	void Server_RequestGenerateProducts(int32 InProductID, const TArray<FTransform>& InTransforms);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Create")
	int32 AddPropInstance(int32 InProductID, const FTransform& InTransform);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Create")
	TArray<int32> AddPropInstances(int32 InProductID, const TArray<FTransform>& InTransforms);

	/** Section 53 & 66: Generates product instances, reusing EmptyInstances first before creating new ISM instances */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Product Generation")
	TArray<int32> GenerateProductInstances(int32 InProductID, const TArray<FTransform>& InTransforms);

	/** C++ overload matching Section 53: GenerateProductInstances(ProductID, Quantity) */
	TArray<int32> GenerateProductInstances(int32 InProductID, int32 Quantity, const FTransform& BaseTransform = FTransform::Identity);

	/** Blueprint-callable overload matching Section 53: GenerateProductInstances(ProductID, Quantity) */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Product Generation", meta = (DisplayName = "Generate Product Instances By Quantity", AutoCreateRefTerm = "BaseTransform"))
	TArray<int32> GenerateProductInstancesByQuantity(int32 InProductID, int32 Quantity, const FTransform& BaseTransform);

	/** Section 66: Generates a single product instance, reusing an Empty Instance if available */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Product Generation", meta = (AutoCreateRefTerm = "InTransform"))
	int32 GenerateProductInstance(int32 InProductID, const FTransform& InTransform);

	/** C++ convenience overload for GenerateProductInstance(ProductID) */
	int32 GenerateProductInstance(int32 InProductID) { return GenerateProductInstance(InProductID, FTransform::Identity); }

	/** Section 66: Reuses up to MaxQuantity EmptyInstances to create new logical product instances */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Product Generation")
	TArray<int32> GenerateProductInstancesFromEmpty(int32 InProductID, int32 MaxQuantity, const TArray<FTransform>& InTransforms);

	/** Section 66: Generates new physical ISM instances for RemainingQuantity after EmptyInstances are exhausted */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Product Generation")
	TArray<int32> GenerateNewISMInstances(int32 InProductID, int32 Quantity, const TArray<FTransform>& InTransforms);

	/** Section 66: Allocates a new permanent ISID for a generated or reused physical slot */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Product Generation")
	int32 AllocateISIDForInstance(int32 InProductID, int32 InICID, int32 InISMI);

	/** Section 66: Initializes runtime data, mappings, and transform for a newly generated or reused instance */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Product Generation")
	bool InitializeGeneratedInstance(int32 InISID, int32 InProductID, int32 InICID, int32 InISMI, const FTransform& InTransform);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Create")
	TArray<int32> CreateInstanceBatch(int32 InProductID, const TArray<FTransform>& InTransforms);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Create")
	bool RegisterInstance(int32 InISID, int32 InProductID, int32 InICID, int32 InISMI, const FTransform& InTransform);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Create")
	bool InitializeInstance(int32 InISID, int32 InProductID, const FTransform& InTransform);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Create")
	bool InitializeInstanceData(int32 InISID, const FPropProductDefinition& ProductDef);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Create")
	bool InitializeInstanceTransform(int32 InISID, const FTransform& InTransform);

	// =====================================================================
	// Sections 40, 41, 54, 55, 66 — Empty Instance Management & Product Conversion
	// =====================================================================
	/** Checks whether an IISM component is compatible with TargetProductID for Empty Instance reuse (Section 55) */
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Empty Instances")
	bool CanComponentReuseForProduct(const UInteractableISMComponent* Comp, int32 TargetProductID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Empty Instances")
	int32 FindEmptyInstance(int32 InProductID, int32& OutICID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Empty Instances")
	int32 FindEmptyInstances(int32 InProductID, int32 MaxCount, int32& OutICID, TArray<int32>& OutISMIs) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Empty Instances")
	int32 ReuseEmptyInstance(int32 InProductID, int32 InICID, int32 InISMI, const FTransform& InTransform);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Empty Instances")
	bool ReleaseInstanceToEmpty(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Empty Instances")
	bool AddEmptyInstance(int32 InICID, int32 InISMI);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Empty Instances")
	bool RemoveEmptyInstance(int32 InICID, int32 InISMI);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Empty Instances")
	void ClearEmptyInstances(int32 InICID = 0);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Empty Instances")
	void RebuildEmptyInstances(int32 InICID = 0);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Empty Instances")
	int32 GetEmptyInstanceCount(int32 InProductID = 0) const;

	/** Section 54 & 66: Product Conversion Functions */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Product Conversion")
	TArray<int32> ConvertProduct(int32 SourceProductID, int32 TargetProductID, int32 Quantity, const TArray<FTransform>& TargetTransforms);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Product Conversion")
	TArray<int32> ConvertProductInstances(const TArray<int32>& SourceISIDs, int32 TargetProductID, const TArray<FTransform>& TargetTransforms);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Product Conversion")
	int32 ConsumeProductInstances(const TArray<int32>& SourceISIDs);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Product Conversion")
	TArray<int32> ReplaceProductInstances(const TArray<int32>& SourceISIDs, int32 TargetProductID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Product Conversion")
	TArray<int32> ConvertInstancesToProduct(const TArray<int32>& SourceISIDs, int32 TargetProductID, const TArray<FTransform>& TargetTransforms);

	// =====================================================================
	// 37.D & Section 16 — Prop Remove / Destroy Functions (Destroys ISID)
	// =====================================================================
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Remove")
	void RequestRemoveProp(int32 InISID, AActor* RequestingActor = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Remove")
	void RequestRemove(int32 InISID, AActor* RequestingActor = nullptr);

	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Remove")
	void Server_RequestRemoveProp(int32 InISID, AActor* RequestingActor);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Remove")
	bool ValidateRemoveProp(int32 InISID, AActor* RequestingActor = nullptr) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Remove")
	bool RemoveProp(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Remove")
	bool RemovePropInstance(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Remove")
	bool DestroyPropInstance(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Remove")
	bool UnregisterInstance(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Remove")
	bool RemoveISMInstance(int32 InICID, int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Remove")
	void RepairISMIMap(int32 InICID);

	// =====================================================================
	// 37.E & Section 7 & 21 — Representation Management (ISM <-> ASM)
	// Invariant: Never creates or destroys a logical ISID.
	// =====================================================================
	/** Exact C++ signature specified in Section 21 */
	bool ConvertISMToASM(
		uint32 ISID,
		EPropActivationReason Reason,
		bool bEnableReplication,
		AActivePropActor*& OutActor
	);

	/** Exact C++ signature specified in Section 21 */
	bool ConvertASMToISM(
		uint32 ISID,
		EPropState NewState,
		bool bPoolActor,
		bool bWriteBackTransform
	);

	/** Blueprint-callable ISM -> ASM promotion (Section 7.2 steps 1-11) */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Representation")
	bool PromoteInstanceToASM(int32 InISID, EPropActivationReason Reason, bool bEnableReplication, AActivePropActor*& OutActor);

	/** Blueprint-callable ASM -> ISM demotion (Section 7.3 steps 12-21) */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Representation")
	bool DemoteActorToISM(int32 InISID, EPropState NewState = EPropState::AtRest, bool bPoolActor = true, bool bWriteBackTransform = true);

	/**
	 * Converts a placed AActivePropActor in the level into an ISM instance managed by PropManager.
	 * Verifies product registration, adds the instance to the corresponding UInteractableISMComponent,
	 * deactivates the actor, and returns it to the ASM pool at ResetTransform.
	 *
	 * @param InActor The placed active prop actor to convert.
	 * @param bForceConvert If true, converts even if the actor is not currently in AtRest state.
	 * @return The allocated permanent ISID (>0) of the new ISM instance, or 0 on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Product Generation")
	int32 ConvertPlacedActorToISM(AActivePropActor* InActor, bool bForceConvert = false);

	/**
	 * Converts/promotes an ISM instance to an active ASM actor (AActivePropActor) by its permanent ISID.
	 * Fully multiplayer compatible:
	 * - On Server: Authoritatively promotes the ISM, activates the ASM actor, applies physics/state,
	 *   and replicates the change to all clients via bulk delta replication.
	 * - On Client: Fast-promotes locally using a dedicated predicted ASM unit from the client's pool partition
	 *   for 0ms local response, and sends a reliable RPC to the Server for verification and reconciliation.
	 *
	 * @param InISID The permanent logical identifier of the prop to promote.
	 * @param TargetState The target state for the promoted ASM (default: Unsettled).
	 * @param Reason The activation reason (default: Interaction).
	 * @param bSimulatePhysics If true, enables physics simulation and sets state to Unsettled.
	 * @return Pointer to the active AActivePropActor representing this ISID.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Representation", meta = (DisplayName = "Make ISM To ASM"))
	AActivePropActor* MakeISMToASM(
		int32 InISID,
		EPropState TargetState = EPropState::Unsettled,
		EPropActivationReason Reason = EPropActivationReason::Interaction,
		bool bSimulatePhysics = false);

	/**
	 * Convenience overload to convert an ISM to an ASM by ISID with default interaction settings.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Representation", meta = (DisplayName = "Convert ISM To ASM From ISID"))
	AActivePropActor* ConvertISMToASMFromISID(int32 InISID);

	/**
	 * Queues a placed ASM actor by its unique ASMID for smooth batched conversion to ISM (10-30 per frame).
	 * Can be called from server or client.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Product Generation")
	void QueuePlacedASMForConversion(int32 InASMID);

	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Product Generation")
	void Server_QueuePlacedASM(int32 InASMID);

	UFUNCTION(NetMulticast, Reliable, Category = "Prop Manager|Product Generation")
	void Multicast_OnPlacedASMsConvertedToISM(const TArray<int32>& ConvertedASMIDs);

	/** Reliable NetMulticast RPC: notifies all clients that an ISM instance has been converted to an active ASM actor */
	UFUNCTION(NetMulticast, Reliable, Category = "Prop Manager|Representation")
	void Multicast_NotifyISMConvertedToASM(int32 InISID, int32 InProductID, int32 InASMID, const FTransform& InTransform, EPropState InState, EPropActivationReason InReason);

	/** Reliable NetMulticast RPC: notifies all clients that an active ASM actor has returned to rest and converted back to ISM */
	UFUNCTION(NetMulticast, Reliable, Category = "Prop Manager|Representation")
	void Multicast_NotifyASMConvertedToISM(int32 InISID, int32 InProductID, int32 InASMID, const FTransform& InTransform, EPropState InState);

	/**
	 * Queues an array of ISIDs for batched promotion from ISM to ASM (10-30 per frame).
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Representation", meta = (DisplayName = "Batch Make ISM To ASM"))
	void BatchMakeISMToASM(const TArray<int32>& InISIDs, EPropState TargetState = EPropState::Unsettled, EPropActivationReason Reason = EPropActivationReason::Interaction, bool bSimulatePhysics = false);

	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Representation")
	void Server_BatchMakeISMToASM(const TArray<int32>& InISIDs, EPropState TargetState, EPropActivationReason Reason, bool bSimulatePhysics);

	/** Returns true if an ISID is already in ASM representation or currently queued/in-flight converting to ASM */
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Representation")
	bool IsISIDConvertingOrConvertedToASM(int32 InISID) const;

	/** Set of ISIDs currently in-flight during conversion between representations to prevent re-entrant calls */
	UPROPERTY(Transient)
	TSet<int32> PendingConversionISIDs;

	void ProcessQueuedPlacedConversions(int32 BatchSize = -1);
	void ProcessQueuedISMPromotions(int32 BatchSize = -1);

	/** Processes round-robin physics synchronization for moving/unsettled props (up to PhysicsSyncPropsPerFrame per tick) */
	void ProcessActivePhysicsReplication(float DeltaSeconds);

	/** Unreliable multicast streaming packet carrying authoritative positions/velocities for active simulating props */
	UFUNCTION(NetMulticast, Unreliable, Category = "Prop Manager|Networking")
	void Multicast_SyncActivePhysicsBatch(const TArray<FActivePropPhysicsSync>& Batch);

	/** Processes a frame-sliced batch of ISIDs (10-20 per frame) for timestamp-based thermal updates */
	void ProcessTimestampedThermalUpdates(float DeltaSeconds);

	/** Lightweight unreliable multicast to sync thermal/portion changes for resting/active ISIDs */
	UFUNCTION(NetMulticast, Unreliable, Category = "Prop Manager|Networking")
	void Multicast_SyncRuntimeDataBatch(const TArray<FPropRuntimeSyncItem>& Batch);

	/** Client RPC / Server function to request temperature update from client */
	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Networking")
	void Server_RequestTemperatureUpdate(int32 InISID, float NewTemperature);

	/** Reads packed runtime data directly from the authoritative ISID record / ISM component */
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Runtime Data")
	bool GetPackedRuntimeData(int32 InISID, FPropPackedRuntimeData& OutData) const;

	/** Consumes portions on an instance by ISID, updating ISMComponent data and timestamp */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Runtime Data")
	bool ConsumeInstancePortion(int32 InISID, AActor* ConsumerActor, float PortionRatio = 1.0f);

	/** Acquires an inactive ASM from ActiveActorPool or spawns a new one */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Representation")
	AActivePropActor* AcquireActiveActor(int32 InProductID);

	/** Disables an ASM, parks it at ResetTransform, and returns it to ActiveActorPool */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Representation")
	void ReturnActiveActorToPool(AActivePropActor* InActor);

	/** Sets a custom Blueprint or C++ ActivePropActor subclass on the ASMComponent (and PropManager) and optionally rebuilds empty pooled ASM actors */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Custom ASM Actor")
	void SetCustomActivePropActorClass(TSubclassOf<AActivePropActor> NewActorClass, bool bRebuildPoolNow = true);

	/** Sets a custom Blueprint or C++ ActivePropActor subclass for a specific ProductID on the ASMComponent */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Custom ASM Actor")
	void SetCustomActorClassForProduct(int32 InProductID, TSubclassOf<AActivePropActor> NewActorClass);

	// =====================================================================
	// 37.F & Sections 9, 10, 25 — Interaction Architecture & Validation
	// =====================================================================
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Interaction")
	bool CanInteract(int32 InISID, AActor* InstigatorActor = nullptr, EPropInteractionType InteractionType = EPropInteractionType::Use) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Interaction")
	bool GetInteractionData(int32 InISID, FPropInteractionRuntimeData& OutInteractionData) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Interaction")
	void GetInteractionOptions(int32 InISID, AActor* InstigatorActor, TArray<EPropInteractionType>& OutOptions) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Interaction")
	void RequestInteraction(int32 InISID, EPropInteractionType InteractionType, AActor* InstigatorActor, FVector OptionalVector = FVector::ZeroVector, float OptionalFloat = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Interaction")
	void RequestInteract(int32 InISID, EPropInteractionType InteractionType, AActor* InstigatorActor);

	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Interaction")
	void Server_RequestInteraction(int32 InISID, EPropInteractionType InteractionType, AActor* InstigatorActor, FVector OptionalVector, float OptionalFloat, int32 ExpectedVersion);

	/** Validates all 10 conditions in Section 25 (ISID, ProductDef, Player, State, Distance, Permission, Lock, Capability, Version, Cooldown) */
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Interaction")
	bool ValidateInteraction(int32 InISID, EPropInteractionType InteractionType, AActor* InstigatorActor, int32 ExpectedVersion = -1) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Interaction")
	bool BeginInteraction(int32 InISID, EPropInteractionType InteractionType, AActor* InstigatorActor);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Interaction")
	bool ExecuteInteraction(int32 InISID, EPropInteractionType InteractionType, AActor* InstigatorActor, FVector OptionalVector = FVector::ZeroVector, float OptionalFloat = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Interaction")
	bool EndInteraction(int32 InISID, AActor* InstigatorActor);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Interaction")
	bool CancelInteraction(int32 InISID, AActor* InstigatorActor = nullptr);

	// =====================================================================
	// 37.G & Section 11 & 26 — Grab / Carry Functions & Ownership Lock
	// =====================================================================
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Grab")
	bool CanGrab(int32 InISID, AActor* InstigatorActor = nullptr) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Grab")
	void RequestGrab(int32 InISID, AActor* InstigatorActor, USceneComponent* AttachParent = nullptr, FName SocketName = NAME_None);

	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Grab")
	void Server_RequestGrab(int32 InISID, AActor* InstigatorActor, USceneComponent* AttachParent, FName SocketName);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Grab")
	bool ValidateGrab(int32 InISID, AActor* InstigatorActor) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Grab")
	bool BeginGrab(int32 InISID, AActor* InstigatorActor, USceneComponent* AttachParent = nullptr, FName SocketName = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Grab")
	bool ExecuteGrab(int32 InISID, AActor* InstigatorActor, USceneComponent* AttachParent = nullptr, FName SocketName = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Grab")
	bool SetGrabber(int32 InISID, AActor* GrabberActor);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Grab")
	bool SetGrabTransform(int32 InISID, const FTransform& NewTransform);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Grab")
	bool UpdateHeldTransform(int32 InISID, const FTransform& HeldWorldTransform);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Grab")
	void RequestRelease(int32 InISID, AActor* InstigatorActor, FVector ReleaseVelocity = FVector::ZeroVector);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Grab")
	bool ReleaseGrab(int32 InISID, AActor* InstigatorActor = nullptr, FVector ReleaseVelocity = FVector::ZeroVector);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Grab")
	bool CancelGrab(int32 InISID, AActor* InstigatorActor = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Grab")
	bool DropProp(int32 InISID, AActor* InstigatorActor = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Grab")
	bool ThrowProp(int32 InISID, AActor* InstigatorActor, FVector ThrowImpulse);

	// =====================================================================
	// 37.H & Section 13 & 19.1 & 29 — Generic Prop Interaction Functions
	// =====================================================================
	// Use
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanUse(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestUse(int32 InISID, AActor* InstigatorActor);
	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Generic Interaction")
	void Server_RequestUse(int32 InISID, AActor* InstigatorActor);
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	bool ExecuteUse(int32 InISID, AActor* InstigatorActor);

	// Open / Close / Toggle
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanOpen(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestOpen(int32 InISID, AActor* InstigatorActor);
	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Generic Interaction")
	void Server_RequestOpen(int32 InISID, AActor* InstigatorActor);
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	bool SetOpenState(int32 InISID, float OpenAmount = 1.0f);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanClose(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestClose(int32 InISID, AActor* InstigatorActor);
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	bool SetClosedState(int32 InISID);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanToggle(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestToggle(int32 InISID, AActor* InstigatorActor);
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	bool SetToggleState(int32 InISID, bool bToggledOn);

	// Activate / Deactivate
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanActivate(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestActivate(int32 InISID, AActor* InstigatorActor);
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	bool SetActiveState(int32 InISID, bool bNewActivated);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanDeactivate(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestDeactivate(int32 InISID, AActor* InstigatorActor);

	// Consume
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanConsume(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestConsume(int32 InISID, AActor* InstigatorActor, int32 Portions = 1);
	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Generic Interaction")
	void Server_RequestConsume(int32 InISID, AActor* InstigatorActor, int32 Portions);
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	bool ConsumePortion(int32 InISID, AActor* ConsumerActor, int32 Portions = 1);
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	bool ApplyConsume(int32 InISID, AActor* ConsumerActor, int32 Portions = 1);

	// PickUp / Drop / Throw
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanPickUp(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestPickUp(int32 InISID, AActor* InstigatorActor);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanDrop(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestDrop(int32 InISID, AActor* InstigatorActor);
	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Generic Interaction")
	void Server_RequestDrop(int32 InISID, AActor* InstigatorActor);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanThrow(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestThrow(int32 InISID, AActor* InstigatorActor, FVector ThrowImpulse);

	// Break / Repair / Damage / Inspect / Combine / Split / Move / Rotate
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanBreak(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestBreak(int32 InISID, AActor* InstigatorActor);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanRepair(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestRepair(int32 InISID, AActor* InstigatorActor, float RepairAmount = 25.0f);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanDamage(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestDamage(int32 InISID, AActor* InstigatorActor, float DamageAmount = 25.0f);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanInspect(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestInspect(int32 InISID, AActor* InstigatorActor);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanCombine(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestCombine(int32 InISID, int32 OtherISID, AActor* InstigatorActor);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanSplit(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestSplit(int32 InISID, AActor* InstigatorActor);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanMove(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestMove(int32 InISID, const FTransform& TargetTransform, AActor* InstigatorActor = nullptr);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Generic Interaction")
	bool CanRotate(int32 InISID, AActor* InstigatorActor = nullptr) const;
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Generic Interaction")
	void RequestRotate(int32 InISID, const FRotator& TargetRotation, AActor* InstigatorActor = nullptr);

	// =====================================================================
	// 37.I & Section 12 & 20 — Physics Functions
	// =====================================================================
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Physics")
	bool EnablePhysics(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Physics")
	bool DisablePhysics(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Physics")
	bool SetPhysicsMode(int32 InISID, EPropPhysicsMode NewMode);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Physics")
	bool ApplyImpulse(int32 InISID, FVector Impulse, bool bVelChange = true);

	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Physics")
	void ApplyImpulse_Server(int32 InISID, FVector Impulse, bool bVelChange = true);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Physics")
	bool ApplyForce(int32 InISID, FVector Force);

	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Physics")
	void ApplyForce_Server(int32 InISID, FVector Force);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Physics")
	bool ApplyTorque(int32 InISID, FVector TorqueInRadians);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Physics")
	bool SetLinearVelocity(int32 InISID, FVector NewVelocity);

	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Physics")
	void SetVelocity_Server(int32 InISID, FVector NewLinearVelocity, FVector NewAngularVelocityDegrees);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Physics")
	bool SetAngularVelocity(int32 InISID, FVector NewAngularVelocityDegrees);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Physics")
	FVector GetLinearVelocity(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Physics")
	FVector GetAngularVelocity(int32 InISID) const;

	/** Evaluates both linear and angular speed tolerances (Section 12) */
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Physics")
	bool CheckSettled(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Physics")
	bool EvaluateRestTolerance(int32 InISID) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Physics")
	bool StartSettling(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Physics")
	bool FinishSettling(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Physics")
	void UpdateSettling(float DeltaSeconds);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Physics")
	bool WakeProp(int32 InISID);

	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Physics")
	void WakeProp_Server(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Physics")
	bool SleepProp(int32 InISID);

	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Physics")
	void SleepProp_Server(int32 InISID);

	// =====================================================================
	// 37.J & Sections 17, 18, 19.2 — Runtime Data & State Functions
	// =====================================================================
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Runtime Data")
	bool GetPropInstanceData(int32 InISID, FISIDRuntimeIndex& OutData) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Runtime Data")
	bool GetPropData(int32 InISID, FInteractablePropData& OutData) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Runtime Data")
	bool SetPropInstanceData(int32 InISID, const FISIDRuntimeIndex& InData);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Runtime Data")
	bool UpdatePropInstanceData(int32 InISID, const FPropPackedRuntimeData& InPackedData);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Runtime Data")
	bool UpdateProductInstanceInfo(int32 InISID, const FPropPackedRuntimeData& InPackedData);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Runtime Data")
	bool PatchPropInstanceData(int32 InISID, const FPropInstancePatch& InPatch);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Runtime Data")
	bool ApplyInstancePatch(int32 InISID, const FPropInstancePatch& InPatch);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Runtime Data")
	bool ResetPropInstanceData(int32 InISID);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Runtime Data")
	bool ValidatePropInstanceData(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Runtime Data")
	int32 GetInstanceVersion(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Runtime Data")
	int32 GetVersion(int32 InISID) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Runtime Data")
	int32 IncrementInstanceVersion(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Runtime Data")
	int32 IncrementVersion(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Runtime Data")
	void MarkInstanceDirty(int32 InISID, int32 DirtyFlags = 4);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Runtime Data")
	void ClearInstanceDirty(int32 InISID);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Runtime Data")
	int32 GetRemainingPortions(int32 InISID) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Runtime Data")
	bool SetRemainingPortions(int32 InISID, int32 NewPortions);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Runtime Data")
	bool AddPortions(int32 InISID, int32 PortionsToAdd);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Runtime Data")
	bool RemovePortions(int32 InISID, int32 PortionsToRemove);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Runtime Data")
	float GetTemperature(int32 InISID) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Runtime Data")
	bool SetTemperature(int32 InISID, float NewTemperature);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Runtime Data")
	bool ModifyTemperature(int32 InISID, float DeltaTemperature);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Runtime Data")
	int32 GetCreationTime(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Runtime Data")
	int32 GetLastUpdatedTime(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Runtime Data")
	bool GetIsGrabbable(int32 InISID) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Runtime Data")
	bool SetIsGrabbable(int32 InISID, bool bNewGrabbable);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|State")
	EPropState GetPropState(int32 InISID) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|State")
	bool SetPropState(int32 InISID, EPropState NewState);

	/** C++ query for prop world transform by ISID (Section 19.2) */
	FTransform GetTransform(int32 InISID) const;

	/** Blueprint-callable query for prop world transform by ISID */
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Queries", meta = (DisplayName = "Get Prop Transform"))
	FTransform GetPropTransform(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Queries")
	EPropRepresentation GetRepresentation(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Queries")
	int32 GetInteractionFlags(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Queries")
	int32 GetISID(UPrimitiveComponent* TargetComponent, int32 InstanceIndex = -1) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|State")
	bool IsAtRest(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|State")
	bool IsInMotion(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|State")
	bool IsHeld(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|State")
	bool IsSettling(int32 InISID) const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|State")
	bool IsUnsettled(int32 InISID) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|State")
	bool StartMotion(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|State")
	bool StopMotion(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|State")
	bool StartHeld(int32 InISID, AActor* Holder = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|State")
	bool EndHeld(int32 InISID);

	// =====================================================================
	// Mesh Instance State Functions (Rest, Unsettled, Settling, Held, InMotion)
	// =====================================================================
	/** Sets a mesh instance (ISM Component + InstanceIndex, or ActivePropActor mesh) to AtRest (demotes ASM back to ISM at its settled transform) */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Instance State")
	bool SetMeshInstanceToRest(UPrimitiveComponent* TargetComponent, int32 InstanceIndex = -1);

	/** Sets a mesh instance (ISM Component + InstanceIndex, or ActivePropActor mesh) to Unsettled (promotes that single ISM instance to ASM, moves ISM to ResetTransform, enables physics + unsettled timer) */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Instance State")
	bool SetMeshInstanceToUnsettled(
		UPrimitiveComponent* TargetComponent,
		int32 InstanceIndex = -1,
		FVector HitImpulse = FVector::ZeroVector,
		FVector ImpactPoint = FVector::ZeroVector,
		float UnsettledDelay = -1.0f);

	/** Sets a mesh instance (ISM Component + InstanceIndex, or ActivePropActor mesh) to Settling (promotes to ASM if needed and monitors velocity until at rest) */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Instance State")
	bool SetMeshInstanceToSettling(UPrimitiveComponent* TargetComponent, int32 InstanceIndex = -1, float VelocityTolerance = -1.0f);

	/** Sets a mesh instance (ISM Component + InstanceIndex, or ActivePropActor mesh) to Held (promotes to ASM, disables physics, optionally attaches to HolderActor/AttachParent) */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Instance State")
	bool SetMeshInstanceToHeld(
		UPrimitiveComponent* TargetComponent,
		int32 InstanceIndex = -1,
		AActor* HolderActor = nullptr,
		USceneComponent* AttachParent = nullptr,
		FName SocketName = NAME_None);

	/** Sets a mesh instance (ISM Component + InstanceIndex, or ActivePropActor mesh) to InMotion (promotes to ASM, moves ISM to ResetTransform, enables physics simulation) */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Instance State")
	bool SetMeshInstanceToInMotion(
		UPrimitiveComponent* TargetComponent,
		int32 InstanceIndex = -1,
		FVector HitImpulse = FVector::ZeroVector,
		FVector ImpactPoint = FVector::ZeroVector);

	/** Sets an instance by ISID to AtRest (demotes ASM back to ISM at its settled transform) */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Instance State")
	bool SetInstanceToRest(int32 InISID);

	/** Sets an instance by ISID to Unsettled (promotes ISM to ASM, moves ISM to ResetTransform, enables physics + unsettled timer) */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Instance State")
	bool SetInstanceToUnsettled(
		int32 InISID,
		FVector HitImpulse = FVector::ZeroVector,
		FVector ImpactPoint = FVector::ZeroVector,
		float UnsettledDelay = -1.0f);

	/** Sets an instance by ISID to Settling (promotes to ASM if needed and monitors velocity until at rest) */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Instance State")
	bool SetInstanceToSettling(int32 InISID, float VelocityTolerance = -1.0f);

	/** Sets an instance by ISID to Held (promotes to ASM, disables physics, optionally attaches to HolderActor/AttachParent) */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Instance State")
	bool SetInstanceToHeld(
		int32 InISID,
		AActor* HolderActor = nullptr,
		USceneComponent* AttachParent = nullptr,
		FName SocketName = NAME_None);

	/** Sets an instance by ISID to InMotion (promotes to ASM, moves ISM to ResetTransform, enables physics simulation) */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Instance State")
	bool SetInstanceToInMotion(
		int32 InISID,
		FVector HitImpulse = FVector::ZeroVector,
		FVector ImpactPoint = FVector::ZeroVector);

	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Instance State")
	void Server_SetInstanceState(
		int32 InISID,
		EPropState TargetState,
		FVector HitImpulse,
		FVector_NetQuantize ImpactPoint,
		float CustomParam,
		AActor* HolderActor);

	// =====================================================================
	// ISM Hit Forwarding (Send Hit Info from FirstPerson/Actor to PropManager)
	// =====================================================================
	/**
	 * Sends hit info from a hitting actor/component (such as FirstPersonCharacter's OnComponentHit / Event Hit)
	 * directly to PropManager with the hit ISM component, ISMI (Hit.Item), ImpactPoint, and Normals.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Hit & Physics")
	bool SendISMHitToPropManager(
		UPrimitiveComponent* HitISMComponent,
		int32 InISMI,
		FVector ImpactPoint,
		FVector ImpactNormal,
		FVector Normal = FVector::ZeroVector,
		FVector NormalImpulse = FVector::ZeroVector,
		AActor* HittingActor = nullptr);

	/**
	 * Sends hit info by IISMComponentID (ICID) and ISMI (Hit.Item) with ImpactPoint and Normals to PropManager.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Hit & Physics")
	bool SendISMHitByICIDToPropManager(
		int32 InICID,
		int32 InISMI,
		FVector ImpactPoint,
		FVector ImpactNormal,
		FVector Normal = FVector::ZeroVector,
		FVector NormalImpulse = FVector::ZeroVector,
		AActor* HittingActor = nullptr);

	/**
	 * Convenience helper: pass the FHitResult from FirstPersonCharacter's OnComponentHit / Event Hit / Trace
	 * directly to PropManager to extract Hit.Component, Hit.Item (ISMI), Hit.ImpactPoint, and Hit.ImpactNormal.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Hit & Physics")
	bool SendISMHitResultToPropManager(
		const FHitResult& Hit,
		FVector NormalImpulse = FVector::ZeroVector,
		AActor* HittingActor = nullptr);

	/** Client -> Server RPC to forward ISM hit info (ICID, ISMI, ImpactPoint, Normals) to the authoritative PropManager */
	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Hit & Physics")
	void Server_ReceiveISMHit(
		int32 InICID,
		int32 InISMI,
		FVector_NetQuantize ImpactPoint,
		FVector_NetQuantizeNormal ImpactNormal,
		FVector_NetQuantizeNormal Normal,
		FVector NormalImpulse,
		AActor* HittingActor);

	UFUNCTION()
	void HandleExternalActorHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit);

	UFUNCTION()
	void HandleExternalComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	// =====================================================================
	// Section 19.3 — Client Presentation Functions
	// =====================================================================
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Presentation")
	void ApplyPropState(int32 InISID, EPropState InState);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Presentation")
	void ApplyPropTransform(int32 InISID, const FTransform& InTransform);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Presentation")
	void ApplyPropRuntimeData(int32 InISID, const FPropPackedRuntimeData& InData);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Presentation")
	AActivePropActor* CreateClientASM(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Presentation")
	void DestroyClientASM(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Presentation")
	void ActivateClientRepresentation(int32 InISID, EPropRepresentation Rep);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Presentation")
	void DeactivateClientRepresentation(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Presentation")
	void UpdateClientISM(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Presentation")
	void UpdateClientASM(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Presentation")
	void SetClientCollision(int32 InISID, bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Presentation")
	void SetClientVisibility(int32 InISID, bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Presentation")
	void SetClientPhysics(int32 InISID, bool bSimulate);

	// =====================================================================
	// 37.K & Sections 34, 56-59, 66 — Bulk Replication & Client Synchronization
	// =====================================================================
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Replication")
	bool BuildBulkUpdateBatch(int32 MaxRecords, FPropBulkUpdateBatch& OutBatch);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Replication")
	void QueueClientSync(APlayerController* ClientController, const TArray<int32>& ISIDsToSync);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Replication")
	void SendInstanceDelta(int32 InISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Replication")
	void SendFullSnapshot(APlayerController* ClientController = nullptr);

	/** Applies an authoritative delta on the client, generating/reusing the product definition, IISM component, and ISM/ASM instance on the client side */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Replication")
	void ApplyInstanceDelta(const FPropInstanceDelta& Delta);

	/** Generates or reuses a product and its ISM instance on the client side from an authoritative server delta (Section 56) */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Sync")
	int32 GenerateClientProductInstance(const FPropInstanceDelta& Delta);

	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Replication")
	void AcknowledgeBulkBatch(int32 BatchSequence);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Replication")
	void RetryBulkBatch(int32 BatchSequence, APlayerController* ClientController = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Replication")
	void ResyncClient(APlayerController* ClientController);

	UFUNCTION(NetMulticast, Reliable, Category = "Prop Manager|Replication")
	void Multicast_ReceiveBulkBatch(const FPropBulkUpdateBatch& Batch);

	/** Section 56, 57, 58, 59, 66: Client Initial Synchronization Functions */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Sync")
	void BuildInitialPropSnapshot(TArray<FPropBulkUpdateBatch>& OutBatches);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Sync")
	void SendInitialPropSnapshot(APlayerController* ClientController = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Sync")
	void SendPropInitializationBatch(const FPropBulkUpdateBatch& Batch, APlayerController* ClientController = nullptr);

	UFUNCTION(NetMulticast, Reliable, Category = "Prop Manager|Client Sync")
	void Multicast_ReceivePropInitializationBatch(const FPropBulkUpdateBatch& Batch);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Sync")
	void ApplyPropInitializationBatch(const FPropBulkUpdateBatch& Batch);

	UFUNCTION(Server, Reliable, WithValidation, Category = "Prop Manager|Client Sync")
	void AcknowledgePropInitialization(int32 BatchSequence, bool bSyncCompleted);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Client Sync")
	bool IsClientPropSyncReady(APlayerController* ClientController = nullptr) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Client Sync")
	void CompleteClientPropSync();

	// =====================================================================
	// 37.L & Section 35 — Task System Functions
	// =====================================================================
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Tasks")
	int32 CreateTask(EPropTaskType TaskType, int32 InISID = 0, int32 InRequestID = 0);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Tasks")
	bool StartTask(int32 InTaskID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Tasks")
	bool UpdateTaskProgress(int32 InTaskID, float NewProgress, int32 ProcessedCount = 0);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Tasks")
	bool CompleteTask(int32 InTaskID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Tasks")
	bool FailTask(int32 InTaskID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Tasks")
	bool CancelTask(int32 InTaskID);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Tasks")
	EPropTaskStatus GetTaskStatus(int32 InTaskID) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Tasks")
	bool RetryTask(int32 InTaskID);

	// =====================================================================
	// 37.M & Sections 36, 48, 60, 61, 66 — Persistence & Save / Load Functions
	// =====================================================================
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|SaveLoad")
	bool SavePropDatabase(const FString& SlotName = TEXT("PropManagerSaveSlot"), int32 UserIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|SaveLoad")
	bool LoadPropDatabase(const FString& SlotName = TEXT("PropManagerSaveSlot"), int32 UserIndex = 0);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|SaveLoad")
	bool SerializeInstance(int32 InISID, FPropSerializedInstance& OutSerialized) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|SaveLoad")
	bool DeserializeInstance(const FPropSerializedInstance& InSerialized, int32& OutISID);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|SaveLoad")
	int32 SaveDirtyInstances(const FString& SlotName = TEXT("PropManagerSaveSlot"), int32 UserIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|SaveLoad")
	bool FlushPropDatabase(const FString& SlotName = TEXT("PropManagerSaveSlot"), int32 UserIndex = 0);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|SaveLoad")
	bool ValidateLoadedInstance(const FPropSerializedInstance& InSerialized) const;

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|SaveLoad")
	int32 RepairLoadedISMI(int32 InISID, int32 InProductID, int32 InICID, int32 SavedISMI, const FTransform& InTransform, const FPropPackedRuntimeData& InPackedData);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|SaveLoad")
	void RebuildISMMappings();

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|SaveLoad")
	void FlushActiveActorState();

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|SaveLoad")
	void RestoreActiveActors();

	// =====================================================================
	// Existing Registration & Mesh Query API (Preserved & Bridged to ISID)
	// =====================================================================
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Registration")
	void RegisterProp(AActivePropActor* Prop);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Registration")
	void UnregisterProp(AActivePropActor* Prop);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Registration")
	void RegisterISMComponent(UInteractableISMComponent* ISMComp);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Registration")
	void UnregisterISMComponent(UInteractableISMComponent* ISMComp);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Registration")
	void RegisterSettlingComponent(UStaticMeshPhysicsSimulationComponent* SimComp);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Registration")
	void UnregisterSettlingComponent(UStaticMeshPhysicsSimulationComponent* SimComp);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Interaction")
	bool CanInteractWithMesh(UPrimitiveComponent* TargetComponent, int32 InstanceIndex, AActor* InstigatorActor, bool& bOutIsGrabbable, FInteractablePropData& OutData);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Interaction")
	bool InteractWithMesh(UPrimitiveComponent* TargetComponent, int32 InstanceIndex, AActor* InstigatorActor, FName ActionName, int32 PortionsToConsume, FInteractablePropData& OutUpdatedData);

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Stats")
	int32 GetRegisteredPropsCount() const { return ASMActors.Num(); }

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Stats")
	int32 GetRegisteredISMCount() const { return RegisteredISMComponents.Num(); }

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Stats")
	int32 GetActiveSettlingCount() const;

	UFUNCTION(BlueprintPure, Category = "Prop Manager|Time")
	int32 GetCurrentWorldMinutes() const;

protected:
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UInteractableISMComponent>> RegisteredISMComponents;

	UPROPERTY(Transient)
	TArray<int32> PendingReleasedToEmptyISIDs;

	UPROPERTY(Transient)
	TArray<int32> PendingDeletedISIDs;

	bool bHasInitializedLevelProps = false;
	int32 CurrentISMUpdateIndex = 0;
	int32 CurrentISMInstanceIndex = 0;
	int32 NextICID = 1;
	int32 NextBatchSequence = 1;

	FTimerHandle SettlingTimerHandle;

	void EnsureLevelPropsInitialized();
	void ApplyProductDefinitionToComponent(UInteractableISMComponent* Comp, const FPropProductDefinition& Def);
	void SetLoadState(EPropManagerLoadState NewState);
	void UpdateDistributedProps(float DeltaSeconds);
	void UpdateDistributedISM(float DeltaSeconds);
	void ProcessPendingTasks(float DeltaSeconds);
	void ProcessBulkReplication(float DeltaSeconds);
	void CheckSettlingQueue();
	void CheckCharacterFloorISMHits();

	TMap<TWeakObjectPtr<AActor>, FIntPoint> ActiveCharacterFloorISMs;
	TSet<TWeakObjectPtr<AActor>> BoundHitActors;

	FPropInstanceDelta BuildDeltaForRecord(const FISIDRuntimeIndex& Record) const;
};

