#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BottleFillingComponent.generated.h"

/** Add to filling equipment. Its owner supplies placement; Prop Manager owns the liquid transaction. */
UCLASS(ClassGroup = (Shop), meta = (BlueprintSpawnableComponent))
class COLOSSALFIZZCO_API UBottleFillingComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UBottleFillingComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filling", meta = (ClampMin = "0.01")) float PortionsPerSecond = 10;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filling", meta = (ClampMin = "1")) float MaxConnectionDistance = 300;
	/** Zero retains the equipment's physical-capacity behavior. Positive values stop at a selected content level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filling", meta = (ClampMin = "0")) float TargetContentLevel = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Filling") bool bIsFilling = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Filling") int32 SourceISID = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Filling") int32 TargetISID = 0;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Filling") bool StartFilling(int32 Source, int32 Target);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Filling") void StopFilling();

private:
	FGuid SessionID;
	float AccumulatedSeconds = 0;
	TWeakObjectPtr<class APropManager> Manager;
	bool ConnectionsInRange() const;
};
