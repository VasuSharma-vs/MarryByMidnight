#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NPCEnumsAndTypes.h"
#include "Props/InteractablePropDatabase.h"
#include "Interfaces/WorldAffordanceInterface.h"
#include "InteractableProp.generated.h"

class UStaticMeshComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UTextRenderComponent;
class UStaticMeshPhysicsSimulationComponent;
class UInteractablePropDataAsset;
class APropManager;
class ANPCCharacter;
class APawn;
class AActivePropActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnActivePropStateChanged, AActivePropActor*, PropActor, EPropState, NewState, EPropState, PreviousState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnActivePropBecomeActive, AActivePropActor*, PropActor, int32, ISID, int32, ProductID, FTransform, ActiveTransform);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnActivePropBeforeReturnToRest, AActivePropActor*, PropActor, int32, ISID, int32, ProductID, FTransform, CurrentTransform);

/**
 * Universal interactive world object & Active Static Mesh Actor (ASM) representation (Sections 7, 8, 27, 30).
 * As an ASM, it represents the active physical form of a permanent logical prop identity (ISID) during
 * physics simulation, grabbing/carrying, throwing, movement, or complex interactions, and returns to the
 * actor pool at ResetTransform when demoted back to an ISM.
 *
 * Zero-Copy Category 1 / Sparse Category 2 Architecture:
 * - Category 1 (Shared Data): All product-wide immutable data is read from FPropProductDefinition via
 *   CachedFallbackProductDef. It is NEVER copied per-instance; getters always read from the definition.
 * - Category 2 (Unique Data): Per-instance mutable data is stored in LocalUniqueData (FPropInstanceUniqueData).
 *   This contains ContinuousData (temperature, lastUpdated) and EventData (contentLevel, strength, capSealed, etc.).
 *   Instances whose product has bEnableInstanceUniqueData=false omit this entirely (zero overhead).
 */
UCLASS(BlueprintType, Blueprintable)
class MARRYBYMIDNIGHT_API AActivePropActor : public AActor, public IWorldAffordanceInterface
{
	GENERATED_BODY()

public:
	AActivePropActor();
	const FPropProductDefinition* ResolveProductDefinition() const;
	const FPropContentDefinition* ResolveContentDefinition() const;

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	// ---------------------------------------------------------
	// Section 30: AActivePropActor Identity & Representation Layout
	// ---------------------------------------------------------
	/** Unique physical ASM actor unit identifier (ASMID) assigned by PropManager */
	UPROPERTY(ReplicatedUsing = OnRep_ASMID, VisibleAnywhere, BlueprintReadOnly, Category = "ASM Identity")
	int32 ASMID = 0;

	UFUNCTION()
	void OnRep_ASMID();

	/** Permanent logical prop identity (Rule 3: ISID is the permanent identity) */
	UPROPERTY(ReplicatedUsing = OnRep_ISID, EditAnywhere, BlueprintReadWrite, Category = "ASM Identity")
	int32 ISID = 0;

	UFUNCTION()
	void OnRep_ISID();

	/** Shared Product Definition Identifier (PID) */
	UPROPERTY(ReplicatedUsing = OnRep_ProductID, EditAnywhere, BlueprintReadWrite, Category = "ASM Identity")
	int32 ProductID = 0;

	UFUNCTION()
	virtual void OnRep_ProductID();

	/** Authoritative physics & interaction state of this ASM (Held, InMotion, Unsettled, Settling, AtRest) */
	UPROPERTY(ReplicatedUsing = OnRep_State, EditAnywhere, BlueprintReadWrite, Category = "ASM State")
	EPropState State = EPropState::AtRest;

	/** Acquisition cost for an explicitly tagged, level-authored ShopInitialStock prop. Never inferred from retail price. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop", meta = (ClampMin = "0"))
	int64 InitialShopPurchaseCents = 0;

	UFUNCTION()
	void OnRep_State(EPropState PreviousState);

	/** Sets the prop state (Held, InMotion, Unsettled, Settling, AtRest), updating physics and replication */
	UFUNCTION(BlueprintCallable, Category = "ASM State", meta = (DisplayName = "Set Prop State"))
	void SetPropState(EPropState NewState);

