#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopATMComponent.h"
#include "UI/ShopTerminalWidget.h"
#include "Props/InteractablePropManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

UShopPlayerInteractionComponent::UShopPlayerInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UShopPlayerInteractionComponent::RequestScannerAction(UShopScannerComponent* Scanner, FGuid ExpectedSession, EShopScannerAction Action, int32 ISID, FGuid CaseID)
{
	if (LocalSequence == MAX_int64) return;
	const int64 Sequence = ++LocalSequence;
	if (GetOwner() && GetOwner()->HasAuthority() && GetOwner()->GetNetMode() != NM_Client)
		OnActionResult.Broadcast(Sequence, ExecuteScannerAction(Scanner, ExpectedSession, Action, ISID, CaseID, Sequence));
	else Server_ScannerAction(Scanner, ExpectedSession, Action, ISID, CaseID, Sequence);
}

void UShopPlayerInteractionComponent::Server_ScannerAction_Implementation(UShopScannerComponent* Scanner, FGuid ExpectedSession, EShopScannerAction Action, int32 ISID, FGuid CaseID, int64 Sequence)
{
	Client_ActionResult(Sequence, ExecuteScannerAction(Scanner, ExpectedSession, Action, ISID, CaseID, Sequence));
}

void UShopPlayerInteractionComponent::Client_ActionResult_Implementation(int64 Sequence, EShopCheckoutResult Result) { OnActionResult.Broadcast(Sequence, Result); }

bool UShopPlayerInteractionComponent::CanReach(const AActor* Station) const
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (const auto* PC = Cast<APlayerController>(GetOwner())) Pawn = PC->GetPawn();
	const auto* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!Pawn || !PC || PC->GetPawn() != Pawn || !IsValid(Station) || Station->GetWorld() != GetWorld()) return false;
	const FVector Origin = Pawn->GetActorLocation(), Destination = Station->GetActorLocation();
	return FMath::IsFinite(MaxInteractionDistance) && MaxInteractionDistance > 0 && !Origin.ContainsNaN() && !Destination.ContainsNaN()
		&& FVector::DistSquared(Origin, Destination) <= FMath::Square(MaxInteractionDistance);
}

void UShopPlayerInteractionComponent::RequestATMWithdrawal(UShopATMComponent* ATM, const TArray<int32>& Values)
{
	if (LocalSequence == MAX_int64 || Values.IsEmpty() || Values.Num() > 256) return;
	const int64 Sequence = ++LocalSequence; const FGuid ID = FGuid::NewGuid();
	if (GetOwner() && GetOwner()->HasAuthority() && GetOwner()->GetNetMode() != NM_Client) OnActionResult.Broadcast(Sequence, ExecuteATMWithdrawal(ATM, Values, ID, Sequence));
	else Server_ATMWithdrawal(ATM, Values, ID, Sequence);
}
void UShopPlayerInteractionComponent::Server_ATMWithdrawal_Implementation(UShopATMComponent* ATM, const TArray<int32>& Values, FGuid ID, int64 Sequence)
{
	Client_ActionResult(Sequence, ExecuteATMWithdrawal(ATM, Values, ID, Sequence));
}
EShopCheckoutResult UShopPlayerInteractionComponent::ExecuteATMWithdrawal(UShopATMComponent* ATM, const TArray<int32>& Values, FGuid ID, int64 Sequence)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetOwner()->GetNetMode() == NM_Client) return EShopCheckoutResult::NotAuthority;
	if (Sequence <= 0 || Sequence <= LastServerSequence) return EShopCheckoutResult::OperationConflict;
	LastServerSequence = Sequence;
	if (!IsValid(ATM) || !CanReach(ATM->GetOwner()) || ATM->GetWorld() != GetWorld() || Values.IsEmpty() || Values.Num() > 256) return EShopCheckoutResult::Unavailable;
	return ATM->Withdraw(ID, Values);
}

