#pragma once
#include "Props/InteractablePropManager.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/SharedTeamWalletComponent.h"

namespace ShopCashTests
{
	inline TArray<int32> Denominations(int64 Cents)
	{
		TArray<int32> Result;
		if (Cents < 0 || Cents % 100 != 0) return Result;
		int64 Dollars = Cents / 100;
		for (int32 D : {100, 50, 20, 10, 5, 2, 1}) while (Dollars >= D && Result.Num() < 256) { Result.Add(D); Dollars -= D; }
		return Result;
	}
	struct FPayment
	{
		APropManager* Manager;
		FGuid CheckoutID, RegisterID = FGuid::NewGuid();
		TArray<int32> Tender, Change;
		FPayment(APropManager* InManager, FGuid ID, int64 Cents) : Manager(InManager), CheckoutID(ID)
		{
			FShopCheckoutView View; Manager->ShopCheckout->GetCheckoutView(ID, View);
			Manager->ShopCheckout->IssueCustomerCoins(FGuid::NewGuid(), View.CustomerID, Denominations(Cents), FTransform(FVector(0, 0, 100)), Tender);
			Recount();
		}
		EShopCheckoutResult Recount() { return Manager->ShopCheckout->ReconcilePhysicalCash(CheckoutID, RegisterID, Tender, Change); }
		void AddChange(int64 Cents)
		{
			// Explicit pre-existing test float: funding and withdrawal cancel, never sale credit.
			Manager->SharedWallet->Credit(FGuid::NewGuid(), Cents, TEXT("TestFloat"));
			TArray<int32> New;
			Manager->ShopCheckout->WithdrawCoins(FGuid::NewGuid(), Denominations(Cents), FTransform(FVector(0, 100, 100)), New);
			Change.Append(New); Recount();
		}
	};
}
