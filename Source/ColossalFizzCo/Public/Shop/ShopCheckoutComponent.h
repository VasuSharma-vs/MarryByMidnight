#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Shop/ShopCheckoutTypes.h"
#include "Shop/SharedWalletTypes.h"
#include "ShopCheckoutComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnShopCheckoutsChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnShopSaleCompleted, FCompletedCheckoutReceipt, Receipt);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnShopAdjustmentsChanged);
struct FPropWorldSaveData;

/** All amounts come from server definitions or server cash handling, never client price overrides. */
UCLASS(ClassGroup = (Shop), meta = (BlueprintSpawnableComponent))
class COLOSSALFIZZCO_API UShopCheckoutComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UShopCheckoutComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void BeginPlay() override;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Cash") TArray<FShopCurrencyRecipe> CurrencyRecipes;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Cash", meta = (ClampMin = "1", ClampMax = "256")) int32 MaxCoinsPerOperation = 128;
	UPROPERTY(ReplicatedUsing = OnRep_Checkouts, BlueprintReadOnly, Category = "Shop|Cash") int64 TeamCashCents = 0;
	/** Trusted server services. Owned-player RPCs use ATM/register volume checks before calling these. */
	EShopCheckoutResult WithdrawCoins(FGuid OperationID, const TArray<int32>& Denominations, const FTransform& Output, TArray<int32>& OutCoins, bool bStartPhysics = false);
	EShopCheckoutResult DepositCoins(FGuid OperationID, const TArray<int32>& ISIDs);
	EShopCheckoutResult IssueCustomerCoins(FGuid OperationID, FGuid CustomerID, const TArray<int32>& Denominations, const FTransform& Output, TArray<int32>& OutCoins);
	EShopCheckoutResult ReconcilePhysicalCash(FGuid CheckoutID, FGuid RegisterID, const TArray<int32>& Tender, const TArray<int32>& Change);
	void ReleasePhysicalCash(FGuid CheckoutID);
	bool GetCoin(int32 ISID, FShopCashCoin& Out) const;
	/** Retire only customer-owned, unallocated currency. Never removes team cash or changes the bank. */
	bool ReclaimCustomerCoins(FGuid CustomerID, const TArray<int32>& CoinISIDs);
	TArray<int32> GetCustomerCoinISIDs(FGuid CustomerID) const;
	bool ShouldPersistInstance(int32 ISID) const;
	int64 GetTeamCashCents() const;
	int64 GetOutstandingBankDebt() const;
	static bool IsSupportedDenomination(int32 Dollars);
	static bool ValidateCashSaveData(const FShopCashSaveData& Saved);
	static bool ValidateCashWorldSaveData(const FPropWorldSaveData& Saved);

	UPROPERTY(BlueprintAssignable, Category = "Shop") FOnShopCheckoutsChanged OnCheckoutsChanged;
	UPROPERTY(BlueprintAssignable, Category = "Shop") FOnShopSaleCompleted OnSaleCompleted;
	UPROPERTY(BlueprintAssignable, Category = "Shop") FOnShopAdjustmentsChanged OnAdjustmentsChanged;
	UPROPERTY(ReplicatedUsing = OnRep_Checkouts, BlueprintReadOnly, Category = "Shop") TArray<FShopCheckoutView> ActiveCheckouts;
	UPROPERTY(ReplicatedUsing = OnRep_Adjustments, BlueprintReadOnly, Category = "Shop") TArray<FShopExpiryCase> PendingExpiryCases;
	/** Bounded replicated display cache. All receipts remain in authoritative history/save data. */
	UPROPERTY(ReplicatedUsing = OnRep_Checkouts, BlueprintReadOnly, Category = "Shop") TArray<FCompletedCheckoutReceipt> RecentReceipts;
	UPROPERTY(ReplicatedUsing = OnRep_Adjustments, BlueprintReadOnly, Category = "Shop") TArray<FShopSettlementSummary> RecentSettlements;
	UPROPERTY(ReplicatedUsing = OnRep_Adjustments, BlueprintReadOnly, Category = "Shop") TArray<FShopPostSaleAdjustment> RecentAdjustments;

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
	TArray<int32> GetCustomerOwnedStockISIDs(FGuid CustomerID) const;
	/** Revalidate live contents and production readiness when an NPC claims or collects stock. */
	bool IsAvailableColaStockISID(int32 ISID) const;
	/** Cola-shop selection only. The generic Prop Manager registry continues to include coins, fragments and other products. */
	UFUNCTION(BlueprintPure, Category = "Shop|Stock") TArray<int32> GetAvailableColaStockISIDs() const;
	UFUNCTION(BlueprintPure, Category = "Shop")
	bool GetCheckoutView(FGuid CheckoutID, FShopCheckoutView& OutView) const;
	UFUNCTION(BlueprintPure, Category = "Shop")
	bool GetCompletedReceipt(FGuid CheckoutID, FCompletedCheckoutReceipt& OutReceipt) const;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|Compensation")
	EShopCheckoutResult ChooseCashCompensation(FGuid CaseID);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|Compensation")
	EShopCheckoutResult ProvideReplacement(FGuid CaseID, int32 ReplacementISID);
	UFUNCTION(BlueprintPure, Category = "Shop|Compensation")
	bool GetExpiryCase(FGuid CaseID, FShopExpiryCase& OutCase) const;
	UFUNCTION(BlueprintPure, Category = "Shop|History")
	bool GetAdjustedRevenue(FGuid CheckoutID, int64& AdjustedCents, int64& OutstandingDeductionCents) const;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|Compensation")
	void CollectOutstandingDeductions();
	bool IsReserved(int32 ISID) const { return Reservations.Contains(ISID); }
	/** Reuse canonical snapshots for invoice and scanner; never accept a client price. */
	EShopCheckoutResult BuildQuote(int32 ISID, FCheckoutItemQuote& OutQuote) const;
	TArray<FCompletedCheckoutReceipt> GetHistoryPage(int32 Offset, int32 Count) const;

	void CancelAllCheckouts();
	FShopCheckoutSaveData ExportCommittedState() const;
	bool RestoreCommittedState(const FShopCheckoutSaveData& Saved);
	bool GetCanceledStockTransform(int32 ISID, FTransform& OutTransform) const;
	static bool ValidateSaveData(const FShopCheckoutSaveData& Saved);

