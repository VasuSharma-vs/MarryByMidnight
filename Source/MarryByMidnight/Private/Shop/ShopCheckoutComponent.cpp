#include "Shop/ShopCheckoutComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/ColaPricingLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Templates/UnrealTemplate.h"

UShopCheckoutComponent::UShopCheckoutComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UShopCheckoutComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UShopCheckoutComponent, ActiveCheckouts);
}

bool UShopCheckoutComponent::IsServer() const { return GetOwner() && GetOwner()->HasAuthority(); }
void UShopCheckoutComponent::OnRep_Checkouts() { OnCheckoutsChanged.Broadcast(); }
void UShopCheckoutComponent::NotifyChanged() { OnCheckoutsChanged.Broadcast(); GetOwner()->ForceNetUpdate(); }

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

EShopCheckoutResult UShopCheckoutComponent::BuildQuote(int32 ISID, FCheckoutItemQuote& Out) const
{
	const APropManager* Manager = Cast<APropManager>(GetOwner());
	const FISIDRuntimeIndex* Record = Manager ? Manager->FindISIDData(ISID) : nullptr;
	const FPropInstanceUniqueData* Unique = Manager ? Manager->FindUniqueData(ISID) : nullptr;
	const FPropProductDefinition* Product = Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
	const FPropContentDefinition* Content = Unique ? Manager->FindContentDefinition(Unique->EventData.ContentID) : nullptr;
	if (!Product || !Content || !Unique || Unique->EventData.ContentLevel <= 0) return EShopCheckoutResult::InvalidProduct;
	const bool Expired = Content->ExpiryProfile.bHasExpiry && (Manager->WorldElapsedSeconds - Unique->EventData.ContentCreationTime) >= Content->ExpiryProfile.ExpiryLifetimeMinutes * 60.0;
	const FColaPriceQuote Quote = UColaPricingLibrary::QuoteCola(Unique->EventData.ContentLevel, Product->ConsumableProfile.NumOfPortions, Content->BasePortionPrice,
		Unique->ContinuousData.Temperature, Content->IdealServingTemperature, Content->TemperaturePenaltyRange, Expired);
	if (!Quote.bValid) return EShopCheckoutResult::InvalidProduct;
	if (Expired) return EShopCheckoutResult::ExpiredProduct;
	Out.ISID = ISID; Out.ProductID = Record->ProductID; Out.ContentID = Content->ContentID;
	Out.SaleCents = Quote.SaleCents; Out.EventSequence = Unique->EventData.EventSequence;
	return EShopCheckoutResult::Success;
}

EShopCheckoutResult UShopCheckoutComponent::BeginCheckout(FGuid ID, FGuid Customer, const TArray<int32>& Items)
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bCommitting || !ID.IsValid() || !Customer.IsValid() || Items.IsEmpty()) return EShopCheckoutResult::InvalidRequest;
	if (Completed.Contains(ID)) return EShopCheckoutResult::AlreadyCompleted;
	if (const FShopCheckoutView* Existing = ActiveCheckouts.FindByPredicate([ID](const auto& C) { return C.CheckoutID == ID; }))
	{
		TArray<int32> Previous; for (const auto& Q : Existing->Items) Previous.Add(Q.ISID);
		return Existing->CustomerID == Customer && Previous == Items ? EShopCheckoutResult::Success : EShopCheckoutResult::OperationConflict;
	}
	FShopCheckoutView Checkout; Checkout.CheckoutID = ID; Checkout.CustomerID = Customer;
	TSet<int32> UniqueItems;
	for (int32 ISID : Items)
	{
		if (UniqueItems.Contains(ISID) || !IsAvailableShopStock(ISID)) return EShopCheckoutResult::Unavailable;
		UniqueItems.Add(ISID);
		FCheckoutItemQuote Quote;
		const EShopCheckoutResult Result = BuildQuote(ISID, Quote);
		if (Result != EShopCheckoutResult::Success) return Result;
		if (Checkout.TotalCents > MAX_int64 - Quote.SaleCents) return EShopCheckoutResult::InvalidRequest;
		Checkout.TotalCents += Quote.SaleCents; Checkout.Items.Add(Quote);
	}
	for (int32 ISID : Items) Reservations.Add(ISID, ID);
	ActiveCheckouts.Add(Checkout); NotifyChanged();
	return EShopCheckoutResult::Success;
}

