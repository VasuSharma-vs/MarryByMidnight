#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Shop/ShopCheckoutTypes.h"
#include "ShopCustomerVisitComponent.generated.h"
class AShopCustomerFlow;
class APropManager;
class ACharacter;
class UShopCustomerCashComponent;
class USceneComponent;

UENUM(BlueprintType)
enum class EShopVisitPhase:uint8 { Inactive,Entering,GettingBucket,Shopping,Queuing,WaitingForCashier,CollectingPurchase,ReturningBucket,Departing,Departed,Aborted };

/** Server-only visit state machine on the existing customer character; phase is replicated for presentation. */
UCLASS(ClassGroup=(Shop),meta=(BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UShopCustomerVisitComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UShopCustomerVisitComponent();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(ReplicatedUsing=OnRep_Phase,BlueprintReadOnly,Category="Visit") EShopVisitPhase Phase=EShopVisitPhase::Inactive;
	UPROPERTY(ReplicatedUsing=OnRep_Phase,BlueprintReadOnly,Category="Visit") FString Status;
	UPROPERTY(EditAnywhere,Category="Visit",meta=(ClampMin="5")) float MoveTimeoutSeconds=45;
	bool StartVisit(AShopCustomerFlow* InFlow,AActor* InBucket);
	void AdvanceVisit();
	void AbortVisit(const FString& Reason);
	bool RetireVisit();
	bool AcceptsInvoiceItems(const TArray<FCheckoutItemQuote>& Items) const;
	AShopCustomerFlow* GetFlow() const { return Flow.Get(); }
	const TArray<int32>& GetCollectedISIDs() const { return Collected; }
private:
	UPROPERTY(Transient) TWeakObjectPtr<AShopCustomerFlow> Flow;
	UPROPERTY(Transient) TWeakObjectPtr<AActor> Bucket;
	UPROPERTY(Transient) TWeakObjectPtr<APropManager> Manager;
	UPROPERTY(Transient) TWeakObjectPtr<UShopCustomerCashComponent> Cash;
	TArray<int32> Collected;
	TMap<int32,int32> Wanted;
	TSet<int32> Rejected;
	int32 TargetISID=0;
	FGuid CheckoutID;
	FVector MoveGoal=FVector::ZeroVector;
	double MoveStarted=0;
	bool bMoving=false;
	bool bMoveFailed=false;
	FString MoveFailureReason;
	bool bAborting=false;
	FTimerHandle VisitTimer;
	void SetPhase(EShopVisitPhase Next,const FString& Message);
	bool MoveTo(FVector Destination);
	bool Arrived() const;
	USceneComponent* NextPocket() const;
	void CarryBucket();
	void ChooseNextProduct();
	bool CollectPaidGoods();
	void ReturnUnsoldGoods();
	void ReleaseBucket();
	UFUNCTION() void OnRep_Phase();
};
