#include "Shop/ShopCheckoutComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Shop/ShopInteractionGeometry.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Templates/UnrealTemplate.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"

bool UShopCheckoutComponent::IsSupportedDenomination(int32 D)
{
	return D == 1 || D == 2 || D == 5 || D == 10 || D == 20 || D == 50 || D == 100;
}

bool UShopCheckoutComponent::GetCoin(int32 ISID, FShopCashCoin& Out) const
{
	if (const auto* Coin = Coins.Find(ISID)) { Out = *Coin; return true; }
	Out = FShopCashCoin(); return false;
}

bool UShopCheckoutComponent::ShouldPersistInstance(int32 ISID) const
{
	const auto* Coin = Coins.Find(ISID);
	return !Coin || !Coin->CustomerOwner.IsValid();
}

int64 UShopCheckoutComponent::GetTeamCashCents() const
{
	if (!IsServer()) return TeamCashCents;
	int64 Sum = 0;
	for (const auto& Pair : Coins) if (!Pair.Value.CustomerOwner.IsValid()) Sum += int64(Pair.Value.Dollars) * 100;
	return Sum;
}

int64 UShopCheckoutComponent::GetOutstandingBankDebt() const
{
	int64 Sum = 0;
	for (const auto& Case : ExpiryCases)
	{
		const int64 Automatic = Case.OriginalBaseCents - Case.AutomaticPaidCents;
		const int64 Compensation = Case.Choice == EShopCompensationChoice::AdditionalDeduction ? Case.OriginalBaseCents - Case.CompensationPaidCents : 0;
		if (Sum > MAX_int64 - Automatic || Sum + Automatic > MAX_int64 - Compensation) return MAX_int64;
		Sum += Automatic + Compensation;
	}
	return Sum;
}

EShopCheckoutResult UShopCheckoutComponent::WithdrawCoins(FGuid ID, const TArray<int32>& Values, const FTransform& Output, TArray<int32>& Out, bool bStartPhysics)
{
	return CreateCoins(ID, FGuid(), Values, Output, true, Out, bStartPhysics);
}

EShopCheckoutResult UShopCheckoutComponent::IssueCustomerCoins(FGuid ID, FGuid Customer, const TArray<int32>& Values, const FTransform& Output, TArray<int32>& Out)
{
	if (!Customer.IsValid()) { Out.Reset(); return EShopCheckoutResult::InvalidRequest; }
	return CreateCoins(ID, Customer, Values, Output, false, Out);
}

TArray<int32> UShopCheckoutComponent::GetCustomerCoinISIDs(FGuid Customer) const
{
	TArray<int32> Result;
	if (IsServer() && Customer.IsValid()) for (const auto& Pair : Coins) if (Pair.Value.CustomerOwner == Customer) Result.Add(Pair.Key);
	return Result;
}

bool UShopCheckoutComponent::ReclaimCustomerCoins(FGuid Customer, const TArray<int32>& IDs)
{
	auto* Manager=Cast<APropManager>(GetOwner());
	if (!IsServer() || bCommitting || !Manager || !Customer.IsValid()) return false;
	TSet<int32> Seen;
	for (int32 ID:IDs)
	{
		const auto* Coin=Coins.Find(ID);
		if (!Coin || Coin->CustomerOwner!=Customer || Coin->CheckoutID.IsValid() || Seen.Contains(ID)) return false;
		Seen.Add(ID);
	}
	for (int32 ID:IDs) { Manager->ReleaseInstanceToEmpty(ID); Coins.Remove(ID); }
	return true;
}

