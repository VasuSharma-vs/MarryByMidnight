#include "Shop/ShopCheckoutComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/ColaPricingLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Templates/UnrealTemplate.h"
#include "Props/InteractablePropDataAsset.h"

UShopCheckoutComponent::UShopCheckoutComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	for (int32 D : {1, 2, 5, 10, 20, 50, 100})
	{
		const FString Path = FString::Printf(TEXT("/Game/Props/Currency/DA_Coin_%d.DA_Coin_%d"), D, D);
		FShopCurrencyRecipe Recipe; Recipe.Dollars = D; Recipe.Product = TSoftObjectPtr<UInteractablePropDataAsset>(FSoftObjectPath(Path)); CurrencyRecipes.Add(Recipe);
	}
}

void UShopCheckoutComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UShopCheckoutComponent, ActiveCheckouts);
	DOREPLIFETIME(UShopCheckoutComponent, PendingExpiryCases);
	DOREPLIFETIME(UShopCheckoutComponent, RecentReceipts);
	DOREPLIFETIME(UShopCheckoutComponent, RecentSettlements);
	DOREPLIFETIME(UShopCheckoutComponent, RecentAdjustments);
	DOREPLIFETIME(UShopCheckoutComponent, TeamCashCents);
}

bool UShopCheckoutComponent::IsServer() const { return GetOwner() && GetOwner()->HasAuthority() && GetOwner()->GetNetMode() != NM_Client; }
void UShopCheckoutComponent::OnRep_Checkouts() { OnCheckoutsChanged.Broadcast(); }
void UShopCheckoutComponent::NotifyChanged() { TeamCashCents = GetTeamCashCents(); OnCheckoutsChanged.Broadcast(); GetOwner()->ForceNetUpdate(); }

bool UShopCheckoutComponent::RegisterShopStock(int32 ISID, int64 PurchaseCents, const FTransform& Transform)
{
	const APropManager* Manager = Cast<APropManager>(GetOwner());
	if (!IsServer() || bCommitting || ISID <= 0 || PurchaseCents < 0 || !Manager || !Manager->FindISIDData(ISID) || Transform.ContainsNaN() || Stock.Contains(ISID)) return false;
	FShopStockEntry Entry; Entry.ISID = ISID; Entry.OriginalPurchaseCents = PurchaseCents; Entry.StockTransform = Transform;
	Stock.Add(ISID, Entry);
	return true;
}

bool UShopCheckoutComponent::GetStockEntry(int32 ISID, FShopStockEntry& OutEntry) const
{
	if (const FShopStockEntry* Entry = Stock.Find(ISID)) { OutEntry = *Entry; return true; }
	return false;
}

bool UShopCheckoutComponent::IsAvailableShopStock(int32 ISID) const
{
	const FShopStockEntry* Entry = Stock.Find(ISID);
	return Entry && !Entry->CustomerOwner.IsValid() && !Reservations.Contains(ISID);
}

bool UShopCheckoutComponent::GetCheckoutView(FGuid ID, FShopCheckoutView& OutView) const
{
	OutView = FShopCheckoutView();
	if (const FShopCheckoutView* View = ActiveCheckouts.FindByPredicate([ID](const auto& C) { return C.CheckoutID == ID; }))
	{
		OutView = *View;
		return true;
	}
	return false;
}

bool UShopCheckoutComponent::GetCompletedReceipt(FGuid ID, FCompletedCheckoutReceipt& OutReceipt) const
{
	OutReceipt = FCompletedCheckoutReceipt();
	if (const FCompletedCheckoutReceipt* Receipt = Completed.Find(ID)) { OutReceipt = *Receipt; return true; }
	if (const auto* Receipt = RecentReceipts.FindByPredicate([ID](const auto& R) { return R.CheckoutID == ID; })) { OutReceipt = *Receipt; return true; }
	return false;
}

EShopCheckoutResult UShopCheckoutComponent::BuildQuote(int32 ISID, FCheckoutItemQuote& Out) const
{
	Out = FCheckoutItemQuote();
	const APropManager* Manager = Cast<APropManager>(GetOwner());
	FShopProductScanData Scan;
	if (!Manager || !Manager->GetProductScanData(ISID, Scan)) return EShopCheckoutResult::InvalidProduct;
	Out = FCheckoutItemQuote::FromScan(Scan);
	return EShopCheckoutResult::Success;
}

