#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Shop/ShopCheckoutTypes.h"
#include "ShopCheckoutComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnShopCheckoutsChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnShopSaleCompleted, FCompletedCheckoutReceipt, Receipt);

/** All amounts come from server definitions or server cash handling, never client price overrides. */
UCLASS(ClassGroup = (Shop), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UShopCheckoutComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UShopCheckoutComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(BlueprintAssignable, Category = "Shop") FOnShopCheckoutsChanged OnCheckoutsChanged;
	UPROPERTY(BlueprintAssignable, Category = "Shop") FOnShopSaleCompleted OnSaleCompleted;
	UPROPERTY(ReplicatedUsing = OnRep_Checkouts, BlueprintReadOnly, Category = "Shop") TArray<FShopCheckoutView> ActiveCheckouts;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop")
	bool RegisterShopStock(int32 ISID, int64 OriginalPurchaseCents, const FTransform& StockTransform);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop")
	EShopCheckoutResult BeginCheckout(FGuid CheckoutID, FGuid CustomerID, const TArray<int32>& ItemISIDs);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop")
	EShopCheckoutResult ReceiveTender(FGuid CheckoutID, FGuid CashOperationID, int64 AmountCents);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop")
	EShopCheckoutResult ReturnChange(FGuid CheckoutID, FGuid CashOperationID, int64 AmountCents);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop")
	EShopCheckoutResult CompleteCheckout(FGuid CheckoutID);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop")
	bool CancelCheckout(FGuid CheckoutID);
	UFUNCTION(BlueprintPure, Category = "Shop")
	bool GetStockEntry(int32 ISID, FShopStockEntry& OutEntry) const;
	UFUNCTION(BlueprintPure, Category = "Shop")
	bool IsAvailableShopStock(int32 ISID) const;
	UFUNCTION(BlueprintPure, Category = "Shop")
	bool GetCheckoutView(FGuid CheckoutID, FShopCheckoutView& OutView) const;
	UFUNCTION(BlueprintPure, Category = "Shop")
	bool GetCompletedReceipt(FGuid CheckoutID, FCompletedCheckoutReceipt& OutReceipt) const;

	void CancelAllCheckouts();
	FShopCheckoutSaveData ExportCommittedState() const;
	bool RestoreCommittedState(const FShopCheckoutSaveData& Saved);
	bool GetCanceledStockTransform(int32 ISID, FTransform& OutTransform) const;
	static bool ValidateSaveData(const FShopCheckoutSaveData& Saved);

private:
	TMap<int32, FShopStockEntry> Stock;
	TMap<int32, FGuid> Reservations;
	TMap<FGuid, FCompletedCheckoutReceipt> Completed;
	struct FCashOperation { FGuid CheckoutID; int64 Amount = 0; bool bChange = false; };
	TMap<FGuid, FCashOperation> CashOperations;
	bool bCommitting = false;
	bool IsServer() const;
	EShopCheckoutResult BuildQuote(int32 ISID, FCheckoutItemQuote& OutQuote) const;
	EShopCheckoutResult ApplyCash(FGuid CheckoutID, FGuid OperationID, int64 Amount, bool bChange);
	void NotifyChanged();
	void ForgetCashOperations(FGuid ID);
	UFUNCTION() void OnRep_Checkouts();
};
