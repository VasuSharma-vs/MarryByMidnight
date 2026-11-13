#include "Shop/ShopInteractionLibrary.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/ShopCustomerCashComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "EngineUtils.h"

void UShopInteractionLibrary::OpenShopTerminal(AActor* Player, AActor* Station)
{
	if (IsValid(Player)) if (auto* Actions = Player->FindComponentByClass<UShopPlayerInteractionComponent>()) Actions->OpenTerminal(Station);
}

bool UShopInteractionLibrary::TryOpenShopTerminalFromView(AActor* Player)
{
	if (IsValid(Player)) if (auto* Actions=Player->FindComponentByClass<UShopPlayerInteractionComponent>())
		return Actions->TryOpenTerminalFromView();
	return false;
}
bool UShopInteractionLibrary::RegisterCustomerAtCounter(AActor* Customer, AActor* Counter)
{
	if (!IsValid(Customer) || !IsValid(Counter) || !Customer->HasAuthority() || Customer->GetNetMode() == NM_Client || Customer->GetWorld() != Counter->GetWorld()) return false;
	auto* Cash = Customer->FindComponentByClass<UShopCustomerCashComponent>(); if (!Cash) return false;
	UShopScannerComponent* Nearest = nullptr; double Distance = FMath::Square(350.);
	for (TActorIterator<AActor> It(Customer->GetWorld()); It; ++It) if (auto* Scanner = It->FindComponentByClass<UShopScannerComponent>())
	{
		const double Current = FVector::DistSquared(It->GetActorLocation(), Counter->GetActorLocation());
		if (Current < Distance) { Nearest = Scanner; Distance = Current; }
	}
	return Nearest && Cash->JoinScanner(Nearest) == EShopCheckoutResult::Success;
}
