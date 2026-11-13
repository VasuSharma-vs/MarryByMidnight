#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopCustomerCashComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "Props/InteractablePropManager.h"

TArray<int32> UShopCheckoutComponent::GetCustomerOwnedStockISIDs(FGuid Customer) const
{
	TArray<int32> Result;
	if (IsServer() && Customer.IsValid()) for (const auto& Pair:Stock) if (Pair.Value.CustomerOwner==Customer) Result.Add(Pair.Key);
	Result.Sort(); return Result;
}

bool UShopCheckoutComponent::IsAvailableColaStockISID(int32 ISID) const
{
	const auto* Manager=Cast<APropManager>(GetOwner());
	if (!IsServer() || !Manager || !IsAvailableShopStock(ISID)) return false;
	FShopProductScanData Scan;
	if (!Manager->GetProductScanData(ISID,Scan) || Scan.ContentLevel<=0) return false;
	if (Manager->ShopSupplies && Manager->ShopSupplies->IsNormalPrototypeActive()
		&& (!Manager->ShopSupplies->IsNormalContent(Scan.ContentID) || Scan.CapSealed<1)) return false;
	const auto* Product=Manager->FindProductDefinition(Scan.ProductID);
	return Product && Product->InteractionProfile.bCanConsume && Product->ConsumableProfile.ConsumableType==EConsumablePropType::Drink
		&& Product->ConsumableProfile.NumOfPortions>0;
}

TArray<int32> UShopCheckoutComponent::GetAvailableColaStockISIDs() const
{
	TArray<int32> Result;
	const auto* Manager = Cast<APropManager>(GetOwner());
	if (!IsServer() || !Manager) return Result;
	for (const auto& Pair : Stock)
	{
		if (IsAvailableColaStockISID(Pair.Key)) Result.Add(Pair.Key);
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
