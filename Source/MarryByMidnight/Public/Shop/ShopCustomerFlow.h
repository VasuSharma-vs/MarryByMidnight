#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShopCustomerFlow.generated.h"
class ACharacter;
class UShopCustomerVisitComponent;
class UShopScannerComponent;
class USceneComponent;

/** Prototype arrival/queue/bucket coordination. Existing services remain the stock and money authorities. */
UCLASS()
class MARRYBYMIDNIGHT_API AShopCustomerFlow : public AActor
{
	GENERATED_BODY()
public:
	AShopCustomerFlow();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(EditAnywhere,Category="Shop") TSubclassOf<ACharacter> CustomerClass;
	UPROPERTY(EditAnywhere,Category="Shop") TObjectPtr<AActor> CheckoutStation;
	UPROPERTY(EditAnywhere,Category="Shop") TObjectPtr<AActor> SpawnPoint;
	UPROPERTY(EditAnywhere,Category="Shop") TObjectPtr<AActor> EntrancePoint;
	UPROPERTY(EditAnywhere,Category="Shop") TObjectPtr<AActor> QueuePoint;
	UPROPERTY(EditAnywhere,Category="Shop") TObjectPtr<AActor> BucketReturnPoint;
	UPROPERTY(EditAnywhere,Category="Shop") TObjectPtr<AActor> DeparturePoint;
	UPROPERTY(EditAnywhere,Category="Shop") TArray<TObjectPtr<AActor>> Buckets;
	UPROPERTY(EditAnywhere,Category="Shop",meta=(ClampMin="1",ClampMax="8")) int32 MaxCustomers=3;
	UPROPERTY(EditAnywhere,Category="Shop",meta=(ClampMin="1",ClampMax="8")) int32 BottlesPerCustomer=3;
	UPROPERTY(EditAnywhere,Category="Shop",meta=(ClampMin="1")) float ArrivalInterval=12;
	/** Approved prototype policy: recycle only after the customer walks beyond the exit. */
	UPROPERTY(EditAnywhere,Category="Shop") bool bRecycleDepartedCustomers=true;
	/** Server-side startup diagnostics; never used as an authority or simulation gate. */
	UPROPERTY(VisibleAnywhere,Category="Shop") FString ArrivalStatus=TEXT("Waiting for shop initialization");
	UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Shop") bool TryArrival();
	UShopScannerComponent* GetScanner() const;
	USceneComponent* GetBucketDock() const;
	int32 QueuePosition(UShopCustomerVisitComponent* Visit);
	FVector QueueLocation(int32 Index) const;
	void JoinQueue(UShopCustomerVisitComponent* Visit);
	void LeaveQueue(UShopCustomerVisitComponent* Visit);
	bool ClaimStock(int32 ISID,UShopCustomerVisitComponent* Visit);
	bool StockIsClaimed(int32 ISID) const;
	void ReleaseClaims(UShopCustomerVisitComponent* Visit);
	void ReturnBucket(AActor* Bucket,UShopCustomerVisitComponent* Visit);
	void ReleaseVisit(UShopCustomerVisitComponent* Visit);
private:
	FTimerHandle ArrivalTimer;
	UPROPERTY(Transient) TArray<TObjectPtr<ACharacter>> Customers;
	TArray<TWeakObjectPtr<UShopCustomerVisitComponent>> Queue;
	TMap<int32,TWeakObjectPtr<UShopCustomerVisitComponent>> StockClaims;
	TMap<TWeakObjectPtr<AActor>,TWeakObjectPtr<UShopCustomerVisitComponent>> BucketClaims;
	TMap<TWeakObjectPtr<AActor>,FTransform> BucketHomes;
};