private:
	TMap<int32, FShopCashCoin> Coins;
	TMap<FGuid, FShopCashOperation> PhysicalCashOperations;
	TMap<FGuid, FShopPhysicalCashAllocation> CashAllocations;
	EShopCheckoutResult CreateCoins(FGuid OperationID, FGuid CustomerID, const TArray<int32>& Denominations, const FTransform& Output, bool bWithdraw, TArray<int32>& OutCoins, bool bStartPhysics = false);
	bool ValidatePhysicalPayment(const FShopCheckoutView& Checkout) const;
	void CommitPhysicalPayment(const FShopCheckoutView& Checkout);
	TMap<int32, FShopStockEntry> Stock;
	TMap<int32, FGuid> Reservations;
	TMap<FGuid, FCompletedCheckoutReceipt> Completed;
	TArray<FShopExpiryCase> ExpiryCases;
	TArray<FShopPostSaleAdjustment> Adjustments;
	struct FCashOperation { FGuid CheckoutID; int64 Amount = 0; bool bChange = false; };
	TMap<FGuid, FCashOperation> CashOperations;
	bool bCommitting = false;
	bool bCollectionScheduled = false;
	bool bEnding = false;
	bool IsServer() const;
	EShopCheckoutResult ApplyCash(FGuid CheckoutID, FGuid OperationID, int64 Amount, bool bChange);
	void NotifyChanged();
	void ForgetCashOperations(FGuid ID);
	void EnsureWalletBinding();
	void CollectOutstandingInternal();
	void RefreshPendingCases();
	void NotifyAdjustments();
	UFUNCTION() void OnWalletChanged(FSharedWalletState State);
	UFUNCTION() void OnRep_Adjustments();
	UFUNCTION() void OnRep_Checkouts();
};