EShopCheckoutResult UShopCheckoutComponent::ReceiveTender(FGuid ID, FGuid OperationID, int64 Amount) { return ApplyCash(ID, OperationID, Amount, false); }
EShopCheckoutResult UShopCheckoutComponent::ReturnChange(FGuid ID, FGuid OperationID, int64 Amount) { return ApplyCash(ID, OperationID, Amount, true); }

EShopCheckoutResult UShopCheckoutComponent::ApplyCash(FGuid ID, FGuid OperationID, int64 Amount, bool bChange)
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bCommitting || !OperationID.IsValid() || Amount <= 0) return EShopCheckoutResult::InvalidRequest;
	if (Completed.Contains(ID)) return EShopCheckoutResult::AlreadyCompleted;
	if (const FCashOperation* Prior = CashOperations.Find(OperationID)) return Prior->CheckoutID == ID && Prior->Amount == Amount && Prior->bChange == bChange ? EShopCheckoutResult::Success : EShopCheckoutResult::OperationConflict;
	FShopCheckoutView* Checkout = ActiveCheckouts.FindByPredicate([ID](const auto& C) { return C.CheckoutID == ID; });
	if (!Checkout) return EShopCheckoutResult::NotFound;
	int64& Target = bChange ? Checkout->ChangeCents : Checkout->TenderCents;
	if (Target > MAX_int64 - Amount || (bChange && Amount > Checkout->TenderCents - Checkout->ChangeCents)) return EShopCheckoutResult::InvalidRequest;
	Target += Amount;
	FCashOperation Cash; Cash.CheckoutID = ID; Cash.Amount = Amount; Cash.bChange = bChange;
	CashOperations.Add(OperationID, Cash);
	NotifyChanged();
	return EShopCheckoutResult::Success;
}

EShopCheckoutResult UShopCheckoutComponent::CompleteCheckout(FGuid ID)
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bCommitting) return EShopCheckoutResult::Unavailable;
	if (Completed.Contains(ID)) return EShopCheckoutResult::AlreadyCompleted;
	const FShopCheckoutView* Pending = ActiveCheckouts.FindByPredicate([ID](const auto& C) { return C.CheckoutID == ID; });
	if (!Pending) return EShopCheckoutResult::NotFound;
	const FShopCheckoutView Checkout = *Pending;
	if (Checkout.TenderCents < Checkout.TotalCents) return EShopCheckoutResult::Unpaid;
	if (Checkout.ChangeCents != Checkout.TenderCents - Checkout.TotalCents) return EShopCheckoutResult::IncorrectChange;
	for (const FCheckoutItemQuote& Item : Checkout.Items)
	{
		const FShopStockEntry* Entry = Stock.Find(Item.ISID);
		const FGuid* Reservation = Reservations.Find(Item.ISID);
		FCheckoutItemQuote Current;
		const EShopCheckoutResult Result = BuildQuote(Item.ISID, Current);
		if (Result != EShopCheckoutResult::Success) return Result;
		if (!Entry || Entry->CustomerOwner.IsValid() || !Reservation || *Reservation != ID || Current.EventSequence != Item.EventSequence || Current.ProductID != Item.ProductID || Current.ContentID != Item.ContentID) return EShopCheckoutResult::QuoteChanged;
	}
	APropManager* Manager = Cast<APropManager>(GetOwner());
	if (!Manager || !Manager->SharedWallet) return EShopCheckoutResult::WalletFailure;
	TGuardValue<bool> Guard(bCommitting, true);
	FCompletedCheckoutReceipt Receipt; Receipt.CheckoutID = ID; Receipt.CustomerID = Checkout.CustomerID; Receipt.TotalCents = Checkout.TotalCents;
	for (const FCheckoutItemQuote& Item : Checkout.Items)
	{
		Stock.FindChecked(Item.ISID).CustomerOwner = Checkout.CustomerID;
		Receipt.ItemISIDs.Add(Item.ISID);
	}
	Completed.Add(ID, Receipt);
	const EWalletOperationResult WalletResult = Checkout.TotalCents == 0 ? EWalletOperationResult::Applied : Manager->SharedWallet->Credit(ID, Checkout.TotalCents, TEXT("Checkout"));
	if (WalletResult != EWalletOperationResult::Applied)
	{
		Completed.Remove(ID);
		for (int32 ISID : Receipt.ItemISIDs) Stock.FindChecked(ISID).CustomerOwner.Invalidate();
		return EShopCheckoutResult::WalletFailure;
	}
	for (int32 ISID : Receipt.ItemISIDs) Reservations.Remove(ISID);
	ActiveCheckouts.RemoveAll([ID](const auto& C) { return C.CheckoutID == ID; });
	ForgetCashOperations(ID);
	NotifyChanged(); OnSaleCompleted.Broadcast(Receipt);
	return EShopCheckoutResult::Success;
}

