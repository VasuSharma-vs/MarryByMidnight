#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCRagdollComponent.generated.h"

class USkeletalMeshComponent;

/**
 * Physics ragdoll simulation states.
 */
UENUM(BlueprintType)
enum class ERagdollState : uint8
{
	AtRest        UMETA(DisplayName = "At Rest"),
	Keep          UMETA(DisplayName = "Keep Simulating"),
	Settling      UMETA(DisplayName = "Settling"),
	Unsettled     UMETA(DisplayName = "Unsettled"),
	Animation     UMETA(DisplayName = "Animation")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRagdollStateChanged, ERagdollState, PreviousState, ERagdollState, NewState);

/**
 * Dedicated Physics Ragdoll Component for managing SkeletalMesh ragdoll physics,
 * bone settling detection in async worker threads, hit responses, and animated get-up transitions.
 */
UCLASS(ClassGroup = (Physics), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UNPCRagdollComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCRagdollComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	/** Current active ragdoll state */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentState, VisibleAnywhere, BlueprintReadOnly, Category = "Ragdoll")
	ERagdollState CurrentState = ERagdollState::Animation;

	UFUNCTION()
	void OnRep_CurrentState(ERagdollState PreviousState);

	/** Target skeletal mesh being driven by ragdoll physics */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll")
	TWeakObjectPtr<USkeletalMeshComponent> TargetMesh;

	/** Pelvis/root bone name used for repositioning actor when restoring animation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll")
	FName PelvisBoneName = FName("pelvis");

	/** Linear velocity threshold (in cm/s) for considering a bone at rest during Settling */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll|Parameters")
	float SettlingVelocityThreshold = 5.0f;

	/** Duration (in seconds) in Unsettled state before transitioning to Settling */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll|Parameters")
	float UnsettledDelay = 1.5f;

	/** Time interval (in seconds) between async bone velocity checks while Settling */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll|Parameters")
	float SettlingCheckInterval = 0.1f;

	/** Whether a physical component hit automatically sets the state to Unsettled */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll|Parameters")
	bool bHitTriggersUnsettled = true;

	/** If false (default), hits while in Animation state will NOT trigger ragdoll, preventing accidental collapses during normal walking */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll|Parameters")
	bool bHitTriggersFromAnimation = false;

	/** Minimum normal impulse required from a collision hit to trigger Unsettled */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll|Parameters")
	float MinHitImpulseThreshold = 50.0f;

	/** Cached relative transform of target skeletal mesh prior to ragdoll physics */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ragdoll|Transform")
	FVector SavedOriginalRelativeLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ragdoll|Transform")
	FRotator SavedOriginalRelativeRotation = FRotator::ZeroRotator;

	/** Event fired when the ragdoll state transitions */
	UPROPERTY(BlueprintAssignable, Category = "Ragdoll|Events")
	FOnRagdollStateChanged OnRagdollStateChanged;

	/** Sets target skeletal mesh to control */
	UFUNCTION(BlueprintCallable, Category = "Ragdoll")
	void SetTargetSkeletalMesh(USkeletalMeshComponent* InMesh);

	/** Transitions the ragdoll into a new state */
	UFUNCTION(BlueprintCallable, Category = "Ragdoll")
	void SetRagdollState(ERagdollState NewState);

	/** Returns current ragdoll state */
	UFUNCTION(BlueprintPure, Category = "Ragdoll")
	ERagdollState GetRagdollState() const { return CurrentState; }

	/** Saves current relative transform of target mesh as the baseline animation transform */
	UFUNCTION(BlueprintCallable, Category = "Ragdoll")
	void SaveOriginalTransform();

	/** Restores target skeletal mesh back to its saved baseline transform */
	UFUNCTION(BlueprintCallable, Category = "Ragdoll")
	void RestoreOriginalTransform();

	/** Convenience method to trigger ragdoll collapse (sets state to Unsettled) */
	UFUNCTION(BlueprintCallable, Category = "Ragdoll")
	void StartRagdoll();

	/** Convenience method to restore locomotion and animation (sets state to Animation) */
	UFUNCTION(BlueprintCallable, Category = "Ragdoll")
	void StopRagdoll();

	/** Manually triggers hit reaction into Unsettled state (useful for damage events, bullet hits, punches) */
	UFUNCTION(BlueprintCallable, Category = "Ragdoll")
	void TriggerHitReaction();

	/** Convenience check if ragdoll is currently active in any physics state */
	UFUNCTION(BlueprintPure, Category = "Ragdoll")
	bool IsRagdollActive() const { return CurrentState != ERagdollState::Animation; }

protected:
	UFUNCTION()
	void HandleMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	void EnterState(ERagdollState NewState);
	void ExitState(ERagdollState OldState);

	void PerformAsyncSettlingCheck();

private:
	FTimerHandle UnsettledTimerHandle;
	FTimerHandle SettlingCheckTimerHandle;
	bool bIsAsyncCheckPending = false;
};