EShopCheckoutResult UShopCheckoutComponent::CreateCoins(FGuid ID, FGuid Customer, const TArray<int32>& Values, const FTransform& Output, bool bWithdraw, TArray<int32>& Out, bool bStartPhysics)
{
	Out.Reset();
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bCommitting || !ID.IsValid() || Values.IsEmpty() || Values.Num() > FMath::Clamp(MaxCoinsPerOperation, 1, 256) || Output.ContainsNaN()) return EShopCheckoutResult::InvalidRequest;
	const auto Kind = bWithdraw ? EShopCashOperationKind::ATMWithdrawal : EShopCashOperationKind::CustomerTender;
	if (const auto* Prior = PhysicalCashOperations.Find(ID))
	{
		if (Prior->Kind != Kind || Prior->CustomerID != Customer || Prior->CoinDollars != Values) return EShopCheckoutResult::OperationConflict;
		Out = Prior->CoinISIDs; return EShopCheckoutResult::Success;
	}
	APropManager* Manager = Cast<APropManager>(GetOwner());
	if (!Manager || !Manager->SharedWallet) return EShopCheckoutResult::Unavailable;
	TGuardValue<bool> Guard(bCommitting, true);
	int64 Amount = 0;
	TArray<const FShopCurrencyRecipe*> Recipes;
	TArray<TStrongObjectPtr<UInteractablePropDataAsset>> Products;
	FVector CellSize=FVector::ZeroVector;
	for (int32 D : Values)
	{
		if (!IsSupportedDenomination(D)) return EShopCheckoutResult::InvalidRequest;
		const auto* Recipe = CurrencyRecipes.FindByPredicate([D](const auto& R) { return R.Dollars == D; });
		UInteractablePropDataAsset* Product = Recipe ? Recipe->Product.LoadSynchronous() : nullptr;
		if (!Product || !Product->StaticMesh || !FMath::IsFinite(Recipe->MeshScale) || Recipe->MeshScale <= 0
			|| Product->bCanConsume || Product->bHasExpiry || Product->NumOfPortions != 0) return EShopCheckoutResult::CashUnavailable;
		const UBodySetup* Body = Product->StaticMesh->GetBodySetup();
		if (!Body || Body->CollisionTraceFlag == CTF_UseComplexAsSimple || Body->AggGeom.GetElementCount() == 0) return EShopCheckoutResult::CashUnavailable;
		CellSize=CellSize.ComponentMax(ShopInteractionGeometry::MeshGridSpacing(Product->StaticMesh,Output.GetScale3D()*Recipe->MeshScale));
		Recipes.Add(Recipe); Products.Emplace(Product); Amount += int64(D) * 100;
	}
	if (bWithdraw && Amount > Manager->SharedWallet->GetWalletState().BalanceCents - FMath::Min(GetOutstandingBankDebt(), Manager->SharedWallet->GetWalletState().BalanceCents)) return EShopCheckoutResult::WalletFailure;
	TArray<FShopCashCoin> NewCoins;
	for (int32 I = 0; I < Values.Num(); ++I)
	{
		const int32 PID = Manager->RegisterProductFromDataAsset(Products[I].Get());
		const FTransform Transform=ShopInteractionGeometry::GridItemTransform(Output,CellSize,I,Values.Num(),FVector(Recipes[I]->MeshScale));
		const int32 ISID = PID > 0 ? Manager->GenerateProductInstance(PID, Transform) : 0;
		if (ISID <= 0)
		{
			for (const auto& Coin : NewCoins) Manager->ReleaseInstanceToEmpty(Coin.ISID);
			return EShopCheckoutResult::CashUnavailable;
		}
		if (bStartPhysics)
		{
			AActivePropActor* Active = nullptr;
			if (!Manager->PromoteInstanceToASM(ISID, EPropActivationReason::Drop, true, Active))
			{
				Manager->ReleaseInstanceToEmpty(ISID);
				for (const auto& Coin : NewCoins) Manager->ReleaseInstanceToEmpty(Coin.ISID);
				return EShopCheckoutResult::CashUnavailable;
			}
		}
		FShopCashCoin Coin; Coin.ISID = ISID; Coin.ProductID = PID; Coin.Dollars = Values[I]; Coin.CustomerOwner = Customer; Coin.SourceOperationID = ID;
		NewCoins.Add(Coin);
	}
	FShopCashOperation Op; Op.OperationID = ID; Op.Kind = Kind; Op.CustomerID = Customer; Op.CoinDollars = Values; Op.AmountCents = Amount;
	Op.Sequence = int64(PhysicalCashOperations.Num()) + 1;
	for (const auto& Coin : NewCoins) { Op.CoinISIDs.Add(Coin.ISID); Op.CoinProductIDs.Add(Coin.ProductID); }
	auto Commit = [&]()
	{
		for (const auto& Coin : NewCoins) Coins.Add(Coin.ISID, Coin);
		PhysicalCashOperations.Add(ID, Op); return true;
	};
	const bool bCommitted = bWithdraw ? Manager->SharedWallet->CommitWithResources(ID, -Amount, TEXT("ATMWithdrawal"), Commit) == EWalletOperationResult::Applied : Commit();
	if (!bCommitted)
	{
		for (const auto& Coin : NewCoins) Manager->ReleaseInstanceToEmpty(Coin.ISID);
		return EShopCheckoutResult::WalletFailure;
	}
	Out = Op.CoinISIDs; NotifyChanged(); return EShopCheckoutResult::Success;
}

