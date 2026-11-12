#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopInstanceVolumeQuery.h"
#include "Props/InteractablePropManager.h"
#include "Components/BoxComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"

void UShopScannerComponent::UpdateCartTotal()
{
	FShopCheckoutView& Cart = ScannerState.Checkout;
	Cart.DetailedTotalCents = 0;
	for (const auto& Item : Cart.Items) Cart.DetailedTotalCents += Item.SaleCents;
	Cart.TotalCents = UColaPricingLibrary::RoundBillToWholeDollar(Cart.DetailedTotalCents);
	Cart.RoundingAdjustmentCents = Cart.TotalCents - Cart.DetailedTotalCents;
}

void UShopScannerComponent::RebuildGroups()
{
	ScannerState.Groups.Reset();
	for (const auto& Item : ScannerState.Checkout.Items)
	{
		FShopGroupedEntry* Group = ScannerState.Groups.FindByPredicate([&Item](const auto& G) { return G.ProductID == Item.ProductID && G.ContentID == Item.ContentID; });
		if (!Group)
		{
			FShopGroupedEntry NewGroup; NewGroup.ProductID = Item.ProductID; NewGroup.ContentID = Item.ContentID; NewGroup.DisplayName = Item.Snapshot.DisplayName;
			ScannerState.Groups.Add(NewGroup); Group = &ScannerState.Groups.Last();
		}
		Group->BottleISIDs.Add(Item.ISID);
		Group->ExpiredCount += (Item.bExpiredAtSale || Item.Snapshot.Price.bExpired) ? 1 : 0;
		Group->OriginalSubtotalCents += Item.Snapshot.Price.OriginalBaseCents;
		Group->ContentDiscountCents += Item.Snapshot.Price.ContentDiscountCents;
		Group->TemperatureDiscountCents += Item.Snapshot.Price.TemperatureDiscountCents;
		Group->FinalSubtotalCents += Item.SaleCents;
	}
}

EShopCheckoutResult UShopScannerComponent::SaveProducts()
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bNotifying || ScannerState.Phase != EShopScannerPhase::Scanning) return EShopCheckoutResult::Unavailable;
	if (ScannerState.Checkout.Items.IsEmpty()) return SetResult(EShopCheckoutResult::NotFound);
	for (auto& Item : ScannerState.Checkout.Items) Item.bSaved = true;
	return SetResult(EShopCheckoutResult::Success);
}

EShopCheckoutResult UShopScannerComponent::SetProductInVolume(int32 ISID, bool bInside)
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (ISID <= 0) return EShopCheckoutResult::InvalidRequest;
	if (bNotifying || ScannerState.Phase != EShopScannerPhase::Scanning) return EShopCheckoutResult::Unavailable;
	if (bInside)
	{
		LiveISIDs.Add(ISID);
		return SuppressedISIDs.Contains(ISID) ? EShopCheckoutResult::Success : ScanProduct(ISID);
	}
	LiveISIDs.Remove(ISID); SuppressedISIDs.Remove(ISID);
	const int32 Index = ScannerState.Checkout.Items.IndexOfByPredicate([ISID](const auto& Q) { return Q.ISID == ISID; });
	if (Index == INDEX_NONE) return EShopCheckoutResult::NotFound;
	if (ScannerState.Checkout.Items[Index].bSaved) ScannerState.Checkout.Items[Index].bInScannerVolume = false;
	else ScannerState.Checkout.Items.RemoveAt(Index);
	UpdateCartTotal();
	return SetResult(EShopCheckoutResult::Success);
}