void UShopPlayerInteractionComponent::Client_OpenTerminal_Implementation(AActor* Station) { OpenTerminal(Station); }
void UShopPlayerInteractionComponent::OpenTerminal(AActor* Station)
{
	if (!IsValid(Station) || Station->GetWorld() != GetWorld() || !CanReach(Station)
		|| (!Station->FindComponentByClass<UShopScannerComponent>() && !Station->FindComponentByClass<UShopATMComponent>())) return;
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC) if (const auto* Pawn = Cast<APawn>(GetOwner())) PC = Cast<APlayerController>(Pawn->GetController());
	if (!PC) return;
	if (!PC->IsLocalController()) { if (GetOwner()->HasAuthority()) Client_OpenTerminal(Station); return; }
	if (TerminalWidget) TerminalWidget->Close();
	TerminalWidget = CreateWidget<UShopTerminalWidget>(PC, UShopTerminalWidget::StaticClass());
	if (!TerminalWidget) return;
	TerminalWidget->Configure(this, Station); TerminalWidget->AddToViewport(50);
	TerminalWidget->SetAlignmentInViewport(FVector2D(.5,.5));
	int32 Width = 0, Height = 0; PC->GetViewportSize(Width, Height); TerminalWidget->SetPositionInViewport(FVector2D(Width*.5,Height*.5), false);
	FInputModeUIOnly Mode; Mode.SetWidgetToFocus(TerminalWidget->TakeWidget()); PC->SetInputMode(Mode); PC->SetShowMouseCursor(true);
}

EShopCheckoutResult UShopPlayerInteractionComponent::ExecuteScannerAction(UShopScannerComponent* Scanner, FGuid ExpectedSession, EShopScannerAction Action, int32 ISID, FGuid CaseID, int64 Sequence)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetOwner()->GetNetMode() == NM_Client) return EShopCheckoutResult::NotAuthority;
	if (Sequence <= LastServerSequence || Sequence <= 0) return EShopCheckoutResult::OperationConflict;
	LastServerSequence = Sequence;
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (const auto* Controller = Cast<APlayerController>(GetOwner())) Pawn = Controller->GetPawn();
	const auto* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!Pawn || !Controller || Controller->GetPawn() != Pawn || !IsValid(Scanner) || Scanner->GetWorld() != GetWorld() || !IsValid(Scanner->GetOwner())) return EShopCheckoutResult::InvalidRequest;
	const FVector Origin = Pawn->GetActorLocation(), Station = Scanner->GetOwner()->GetActorLocation();
	if (!FMath::IsFinite(MaxInteractionDistance) || MaxInteractionDistance <= 0 || Origin.ContainsNaN() || Station.ContainsNaN()
		|| FVector::DistSquared(Origin, Station) > FMath::Square(MaxInteractionDistance)) return EShopCheckoutResult::Unavailable;
	const FShopScannerState State = Scanner->GetScannerState();
	if (!ExpectedSession.IsValid() || State.SessionID != ExpectedSession) return EShopCheckoutResult::OperationConflict;
	switch (Action)
	{
	case EShopScannerAction::Save: return Scanner->SaveProducts();
	case EShopScannerAction::RemoveItem: return Scanner->RemoveProduct(ISID);
	case EShopScannerAction::Clear: return Scanner->ClearCart();
	case EShopScannerAction::LockBill: return Scanner->BeginCheckout();
	case EShopScannerAction::Complete: return Scanner->CompleteCheckout();
	case EShopScannerAction::CashCompensation:
	case EShopScannerAction::ProvideReplacement:
	{
		APropManager* Manager = APropManager::Get(this);
		FShopExpiryCase Case;
		if (State.Phase != EShopScannerPhase::Completed || !Manager || !Manager->ShopCheckout || !Manager->ShopCheckout->GetExpiryCase(CaseID, Case)
			|| Case.CheckoutID != State.Checkout.CheckoutID) return EShopCheckoutResult::InvalidRequest;
		if (Action == EShopScannerAction::CashCompensation) return Manager->ShopCheckout->ChooseCashCompensation(CaseID);
		if (!Scanner->IsProductPhysicallyInside(ISID)) return EShopCheckoutResult::InvalidReplacement;
		return Manager->ShopCheckout->ProvideReplacement(CaseID, ISID);
	}
	default: return EShopCheckoutResult::InvalidRequest;
	}
}
