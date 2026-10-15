#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NPCEnumsAndTypes.h"
#include "Props/InteractablePropDatabase.h"
#include "Interfaces/WorldAffordanceInterface.h"
#include "InteractableProp.generated.h"

class UStaticMeshComponent;
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
 */
UCLASS(BlueprintType, Blueprintable)
class MARRYBYMIDNIGHT_API AActivePropActor : public AActor, public IWorldAffordanceInterface
{
	GENERATED_BODY()

public:
	AActivePropActor();

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
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "ASM Identity")
	int32 ASMID = 0;

	/** Permanent logical prop identity (Rule 3: ISID is the permanent identity) */
	UPROPERTY(ReplicatedUsing = OnRep_ISID, EditAnywhere, BlueprintReadWrite, Category = "ASM Identity")
	int32 ISID = 0;

	UFUNCTION()
	void OnRep_ISID();

	/** Shared Product Definition Identifier (PID) */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "ASM Identity")
	int32 ProductID = 0;

	/** Authoritative physics & interaction state of this ASM (Held, InMotion, Unsettled, Settling, AtRest) */
	UPROPERTY(ReplicatedUsing = OnRep_State, EditAnywhere, BlueprintReadWrite, Category = "ASM State")
	EPropState State = EPropState::AtRest;

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

	/** Tracks whether network replication is enabled for this ASM (Section 30) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ASM State")
	bool bIsReplicated = true;

	/** Reference to the central Prop Manager orchestrating this ASM (Section 30) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ASM State")
	TObjectPtr<APropManager> PropManagerReference = nullptr;

	// ---------------------------------------------------------
	// ASM Promotion / Demotion / Pooling Lifecycle Helpers & BP Hooks
	// ---------------------------------------------------------
	/** Populates this ASM from an authoritative ISID record and Product Definition during ISM -> ASM promotion (Section 7.2) */
	void InitializeFromISIDRecord(const FISIDRuntimeIndex& Record, const FPropProductDefinition* ProductDef, APropManager* InManager);

	/** Reads current ASM transform, velocities, state, and runtime data back into the authoritative ISID record during ASM -> ISM demotion (Section 7.3) */
	void WriteToISIDRecord(FISIDRuntimeIndex& InOutRecord) const;

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

	/** Applies shared settings from an FPropProductDefinition */
	UFUNCTION(BlueprintCallable, Category = "Prop Data")
	void ApplyProductDefinition(const FPropProductDefinition& InProductDef);

	// ---------------------------------------------------------
	// Components
	// ---------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> PropMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTextRenderComponent> DebugText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshPhysicsSimulationComponent> PhysicsSimComponent;

	// ---------------------------------------------------------
	// Interaction & Grabbability
	// ---------------------------------------------------------
	/** Whether this prop is currently grabbable by player or NPC (Instance editable & exposed on spawn) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ExposeOnSpawn = "true", DisplayName = "Is Grabable"), Category = "Interaction")
	bool bIsGrabbable = true;

	UFUNCTION()
	void OnRep_IsGrabbable();

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void SetGrabbable(bool bNewGrabbable);

	/** Retriggers/resets the unsettled timer back to 3.0s (or custom delay) */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void RetriggerUnsettled(float NewDelay = -1.0f);

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
	// Consumable & Physical Properties
	// ---------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Identity")
	FText ItemDisplayName = FText::FromString(TEXT("Cold Drink Can"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Identity")
	EConsumablePropType ConsumableType = EConsumablePropType::Drink;

	/** Monetary purchase price or value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Economy")
	float Price = 2.0f;

	// ---------------------------------------------------------
	// Temperature Configuration & State
	// ---------------------------------------------------------
	/** Option to enable temperature simulation update. If false, prop only has ConsumableTemperature and does not adjust to environment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Temperature", meta = (DisplayName = "Enable Temperature Update"))
	bool bEnableTemperatureUpdate = false;

	/** Ideal serving temperature in Celsius (e.g. 4.0 for cold soda, 65.0 for hot coffee) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Temperature", meta = (Units = "Celsius"))
	float ConsumableTemperature = 4.0f;

	/** Current physical temperature of this prop in Celsius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Temperature", meta = (Units = "Celsius"))
	float CurrentTemperature = 4.0f;

	/** Called by PropManager during distributed round-robin frame loop to update temperature */
	UFUNCTION(BlueprintCallable, Category = "Consumable|Temperature")
	void UpdateTemperature(float AmbientTemp, float DeltaSeconds);

	/** Max dopamine bonus granted per portion when consumed at ideal temperature (decreases and goes negative as temperature diverges) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Temperature")
	float TemperatureDopamineBonus = 0.5f;

	/** Rate at which prop temperature normalizes towards ambient room temp (21°C) per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Temperature")
	float ThermalExchangeRate = 0.015f;

	/** Ambient room temperature in Celsius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Temperature", meta = (Units = "Celsius"))
	float AmbientRoomTemperature = 21.0f;

	/** Legacy coldness level [0.0 = Warm / Ambient, 1.0 = Ice Cold] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Physical", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Coldness = 0.85f;

	// ---------------------------------------------------------
	// Expiry & Freshness Configuration & State
	// ---------------------------------------------------------
	/** Whether this prop has an expiry date */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Freshness & Expiry")
	bool bHasExpiry = true;

	/** Total lifetime in minutes before item expires */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Freshness & Expiry", meta = (EditCondition = "bHasExpiry", ClampMin = "0.5"))
	float ExpiryLifetimeMinutes = 60.0f;

	/** Age of the item in minutes since spawned */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Consumable|Freshness & Expiry")
	float AgeMinutes = 0.0f;

	/** Sickness level added to consumer when expired */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Freshness & Expiry", meta = (EditCondition = "bHasExpiry"))
	float SicknessLevel = 35.0f;

	/** Dopamine subtracted when consumed expired (negative values represent depression) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Freshness & Expiry", meta = (EditCondition = "bHasExpiry"))
	float ExpiredDopaminePenalty = 25.0f;

	/** Stimuli modifiers applied per portion when consumed expired (enabled when bHasExpiry is true) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Freshness & Expiry", meta = (EditCondition = "bHasExpiry", DisplayName = "Expiry Stimulus Profile"))
	TArray<FNPCSimulationStatModifier> ExpiryStimulusProfile;

	/** Expiry freshness [1.0 = Fresh, 0.0 = Expired / Rotten] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Freshness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExpiryFreshness = 1.0f;

	/** Rate at which freshness decays per minute (legacy) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Freshness")
	float ExpiryRatePerMinute = 0.02f;

	/** Quantity level fraction [0.0 = Empty, 1.0 = Full] */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Consumable|Quantity", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float QuantityLevel = 1.0f;

	UFUNCTION()
	void OnRep_QuantityLevel();

	/** Total number of portions (sips or bites) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Quantity", meta = (DisplayName = "Num of Portions", ClampMin = "1"))
	int32 NumOfPortions = 4;

	/** Current remaining portions */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Quantity", meta = (ClampMin = "0"))
	int32 RemainingPortions = 4;

	UFUNCTION()
	void OnRep_RemainingPortions();

	/** Reads packed runtime defaults from PropManager / ISMComponent authoritative record */
	UFUNCTION(BlueprintPure, Category = "Consumable|Runtime Data")
	bool GetRuntimeDefaults(FPropPackedRuntimeData& OutData) const;

	/** Gets current temperature from authoritative runtime data */
	UFUNCTION(BlueprintPure, Category = "Consumable|Runtime Data")
	float GetAuthoritativeTemperature() const;

	/** Gets remaining portions from authoritative runtime data */
	UFUNCTION(BlueprintPure, Category = "Consumable|Runtime Data")
	int32 GetAuthoritativeRemainingPortions() const;

	/** Multi-dimensional taste profile */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Taste")
	FNPCTasteVector TasteProfile;

	/** Stimuli modifiers applied per portion consumed (Index stats are chooseable) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Stimuli")
	TArray<FNPCSimulationStatModifier> StimuliPerPortion;

	/** Whether this prop is automatically destroyed when completely consumed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable")
	bool bDestroyWhenEmpty = true;

	// ---------------------------------------------------------
	// Consumption Logic & Networking
	// ---------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Consumable")
	bool ConsumePortion(AActor* ConsumerActor, float PortionRatio = 1.0f);

	UFUNCTION(BlueprintPure, Category = "Consumable")
	bool IsEmpty() const { return RemainingPortions <= 0 || QuantityLevel <= 0.0f; }

	UFUNCTION(BlueprintPure, Category = "Consumable")
	bool IsExpired() const { return bHasExpiry && (AgeMinutes >= ExpiryLifetimeMinutes); }

	UFUNCTION(BlueprintPure, Category = "Consumable")
	bool IsSpoiled() const { return IsExpired() || ExpiryFreshness <= 0.25f; }

	UFUNCTION(BlueprintPure, Category = "Consumable")
	bool IsAtIdealTemperature(float Tolerance = 2.0f) const { return FMath::Abs(CurrentTemperature - ConsumableTemperature) <= Tolerance; }

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
	void InitializeDefaultDrinkStimuli();
	bool bHasNotifiedBeforeReturnToRest = false;
};

