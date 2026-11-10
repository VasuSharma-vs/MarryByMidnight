#include "UI/ShopTerminalWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ShopATMComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"

namespace ShopTerminal
{
	FString Money(int64 Cents)
	{
		const int64 Dollars = Cents / 100, Remainder = FMath::Abs(Cents % 100);
		return FString::Printf(TEXT("%s$%lld.%02lld"), Cents < 0 ? TEXT("-") : TEXT(""), FMath::Abs(Dollars), Remainder);
	}
}

void UShopTerminalButton::Configure(UShopTerminalWidget* Owner, const FString& Label)
{
	Terminal = Owner;
	auto* Text = NewObject<UTextBlock>(this); Text->SetText(FText::FromString(Label)); Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	SetContent(Text); SetBackgroundColor(FLinearColor(.08f,.24f,.3f));
	OnClicked.AddDynamic(this, &UShopTerminalButton::HandleClick);
}
void UShopTerminalButton::HandleClick() { if (Terminal.IsValid()) Terminal->HandleButton(this); }

TSharedRef<SWidget> UShopTerminalWidget::RebuildWidget()
{
	if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this);
	auto* Size = WidgetTree->ConstructWidget<USizeBox>(); Size->SetWidthOverride(720); Size->SetHeightOverride(650);
	auto* Border = WidgetTree->ConstructWidget<UBorder>(); Border->SetPadding(FMargin(16)); Border->SetBrushColor(FLinearColor(.025f,.035f,.045f,.98f)); Size->SetContent(Border);
	auto* Layout = WidgetTree->ConstructWidget<UVerticalBox>(); Border->SetContent(Layout);
	Summary = WidgetTree->ConstructWidget<UTextBlock>(); Summary->SetAutoWrapText(true); Layout->AddChild(Summary);
	ResultText = WidgetTree->ConstructWidget<UTextBlock>(); ResultText->SetAutoWrapText(true); Layout->AddChild(ResultText);
	auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>(); Layout->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Body = WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Body);
	WidgetTree->RootWidget = Size;
	SetIsFocusable(true);
	return Super::RebuildWidget();
}
void UShopTerminalWidget::Configure(UShopPlayerInteractionComponent* InActions, AActor* InStation)
{
	Actions = InActions; Station = InStation;
	InActions->OnActionResult.AddUniqueDynamic(this, &UShopTerminalWidget::ActionResult);
	LastContents.Empty(); RefreshCountdown = 0;
}
void UShopTerminalWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);
	RefreshCountdown -= DeltaSeconds;
	if (RefreshCountdown <= 0) { RefreshCountdown = .2f; Refresh(); }
}
FReply UShopTerminalWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape) { Close(); return FReply::Handled(); }
	return Super::NativeOnKeyDown(Geometry, Event);
}
void UShopTerminalWidget::AddText(const FString& Value)
{
	auto* Text = WidgetTree->ConstructWidget<UTextBlock>(); Text->SetText(FText::FromString(Value)); Text->SetAutoWrapText(true); Body->AddChild(Text);
}
UShopTerminalButton* UShopTerminalWidget::AddButton(const FString& Label)
{
	auto* Button = WidgetTree->ConstructWidget<UShopTerminalButton>(); Button->Configure(this, Label); Body->AddChild(Button); return Button;
}
void UShopTerminalWidget::Refresh()
{
	if (!Body || !Summary) return;
	if (!Actions.IsValid() || !Station.IsValid() || !Actions->IsStationReachable(Station.Get())) { Close(); return; }
	auto* Manager = APropManager::Get(this);
	if (!Manager || !Manager->SharedWallet || !Manager->ShopCheckout) return;
	Summary->SetText(FText::FromString(FString::Printf(TEXT("Shop bank: %s    Physical shop cash: %s\n"), *ShopTerminal::Money(Manager->SharedWallet->GetWalletState().BalanceCents), *ShopTerminal::Money(Manager->ShopCheckout->TeamCashCents))));
	auto* Scanner = Station->FindComponentByClass<UShopScannerComponent>();
	if (!Scanner)
	{
		if (LastContents == TEXT("ATM")) return;
		LastContents = TEXT("ATM"); Body->ClearChildren(); AddText(TEXT("ATM\nWithdraw a coin. Drop shop-owned coins into the deposit tray to bank them."));
		for (int32 D : {1,2,5,10,20,50,100}) AddButton(FString::Printf(TEXT("Withdraw $%d coin"), D))->Denomination = D;
		AddButton(TEXT("Close"))->bClose = true; return;
	}
	const auto State = Scanner->GetScannerState();
	FString Contents = State.SessionID.ToString() + FString::FromInt(int32(State.Phase)) + FString::Printf(TEXT("/%lld/%lld/%lld"), State.Checkout.TotalCents, State.Checkout.TenderCents, State.Checkout.ChangeCents);
	for (const auto& Item : State.Checkout.Items) Contents += FString::Printf(TEXT("/%d/%lld/%d/%d/%d/%.3f/%.2f"), Item.ISID, Item.SaleCents, Item.Snapshot.Price.bExpired, Item.bSaved, Item.bAvailable, Item.Snapshot.ContentLevel, Item.Snapshot.Temperature);
	for (const auto& Case : Manager->ShopCheckout->PendingExpiryCases) if (Case.CheckoutID == State.Checkout.CheckoutID) Contents += Case.CaseID.ToString() + FString::Printf(TEXT("/%d/%lld/%lld"), int32(Case.Choice), Case.AutomaticPaidCents, Case.CompensationPaidCents);
	for (const auto& Candidate : State.ReplacementCandidates) Contents += FString::Printf(TEXT("/%d/%d/%d/%.3f"), Candidate.ISID, Candidate.ContentID, Candidate.NominalPortions, Candidate.ContentLevel);
	for (const auto& Settlement : Manager->ShopCheckout->RecentSettlements) if (Settlement.CheckoutID == State.Checkout.CheckoutID)
		Contents += FString::Printf(TEXT("/%lld/%lld"), Settlement.AdjustedRevenueCents, Settlement.OutstandingDeductionCents);
	if (Contents == LastContents) return;
	LastContents = Contents; Body->ClearChildren();
	AddText(FString::Printf(TEXT("%s\nBill: %s   Tender: %s   Change counted: %s\nChange required: %s"),
		State.Phase == EShopScannerPhase::Scanning ? TEXT("Scan bottles") : State.Phase == EShopScannerPhase::Checkout ? TEXT("Invoice locked — count physical payment") : TEXT("Sale completed"),
		*ShopTerminal::Money(State.Checkout.TotalCents), *ShopTerminal::Money(State.Checkout.TenderCents), *ShopTerminal::Money(State.Checkout.ChangeCents),
		*ShopTerminal::Money(FMath::Max(int64(0), State.Checkout.TenderCents - State.Checkout.TotalCents))));
	for (const auto& Group : State.Groups)
	{
		AddText(FString::Printf(TEXT("\n%s x%d   %s\nBase %s   Content discount %s   Temperature discount %s"), *Group.DisplayName.ToString(), Group.BottleISIDs.Num(),
			*ShopTerminal::Money(Group.FinalSubtotalCents), *ShopTerminal::Money(Group.OriginalSubtotalCents), *ShopTerminal::Money(Group.ContentDiscountCents), *ShopTerminal::Money(Group.TemperatureDiscountCents)));
	}
	for (const auto& Item : State.Checkout.Items)
	{
		AddText(FString::Printf(TEXT("Bottle #%d: %.0f%% full, %.1f C (optimum %.1f C), %s%s"), Item.ISID, Item.Snapshot.ContentLevel * 100,
			Item.Snapshot.Temperature, Item.Snapshot.OptimumTemperature, (Item.bExpiredAtSale || Item.Snapshot.Price.bExpired) ? TEXT("EXPIRED") : TEXT("fresh"), Item.bSaved ? TEXT(" — saved") : TEXT("")));
		if (State.Phase == EShopScannerPhase::Scanning)
		{
			auto* Remove = AddButton(FString::Printf(TEXT("Remove bottle #%d"), Item.ISID)); Remove->Action = EShopScannerAction::RemoveItem; Remove->ISID = Item.ISID; Remove->SessionID = State.SessionID;
		}
	}
	auto ScannerButton = [&](const FString& Label, EShopScannerAction Action)
	{
		auto* Button = AddButton(Label); Button->Action = Action; Button->SessionID = State.SessionID; return Button;
	};
	if (State.Phase == EShopScannerPhase::Scanning)
	{
		ScannerButton(TEXT("Save scanned bottles"), EShopScannerAction::Save);
		ScannerButton(TEXT("Finalize and lock bill"), EShopScannerAction::LockBill);
	}
	if (State.Phase == EShopScannerPhase::Checkout) ScannerButton(TEXT("Complete paid checkout"), EShopScannerAction::Complete);
	if (State.Phase == EShopScannerPhase::Completed)
	{
		int64 Adjusted = 0, Owed = 0; Manager->ShopCheckout->GetAdjustedRevenue(State.Checkout.CheckoutID, Adjusted, Owed);
		AddText(FString::Printf(TEXT("Adjusted sale revenue: %s   Bank deductions still owed: %s"), *ShopTerminal::Money(Adjusted), *ShopTerminal::Money(Owed)));
		for (const auto& Case : Manager->ShopCheckout->PendingExpiryCases) if (Case.CheckoutID == State.Checkout.CheckoutID && Case.Choice == EShopCompensationChoice::Unselected)
		{
			AddText(FString::Printf(TEXT("Expired bottle #%d: mandatory penalty %s. Choose compensation:"), Case.OriginalISID, *ShopTerminal::Money(Case.OriginalBaseCents)));
			ScannerButton(TEXT("Deduct another ") + ShopTerminal::Money(Case.OriginalBaseCents), EShopScannerAction::CashCompensation)->CaseID = Case.CaseID;
			for (const auto& Candidate : State.ReplacementCandidates) if (Candidate.ContentID == Case.ContentID && Candidate.NominalPortions == Case.NominalPortions)
			{
				auto* Replace = ScannerButton(FString::Printf(TEXT("Give fresh bottle #%d free"), Candidate.ISID), EShopScannerAction::ProvideReplacement); Replace->CaseID = Case.CaseID; Replace->ISID = Candidate.ISID;
			}
			AddText(TEXT("Place a full, fresh bottle of the same cola and capacity inside the scanner for a free replacement."));
		}
	}
	ScannerButton(State.Phase == EShopScannerPhase::Completed ? TEXT("Next customer") : TEXT("Clear / cancel checkout"), EShopScannerAction::Clear);
	AddButton(TEXT("Close"))->bClose = true;
}
void UShopTerminalWidget::HandleButton(const UShopTerminalButton* Button)
{
	if (Button->bClose) { Close(); return; }
	if (!Actions.IsValid() || !Station.IsValid()) return;
	if (Button->Denomination > 0) Actions->RequestATMWithdrawal(Station->FindComponentByClass<UShopATMComponent>(), {Button->Denomination});
	else Actions->RequestScannerAction(Station->FindComponentByClass<UShopScannerComponent>(), Button->SessionID, Button->Action, Button->ISID, Button->CaseID);
}
void UShopTerminalWidget::ActionResult(int64 Sequence, EShopCheckoutResult Result)
{
	if (ResultText) ResultText->SetText(StaticEnum<EShopCheckoutResult>()->GetDisplayNameTextByValue(int64(Result)));
}
void UShopTerminalWidget::Close()
{
	if (Actions.IsValid()) Actions->OnActionResult.RemoveDynamic(this, &UShopTerminalWidget::ActionResult);
	RemoveFromParent();
	if (auto* PC = GetOwningPlayer()) { PC->SetInputMode(FInputModeGameOnly()); PC->SetShowMouseCursor(false); }
}
