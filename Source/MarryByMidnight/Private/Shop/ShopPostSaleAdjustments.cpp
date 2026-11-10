#include "Shop/ShopCheckoutComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Props/InteractablePropManager.h"
#include "Templates/UnrealTemplate.h"
#include "TimerManager.h"

void UShopCheckoutComponent::BeginPlay()
{
	Super::BeginPlay();
	if (IsServer()) EnsureWalletBinding();
}

void UShopCheckoutComponent::EnsureWalletBinding()
{
	if (APropManager* Manager = Cast<APropManager>(GetOwner()))
		if (IsServer() && Manager->SharedWallet)
			Manager->SharedWallet->OnWalletChanged.AddUniqueDynamic(this, &UShopCheckoutComponent::OnWalletChanged);
}

void UShopCheckoutComponent::OnWalletChanged(FSharedWalletState State)
{
	if (!IsServer() || bCommitting || bCollectionScheduled || bEnding || State.BalanceCents <= 0 || ExpiryCases.IsEmpty()) return;
	// A wallet restore happens before checkout restore during load. Never collect against the old world's cases.
	bCollectionScheduled = true;
	TWeakObjectPtr<UShopCheckoutComponent> WeakThis(this);
	GetWorld()->GetTimerManager().SetTimerForNextTick([WeakThis]()
	{
		if (!WeakThis.IsValid() || WeakThis->bEnding) return;
		WeakThis->bCollectionScheduled = false;
		WeakThis->CollectOutstandingDeductions();
	});
}

void UShopCheckoutComponent::OnRep_Adjustments() { OnAdjustmentsChanged.Broadcast(); }

bool UShopCheckoutComponent::GetExpiryCase(FGuid ID, FShopExpiryCase& Out) const
{
	Out = FShopExpiryCase();
	const auto& Source = IsServer() ? ExpiryCases : PendingExpiryCases;
	if (const auto* Found = Source.FindByPredicate([ID](const auto& C) { return C.CaseID == ID; })) { Out = *Found; return true; }
	return false;
}

bool UShopCheckoutComponent::GetAdjustedRevenue(FGuid ID, int64& Adjusted, int64& Outstanding) const
{
	Adjusted = Outstanding = 0;
	if (!IsServer())
	{
		if (const auto* Summary = RecentSettlements.FindByPredicate([ID](const auto& S) { return S.CheckoutID == ID; }))
		{
			Adjusted = Summary->AdjustedRevenueCents; Outstanding = Summary->OutstandingDeductionCents; return true;
		}
		return false;
	}
	const FCompletedCheckoutReceipt* Receipt = Completed.Find(ID);
	if (!Receipt) return false;
	Adjusted = Receipt->TotalCents;
	for (const auto& Case : ExpiryCases) if (Case.CheckoutID == ID)
	{
		// Revenue recognizes the unavoidable obligation immediately, even when the bank cannot pay yet.
		Adjusted -= Case.OriginalBaseCents;
		if (Case.Choice == EShopCompensationChoice::AdditionalDeduction) Adjusted -= Case.OriginalBaseCents;
		Outstanding += Case.OriginalBaseCents - Case.AutomaticPaidCents;
		if (Case.Choice == EShopCompensationChoice::AdditionalDeduction) Outstanding += Case.OriginalBaseCents - Case.CompensationPaidCents;
	}
	return true;
}

TArray<FCompletedCheckoutReceipt> UShopCheckoutComponent::GetHistoryPage(int32 Offset, int32 Count) const
{
	TArray<FCompletedCheckoutReceipt> Page;
	if (!IsServer() || Offset < 0 || Count <= 0) return Page;
	Completed.GenerateValueArray(Page);
	Page.Sort([](const auto& A, const auto& B) { return A.Sequence > B.Sequence; });
	if (Offset >= Page.Num()) { Page.Reset(); return Page; }
	Page.RemoveAt(0, Offset);
	Page.SetNum(FMath::Min(Page.Num(), FMath::Clamp(Count, 1, 32)));
	return Page;
}

void UShopCheckoutComponent::RefreshPendingCases()
{
	RecentAdjustments.Reset();
	for (int32 I = FMath::Max(0, Adjustments.Num() - 64); I < Adjustments.Num(); ++I) RecentAdjustments.Add(Adjustments[I]);
	PendingExpiryCases.Reset();
	for (const auto& Case : ExpiryCases) if (!Case.IsResolved()) PendingExpiryCases.Add(Case);
	RecentSettlements.Reset();
	for (const auto& Receipt : RecentReceipts)
	{
		FShopSettlementSummary Summary; Summary.CheckoutID = Receipt.CheckoutID;
		GetAdjustedRevenue(Receipt.CheckoutID, Summary.AdjustedRevenueCents, Summary.OutstandingDeductionCents);
		for (const auto& Case : ExpiryCases) if (Case.CheckoutID == Receipt.CheckoutID && Case.Choice == EShopCompensationChoice::Unselected) ++Summary.UnselectedCompensations;
		RecentSettlements.Add(Summary);
	}
}

void UShopCheckoutComponent::NotifyAdjustments()
{
	RefreshPendingCases();
	OnAdjustmentsChanged.Broadcast();
	GetOwner()->ForceNetUpdate();
}

