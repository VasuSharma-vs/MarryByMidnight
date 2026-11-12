#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PropGrabInputComponent.generated.h"

class UPrimitiveComponent;

UENUM(BlueprintType)
enum class EPropGrabInputPhase : uint8 { Started, Ongoing, Triggered, Completed, Canceled };

UENUM(BlueprintType)
enum class EPropGrabIntent : uint8 { None, Grab, Drop, Place };

/** Local gesture recognition only. Actual interaction still uses the authoritative Prop Manager. */
struct MARRYBYMIDNIGHT_API FPropGrabGesture
{
	EPropGrabIntent Process(EPropGrabInputPhase Phase, double Now, uint64 HeldToken, double HoldSeconds, double DoubleTapSeconds);
private:
	bool bActive = false;
	bool bPerformed = false;
	double StartedAt = 0;
	uint64 StartedToken = 0;
	double LastTapAt = -1;
	uint64 LastTapToken = 0;
};

/** Shared G timing for actor/component input adapters; no Tick, inventory or physics implementation. */
UCLASS(ClassGroup = (Interaction), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UPropGrabInputComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	UPropGrabInputComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab", meta = (ClampMin = "0.01")) float HoldSeconds = .4f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab", meta = (ClampMin = "0.01")) float DoubleTapSeconds = .4f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Grab") EPropGrabIntent LastIntent = EPropGrabIntent::None;
	/** Triggered/Completed/Canceled may all arrive on release; only the first ending consumes a gesture. */
	UFUNCTION(BlueprintCallable, Category = "Grab")
	void ProcessGrabInput(EPropGrabInputPhase Phase, AActor* HeldActor, UPrimitiveComponent* HeldComponent);
	/** Guards used by the existing owned-character RPC adapters before changing carry references. */
	UFUNCTION(BlueprintPure, Category = "Grab") bool CanGrabTarget(AActor* Target) const;
	UFUNCTION(BlueprintPure, Category = "Grab") bool OwnsHeldTarget(AActor* Target) const;
private:
	FPropGrabGesture Gesture;
};
