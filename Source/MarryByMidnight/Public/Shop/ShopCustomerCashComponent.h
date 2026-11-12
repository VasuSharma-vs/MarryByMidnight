#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Shop/ShopCheckoutTypes.h"
#include "ShopCustomerCashComponent.generated.h"
class UShopScannerComponent;

/** Server-owned customer visit budget. Never accepts a player-provided customer ID or cash value. */
UCLASS(ClassGroup = (Shop), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UShopCustomerCashComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UShopCustomerCashComponent();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Customer", meta = (ClampMin = "0")) int64 VisitBudgetCents = 1000000;
	/** Zero pays exactly. Positive rounding is used only when this register physically holds exact change. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Customer", meta = (ClampMin = "0", ClampMax = "100")) int32 TenderRoundUpDollars=0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Customer") FGuid CustomerID;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Customer") EShopCheckoutResult JoinScanner(UShopScannerComponent* Scanner);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Customer") EShopCheckoutResult PlacePayment();
	/** Plans actual available bottles under the visit's budget and physical coin limit. Repeated product IDs are grouped. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Customer") TArray<int32> BuildShoppingPlan(int32 MaxBottles = 3);
	UFUNCTION(BlueprintPure, Category = "Customer") int32 GetPlannedQuantity(int32 ProductID) const;
	bool ReleaseStation(bool bCancelUnfinished);
	bool ResetVisit();
	bool FindCompletedVisitReceipt(FCompletedCheckoutReceipt& Out) const;
	UShopScannerComponent* GetStation() const { return Station.Get(); }
private:
	TMap<int32, int32> PlannedQuantities;
	TWeakObjectPtr<UShopScannerComponent> Station;
	struct FVisitPayment { TArray<int32> Coins; int64 AmountCents=0; };
	TMap<FGuid, FVisitPayment> IssuedPayments;
	int64 SpentCents = 0;
	void ReclaimCanceledPayments();
};