	/** Monotonically increasing instance version for stale-update detection */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "ASM State")
	int32 Version = 1;

	/** Compact packed runtime data synchronized with ISID record */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASM State")
	FPropPackedRuntimeData PackedRuntimeData;

	/** Extended interaction-specific runtime data */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASM State")
	FPropInteractionRuntimeData RuntimeData;

	/** Alias pointer to PropMesh matching Section 30 variable layout (MeshComponent) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	/** Current interaction lock state (Free, Reserved, Held, Interacting, Locked) */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "ASM Interaction")
	EPropInteractionLockState InteractionState = EPropInteractionLockState::Free;

	/** Actor currently owning/holding the interaction lock on this prop */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "ASM Interaction")
	TObjectPtr<AActor> InteractionOwner = nullptr;

	/** Replicated linear velocity snapshot (cm/s) */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "ASM Physics")
	FVector LinearVelocity = FVector::ZeroVector;

	/** Replicated angular velocity snapshot (deg/s) */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "ASM Physics")
	FVector AngularVelocity = FVector::ZeroVector;

	/** Whether this ASM actor is currently active in the gameplay world (false when pooled at ResetTransform) */
	UPROPERTY(ReplicatedUsing = OnRep_IsActive, VisibleAnywhere, BlueprintReadOnly, Category = "ASM State")
	bool bIsActive = true;

	UFUNCTION()
	void OnRep_IsActive();

	/** Whether this ASM is currently held by a player or NPC */
	UPROPERTY(ReplicatedUsing = OnRep_IsHeld, VisibleAnywhere, BlueprintReadOnly, Category = "ASM State")
	bool bIsHeld = false;

	UFUNCTION()
	void OnRep_IsHeld();

	UFUNCTION(BlueprintCallable, Category = "ASM State")
	void SetHeld(bool bNewHeld);

	/** Whether this ASM is actively driven in motion (when true, prevents settling to rest; when false, settling is allowed) */
	UPROPERTY(ReplicatedUsing = OnRep_IsInMotion, BlueprintReadWrite, Category = "ASM State")
	bool bIsInMotion = false;

	UFUNCTION()
	void OnRep_IsInMotion();

	UFUNCTION(BlueprintCallable, Category = "ASM State")
	void SetInMotion(bool bNewInMotion);

	/** Tracks whether network replication is enabled for this ASM (Section 30) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ASM State")
	bool bIsReplicated = true;

	/** Reference to the central Prop Manager orchestrating this ASM (Section 30) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ASM State")
	TObjectPtr<APropManager> PropManagerReference = nullptr;

	// ---------------------------------------------------------
	// Category 2: Per-Instance Unique Data (Zero-Copy Architecture)
	// ---------------------------------------------------------
	/**
	 * Per-instance unique runtime data for this ASM. Contains:
	 *   ContinuousData: temperature, lastUpdatedTime (updated continuously by PropBatchUpdateComponent).
	 *   EventData: contentLevel, strength, capSealed, capType, contentID, creationTime, etc. (updated on discrete events).
	 * Only allocated/replicated when product's bEnableInstanceUniqueData=true.
	 * Replicated with OnRep_LocalUniqueData so clients receive discrete event deltas.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_LocalUniqueData, VisibleAnywhere, BlueprintReadOnly, Category = "Unique Data")
	FPropInstanceUniqueData LocalUniqueData;

	UFUNCTION()
	virtual void OnRep_LocalUniqueData();

	/** True when LocalUniqueData has been initialized from an authoritative ISID record */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Unique Data")
	bool bHasValidUniqueData = false;

	// ---------------------------------------------------------
	// Pool Lifecycle State (not replicated — transient per-unit)
	// ---------------------------------------------------------
	/**
	 * Monotonically increasing generation counter. Incremented each time this pooled unit is activated
	 * for a new ISID. Used by timer/lambda captures to detect stale callbacks after pool reuse.
	 * int32 (not uint32) for UHT/Blueprint safety.
	 */
	int32 PoolGeneration = 0;

	/** True while awaiting server acknowledgement of a client-predicted ISM->ASM promotion */
	bool bPendingServerPromoteAck = false;

	/** WorldElapsedSeconds of the last authoritative discrete event applied to this actor (guards stale continuous sync packets) */
	double LastDiscreteEventTime = 0.0;

	/** Returns the current pool generation (safe for TWeakObjectPtr captures and timer lambdas) */
	int32 GetPoolGeneration() const { return PoolGeneration; }

	// ---------------------------------------------------------
	// Cached Category 1 Product Definition (zero-copy read path)
	// ---------------------------------------------------------
	/**
	 * Server/client-cached copy of the FPropProductDefinition for the current ProductID.
	 * Populated by ApplyProductDefinition() / InitializeFromISIDRecord().
	 * All Category 1 getters (GetDisplayName, GetMaxStrength, GetNumPortions, etc.) read from this.
	 * NOT replicated — clients rebuild from ProductDefinitions on PropManager.
	 */
	FPropProductDefinition CachedFallbackProductDef;

	/** True when CachedFallbackProductDef has been populated with valid data for the current ProductID */
	bool bHasCachedFallbackProductDef = false;

	// ---------------------------------------------------------
	// ASM Promotion / Demotion / Pooling Lifecycle
	// ---------------------------------------------------------
	/**
	 * 5-arg: Populates this ASM from an authoritative ISID record, its product definition, content definition,
	 * and optional Category 2 unique data during ISM->ASM promotion (Section 7.2).
	 * This is the canonical promotion path used by ASMComponent.
	 */
	void InitializeFromISIDRecord(
		const FISIDRuntimeIndex& Record,
		const FPropProductDefinition* ProductDef,
		const FPropContentDefinition* ContentDef,
		const FPropInstanceUniqueData* UniqueData,
		APropManager* InManager);

	/**
	 * 3-arg compatibility wrapper: calls 5-arg with nullptr ContentDef and UniqueData.
	 * Keeps existing call sites that pre-date the Category 2 architecture compiling.
	 */
	void InitializeFromISIDRecord(const FISIDRuntimeIndex& Record, const FPropProductDefinition* ProductDef, APropManager* InManager)
	{
		InitializeFromISIDRecord(Record, ProductDef, nullptr, nullptr, InManager);
	}

	/**
	 * 2-arg: Reads current ASM transform, velocities, state, and runtime data back into the
	 * authoritative ISID record during ASM->ISM demotion. When OutUniqueData is non-null,
	 * also exports LocalUniqueData into it (for PropManager UniqueDataByISID persistence).
	 */
	void WriteToISIDRecord(FISIDRuntimeIndex& InOutRecord, FPropInstanceUniqueData* OutUniqueData) const;

	/**
	 * 1-arg compatibility wrapper: calls 2-arg with nullptr OutUniqueData.
	 * Keeps existing call sites compiling.
	 */
	void WriteToISIDRecord(FISIDRuntimeIndex& InOutRecord) const
	{
		WriteToISIDRecord(InOutRecord, nullptr);
	}

	/** Activates collision, physics, tick, visibility, and replication for gameplay at the specified world transform */
	UFUNCTION(BlueprintCallable, Category = "ASM Lifecycle")
	void ActivateASMRepresentation(const FTransform& GameplayTransform, EPropState InitialPropState, bool bEnableReplication = true);

	/** Fires pre-demotion events while the actor is still active at its gameplay transform before returning to ResetTransform / ISM */
	UFUNCTION(BlueprintCallable, Category = "ASM Lifecycle")
	void NotifyBeforeReturnToRestPosition();

	/** Disables collision, physics, replication, tick, and visibility and parks the ASM at ResetTransform (Section 7.3 Steps 19-20 & Section 8) */
	UFUNCTION(BlueprintCallable, Category = "ASM Lifecycle")
	void DeactivateToPoolState(const FTransform& ResetTransform);

	/** Applies server-authoritative physics sync using velocity nudging (spring-damper) to steer client physics smoothly without popping */
	UFUNCTION(BlueprintCallable, Category = "ASM Physics")
	void ApplyAuthoritativePhysicsSync(const FActivePropPhysicsSync& SyncData);

	/** Begins a smooth kinematic interpolation to TargetTransform over Duration before converting to ISM */
	void StartInterpolateToRestTransform(const FTransform& InTargetTransform, float Duration, TFunction<void()> OnComplete);

	/** Returns true if this ASM is currently interpolating to its final rest transform before demoting to ISM */
	UFUNCTION(BlueprintPure, Category = "ASM Lifecycle")
	bool IsInterpolatingToRest() const { return bIsInterpolatingToRest; }

	/** Target transform to interpolate to before returning to pool */
	FTransform TargetRestTransform;

	/** Starting transform for interpolation */
	FTransform StartRestTransform;

	/** Whether this ASM is currently actively interpolating towards TargetRestTransform before demoting to ISM */
	bool bIsInterpolatingToRest = false;

	/** Elapsed alpha [0..1] for rest interpolation */
	float RestInterpAlpha = 0.0f;

	/** Duration in seconds for rest interpolation */
	float RestInterpDuration = 0.20f;

	/** Callback fired when interpolation to rest completes */
	TFunction<void()> OnRestInterpComplete;

	/** If true, placed level instances of this prop at rest automatically convert to ISM instances on BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASM Lifecycle", meta = (DisplayName = "Auto Convert To ISM If At Rest"))
	bool bAutoConvertToISMIfAtRest = true;

	/**
	 * Checks if this actor's product is registered with PropManager.
	 * If not registered, registers the product (from PropDataAsset or PropMesh).
	 * Then checks its state: if AtRest, converts this placed actor to an ISM instance managed by PropManager
	 * and returns the actor to the ASM pool.
	 * Safe to call from Construction Script or BeginPlay in Blueprint.
	 *
	 * @param bForceConvert If true, converts even if state is not AtRest.
	 * @return The allocated ISID (>0) if converted to ISM, or 0 if not converted.
	 */
	UFUNCTION(BlueprintCallable, Category = "ASM Lifecycle", meta = (DisplayName = "Convert Placed Prop To ISM If At Rest"))
	int32 ConvertPlacedPropToISMIfAtRest(bool bForceConvert = false);

	/**
	 * Registers this actor's product definition with PropManager (from PropDataAsset or PropMesh)
	 * if it is not already registered. Can be called in Construction Script or BeginPlay.
	 *
	 * @return The registered ProductID (>0) on success, or 0 on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "ASM Lifecycle", meta = (DisplayName = "Register Product With Manager"))
	int32 RegisterProductWithManager();

	/**
	 * Returns true if this actor's ProductID is registered with the central PropManager.
	 */
	UFUNCTION(BlueprintPure, Category = "ASM Lifecycle", meta = (DisplayName = "Is Product Registered"))
	bool IsProductRegistered() const;

	/**
	 * Queues this placed actor by its unique ASMID with PropManager for smooth batched conversion to ISM (10-30 per frame).
	 * Safe to call from BeginPlay in Blueprint or C++.
	 */
	UFUNCTION(BlueprintCallable, Category = "ASM Lifecycle", meta = (DisplayName = "Queue Placed Prop For Conversion"))
	void QueueSelfForISMConversion();

	/** Blueprint event fired when this ASM actor is placed at the ISM position and becomes active */
	UFUNCTION(BlueprintImplementableEvent, Category = "ASM Lifecycle", meta = (DisplayName = "On Activated From ISM"))
	void BP_OnActivatedFromISM(int32 InISID, int32 InProductID, EPropState InitialState);

	/** Blueprint event fired right after this ASM actor is placed on the ISM position and becomes active (GetActorLocation/Transform is ready) */
	UFUNCTION(BlueprintImplementableEvent, Category = "ASM Lifecycle", meta = (DisplayName = "On Become Active (Placed On ISM Position)"))
	void BP_OnBecomeActive(int32 InISID, int32 InProductID, FTransform ActiveTransform, EPropState InitialState);

	/** Blueprint event fired right BEFORE this ASM actor goes back to rest / pool position and converts to ISM (actor is still at its gameplay transform) */
	UFUNCTION(BlueprintImplementableEvent, Category = "ASM Lifecycle", meta = (DisplayName = "On Before Return To Rest Position"))
	void BP_OnBeforeReturnToRestPosition(int32 CurrentISID, int32 InProductID, FTransform CurrentGameplayTransform);

	/** Blueprint event fired when this ASM actor settles back to AtRest and is demoted/returned to the ISM pool */
	UFUNCTION(BlueprintImplementableEvent, Category = "ASM Lifecycle", meta = (DisplayName = "On Returned To ISM"))
	void BP_OnReturnedToISM(int32 LastISID);

	/** Blueprint event fired whenever this ASM actor's prop state changes (AtRest, Unsettled, Settling, Held, InMotion) */
	UFUNCTION(BlueprintImplementableEvent, Category = "ASM Lifecycle", meta = (DisplayName = "On Prop State Changed"))
	void BP_OnPropStateChanged(EPropState NewPropState, EPropState PreviousPropState);

	/** Broadcast whenever this ASM actor's state changes (AtRest, Unsettled, Settling, Held, InMotion) */
	UPROPERTY(BlueprintAssignable, Category = "ASM Lifecycle|Events")
	FOnActivePropStateChanged OnActivePropStateChanged;

	/** Broadcast when this ASM actor is placed at the ISM world transform and becomes active */
	UPROPERTY(BlueprintAssignable, Category = "ASM Lifecycle|Events")
	FOnActivePropBecomeActive OnPropBecomeActive;

	/** Broadcast right BEFORE this ASM actor returns to its rest/pool position and converts back to an ISM */
	UPROPERTY(BlueprintAssignable, Category = "ASM Lifecycle|Events")
	FOnActivePropBeforeReturnToRest OnBeforeReturnToRestPosition;

	// ---------------------------------------------------------
	// New Lifecycle Virtual Methods (Category 1 & 2 Architecture)
	// ---------------------------------------------------------
	/**
	 * Called once during InitializeFromISIDRecord after the product definition and content definition
	 * are cached. Override to consume Category 1 shared data (mesh, materials, profiles, etc.).
	 * NOT a UFUNCTION — pure C++ virtual to allow full struct reference passing.
	 */
	virtual void OnApplySharedDefinitions(const FPropProductDefinition* ProductDef, const FPropContentDefinition* ContentDef);

	/**
	 * Called when continuous runtime data (temperature, lastUpdatedTime) is applied or refreshed.
	 * Also receives the current Strength to allow mesh/material updates in a single call.
	 * Override in subclasses to react to thermal changes, GPU slot updates, etc.
	 */
	UFUNCTION(BlueprintCallable, Category = "Unique Data|Continuous")
	virtual void OnApplyContinuousData(const FPropContinuousUpdateData& InContinuousData, float NewStrength = 1.0f, bool bStrengthChanged = false);

	/**
	 * Called when discrete event data (contentLevel, capSealed, capType, strength, contentID, etc.) is applied.
	 * DirtyMask is a bitmask of EPropEventDirtyMask flags indicating which fields changed.
	 * Only rejects out-of-order external packets (never self-rejects when &InEventData == &LocalUniqueData.EventData).
	 */
	UFUNCTION(BlueprintCallable, Category = "Unique Data|Event")
	virtual void OnApplyEventData(const FPropEventUpdateData& InEventData, uint8 DirtyMask = 0xFF);

	/** Resolve an authored placed instance's initial quantity before its first canonical registration. */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Unique Data|Initialization")
	float GetInitialContentLevel(float DefinitionDefault) const;
	virtual float GetInitialContentLevel_Implementation(float DefinitionDefault) const;

	/** C++ non-UFUNCTION inline overload accepting EPropEventDirtyMask enum for type-safe call sites */
	void OnApplyEventData(const FPropEventUpdateData& InEventData, EPropEventDirtyMask DirtyMask)
	{
		OnApplyEventData(InEventData, static_cast<uint8>(DirtyMask));
	}

	/**
	 * Submits a reliable event update for this instance to PropManager and broadcasts it to clients.
	 * Marks LocalUniqueData.EventData as dirty for the specified fields (DirtyMask).
	 * Increments EventSequence before sending.
	 */
	UFUNCTION(BlueprintCallable, Category = "Unique Data|Event")
	virtual void EmitPropEventUpdate(uint8 DirtyMask = 0xFF, uint8 ExtendedDirtyMask = 0);

	/** C++ non-UFUNCTION inline overload accepting EPropEventDirtyMask enum */
	void EmitPropEventUpdate(EPropEventDirtyMask DirtyMask)
	{
		EmitPropEventUpdate(static_cast<uint8>(DirtyMask));
	}

	/**
	 * Returns true when this ASM is eligible for demotion back to ISM.
	 * Base implementation: returns true when State == AtRest and not interpolating.
	 * Override to add subclass-specific demotion guards (e.g. active thermal sources, foam active, etc.).
	 */
	UFUNCTION(BlueprintCallable, Category = "ASM Lifecycle")
	virtual bool CanDemoteToISM() const;

	/**
	 * Returns true when this ASM currently has active thermal sources (e.g. nearby heat/cold sources)
	 * that should prevent demotion to ISM. Base returns false.
	 */
	UFUNCTION(BlueprintCallable, Category = "ASM Lifecycle")
	virtual bool HasActiveThermalSources() const;

	/**
	 * Called by PropManager immediately after ISM->ASM promotion to apply an optional initial physics impulse
	 * (e.g. from a hit that triggered the promotion). Server-only.
	 * @param ImpulseLocation  World-space location of the applied impulse.
	 * @param ImpulseDirection Normalized direction (or velocity vector) of the impulse.
	 * @param InstigatorActor  The actor responsible for the interaction event (may be nullptr).
	 * @param InteractionEventID Unique interaction event ID allocated by PropManager (0 = no dedup).
	 */
	virtual void ApplyInitialImpactFromPromotion(FVector ImpulseLocation, const FVector& ImpulseDirection, AActor* InstigatorActor, int32 InteractionEventID = 0);

	/**
	 * Called by the pool manager immediately before this ASM unit is deactivated and returned to the pool.
	 * Override to flush/cancel any timers, clear audio, stop VFX, etc.
	 * Always call Super::OnBeforePoolDeactivate() first.
	 */
	virtual void OnBeforePoolDeactivate();

	/**
	 * Called by the pool manager after this ASM unit is reactivated to clear any leftover transient state
	 * from a previous ISID occupancy (e.g. morph targets, cached material overrides, foam, foam VFX).
	 * Always call Super::ResetTransientPoolState() first.
	 */
	virtual void ResetTransientPoolState();

	// ---------------------------------------------------------
	// Data Asset Configuration
	// ---------------------------------------------------------
	/** Primary Data Asset defining this prop's mesh, materials, temperature, portions, expiry, taste profile, and stimuli */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Data", meta = (ExposeOnSpawn = "true", DisplayName = "Interactable Prop Data Asset"))
	TObjectPtr<UInteractablePropDataAsset> PropDataAsset;

	/** Sets the Data Asset and immediately configures the static mesh, materials, and all properties */
	UFUNCTION(BlueprintCallable, Category = "Prop Data", meta = (DisplayName = "Set Prop Data Asset"))
	void SetPropDataAsset(UInteractablePropDataAsset* InDataAsset);

	/** Applies all properties, mesh, materials, portions, temperature, stimuli, and taste profile from the given Data Asset (falls back to PropDataAsset if InDataAsset is null) */
	UFUNCTION(BlueprintCallable, Category = "Prop Data", meta = (DisplayName = "Apply Data Asset"))
	void ApplyDataAsset(const UInteractablePropDataAsset* InDataAsset = nullptr);

	/** Blueprint implementable event fired after Data Asset settings and mesh are applied in Construction Script or runtime */
	UFUNCTION(BlueprintImplementableEvent, Category = "Prop Data", meta = (DisplayName = "On Data Asset Applied"))
	void BP_OnDataAssetApplied(const UInteractablePropDataAsset* AppliedDataAsset);

	/** Applies shared settings from an FPropProductDefinition, caches it in CachedFallbackProductDef, and calls OnApplySharedDefinitions */
	UFUNCTION(BlueprintCallable, Category = "Prop Data")
	void ApplyProductDefinition(const FPropProductDefinition& InProductDef);

	/**
	 * Changes this active prop's product definition at runtime (e.g. Empty Bottle -> Full Cola -> Empty Bottle).
	 * Updates the mesh (swapping to SkeletalMesh or StaticMesh as defined by the new product),
	 * updates materials, updates PropManager's registry, and releases the old ISM slot to the pool.
	 * When placed down, it will demote to ISM as the new product.
	 * Calls ChangeProductWithMode(NewProductID, bResetRuntimeData ? EPropProductResetMode::FullReset : EPropProductResetMode::PreserveUniqueState).
	 *
	 * @param NewProductID The target ProductID to change to.
	 * @param bResetRuntimeData If true, resets portions and temperature to the new product's defaults.
	 * @return True if product changed successfully.
	 */
	UFUNCTION(BlueprintCallable, Category = "ASM Product", meta = (DisplayName = "Change Product"))
	bool ChangeProduct(int32 NewProductID, bool bResetRuntimeData = true);

	/**
	 * Changes this active prop's product definition with explicit reset mode control.
	 * This is the canonical product-change path; ChangeProduct() delegates here.
	 * @param NewProductID   The target ProductID to change to.
	 * @param ResetMode      Controls how unique/runtime data is preserved or reset.
	 * @return True if product changed successfully.
	 */
	bool ChangeProductWithMode(int32 NewProductID, EPropProductResetMode ResetMode);

	/**
	 * Changes this active prop's product to match a given Data Asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "ASM Product", meta = (DisplayName = "Change Product From Data Asset"))
	bool ChangeProductFromDataAsset(UInteractablePropDataAsset* NewDataAsset, bool bResetRuntimeData = true);

	/**
	 * Updates Per-Instance / Custom Primitive Data on this actor's mesh components:
	 *   Slot 0: ContentLevel       — fraction [0..1+] (unclamped to allow overfill visuals)
	 *   Slot 1: ExpiryAge          — normalized age [0..1] (0=fresh, 1=fully expired)
	 *   Slot 2: Temperature        — raw Celsius (drives frost/condensation/heat material effects)
	 *   Slot 3: StrengthRatio      — structural integrity [0..1] (drives crack/deformation intensity)
	 */
	UFUNCTION(BlueprintCallable, Category = "ASM Visual")
	void UpdateMaterialCustomPrimitiveData();

	// ---------------------------------------------------------
	// Components
	// ---------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> PropMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTextRenderComponent> DebugText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshPhysicsSimulationComponent> PhysicsSimComponent;

	// ---------------------------------------------------------
	// Interaction & Grabbability
	// ---------------------------------------------------------
	/** Whether this prop is currently grabbable by player or NPC (Instance editable & exposed on spawn) */
	UPROPERTY(ReplicatedUsing = OnRep_IsGrabbable, EditAnywhere, BlueprintReadWrite, meta = (ExposeOnSpawn = "true", DisplayName = "Is Grabable"), Category = "Interaction")
	bool bIsGrabbable = true;

	UFUNCTION()
	void OnRep_IsGrabbable();

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void SetGrabbable(bool bNewGrabbable);

	/** Retriggers/resets the unsettled timer back to 3.0s (or custom delay) */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void RetriggerUnsettled(float NewDelay = -1.0f);

	/** Transitions this active prop into Unsettled state (physics simulating, countdown to settling) */
	UFUNCTION(BlueprintCallable, Category = "Interaction|State", meta = (DisplayName = "Set Prop To Unsettled"))
	void SetPropToUnsettled(float UnsettledDelay = -1.0f);

	/** Transitions this active prop into the Settling state (e.g. when custom InMotion flight ends or after throwing) */
	UFUNCTION(BlueprintCallable, Category = "Interaction|State", meta = (DisplayName = "Set Prop To Settling"))
	void SetPropToSettling(float VelocityTolerance = -1.0f);

	/** Transitions this active prop into InMotion state with an optional physics impulse (e.g. rocket launch, thrown object) */
	UFUNCTION(BlueprintCallable, Category = "Interaction|State", meta = (DisplayName = "Set Prop To InMotion"))
	void SetPropToInMotion(FVector LaunchImpulse = FVector::ZeroVector);

	// ---------------------------------------------------------
	// Player Interaction Pathway
	// ---------------------------------------------------------
	/** Base interaction triggered by a Player Character / Controller */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction|Player")
	bool InteractWithPlayer(APawn* PlayerPawn);
	virtual bool InteractWithPlayer_Implementation(APawn* PlayerPawn);

	/** Triggered when a player grabs this prop into hands/socket */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction|Player")
	bool OnPlayerGrab(USceneComponent* AttachToParent, FName SocketName = NAME_None);
	virtual bool OnPlayerGrab_Implementation(USceneComponent* AttachToParent, FName SocketName = NAME_None);

	/** Triggered when a player drops or throws this prop */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction|Player")
	bool OnPlayerRelease(FVector LaunchVelocity = FVector::ZeroVector);
	virtual bool OnPlayerRelease_Implementation(FVector LaunchVelocity = FVector::ZeroVector);

	/** Event fired when a player consumes a portion of this prop */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction|Player")
	void OnConsumedByPlayer(APawn* PlayerPawn, float BodyTempDelta, float ItemTemp, bool bWasExpired);
	virtual void OnConsumedByPlayer_Implementation(APawn* PlayerPawn, float BodyTempDelta, float ItemTemp, bool bWasExpired);

	// ---------------------------------------------------------
	// NPC Interaction Pathway
	// ---------------------------------------------------------
	/** Base interaction triggered by an NPC character */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction|NPC")
	bool InteractWithNPC(ANPCCharacter* NPCCharacter, FName ActionName = NAME_None);
	virtual bool InteractWithNPC_Implementation(ANPCCharacter* NPCCharacter, FName ActionName = NAME_None);

	// ---------------------------------------------------------
	// Serialized Blueprint compatibility views. Authoritative unique state lives in LocalUniqueData.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Identity")
	FText ItemDisplayName = FText::FromString(TEXT("Cold Drink Can"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Identity")
	EConsumablePropType ConsumableType = EConsumablePropType::Drink;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Economy")
	float Price = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Temperature", meta = (DisplayName = "Enable Temperature Update"))
	bool bEnableTemperatureUpdate = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Temperature", meta = (Units = "Celsius"))
	float ConsumableTemperature = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Temperature", meta = (Units = "Celsius"))
	float CurrentTemperature = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Temperature")
	float TemperatureDopamineBonus = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Temperature")
	float ThermalExchangeRate = 0.015f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Temperature", meta = (Units = "Celsius"))
	float AmbientRoomTemperature = 21.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Physical", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Coldness = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Freshness & Expiry")
	bool bHasExpiry = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Freshness & Expiry", meta = (EditCondition = "bHasExpiry", ClampMin = "0.5"))
	float ExpiryLifetimeMinutes = 60.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Consumable|Freshness & Expiry")
	float AgeMinutes = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Freshness & Expiry", meta = (EditCondition = "bHasExpiry"))
	float SicknessLevel = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Freshness & Expiry", meta = (EditCondition = "bHasExpiry"))
	float ExpiredDopaminePenalty = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Freshness & Expiry", meta = (EditCondition = "bHasExpiry", DisplayName = "Expiry Stimulus Profile"))
	TArray<FNPCSimulationStatModifier> ExpiryStimulusProfile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Freshness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExpiryFreshness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Freshness")
	float ExpiryRatePerMinute = 0.02f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Consumable|Quantity", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float QuantityLevel = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Quantity", meta = (DisplayName = "Num of Portions", ClampMin = "0"))
	int32 NumOfPortions = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Quantity", meta = (ClampMin = "0"))
	int32 RemainingPortions = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Taste")
	FNPCTasteVector TasteProfile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Stimuli")
	TArray<FNPCSimulationStatModifier> StimuliPerPortion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable")
	bool bDestroyWhenEmpty = true;


	void InitializeDefaultDrinkStimuli();
	UFUNCTION() void OnRep_QuantityLevel();
	UFUNCTION() void OnRep_RemainingPortions();

	// Shared definition queries
	// ---------------------------------------------------------
	/** Returns the display name from the product definition */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	FText GetDisplayName() const;

	/** Returns the consumable type from the product definition */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	EConsumablePropType GetConsumableType() const;

	/** Returns the purchase price from the product definition */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	float GetPrice() const;

	/** Returns true if temperature simulation is enabled for this product */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	bool GetEnableTemperatureUpdate() const;

	/** Returns the ideal serving temperature in Celsius */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	float GetConsumableTemperature() const;

	/** Returns the temperature dopamine bonus for this product */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	float GetTemperatureDopamineBonus() const;

	/** Returns the thermal exchange rate (Newton's law of cooling coefficient) */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	float GetThermalExchangeRate() const;

	/** Returns true if this product has an expiry profile */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	bool GetHasExpiry() const;

	/** Returns the expiry lifetime in minutes */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	float GetExpiryLifetimeMinutes() const;

	/** Returns the sickness level applied when consuming expired product */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	float GetSicknessLevel() const;

	/** Returns the dopamine penalty applied when consuming expired product */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	float GetExpiredDopaminePenalty() const;

	/** Returns the expiry stimulus profile (stimuli applied when consuming expired product) */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	const TArray<FNPCSimulationStatModifier>& GetExpiryStimulusProfile() const;

	/** Returns the total max number of portions for this product */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	int32 GetNumPortions() const;

	/** Returns the taste profile for this product */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	const FNPCTasteVector& GetTasteProfile() const;

	/** Returns the stimulus modifiers applied per portion */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	const TArray<FNPCSimulationStatModifier>& GetStimuliPerPortion() const;

	/** Returns true if this product is destroyed when completely consumed */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	bool GetDestroyWhenEmpty() const;

	/** Returns the maximum structural strength for this product */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	float GetMaxStrength() const;

	/** Returns the product ID to convert to when this bottle becomes empty (0 = no conversion) */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	int32 GetEmptyBottleProductID() const;

	/** Returns the max overfill level (e.g. 1.7 = 170% of normal capacity) */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	float GetMaxOverfillLevel() const;

	/** Returns the thermal conductivity value for this product's bottle shell */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	float GetThermalConductivity() const;
	float GetThermalMass() const;

	/** Returns the normal (100%) fill capacity for this bottle */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	float GetNormalCapacity() const;

	/** Returns the breakage/failure strength threshold (strength at or below this causes breakage) */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	float GetBreakageFailureThreshold() const;

	/** Returns the bottle material type (Glass, Plastic, etc.) */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	EBottleMaterialType GetBottleMaterialType() const;

	/** Returns true if Category 2 unique data is enabled for this product */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	bool GetEnableInstanceUniqueData() const;

	/** Returns true if continuous update data ticks (temperature, thermal strength drain) are enabled */
	UFUNCTION(BlueprintPure, Category = "Prop Data|Category1")
	bool GetEnableContinuousUpdateData() const;

	// ---------------------------------------------------------
	// Zero-Copy Category 2 Getters (read LocalUniqueData)
	// ---------------------------------------------------------
	/** Returns the current content level [0..1+] from LocalUniqueData.EventData */
	UFUNCTION(BlueprintPure, Category = "Unique Data|Category2")
	float GetContentLevel() const;

	/** Returns remaining portions calculated from ContentLevel and NumPortions */
	UFUNCTION(BlueprintPure, Category = "Unique Data|Category2")
	int32 GetRemainingPortions() const;

	/** Returns the current structural strength from LocalUniqueData.EventData */
	UFUNCTION(BlueprintPure, Category = "Unique Data|Category2")
	float GetStrength() const;

	/** Returns the structural strength ratio [0..1] (Strength / MaxStrength) */
	UFUNCTION(BlueprintPure, Category = "Unique Data|Category2")
	float GetStrengthRatio() const;

	/** Returns the cap sealed fraction [0..1] from LocalUniqueData.EventData */
	UFUNCTION(BlueprintPure, Category = "Unique Data|Category2")
	float GetCapSealed() const;

	/** Returns the cap type byte from LocalUniqueData.EventData */
	UFUNCTION(BlueprintPure, Category = "Unique Data|Category2")
	uint8 GetCapType() const;

	/** Returns the content ID from LocalUniqueData.EventData */
	UFUNCTION(BlueprintPure, Category = "Unique Data|Category2")
	int32 GetContentID() const;

	/** Returns the creation time (WorldElapsedSeconds) from LocalUniqueData.EventData */
	UFUNCTION(BlueprintPure, Category = "Unique Data|Category2")
	double GetCreationTime() const;

	UFUNCTION(BlueprintPure, Category = "Unique Data|Category2")
	double GetContentCreationTime() const;

	/** Returns the current temperature in Celsius from LocalUniqueData.ContinuousData */
	UFUNCTION(BlueprintPure, Category = "Unique Data|Category2")
	float GetCurrentTemperature() const;

	/** Returns the last continuous update timestamp (WorldElapsedSeconds) from LocalUniqueData.ContinuousData */
	UFUNCTION(BlueprintPure, Category = "Unique Data|Category2")
	double GetLastUpdatedTime() const;

	/**
	 * Returns the normalized age [0..1] derived from CreationTime, current WorldElapsedSeconds, and ExpiryLifetimeMinutes.
	 * 0.0 = just created, 1.0 = fully expired.
	 */
	UFUNCTION(BlueprintPure, Category = "Unique Data|Category2")
	float GetExpiryAge() const;

	/**
	 * Returns the expiry freshness [1.0 = Fresh, 0.0 = Expired].
	 * Inverse of GetExpiryAge(): 1.0f - GetExpiryAge().
	 */
	UFUNCTION(BlueprintPure, Category = "Unique Data|Category2")
	float GetExpiryFreshness() const;

	/**
	 * Returns the age of this instance in minutes since creation.
	 * Derived from CreationTime and current WorldElapsedSeconds.
	 */
	UFUNCTION(BlueprintPure, Category = "Unique Data|Category2")
	float GetAgeMinutes() const;

	/** Returns true if this prop is currently grabbable (from LocalUniqueData.EventData.bIsGrabbable) */
	UFUNCTION(BlueprintPure, Category = "Unique Data|Category2")
	bool GetIsGrabbableFromUniqueData() const;

	// ---------------------------------------------------------
	// Legacy Runtime Queries (delegate to new getters)
	// ---------------------------------------------------------
	/** Reads packed runtime defaults from PropManager / ISMComponent authoritative record */
	UFUNCTION(BlueprintPure, Category = "Consumable|Runtime Data")
	bool GetRuntimeDefaults(FPropPackedRuntimeData& OutData) const;

	/** Gets current temperature from authoritative runtime data. Delegates to GetCurrentTemperature(). */
	UFUNCTION(BlueprintPure, Category = "Consumable|Runtime Data")
	float GetAuthoritativeTemperature() const;

	/** Gets remaining portions from authoritative runtime data. Delegates to GetRemainingPortions(). */
	UFUNCTION(BlueprintPure, Category = "Consumable|Runtime Data")
	int32 GetAuthoritativeRemainingPortions() const;

	// ---------------------------------------------------------
	// Consumable State Queries (delegate to new getters)
	// ---------------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "Consumable")
	bool IsEmpty() const;

	UFUNCTION(BlueprintPure, Category = "Consumable")
	bool IsExpired() const;

	UFUNCTION(BlueprintPure, Category = "Consumable")
	bool IsSpoiled() const;

	UFUNCTION(BlueprintPure, Category = "Consumable")
	bool IsAtIdealTemperature(float Tolerance = 2.0f) const;

	// ---------------------------------------------------------
	// Consumable Logic & Networking
	// ---------------------------------------------------------
	/** Called by PropManager during distributed round-robin frame loop to update temperature */
	UFUNCTION(BlueprintCallable, Category = "Consumable|Temperature")
	void UpdateTemperature(float AmbientTemp, float DeltaSeconds);

	/** Returns the current time in world-minutes (used for legacy expiry/age checks) */
	UFUNCTION(BlueprintPure, Category = "Consumable|Runtime Data")
	float GetCurrentWorldMinutes() const;

	UFUNCTION(BlueprintCallable, Category = "Consumable")
	bool ConsumePortion(AActor* ConsumerActor, float PortionRatio = 1.0f);

	UFUNCTION(Server, Reliable, WithValidation, Category = "Consumable|Network")
	void Server_ConsumePortion(AActor* ConsumerActor, float PortionRatio = 1.0f);

	UFUNCTION(NetMulticast, Unreliable, Category = "Consumable|Network")
	void Multicast_OnPortionConsumed(AActor* ConsumerActor, int32 PortionsLeft);

	// ---------------------------------------------------------
	// IWorldAffordanceInterface
	// ---------------------------------------------------------
	virtual bool CanInteract_Implementation(AActor* InstigatorActor) override;
	virtual void QueryAffordanceOptions_Implementation(AActor* InstigatorActor, TArray<FAffordanceOption>& OutOptions) override;
	virtual bool ExecuteAffordanceOption_Implementation(FName OptionId, AActor* InstigatorActor) override;

	void UpdateDebugBillboard();

protected:
	UFUNCTION()
	void HandlePhysicsSimStateChanged(EPhysicsSimulationState NewSimState, EPhysicsSimulationState PrevSimState);

private:
	bool bHasNotifiedBeforeReturnToRest = false;
};