EShopCheckoutResult UShopCheckoutComponent::DepositCoins(FGuid ID, const TArray<int32>& Items)
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bCommitting || !ID.IsValid() || Items.IsEmpty() || Items.Num() > FMath::Clamp(MaxCoinsPerOperation, 1, 256)) return EShopCheckoutResult::InvalidRequest;
	if (const auto* Prior = PhysicalCashOperations.Find(ID)) return Prior->Kind == EShopCashOperationKind::ATMDeposit && Prior->CoinISIDs == Items ? EShopCheckoutResult::Success : EShopCheckoutResult::OperationConflict;
	APropManager* Manager = Cast<APropManager>(GetOwner());
	if (!Manager || !Manager->SharedWallet) return EShopCheckoutResult::Unavailable;
	FShopCashOperation Op; Op.OperationID = ID; Op.Kind = EShopCashOperationKind::ATMDeposit; Op.CoinISIDs = Items; Op.Sequence = int64(PhysicalCashOperations.Num()) + 1;
	TSet<int32> Unique;
	for (int32 ISID : Items)
	{
		const auto* Coin = Coins.Find(ISID);
		const auto* Record = Manager->FindISIDData(ISID);
		if (!Coin || !Record || Record->ProductID != Coin->ProductID || Coin->CustomerOwner.IsValid() || Coin->CheckoutID.IsValid() || Unique.Contains(ISID)) return EShopCheckoutResult::Unavailable;
		Unique.Add(ISID); Op.AmountCents += int64(Coin->Dollars) * 100; Op.CoinDollars.Add(Coin->Dollars); Op.CoinProductIDs.Add(Coin->ProductID);
	}
	TGuardValue<bool> Guard(bCommitting, true);
	// No callbacks/ticks between the complete preflight and retirement; each ID is known to exist.
	const auto Result = Manager->SharedWallet->CommitWithResources(ID, Op.AmountCents, TEXT("ATMDeposit"), [&]()
	{
		for (int32 ISID : Items) { verify(Manager->ReleaseInstanceToEmpty(ISID)); Coins.Remove(ISID); }
		PhysicalCashOperations.Add(ID, Op); return true;
	});
	if (Result != EWalletOperationResult::Applied) return EShopCheckoutResult::WalletFailure;
	EnsureWalletBinding(); NotifyChanged();
	// The wallet delegate observes coherent cash state. Collection is deferred while this guard is held.
	GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this] { CollectOutstandingDeductions(); }));
	return EShopCheckoutResult::Success;
}