EShopCheckoutResult UShopCheckoutComponent::BeginCheckout(FGuid ID, FGuid Customer, const TArray<int32>& Items)
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bCommitting || !ID.IsValid() || !Customer.IsValid() || Items.IsEmpty() || Items.Num() > 64) return EShopCheckoutResult::InvalidRequest;
	if (Completed.Contains(ID)) return EShopCheckoutResult::AlreadyCompleted;
	if (PhysicalCashOperations.Contains(ID)) return EShopCheckoutResult::OperationConflict;
	if (const FShopCheckoutView* Existing = ActiveCheckouts.FindByPredicate([ID](const auto& C) { return C.CheckoutID == ID; }))
	{
		TArray<int32> Previous; for (const auto& Q : Existing->Items) Previous.Add(Q.ISID);
		return Existing->CustomerID == Customer && Previous == Items ? EShopCheckoutResult::Success : EShopCheckoutResult::OperationConflict;
	}
	FShopCheckoutView Checkout; Checkout.CheckoutID = ID; Checkout.CustomerID = Customer;
	if (ActiveCheckouts.ContainsByPredicate([Customer](const auto& C) { return C.CustomerID == Customer; })) return EShopCheckoutResult::Unavailable;
	TSet<int32> UniqueItems;
	for (int32 ISID : Items)
	{
		if (UniqueItems.Contains(ISID) || !IsAvailableShopStock(ISID)) return EShopCheckoutResult::Unavailable;
		UniqueItems.Add(ISID);
		FCheckoutItemQuote Quote;
		const EShopCheckoutResult Result = BuildQuote(ISID, Quote);
		if (Result != EShopCheckoutResult::Success) return Result;
		if (Checkout.DetailedTotalCents > MAX_int64 - Quote.SaleCents) return EShopCheckoutResult::InvalidRequest;
		Checkout.DetailedTotalCents += Quote.SaleCents; Checkout.Items.Add(Quote);
	}
	Checkout.TotalCents = UColaPricingLibrary::RoundBillToWholeDollar(Checkout.DetailedTotalCents);
	if (Checkout.TotalCents < 0) return EShopCheckoutResult::InvalidRequest;
	Checkout.RoundingAdjustmentCents = Checkout.TotalCents - Checkout.DetailedTotalCents;
	Checkout.bInvoiceLocked = true;
	for (int32 ISID : Items) Reservations.Add(ISID, ID);
	ActiveCheckouts.Add(Checkout); NotifyChanged();
	return EShopCheckoutResult::Success;
}

EShopCheckoutResult UShopCheckoutComponent::ReceiveTender(FGuid ID, FGuid OperationID, int64 Amount) { return ApplyCash(ID, OperationID, Amount, false); }
EShopCheckoutResult UShopCheckoutComponent::ReturnChange(FGuid ID, FGuid OperationID, int64 Amount) { return ApplyCash(ID, OperationID, Amount, true); }

EShopCheckoutResult UShopCheckoutComponent::ApplyCash(FGuid ID, FGuid OperationID, int64 Amount, bool bChange)
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	// Compatibility entry points deliberately reject amount-only payment. Currency requires real coin identities.
	return EShopCheckoutResult::InvalidRequest;
}

