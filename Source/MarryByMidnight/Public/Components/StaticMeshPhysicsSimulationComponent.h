#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StaticMeshPhysicsSimulationComponent.generated.h"

class UPrimitiveComponent;

/**
 * States for physics simulated mesh objects:
 * 1. At Rest: Physics off, default collision profile, not grabbable, replicated.
 * 2. In Motion: Physics on, default collision profile, not grabbable, replicated.
 * 3. Held: Physics off, IgnoreOnlyPawn collision, not grabbable, not replicated.
 * 4. Settling: Physics on, monitors linear velocity until <= tolerance, then sets state to At Rest.
 * 5. Unsettled: Physics on, default collision profile, grabbable, replicated, transitions to Settling after delay.
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

	/** Whether the object is currently grabbable in its active state (Replicated) */
	UPROPERTY(ReplicatedUsing = OnRep_IsGrabbable, VisibleAnywhere, BlueprintReadOnly, Category = "Physics Simulation")
	bool bIsGrabbable = false;

	UFUNCTION()
	void OnRep_IsGrabbable();

	/** Default delay in seconds before transitioning from Unsettled to Settling (default: 3.0s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ExposeOnSpawn = "true", DisplayName = "Default Unsettled Delay"), Category = "Physics Simulation")
	float DefaultUnsettledDelay = 3.0f;

	/** Velocity tolerance threshold (cm/s). When linear velocity falls below this in Settling, transitions to At Rest */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ExposeOnSpawn = "true", DisplayName = "Velocity Tolerance"), Category = "Physics Simulation")
	float VelocityTolerance = 10.0f;

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
	bool IsGrabbable() const { return bIsGrabbable; }

	UFUNCTION(Server, Reliable, WithValidation, Category = "Physics Simulation|Network")
	void Server_SetPhysicsState(EPhysicsSimulationState NewState, float CustomDelay = -1.0f, float CustomTolerance = -1.0f);

protected:
	FTimerHandle UnsettledTimerHandle;
	float ActiveVelocityTolerance = 10.0f;

	void ApplyState(EPhysicsSimulationState NewState, float CustomDelay = -1.0f, float CustomTolerance = -1.0f);
	void SetGrabbableInternal(bool bNewGrabbable);
	void TransitionFromUnsettledToSettling();
	void EnsureTargetMeshResolved();
};