EShopCheckoutResult UShopCheckoutComponent::ReconcilePhysicalCash(FGuid ID, FGuid Register, const TArray<int32>& Tender, const TArray<int32>& Change)
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bCommitting || !Register.IsValid() || Tender.Num() + Change.Num() > 256) return EShopCheckoutResult::InvalidRequest;
	auto* View = ActiveCheckouts.FindByPredicate([ID](const auto& C) { return C.CheckoutID == ID; });
	if (!View) return EShopCheckoutResult::NotFound;
	if (const auto* Prior = CashAllocations.Find(ID)) if (Prior->RegisterID != Register) return EShopCheckoutResult::OperationConflict;
	APropManager* Manager = Cast<APropManager>(GetOwner());
	TSet<int32> Unique;
	int64 TenderValue = 0, ChangeValue = 0;
	for (int32 Stage = 0; Stage < 2; ++Stage) for (int32 ISID : Stage == 0 ? Tender : Change)
	{
		const auto* Coin = Coins.Find(ISID);
		const auto* Record = Manager ? Manager->FindISIDData(ISID) : nullptr;
		if (!Coin || !Record || Record->ProductID != Coin->ProductID || Unique.Contains(ISID)
			|| (Coin->CheckoutID.IsValid() && Coin->CheckoutID != ID)
			|| (Stage == 0 ? Coin->CustomerOwner != View->CustomerID : Coin->CustomerOwner.IsValid())) return EShopCheckoutResult::Unavailable;
		Unique.Add(ISID); (Stage == 0 ? TenderValue : ChangeValue) += int64(Coin->Dollars) * 100;
	}
	const bool bChanged = View->TenderCents != TenderValue || View->ChangeCents != ChangeValue;
	ReleasePhysicalCash(ID);
	FShopPhysicalCashAllocation Allocation; Allocation.RegisterID = Register; Allocation.Tender = Tender; Allocation.Change = Change;
	CashAllocations.Add(ID, Allocation);
	for (int32 ISID : Tender) { auto& Coin = Coins.FindChecked(ISID); Coin.CheckoutID = ID; Coin.RegisterID = Register; Coin.Custody = EShopCoinCustody::Tender; }
	for (int32 ISID : Change) { auto& Coin = Coins.FindChecked(ISID); Coin.CheckoutID = ID; Coin.RegisterID = Register; Coin.Custody = EShopCoinCustody::Change; }
	View->TenderCents = TenderValue; View->ChangeCents = ChangeValue;
	if (bChanged) NotifyChanged();
	return EShopCheckoutResult::Success;
}

void UShopCheckoutComponent::ReleasePhysicalCash(FGuid ID)
{
	const auto* Allocation = CashAllocations.Find(ID);
	if (!Allocation) return;
	for (int32 ISID : Allocation->Tender) if (auto* Coin = Coins.Find(ISID)) { Coin->CheckoutID.Invalidate(); Coin->RegisterID.Invalidate(); Coin->Custody = EShopCoinCustody::World; }
	for (int32 ISID : Allocation->Change) if (auto* Coin = Coins.Find(ISID)) { Coin->CheckoutID.Invalidate(); Coin->RegisterID.Invalidate(); Coin->Custody = EShopCoinCustody::World; }
	CashAllocations.Remove(ID);
}

bool UShopCheckoutComponent::ValidatePhysicalPayment(const FShopCheckoutView& View) const
{
	const auto* Allocation = CashAllocations.Find(View.CheckoutID);
	if (!Allocation) return View.TotalCents == 0 && View.TenderCents == 0 && View.ChangeCents == 0;
	const APropManager* Manager = Cast<APropManager>(GetOwner());
	int64 Tender = 0, Change = 0; TSet<int32> Unique;
	for (int32 Stage = 0; Stage < 2; ++Stage) for (int32 ISID : Stage == 0 ? Allocation->Tender : Allocation->Change)
	{
		const auto* Coin = Coins.Find(ISID); const auto* Record = Manager ? Manager->FindISIDData(ISID) : nullptr;
		if (!Coin || !Record || Coin->ProductID != Record->ProductID || Unique.Contains(ISID) || Coin->CheckoutID != View.CheckoutID
			|| Coin->RegisterID != Allocation->RegisterID || (Stage == 0 ? Coin->CustomerOwner != View.CustomerID : Coin->CustomerOwner.IsValid())) return false;
		Unique.Add(ISID); (Stage == 0 ? Tender : Change) += int64(Coin->Dollars) * 100;
	}
	return Tender == View.TenderCents && Change == View.ChangeCents && Tender - Change == View.TotalCents;
}