EShopCheckoutResult UShopCheckoutComponent::CompleteCheckout(FGuid ID)
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bCommitting) return EShopCheckoutResult::Unavailable;
	if (Completed.Contains(ID)) return EShopCheckoutResult::AlreadyCompleted;
	const FShopCheckoutView* Pending = ActiveCheckouts.FindByPredicate([ID](const auto& C) { return C.CheckoutID == ID; });
	if (!Pending) return EShopCheckoutResult::NotFound;
	FShopCheckoutView Checkout = *Pending;
	if (Checkout.TenderCents < Checkout.TotalCents) return EShopCheckoutResult::Unpaid;
	if (Checkout.ChangeCents != Checkout.TenderCents - Checkout.TotalCents) return EShopCheckoutResult::IncorrectChange;
	if (!ValidatePhysicalPayment(Checkout)) return EShopCheckoutResult::CashUnavailable;
	for (FCheckoutItemQuote& Item : Checkout.Items)
	{
		const FShopStockEntry* Entry = Stock.Find(Item.ISID);
		const FGuid* Reservation = Reservations.Find(Item.ISID);
		FCheckoutItemQuote Current;
		const EShopCheckoutResult Result = BuildQuote(Item.ISID, Current);
		if (Result != EShopCheckoutResult::Success) return Result;
		if (!Entry || Entry->CustomerOwner.IsValid() || !Reservation || *Reservation != ID || Current.EventSequence != Item.EventSequence || Current.ProductID != Item.ProductID || Current.ContentID != Item.ContentID) return EShopCheckoutResult::QuoteChanged;
		Item.bExpiredAtSale = Current.Snapshot.Price.bExpired;
	}
	APropManager* Manager = Cast<APropManager>(GetOwner());
	if (!Manager || !Manager->SharedWallet) return EShopCheckoutResult::WalletFailure;
	TGuardValue<bool> Guard(bCommitting, true);
	EnsureWalletBinding();
	FCompletedCheckoutReceipt Receipt; Receipt.CheckoutID = ID; Receipt.CustomerID = Checkout.CustomerID; Receipt.TotalCents = Checkout.TotalCents;
	Receipt.DetailedTotalCents = Checkout.DetailedTotalCents; Receipt.RoundingAdjustmentCents = Checkout.RoundingAdjustmentCents;
	Receipt.Items = Checkout.Items; Receipt.CompletedTime = Manager->WorldElapsedSeconds;
	Receipt.Sequence = static_cast<int64>(Completed.Num()) + 1;
	if (const auto* Allocation = CashAllocations.Find(ID)) { Receipt.TenderCoinISIDs = Allocation->Tender; Receipt.ChangeCoinISIDs = Allocation->Change; }
	TArray<FGuid> NewCases;
	for (const FCheckoutItemQuote& Item : Checkout.Items)
	{
		Stock.FindChecked(Item.ISID).CustomerOwner = Checkout.CustomerID;
		Receipt.ItemISIDs.Add(Item.ISID);
		if (Item.bExpiredAtSale)
		{
			FShopExpiryCase Case;
			Case.CaseID = FGuid::NewGuid(); Case.CheckoutID = ID; Case.CustomerID = Checkout.CustomerID;
			Case.OriginalISID = Item.ISID; Case.ContentID = Item.ContentID;
			Case.NominalPortions = Item.Snapshot.NominalPortions; Case.OriginalBaseCents = Item.Snapshot.Price.OriginalBaseCents;
			NewCases.Add(Case.CaseID); ExpiryCases.Add(Case);
		}
	}
	Completed.Add(ID, Receipt);
	// Actual customer coins become register cash. Banking occurs only at an ATM deposit.
	CommitPhysicalPayment(Checkout);
	for (int32 ISID : Receipt.ItemISIDs) Reservations.Remove(ISID);
	ActiveCheckouts.RemoveAll([ID](const auto& C) { return C.CheckoutID == ID; });
	ForgetCashOperations(ID);
	RecentReceipts.Insert(Receipt, 0);
	if (RecentReceipts.Num() > 32) RecentReceipts.SetNum(32);
	CollectOutstandingInternal();
	RefreshPendingCases();
	OnAdjustmentsChanged.Broadcast();
	NotifyChanged(); OnSaleCompleted.Broadcast(Receipt);
	return EShopCheckoutResult::Success;
}

bool UShopCheckoutComponent::CancelCheckout(FGuid ID)
{
	if (!IsServer() || bCommitting) return false;
	const FShopCheckoutView* Checkout = ActiveCheckouts.FindByPredicate([ID](const auto& C) { return C.CheckoutID == ID; });
	if (!Checkout) return false;
	ReleasePhysicalCash(ID);
	if (APropManager* Manager = Cast<APropManager>(GetOwner())) for (const auto& Item : Checkout->Items)
	{
		if (const FShopStockEntry* Entry = Stock.Find(Item.ISID)) Manager->InitializeInstanceTransform(Item.ISID, Entry->StockTransform);
		Reservations.Remove(Item.ISID);
	}
	ActiveCheckouts.RemoveAll([ID](const auto& C) { return C.CheckoutID == ID; });
	ForgetCashOperations(ID);
	NotifyChanged(); return true;
}

