#include "Shop/ShopCustomerCashComponent.h"
#include "Shop/ShopCashRegisterComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopInteractionGeometry.h"
#include "Props/InteractablePropManager.h"
#include "Components/BoxComponent.h"
#include "Net/UnrealNetwork.h"

UShopCustomerCashComponent::UShopCustomerCashComponent() { PrimaryComponentTick.bCanEverTick = false; SetIsReplicatedByDefault(true); }
void UShopCustomerCashComponent::BeginPlay()
{
	Super::BeginPlay(); if (GetOwner()->HasAuthority() && GetOwner()->GetNetMode() != NM_Client) CustomerID = FGuid::NewGuid();
}
void UShopCustomerCashComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UShopCustomerCashComponent, CustomerID);
}
EShopCheckoutResult UShopCustomerCashComponent::JoinScanner(UShopScannerComponent* Scanner)
{
	if (!GetOwner()->HasAuthority() || GetOwner()->GetNetMode() == NM_Client) return EShopCheckoutResult::NotAuthority;
	if (!IsValid(Scanner) || Scanner->GetWorld() != GetWorld() || !CustomerID.IsValid()
		|| ShopInteractionGeometry::CustomerDistanceToStation(GetOwner(),Scanner->GetOwner())>350) return EShopCheckoutResult::Unavailable;
	if (Station.IsValid() && Station != Scanner && Station->GetScannerState().Phase != EShopScannerPhase::Completed) return EShopCheckoutResult::Unavailable;
	const auto State = Scanner->GetScannerState();
	if (State.Checkout.CustomerID.IsValid() && State.Checkout.CustomerID != CustomerID) return EShopCheckoutResult::Unavailable;
	const auto Result = Scanner->AssignCustomer(CustomerID); if (Result == EShopCheckoutResult::Success) Station = Scanner;
	return Result;
}
EShopCheckoutResult UShopCustomerCashComponent::PlacePayment()
{
	if (!GetOwner()->HasAuthority() || GetOwner()->GetNetMode() == NM_Client) return EShopCheckoutResult::NotAuthority;
	if (!Station.IsValid() || ShopInteractionGeometry::CustomerDistanceToStation(GetOwner(),Station->GetOwner())>350) return EShopCheckoutResult::Unavailable;
	const auto State = Station->GetScannerState();
	if (State.Phase != EShopScannerPhase::Checkout || State.Checkout.CustomerID != CustomerID) return EShopCheckoutResult::InvalidRequest;
	ReclaimCanceledPayments();
	if (IssuedPayments.Contains(State.Checkout.CheckoutID)) return EShopCheckoutResult::Success;
	const auto* Register = Station->GetOwner()->FindComponentByClass<UShopCashRegisterComponent>();
	APropManager* Manager = APropManager::Get(this);
	if (!Register || !Register->TenderVolume || !Manager || !Manager->ShopCheckout || VisitBudgetCents < 0
		|| State.Checkout.TotalCents > VisitBudgetCents - SpentCents) return EShopCheckoutResult::CashUnavailable;
	int64 PaymentCents=State.Checkout.TotalCents;
	const int32 RoundTo=FMath::Clamp(TenderRoundUpDollars,0,100);
	if (RoundTo>0 && PaymentCents>0 && PaymentCents<=MAX_int64-int64(RoundTo)*100)
	{
		const int64 Unit=int64(RoundTo)*100, Rounded=((PaymentCents+Unit-1)/Unit)*Unit;
		const int32 ChangeDollars=int32((Rounded-PaymentCents)/100);
		if (ChangeDollars>0 && Rounded<=VisitBudgetCents-SpentCents)
		{
			TArray<bool> Reachable; Reachable.Init(false,ChangeDollars+1); Reachable[0]=true;
			TSet<int32> Available=UShopCashRegisterComponent::QueryCoinsInVolume(Register->TenderVolume);
			Available.Append(UShopCashRegisterComponent::QueryCoinsInVolume(Register->ChangeVolume));
			for (int32 ID:Available)
			{
				FShopCashCoin Coin;
				if (!Manager->ShopCheckout->GetCoin(ID,Coin) || Coin.CustomerOwner.IsValid() || Coin.CheckoutID.IsValid()) continue;
				for (int32 Amount=ChangeDollars; Amount>=Coin.Dollars; --Amount) if (Reachable[Amount-Coin.Dollars]) Reachable[Amount]=true;
			}
			if (Reachable.Last()) PaymentCents=Rounded;
		}
	}
	int64 Dollars = PaymentCents / 100; TArray<int32> Values;
	for (int32 D : {100, 50, 20, 10, 5, 2, 1}) while (Dollars >= D && Values.Num() < 256) { Values.Add(D); Dollars -= D; }
	if (Dollars != 0) return EShopCheckoutResult::CashUnavailable;
	TArray<int32> Coins;
	if (!Values.IsEmpty())
	{
		FTransform Output = Register->TenderVolume->GetComponentTransform(); Output.SetScale3D(FVector::OneVector);
		const auto Result = Manager->ShopCheckout->IssueCustomerCoins(FGuid::NewGuid(), CustomerID, Values, Output, Coins);
		if (Result != EShopCheckoutResult::Success) return Result;
	}
	SpentCents += State.Checkout.TotalCents;
	FVisitPayment Payment; Payment.Coins=Coins; Payment.AmountCents=State.Checkout.TotalCents;
	IssuedPayments.Add(State.Checkout.CheckoutID,MoveTemp(Payment)); return EShopCheckoutResult::Success;
}

