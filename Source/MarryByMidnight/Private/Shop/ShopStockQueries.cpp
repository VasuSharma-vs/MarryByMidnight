#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopCustomerCashComponent.h"
#include "Props/InteractablePropManager.h"

TArray<int32> UShopCheckoutComponent::GetAvailableColaStockISIDs() const
{
	TArray<int32> Result;
	const auto* Manager = Cast<APropManager>(GetOwner());
	if (!IsServer() || !Manager) return Result;
	for (const auto& Pair : Stock)
	{
		if (!IsAvailableShopStock(Pair.Key)) continue;
		FShopProductScanData Scan;
		if (!Manager->GetProductScanData(Pair.Key, Scan) || Scan.ContentLevel <= 0) continue;
		const auto* Product = Manager->FindProductDefinition(Scan.ProductID);
		if (Product && Product->InteractionProfile.bCanConsume && Product->ConsumableProfile.ConsumableType == EConsumablePropType::Drink
			&& Product->ConsumableProfile.NumOfPortions > 0) Result.Add(Pair.Key);
	}
	Result.Sort(); return Result;
}

TArray<int32> UShopCustomerCashComponent::BuildShoppingPlan(int32 MaxBottles)
{
	TArray<int32> Products;
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetOwner()->GetNetMode() == NM_Client || VisitBudgetCents < 0) return Products;
	APropManager* Manager = APropManager::Get(this);
	if (!Manager || !Manager->ShopCheckout) return Products;
	PlannedQuantities.Empty();
	TArray<int32> Candidates = Manager->ShopCheckout->GetAvailableColaStockISIDs();
	for (int32 I = Candidates.Num() - 1; I > 0; --I) Candidates.Swap(I, FMath::RandRange(0, I));
	int64 MaximumTotal = 0; int32 Selected = 0;
	const int64 Budget = FMath::Max(int64(0), VisitBudgetCents - SpentCents);
	for (int32 ISID : Candidates)
	{
		if (Selected >= FMath::Clamp(MaxBottles, 1, 64)) break;
		FShopProductScanData Data;
		if (!Manager->GetProductScanData(ISID, Data) || Data.Price.OriginalBaseCents > Budget - MaximumTotal) continue;
		const int64 CandidateTotal = UColaPricingLibrary::RoundBillToWholeDollar(MaximumTotal + Data.Price.OriginalBaseCents);
		if (CandidateTotal < 0 || CandidateTotal > Budget) continue;
		int64 Dollars = CandidateTotal / 100, CoinCount = 0;
		for (int32 D : {100,50,20,10,5,2,1}) { CoinCount += Dollars / D; Dollars %= D; }
		if (CoinCount > FMath::Clamp(Manager->ShopCheckout->MaxCoinsPerOperation, 1, 256)) continue;
		MaximumTotal += Data.Price.OriginalBaseCents;
		if (!PlannedQuantities.Contains(Data.ProductID)) Products.Add(Data.ProductID);
		++PlannedQuantities.FindOrAdd(Data.ProductID); ++Selected;
	}
	return Products;
}

int32 UShopCustomerCashComponent::GetPlannedQuantity(int32 ProductID) const { return PlannedQuantities.FindRef(ProductID); }