void UShopCheckoutComponent::CancelAllCheckouts()
{
	if (!IsServer() || bCommitting) return;
	TArray<FGuid> IDs; for (const auto& C : ActiveCheckouts) IDs.Add(C.CheckoutID);
	for (const FGuid& ID : IDs) CancelCheckout(ID);
}

void UShopCheckoutComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEnding = true;
	if (APropManager* Manager = Cast<APropManager>(GetOwner())) if (Manager->SharedWallet) Manager->SharedWallet->OnWalletChanged.RemoveDynamic(this, &UShopCheckoutComponent::OnWalletChanged);
	CancelAllCheckouts(); Super::EndPlay(Reason);
}

bool UShopCheckoutComponent::GetCanceledStockTransform(int32 ISID, FTransform& Out) const
{
	const FShopStockEntry* Entry = Stock.Find(ISID);
	if (!Entry || !Reservations.Contains(ISID) || Entry->CustomerOwner.IsValid()) return false;
	Out = Entry->StockTransform; return true;
}

FShopCheckoutSaveData UShopCheckoutComponent::ExportCommittedState() const
{
	FShopCheckoutSaveData Saved; Stock.GenerateValueArray(Saved.Stock); Completed.GenerateValueArray(Saved.Completed);
	Saved.ExpiryCases = ExpiryCases; Saved.Adjustments = Adjustments;
	for (const auto& Pair : Coins) if (!Pair.Value.CustomerOwner.IsValid())
	{
		FShopCashCoin Coin = Pair.Value;
		if (Coin.CheckoutID.IsValid()) { Coin.CheckoutID.Invalidate(); Coin.RegisterID.Invalidate(); Coin.Custody = EShopCoinCustody::World; }
		Saved.Cash.Coins.Add(Coin);
	}
	PhysicalCashOperations.GenerateValueArray(Saved.Cash.Operations);
	Saved.Cash.Operations.Sort([](const auto& A, const auto& B) { return A.Sequence < B.Sequence; });
	return Saved;
}