void UShopCheckoutComponent::CommitPhysicalPayment(const FShopCheckoutView& View)
{
	const auto* Allocation = CashAllocations.Find(View.CheckoutID);
	if (!Allocation) return; // A zero-dollar sale does not require coins.
	FShopCashOperation Op; Op.OperationID = View.CheckoutID; Op.Kind = EShopCashOperationKind::CheckoutTransfer; Op.CustomerID = View.CustomerID;
	Op.Sequence = int64(PhysicalCashOperations.Num()) + 1; Op.AmountCents = View.TotalCents; Op.TenderCoinCount = Allocation->Tender.Num();
	for (int32 ISID : Allocation->Tender)
	{
		auto& Coin = Coins.FindChecked(ISID); Coin.CustomerOwner.Invalidate(); Coin.CheckoutID.Invalidate(); Coin.Custody = EShopCoinCustody::Drawer;
		Op.CoinISIDs.Add(ISID); Op.CoinDollars.Add(Coin.Dollars); Op.CoinProductIDs.Add(Coin.ProductID);
	}
	for (int32 ISID : Allocation->Change)
	{
		auto& Coin = Coins.FindChecked(ISID); Coin.CustomerOwner = View.CustomerID; Coin.CheckoutID.Invalidate(); Coin.RegisterID.Invalidate(); Coin.Custody = EShopCoinCustody::World;
		Op.CoinISIDs.Add(ISID); Op.CoinDollars.Add(Coin.Dollars); Op.CoinProductIDs.Add(Coin.ProductID);
	}
	PhysicalCashOperations.Add(Op.OperationID, Op); CashAllocations.Remove(View.CheckoutID);
}

bool UShopCheckoutComponent::ValidateCashSaveData(const FShopCashSaveData& Saved)
{
	struct FHistoryCoin { int32 Dollars; int32 ProductID; FGuid Owner; FGuid Source; };
	TMap<int32, FHistoryCoin> History;
	TSet<int32> EverCreated; TSet<FGuid> Operations;
	int64 ExpectedSequence = 1;
	for (const auto& Op : Saved.Operations)
	{
		if (!Op.OperationID.IsValid() || Operations.Contains(Op.OperationID) || Op.Sequence != ExpectedSequence++ || Op.CoinISIDs.IsEmpty()
			|| Op.CoinISIDs.Num() != Op.CoinDollars.Num() || Op.CoinISIDs.Num() != Op.CoinProductIDs.Num() || Op.CoinISIDs.Num() > 256 || Op.AmountCents < 0
			|| static_cast<uint8>(Op.Kind) > static_cast<uint8>(EShopCashOperationKind::CheckoutTransfer)) return false;
		const bool bSale = Op.Kind == EShopCashOperationKind::CheckoutTransfer;
		if ((bSale && (!Op.CustomerID.IsValid() || Op.TenderCoinCount < 0 || Op.TenderCoinCount > Op.CoinISIDs.Num()))
			|| (!bSale && Op.TenderCoinCount != 0)
			|| (Op.Kind == EShopCashOperationKind::CustomerTender ? !Op.CustomerID.IsValid() : !bSale && Op.CustomerID.IsValid())) return false;
		int64 Sum = 0; TSet<int32> Unique;
		for (int32 I = 0; I < Op.CoinISIDs.Num(); ++I)
		{
			const int32 ID = Op.CoinISIDs[I], D = Op.CoinDollars[I];
			const int32 PID = Op.CoinProductIDs[I];
			if (ID <= 0 || PID <= 0 || !IsSupportedDenomination(D) || Unique.Contains(ID)) return false;
			Unique.Add(ID);
			if (Op.Kind == EShopCashOperationKind::ATMWithdrawal || Op.Kind == EShopCashOperationKind::CustomerTender)
			{
				if (EverCreated.Contains(ID)) return false;
				History.Add(ID, {D, PID, Op.CustomerID, Op.OperationID}); EverCreated.Add(ID); Sum += int64(D) * 100;
			}
			else
			{
				auto* Coin = History.Find(ID);
				if (!Coin || Coin->Dollars != D || Coin->ProductID != PID) return false;
				if (!bSale) { if (Coin->Owner.IsValid()) return false; History.Remove(ID); Sum += int64(D) * 100; }
				else if (I < Op.TenderCoinCount) { if (Coin->Owner != Op.CustomerID) return false; Coin->Owner.Invalidate(); Sum += int64(D) * 100; }
				else { if (Coin->Owner.IsValid()) return false; Coin->Owner = Op.CustomerID; Sum -= int64(D) * 100; }
			}
		}
		if (Sum != Op.AmountCents) return false;
		Operations.Add(Op.OperationID);
	}
	TSet<int32> Current;
	for (const auto& Coin : Saved.Coins)
	{
		const auto* Expected = History.Find(Coin.ISID);
		if (!Expected || Current.Contains(Coin.ISID) || Coin.ProductID != Expected->ProductID || Coin.Dollars != Expected->Dollars || Coin.CustomerOwner.IsValid() || Expected->Owner.IsValid()
			|| Coin.SourceOperationID != Expected->Source || Coin.CheckoutID.IsValid()
			|| (Coin.Custody != EShopCoinCustody::World && Coin.Custody != EShopCoinCustody::Drawer)
			|| (Coin.Custody == EShopCoinCustody::Drawer && !Coin.RegisterID.IsValid())) return false;
		Current.Add(Coin.ISID);
	}
	for (const auto& Pair : History) if (!Pair.Value.Owner.IsValid() && !Current.Contains(Pair.Key)) return false;
	return true;
}

