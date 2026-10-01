#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StaticMeshPhysicsSimulationComponent.generated.h"

class UPrimitiveComponent;

/**
 * States for physics simulated mesh objects:
 * 1. At Rest: Physics off, default collision profile, grabbable = true, replicated.
 * 2. In Motion: Physics on, default collision profile, grabbable = true, replicated.
 * 3. Held: Physics off, IgnoreOnlyPawn collision, grabbable = false, not replicated.
 * 4. Settling: Physics on, monitors linear velocity until <= tolerance, grabbable = true.
 * 5. Unsettled: Physics on, default collision profile, grabbable = true, replicated, transitions to Settling after delay.
 */
UENUM(BlueprintType)
enum class EPhysicsSimulationState : uint8
{
	AtRest UMETA(DisplayName = "At Rest"),
	InMotion UMETA(DisplayName = "In Motion"),
	Held UMETA(DisplayName = "Held"),
	Settling UMETA(DisplayName = "Settling"),
	Unsettled UMETA(DisplayName = "Unsettled")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPhysicsSimulationStateChanged, EPhysicsSimulationState, NewState, EPhysicsSimulationState, PrevState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGrabbableStateChanged, bool, bIsGrabbable);

/**
 * Universal physics simulation state component modeled after POS_BaseMesh.
 * Manages transitions between At Rest, In Motion, Held, Settling, and Unsettled states.
 */
UCLASS(ClassGroup = (Physics), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UStaticMeshPhysicsSimulationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStaticMeshPhysicsSimulationComponent();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	/** Target mesh or primitive component being simulated (auto-resolves from owner if null) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Simulation")
	TObjectPtr<UPrimitiveComponent> TargetMesh;

	/** Stored default collision profile captured at BeginPlay */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Physics Simulation")
	FName DefaultCollisionProfile;

	/** Initial state on BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ExposeOnSpawn = "true"), Category = "Physics Simulation")
	EPhysicsSimulationState InitialState = EPhysicsSimulationState::AtRest;

	/** Current active simulation state (Replicated) */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentState, VisibleAnywhere, BlueprintReadOnly, Category = "Physics Simulation")
	EPhysicsSimulationState CurrentState = EPhysicsSimulationState::AtRest;

	UFUNCTION()
	void OnRep_CurrentState();

	/** Internal replicated grabbable state (hidden from Details panel to avoid duplicating Actor's Is Grabable) */
	UPROPERTY(ReplicatedUsing = OnRep_IsGrabbable)
	bool bIsGrabbable = false;

	UFUNCTION()
	void OnRep_IsGrabbable();

	/** Attempts to find the grabbable boolean property on the owning Actor */
	FBoolProperty* FindActorGrabbableProperty() const;

	/** Reads the grabbable value from the owning Actor if available */
	UFUNCTION(BlueprintPure, Category = "Physics Simulation")
	bool GetActorGrabbableValue(bool& bOutHasProperty) const;

	/** Updates the grabbable value on the owning Actor if available; skips if not available */
	UFUNCTION(BlueprintCallable, Category = "Physics Simulation")
	void SetActorGrabbableValue(bool bNewGrabbable);

	/** Default delay in seconds before transitioning from Unsettled to Settling (default: 3.0s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ExposeOnSpawn = "true", DisplayName = "Default Unsettled Delay"), Category = "Physics Simulation")
	float DefaultUnsettledDelay = 3.0f;

	/** Velocity tolerance threshold (cm/s). When linear velocity falls below this in Settling, transitions to At Rest */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ExposeOnSpawn = "true", DisplayName = "Velocity Tolerance"), Category = "Physics Simulation")
	float VelocityTolerance = 10.0f;

	/** Whether to use the central Prop Manager / ISM settling queue instead of individual per-component tick */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Check Instanced / Manager Settling"), Category = "Physics Simulation")
	bool bCheckInstancedStaticMeshes = true;

	/** Whether getting hit by another object / pawn automatically knocks this prop into Unsettled state */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Simulation")
	bool bUnsettleOnHit = true;

	/** Minimum hit impulse magnitude to trigger Unsettled state on collision */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Simulation", meta = (ClampMin = "0.0"))
	float HitImpulseThreshold = 10.0f;

	/** Broadcast when the simulation state transitions */
	UPROPERTY(BlueprintAssignable, Category = "Physics Simulation")
	FOnPhysicsSimulationStateChanged OnPhysicsSimulationStateChanged;

	/** Broadcast when grabbability changes */
	UPROPERTY(BlueprintAssignable, Category = "Physics Simulation")
	FOnGrabbableStateChanged OnGrabbableStateChanged;

	// -----------------------------------------------------------------
	// State Machine Interface
	// -----------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Physics Simulation")
	void SetPhysicsState(EPhysicsSimulationState NewState, float CustomDelay = -1.0f, float CustomTolerance = -1.0f);

	UFUNCTION(BlueprintPure, Category = "Physics Simulation")
	EPhysicsSimulationState GetPhysicsState() const { return CurrentState; }

	UFUNCTION(BlueprintCallable, Category = "Physics Simulation")
	void SetStateAtRest();

	UFUNCTION(BlueprintCallable, Category = "Physics Simulation")
	void SetStateInMotion();

	UFUNCTION(BlueprintCallable, Category = "Physics Simulation")
	void SetStateHeld();

	UFUNCTION(BlueprintCallable, Category = "Physics Simulation")
	void SetStateSettling(float InToleranceLevel = -1.0f);

	UFUNCTION(BlueprintCallable, Category = "Physics Simulation")
	void SetStateUnsettled(float UnsettledDelay = -1.0f);

	/** Retriggers/resets the unsettled countdown timer back to full delay (e.g. resets to 3.0s if re-triggered after 2s) */
	UFUNCTION(BlueprintCallable, Category = "Physics Simulation")
	void RetriggerUnsettledTimer(float NewDelay = -1.0f);

	/** Returns remaining time in seconds on the Unsettled countdown timer before transitioning to Settling */
	UFUNCTION(BlueprintPure, Category = "Physics Simulation")
	float GetRemainingUnsettledTime() const;

	/** Returns true if the Unsettled countdown timer is actively ticking */
	UFUNCTION(BlueprintPure, Category = "Physics Simulation")
	bool IsUnsettledTimerActive() const;

	UFUNCTION(BlueprintPure, Category = "Physics Simulation")
	bool IsGrabbable() const;

	/** Returns current linear speed of target mesh in cm/s */
	UFUNCTION(BlueprintPure, Category = "Physics Simulation")
	float GetLinearSpeed() const;

	UFUNCTION(Server, Reliable, WithValidation, Category = "Physics Simulation|Network")
	void Server_SetPhysicsState(EPhysicsSimulationState NewState, float CustomDelay = -1.0f, float CustomTolerance = -1.0f);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnTargetMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	FTimerHandle UnsettledTimerHandle;
	float ActiveVelocityTolerance = 10.0f;

	void ApplyState(EPhysicsSimulationState NewState, float CustomDelay = -1.0f, float CustomTolerance = -1.0f);
	void SetGrabbableInternal(bool bNewGrabbable);
	void TransitionFromUnsettledToSettling();
	void EnsureTargetMeshResolved();
};