bool UShopCheckoutComponent::ValidateSaveData(const FShopCheckoutSaveData& Saved)
{
	if (!ValidateCashSaveData(Saved.Cash)) return false;
	TMap<int32, const FShopStockEntry*> Stocks;
	TMap<FGuid, const FCompletedCheckoutReceipt*> Receipts;
	TSet<int64> Sequences;
	for (const auto& Entry : Saved.Stock)
	{
		if (Entry.ISID <= 0 || Stocks.Contains(Entry.ISID) || Entry.OriginalPurchaseCents < 0 || Entry.StockTransform.ContainsNaN()) return false;
		Stocks.Add(Entry.ISID, &Entry);
	}
	for (const auto& Receipt : Saved.Completed)
	{
		if (!Receipt.CheckoutID.IsValid() || !Receipt.CustomerID.IsValid() || Receipt.TotalCents < 0 || Receipt.DetailedTotalCents < 0 || Receipt.Items.IsEmpty()
			|| Receipt.Items.Num() > 64 || Receipt.Items.Num() != Receipt.ItemISIDs.Num() || Receipts.Contains(Receipt.CheckoutID)
			|| Receipt.Sequence <= 0 || Receipt.Sequence > Saved.Completed.Num() || Sequences.Contains(Receipt.Sequence)
			|| !FMath::IsFinite(Receipt.CompletedTime) || Receipt.CompletedTime < 0) return false;
		int64 Sum = 0; TSet<int32> ItemIDs;
		for (int32 I = 0; I < Receipt.Items.Num(); ++I)
		{
			const auto& Item = Receipt.Items[I];
			const auto* Stock = Stocks.Find(Item.ISID);
			if (!Stock || (*Stock)->CustomerOwner != Receipt.CustomerID || Item.ISID != Receipt.ItemISIDs[I] || ItemIDs.Contains(Item.ISID)
				|| Item.ISID != Item.Snapshot.ISID || Item.ProductID != Item.Snapshot.ProductID || Item.ContentID != Item.Snapshot.ContentID
				|| Item.SaleCents < 0 || Item.SaleCents != Item.Snapshot.Price.SaleCents || !Item.Snapshot.Price.bValid
				|| Item.Snapshot.Price.OriginalBaseCents < 0 || Item.Snapshot.Price.OriginalBaseCents > 9007199254740991LL || Sum > MAX_int64 - Item.SaleCents) return false;
			ItemIDs.Add(Item.ISID); Sum += Item.SaleCents;
		}
		if (Sum != Receipt.DetailedTotalCents || UColaPricingLibrary::RoundBillToWholeDollar(Sum) != Receipt.TotalCents
			|| Receipt.RoundingAdjustmentCents != Receipt.TotalCents - Sum) return false;
		const auto* Cash = Saved.Cash.Operations.FindByPredicate([&Receipt](const auto& O) { return O.OperationID == Receipt.CheckoutID; });
		if (Cash)
		{
			TArray<int32> AllCoins = Receipt.TenderCoinISIDs; AllCoins.Append(Receipt.ChangeCoinISIDs);
			if (Cash->Kind != EShopCashOperationKind::CheckoutTransfer || Cash->CustomerID != Receipt.CustomerID || Cash->AmountCents != Receipt.TotalCents
				|| Cash->TenderCoinCount != Receipt.TenderCoinISIDs.Num() || Cash->CoinISIDs != AllCoins) return false;
		}
		else if (Receipt.TotalCents != 0 || !Receipt.TenderCoinISIDs.IsEmpty() || !Receipt.ChangeCoinISIDs.IsEmpty()) return false;
		Receipts.Add(Receipt.CheckoutID, &Receipt); Sequences.Add(Receipt.Sequence);
	}
	TMap<FGuid, const FShopExpiryCase*> Cases;
	TSet<FString> ExpiredItems;
	for (const auto& Case : Saved.ExpiryCases)
	{
		const auto* Receipt = Receipts.Find(Case.CheckoutID);
		const auto* Item = Receipt ? (*Receipt)->Items.FindByPredicate([&Case](const auto& Q) { return Q.ISID == Case.OriginalISID; }) : nullptr;
		const FString Key = Case.CheckoutID.ToString() + TEXT(":") + FString::FromInt(Case.OriginalISID);
		if (!Case.CaseID.IsValid() || Cases.Contains(Case.CaseID) || ExpiredItems.Contains(Key) || !Receipt || !Item || !Item->bExpiredAtSale
			|| Case.CustomerID != (*Receipt)->CustomerID || Case.ContentID != Item->ContentID || Case.NominalPortions != Item->Snapshot.NominalPortions
			|| Case.OriginalBaseCents != Item->Snapshot.Price.OriginalBaseCents || Case.AutomaticPaidCents < 0 || Case.AutomaticPaidCents > Case.OriginalBaseCents
			|| Case.CompensationPaidCents < 0 || Case.CompensationPaidCents > Case.OriginalBaseCents
			|| static_cast<uint8>(Case.Choice) > static_cast<uint8>(EShopCompensationChoice::AdditionalDeduction)
			|| (Case.Choice != EShopCompensationChoice::AdditionalDeduction && Case.CompensationPaidCents != 0)
			|| (Case.Choice != EShopCompensationChoice::Replacement && Case.ReplacementISID != 0)) return false;
		if (Case.Choice == EShopCompensationChoice::Replacement)
		{
			const auto* Replacement = Stocks.Find(Case.ReplacementISID);
			if (!Replacement || (*Replacement)->CustomerOwner != Case.CustomerID || Case.ReplacementISID == Case.OriginalISID) return false;
		}
		Cases.Add(Case.CaseID, &Case); ExpiredItems.Add(Key);
	}
	// Omitting a case for an expired sale must never erase a compensation obligation on reload.
	for (const auto& Receipt : Saved.Completed) for (const auto& Item : Receipt.Items)
		if (Item.bExpiredAtSale && !ExpiredItems.Contains(Receipt.CheckoutID.ToString() + TEXT(":") + FString::FromInt(Item.ISID))) return false;
	TSet<FGuid> AdjustmentIDs; TSet<int32> Replacements;
	TMap<FGuid, int64> AutomaticTotals, CompensationTotals;
	for (const auto& Adjustment : Saved.Adjustments)
	{
		const auto* Found = Cases.Find(Adjustment.CaseID);
		if (!Found || !Adjustment.AdjustmentID.IsValid() || AdjustmentIDs.Contains(Adjustment.AdjustmentID) || Adjustment.CheckoutID != (*Found)->CheckoutID
			|| !FMath::IsFinite(Adjustment.Time) || Adjustment.Time < 0 || Adjustment.Time < Receipts.FindChecked(Adjustment.CheckoutID)->CompletedTime
			|| static_cast<uint8>(Adjustment.Kind) > static_cast<uint8>(EShopAdjustmentKind::FreeReplacement)) return false;
		if (Adjustment.Kind == EShopAdjustmentKind::FreeReplacement)
		{
			if (Adjustment.DeductedCents != 0 || (*Found)->Choice != EShopCompensationChoice::Replacement || Adjustment.ReplacementISID <= 0
				|| Adjustment.ReplacementISID != (*Found)->ReplacementISID || Replacements.Contains(Adjustment.ReplacementISID)) return false;
			Replacements.Add(Adjustment.ReplacementISID);
		}
		else
		{
			if (Adjustment.DeductedCents <= 0 || Adjustment.ReplacementISID != 0) return false;
			int64& Sum = Adjustment.Kind == EShopAdjustmentKind::AutomaticExpiryPenalty ? AutomaticTotals.FindOrAdd(Adjustment.CaseID) : CompensationTotals.FindOrAdd(Adjustment.CaseID);
			if (Sum > (*Found)->OriginalBaseCents - Adjustment.DeductedCents) return false;
			Sum += Adjustment.DeductedCents;
		}
		AdjustmentIDs.Add(Adjustment.AdjustmentID);
	}
	for (const auto& Case : Saved.ExpiryCases)
		if (AutomaticTotals.FindRef(Case.CaseID) != Case.AutomaticPaidCents || CompensationTotals.FindRef(Case.CaseID) != Case.CompensationPaidCents
			|| (Case.Choice == EShopCompensationChoice::Replacement && !Replacements.Contains(Case.ReplacementISID))) return false;
	for (const auto& Op : Saved.Cash.Operations) if (Op.Kind == EShopCashOperationKind::CheckoutTransfer && !Receipts.Contains(Op.OperationID)) return false;
	return true;
}