bool UShopCheckoutComponent::CancelCheckout(FGuid ID)
{
	if (!IsServer() || bCommitting) return false;
	const FShopCheckoutView* Checkout = ActiveCheckouts.FindByPredicate([ID](const auto& C) { return C.CheckoutID == ID; });
	if (!Checkout) return false;
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

void UShopCheckoutComponent::EndPlay(const EEndPlayReason::Type Reason) { CancelAllCheckouts(); Super::EndPlay(Reason); }

bool UShopCheckoutComponent::GetCanceledStockTransform(int32 ISID, FTransform& Out) const
{
	const FShopStockEntry* Entry = Stock.Find(ISID);
	if (!Entry || !Reservations.Contains(ISID) || Entry->CustomerOwner.IsValid()) return false;
	Out = Entry->StockTransform; return true;
}

FShopCheckoutSaveData UShopCheckoutComponent::ExportCommittedState() const
{
	FShopCheckoutSaveData Saved; Stock.GenerateValueArray(Saved.Stock); Completed.GenerateValueArray(Saved.Completed); return Saved;
}

bool UShopCheckoutComponent::ValidateSaveData(const FShopCheckoutSaveData& Saved)
{
	TSet<int32> StockIDs; TSet<FGuid> TransactionIDs;
	for (const auto& Entry : Saved.Stock)
	{
		if (Entry.ISID <= 0 || StockIDs.Contains(Entry.ISID) || Entry.OriginalPurchaseCents < 0 || Entry.StockTransform.ContainsNaN()) return false;
		StockIDs.Add(Entry.ISID);
	}
	for (const auto& Receipt : Saved.Completed)
	{
		if (!Receipt.CheckoutID.IsValid() || !Receipt.CustomerID.IsValid() || Receipt.TotalCents < 0 || Receipt.ItemISIDs.IsEmpty() || TransactionIDs.Contains(Receipt.CheckoutID)) return false;
		TransactionIDs.Add(Receipt.CheckoutID);
	}
	return true;
}

bool UShopCheckoutComponent::RestoreCommittedState(const FShopCheckoutSaveData& Saved)
{
	if (!IsServer() || bCommitting || !ValidateSaveData(Saved)) return false;
	CancelAllCheckouts(); Stock.Empty(); Completed.Empty(); CashOperations.Empty(); Reservations.Empty();
	for (const auto& Entry : Saved.Stock) Stock.Add(Entry.ISID, Entry);
	for (const auto& Receipt : Saved.Completed) Completed.Add(Receipt.CheckoutID, Receipt);
	NotifyChanged(); return true;
}

void UShopCheckoutComponent::ForgetCashOperations(FGuid ID)
{
	for (auto It = CashOperations.CreateIterator(); It; ++It) if (It.Value().CheckoutID == ID) It.RemoveCurrent();
}