void UShopCheckoutComponent::CollectOutstandingInternal()
{
	APropManager* Manager = Cast<APropManager>(GetOwner());
	if (!Manager || !Manager->SharedWallet) return;
	for (FShopExpiryCase& Case : ExpiryCases)
	{
		for (int32 Stage = 0; Stage < 2; ++Stage)
		{
			if (Stage == 1 && Case.Choice != EShopCompensationChoice::AdditionalDeduction) continue;
			int64& Paid = Stage == 0 ? Case.AutomaticPaidCents : Case.CompensationPaidCents;
			const int64 Amount = FMath::Min(Case.OriginalBaseCents - Paid, Manager->SharedWallet->GetWalletState().BalanceCents);
			if (Amount <= 0) continue;
			FShopPostSaleAdjustment Adjustment;
			Adjustment.AdjustmentID = FGuid::NewGuid(); Adjustment.CaseID = Case.CaseID; Adjustment.CheckoutID = Case.CheckoutID;
			Adjustment.Kind = Stage == 0 ? EShopAdjustmentKind::AutomaticExpiryPenalty : EShopAdjustmentKind::CashCompensation;
			Adjustment.DeductedCents = Amount; Adjustment.Time = Manager->WorldElapsedSeconds;
			// Publish coherent case/history values to wallet callbacks. The mutation guard rejects reentry.
			Paid += Amount; Adjustments.Add(Adjustment);
			if (Manager->SharedWallet->Debit(Adjustment.AdjustmentID, Amount, Stage == 0 ? FName(TEXT("ExpiryPenalty")) : FName(TEXT("ExpiryCompensation"))) != EWalletOperationResult::Applied)
			{
				Paid -= Amount; Adjustments.Pop(); return;
			}
		}
	}
}

void UShopCheckoutComponent::CollectOutstandingDeductions()
{
	if (!IsServer() || bCommitting || bEnding) return;
	TGuardValue<bool> Guard(bCommitting, true);
	EnsureWalletBinding(); CollectOutstandingInternal(); NotifyAdjustments();
}

EShopCheckoutResult UShopCheckoutComponent::ChooseCashCompensation(FGuid ID)
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bCommitting) return EShopCheckoutResult::Unavailable;
	FShopExpiryCase* Case = ExpiryCases.FindByPredicate([ID](const auto& C) { return C.CaseID == ID; });
	if (!Case) return EShopCheckoutResult::NotFound;
	if (Case->Choice == EShopCompensationChoice::Replacement) return EShopCheckoutResult::OperationConflict;
	if (Case->IsResolved()) return EShopCheckoutResult::AlreadyResolved;
	TGuardValue<bool> Guard(bCommitting, true);
	Case->Choice = EShopCompensationChoice::AdditionalDeduction;
	CollectOutstandingInternal(); NotifyAdjustments();
	return EShopCheckoutResult::Success;
}

EShopCheckoutResult UShopCheckoutComponent::ProvideReplacement(FGuid ID, int32 ReplacementISID)
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bCommitting) return EShopCheckoutResult::Unavailable;
	FShopExpiryCase* Case = ExpiryCases.FindByPredicate([ID](const auto& C) { return C.CaseID == ID; });
	if (!Case) return EShopCheckoutResult::NotFound;
	if (Case->Choice == EShopCompensationChoice::AdditionalDeduction) return EShopCheckoutResult::OperationConflict;
	if (Case->Choice == EShopCompensationChoice::Replacement)
		return Case->ReplacementISID == ReplacementISID ? EShopCheckoutResult::AlreadyResolved : EShopCheckoutResult::OperationConflict;
	FCheckoutItemQuote Replacement;
	if (!IsAvailableShopStock(ReplacementISID) || BuildQuote(ReplacementISID, Replacement) != EShopCheckoutResult::Success
		|| Replacement.Snapshot.Price.bExpired || Replacement.ContentID != Case->ContentID
		|| Replacement.Snapshot.NominalPortions != Case->NominalPortions || Replacement.Snapshot.ContentLevel < 1.0f)
		return EShopCheckoutResult::InvalidReplacement;
	APropManager* Manager = Cast<APropManager>(GetOwner());
	if (!Manager) return EShopCheckoutResult::Unavailable;
	TGuardValue<bool> Guard(bCommitting, true);
	Stock.FindChecked(ReplacementISID).CustomerOwner = Case->CustomerID;
	Case->Choice = EShopCompensationChoice::Replacement; Case->ReplacementISID = ReplacementISID;
	FShopPostSaleAdjustment Adjustment;
	Adjustment.AdjustmentID = FGuid::NewGuid(); Adjustment.CaseID = Case->CaseID; Adjustment.CheckoutID = Case->CheckoutID;
	Adjustment.Kind = EShopAdjustmentKind::FreeReplacement; Adjustment.ReplacementISID = ReplacementISID; Adjustment.Time = Manager->WorldElapsedSeconds;
	Adjustments.Add(Adjustment);
	CollectOutstandingInternal(); NotifyAdjustments();
	return EShopCheckoutResult::Success;
}