bool UShopCheckoutComponent::ValidateCashWorldSaveData(const FPropWorldSaveData& Saved)
{
	TMap<FGuid, const FWalletReceipt*> Bank;
	for (const auto& Receipt : Saved.SharedWallet.Receipts) Bank.Add(Receipt.OperationID, &Receipt);
	for (const auto& Op : Saved.ShopCheckout.Cash.Operations)
	{
		for (int32 CoinID : Op.CoinISIDs) if (CoinID >= Saved.NextISID) return false;
		const bool bBank = Op.Kind == EShopCashOperationKind::ATMWithdrawal || Op.Kind == EShopCashOperationKind::ATMDeposit;
		const auto* Receipt = Bank.Find(Op.OperationID);
		if (bBank)
		{
			const int64 Delta = Op.Kind == EShopCashOperationKind::ATMWithdrawal ? -Op.AmountCents : Op.AmountCents;
			const FName Reason = Op.Kind == EShopCashOperationKind::ATMWithdrawal ? TEXT("ATMWithdrawal") : TEXT("ATMDeposit");
			if (!Receipt || (*Receipt)->DeltaCents != Delta || (*Receipt)->Reason != Reason) return false;
		}
		else if (Receipt) return false; // A cash sale/customer tender may never also credit the bank.
	}
	TMap<int32, const FPropSerializedInstance*> Instances;
	for (const auto& Instance : Saved.SavedInstances) { if (Instances.Contains(Instance.ISID)) return false; Instances.Add(Instance.ISID, &Instance); }
	for (const auto& Coin : Saved.ShopCheckout.Cash.Coins)
	{
		const auto* Instance = Instances.Find(Coin.ISID);
		if (!Instance || (*Instance)->ProductID != Coin.ProductID) return false;
		const auto* Product = Saved.ProductDefinitions.FindByPredicate([&Coin](const auto& P) { return P.ProductID == Coin.ProductID; });
		if (!Product || !Product->StaticMesh || Product->InteractionProfile.bCanConsume || Product->ConsumableProfile.NumOfPortions != 0) return false;
	}
	for (const auto& Receipt : Saved.SharedWallet.Receipts) if (Receipt.Reason == TEXT("ATMWithdrawal") || Receipt.Reason == TEXT("ATMDeposit"))
		if (!Saved.ShopCheckout.Cash.Operations.ContainsByPredicate([&Receipt](const auto& Op) { return Op.OperationID == Receipt.OperationID; })) return false;
	return true;
}