void UShopCustomerCashComponent::ReclaimCanceledPayments()
{
	auto* Manager=APropManager::Get(this); auto* Checkout=Manager ? Manager->ShopCheckout.Get() : nullptr;
	if (!Checkout) return;
	for (auto It=IssuedPayments.CreateIterator(); It; ++It)
	{
		FShopCheckoutView Pending; FCompletedCheckoutReceipt Receipt;
		if (!Checkout->GetCheckoutView(It.Key(),Pending) && !Checkout->GetCompletedReceipt(It.Key(),Receipt)
			&& Checkout->ReclaimCustomerCoins(CustomerID,It.Value().Coins))
		{
			SpentCents=FMath::Max(int64(0),SpentCents-It.Value().AmountCents); It.RemoveCurrent();
		}
	}
}

bool UShopCustomerCashComponent::ReleaseStation(bool bCancelUnfinished)
{
	if (!GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return false;
	if (Station.IsValid())
	{
		const auto State=Station->GetScannerState();
		if (State.Checkout.CustomerID==CustomerID)
		{
			if (!bCancelUnfinished && State.Phase!=EShopScannerPhase::Completed) return false;
			if (Station->ReleaseCustomer(CustomerID,State.SessionID)!=EShopCheckoutResult::Success) return false;
		}
		Station.Reset();
	}
	ReclaimCanceledPayments(); return true;
}

bool UShopCustomerCashComponent::ResetVisit()
{
	if (!GetOwner()->HasAuthority() || GetNetMode()==NM_Client || Station.IsValid()) return false;
	auto* Manager=APropManager::Get(this);
	if (!Manager || !Manager->ShopCheckout || !Manager->ShopCheckout->ReclaimCustomerCoins(CustomerID,Manager->ShopCheckout->GetCustomerCoinISIDs(CustomerID))) return false;
	IssuedPayments.Empty(); PlannedQuantities.Empty(); SpentCents=0; CustomerID=FGuid::NewGuid();
	GetOwner()->ForceNetUpdate(); return true;
}

bool UShopCustomerCashComponent::FindCompletedVisitReceipt(FCompletedCheckoutReceipt& Out) const
{
	auto* Manager=APropManager::Get(this);
	if (!Manager || !Manager->ShopCheckout || !GetOwner()->HasAuthority()) return false;
	for (const auto& Pair:IssuedPayments)
		if (Manager->ShopCheckout->GetCompletedReceipt(Pair.Key,Out) && Out.CustomerID==CustomerID) return true;
	return false;
}
