#include "Shop/ShopATMComponent.h"
#include "Shop/ShopCashRegisterComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Props/InteractablePropManager.h"
#include "Components/BoxComponent.h"
#include "TimerManager.h"

UShopATMComponent::UShopATMComponent() { PrimaryComponentTick.bCanEverTick = false; SetIsReplicatedByDefault(true); }
void UShopATMComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetOwner()->HasAuthority() || GetOwner()->GetNetMode() == NM_Client) return;
	TArray<UBoxComponent*> Boxes; GetOwner()->GetComponents(Boxes);
	for (auto* Box : Boxes) if (!DepositVolume && (Box->GetFName() == TEXT("DepositVolume") || Box->ComponentHasTag(TEXT("ATMDeposit")))) DepositVolume = Box;
	TArray<USceneComponent*> Components; GetOwner()->GetComponents(Components);
	for (auto* Component : Components) if (!CoinOutput && (Component->GetFName() == TEXT("CoinOutput") || Component->ComponentHasTag(TEXT("ATMCoinOutput")))) CoinOutput = Component;
	GetWorld()->GetTimerManager().SetTimer(DepositTimer, this, &UShopATMComponent::CollectDeposits, .2f, true);
}
EShopCheckoutResult UShopATMComponent::Withdraw(FGuid ID, const TArray<int32>& Values)
{
	if (!GetOwner()->HasAuthority() || GetOwner()->GetNetMode() == NM_Client) return EShopCheckoutResult::NotAuthority;
	if (!CoinOutput || !DepositVolume || CoinOutput->GetWorld() != GetWorld() || DepositVolume->GetWorld() != GetWorld()) return EShopCheckoutResult::CashUnavailable;
	// Keep withdrawal output out of the deposit mouth; otherwise every withdrawal would immediately redeposit.
	if (DepositVolume->Bounds.GetBox().IsInside(CoinOutput->GetComponentLocation())) return EShopCheckoutResult::InvalidRequest;
	APropManager* Manager = APropManager::Get(this); TArray<int32> Coins;
	return Manager && Manager->ShopCheckout ? Manager->ShopCheckout->WithdrawCoins(ID, Values, CoinOutput->GetComponentTransform(), Coins, true) : EShopCheckoutResult::Unavailable;
}
void UShopATMComponent::CollectDeposits()
{
	if (!GetOwner()->HasAuthority() || GetOwner()->GetNetMode() == NM_Client) return;
	APropManager* Manager = APropManager::Get(this);
	if (!Manager || !Manager->ShopCheckout) return;
	TArray<int32> IDs;
	for (int32 ID : UShopCashRegisterComponent::QueryCoinsInVolume(DepositVolume))
	{
		FShopCashCoin Coin;
		if (Manager->ShopCheckout->GetCoin(ID, Coin) && !Coin.CustomerOwner.IsValid() && !Coin.CheckoutID.IsValid()) IDs.Add(ID);
	}
	IDs.Sort(); if (IDs.Num() > Manager->ShopCheckout->MaxCoinsPerOperation) IDs.SetNum(FMath::Clamp(Manager->ShopCheckout->MaxCoinsPerOperation, 1, 256));
	if (!IDs.IsEmpty()) Manager->ShopCheckout->DepositCoins(FGuid::NewGuid(), IDs);
}
void UShopATMComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorld()->GetTimerManager().ClearTimer(DepositTimer); Super::EndPlay(Reason);
}
