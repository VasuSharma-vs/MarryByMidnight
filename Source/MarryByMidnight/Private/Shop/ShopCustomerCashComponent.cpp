#include "Shop/ShopCustomerCashComponent.h"
#include "Shop/ShopCashRegisterComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCheckoutComponent.h"
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
		|| FVector::DistSquared(GetOwner()->GetActorLocation(), Scanner->GetOwner()->GetActorLocation()) > FMath::Square(350.f)) return EShopCheckoutResult::Unavailable;
	if (Station.IsValid() && Station != Scanner && Station->GetScannerState().Phase != EShopScannerPhase::Completed) return EShopCheckoutResult::Unavailable;
	const auto State = Scanner->GetScannerState();
	if (State.Checkout.CustomerID.IsValid() && State.Checkout.CustomerID != CustomerID) return EShopCheckoutResult::Unavailable;
	const auto Result = Scanner->AssignCustomer(CustomerID); if (Result == EShopCheckoutResult::Success) Station = Scanner;
	return Result;
}
EShopCheckoutResult UShopCustomerCashComponent::PlacePayment()
{
	if (!GetOwner()->HasAuthority() || GetOwner()->GetNetMode() == NM_Client) return EShopCheckoutResult::NotAuthority;
	if (!Station.IsValid() || FVector::DistSquared(GetOwner()->GetActorLocation(), Station->GetOwner()->GetActorLocation()) > FMath::Square(350.f)) return EShopCheckoutResult::Unavailable;
	const auto State = Station->GetScannerState();
	if (State.Phase != EShopScannerPhase::Checkout || State.Checkout.CustomerID != CustomerID) return EShopCheckoutResult::InvalidRequest;
	if (IssuedPayments.Contains(State.Checkout.CheckoutID)) return EShopCheckoutResult::Success;
	const auto* Register = Station->GetOwner()->FindComponentByClass<UShopCashRegisterComponent>();
	APropManager* Manager = APropManager::Get(this);
	if (!Register || !Register->TenderVolume || !Manager || !Manager->ShopCheckout || VisitBudgetCents < 0
		|| State.Checkout.TotalCents > VisitBudgetCents - SpentCents) return EShopCheckoutResult::CashUnavailable;
	int64 Dollars = State.Checkout.TotalCents / 100; TArray<int32> Values;
	for (int32 D : {100, 50, 20, 10, 5, 2, 1}) while (Dollars >= D && Values.Num() < 256) { Values.Add(D); Dollars -= D; }
	if (Dollars != 0) return EShopCheckoutResult::CashUnavailable;
	TArray<int32> Coins;
	if (!Values.IsEmpty())
	{
		FTransform Output = Register->TenderVolume->GetComponentTransform(); Output.SetScale3D(FVector::OneVector);
		const auto Result = Manager->ShopCheckout->IssueCustomerCoins(FGuid::NewGuid(), CustomerID, Values, Output, Coins);
		if (Result != EShopCheckoutResult::Success) return Result;
	}
	SpentCents += State.Checkout.TotalCents; IssuedPayments.Add(State.Checkout.CheckoutID, Coins); return EShopCheckoutResult::Success;
}