void UShopScannerComponent::RefreshLiveProducts()
{
	if (!IsServer() || bNotifying || !IsValid(ScanVolume)) return;
	APropManager* ManagerPtr = ResolveManager();
	UShopCheckoutComponent* Checkout = ResolveCheckout();
	if (!ManagerPtr || !Checkout) return;
	if (ScannerState.Phase == EShopScannerPhase::Completed)
	{
		TArray<FShopProductScanData> Candidates; TArray<int32> IDs = QueryVolumeISIDs().Array(); IDs.Sort();
		for (int32 ISID : IDs)
		{
			FShopProductScanData Data;
			if (Checkout->IsAvailableShopStock(ISID) && ManagerPtr->GetProductScanData(ISID, Data) && !Data.Price.bExpired && Data.ContentLevel >= 1 && Candidates.Num() < 64) Candidates.Add(Data);
		}
		bool bChanged = Candidates.Num() != ScannerState.ReplacementCandidates.Num();
		if (!bChanged) for (int32 I = 0; I < Candidates.Num(); ++I) if (Candidates[I].ISID != ScannerState.ReplacementCandidates[I].ISID) { bChanged = true; break; }
		ScannerState.ReplacementCandidates = MoveTemp(Candidates);
		if (bChanged) PublishState();
		return;
	}
	if (ScannerState.Phase != EShopScannerPhase::Scanning) return;
	TSet<int32> Inside = QueryVolumeISIDs();
	bool bChanged = false;
	for (auto It = SuppressedISIDs.CreateIterator(); It; ++It) if (!Inside.Contains(*It)) It.RemoveCurrent();
	for (int32 I = ScannerState.Checkout.Items.Num() - 1; I >= 0; --I)
	{
		auto& Item = ScannerState.Checkout.Items[I];
		const bool bInside = Inside.Contains(Item.ISID);
		if (!bInside && !Item.bSaved) { ScannerState.Checkout.Items.RemoveAt(I); bChanged = true; continue; }
		FCheckoutItemQuote Current;
		const bool bAvailable = Checkout->IsAvailableShopStock(Item.ISID) && Checkout->BuildQuote(Item.ISID, Current) == EShopCheckoutResult::Success;
		if (Item.bAvailable != bAvailable || Item.bInScannerVolume != bInside) bChanged = true;
		if (bAvailable)
		{
			if (Item.SaleCents != Current.SaleCents || Item.EventSequence != Current.EventSequence || Item.Snapshot.Price.bExpired != Current.Snapshot.Price.bExpired
				|| Item.Snapshot.Temperature != Current.Snapshot.Temperature || Item.Snapshot.Strength != Current.Snapshot.Strength) bChanged = true;
			Current.bSaved = Item.bSaved; Current.bInScannerVolume = bInside; Item = Current;
		}
		else { Item.bAvailable = false; Item.bInScannerVolume = bInside; }
	}
	TArray<int32> IDs = Inside.Array(); IDs.Sort();
	for (int32 ISID : IDs)
	{
		if (SuppressedISIDs.Contains(ISID) || ScannerState.Checkout.Items.ContainsByPredicate([ISID](const auto& Q) { return Q.ISID == ISID; })) continue;
		if (ScannerState.Checkout.Items.Num() >= FMath::Clamp(MaxCartItems, 1, 64)) break;
		FCheckoutItemQuote Quote;
		if (!Checkout->IsAvailableShopStock(ISID) || Checkout->BuildQuote(ISID, Quote) != EShopCheckoutResult::Success) continue;
		Quote.bInScannerVolume = true; ScannerState.Checkout.Items.Add(Quote); bChanged = true;
		ScannerState.LastScan = Quote.Snapshot;
	}
	LiveISIDs = MoveTemp(Inside);
	if (bChanged) { UpdateCartTotal(); PublishState(); }
}

TSet<int32> UShopScannerComponent::QueryVolumeISIDs() const
{
	TSet<int32> Inside;
	if (!IsServer() || !IsValid(ScanVolume) || ScanVolume->GetWorld() != GetWorld()) return Inside;
	return ShopInstanceVolumeQuery::Query(ScanVolume);
}

bool UShopScannerComponent::IsProductPhysicallyInside(int32 ISID) const { return QueryVolumeISIDs().Contains(ISID); }