bool UShopCheckoutComponent::RestoreCommittedState(const FShopCheckoutSaveData& Saved)
{
	if (!IsServer() || bCommitting || !ValidateSaveData(Saved)) return false;
	CancelAllCheckouts(); Stock.Empty(); Completed.Empty(); CashOperations.Empty(); Reservations.Empty();
	for (const auto& Entry : Saved.Stock) Stock.Add(Entry.ISID, Entry);
	for (const auto& Receipt : Saved.Completed) Completed.Add(Receipt.CheckoutID, Receipt);
	ExpiryCases = Saved.ExpiryCases; Adjustments = Saved.Adjustments;
	Coins.Empty(); PhysicalCashOperations.Empty(); CashAllocations.Empty();
	for (const auto& Coin : Saved.Cash.Coins) Coins.Add(Coin.ISID, Coin);
	for (const auto& Op : Saved.Cash.Operations) PhysicalCashOperations.Add(Op.OperationID, Op);
	RecentReceipts = GetHistoryPage(0, 32);
	RefreshPendingCases(); EnsureWalletBinding();
	NotifyChanged(); OnAdjustmentsChanged.Broadcast(); return true;
}

void UShopCheckoutComponent::ForgetCashOperations(FGuid ID)
{
	for (auto It = CashOperations.CreateIterator(); It; ++It) if (It.Value().CheckoutID == ID) It.RemoveCurrent();
}
