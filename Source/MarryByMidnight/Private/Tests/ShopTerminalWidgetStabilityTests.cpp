#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Components/VerticalBox.h"
#include "UI/ShopTerminalWidget.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "ShopCashTestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopTerminalWidgetStabilityTest,"MarryByMidnight.Shop.TerminalWidgetStability",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopTerminalWidgetStabilityTest::RunTest(const FString& Parameters)
{
	auto* GI=NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
	auto* World=GI->GetWorld();
	if (!World) return false;
	ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false;
	FPropProductDefinition Product; Product.ProductID=21; Product.bEnableInstanceUniqueData=true;
	Product.ConsumableProfile.NumOfPortions=4; Product.DefaultUniqueData.EventData.ContentID=7; Manager->ProductDefinitions.Add(21,Product);
	FPropContentDefinition Content; Content.ContentID=7; Content.BasePortionPrice=4; Content.IdealServingTemperature=4; Content.ExpiryProfile.bHasExpiry=false;
	Manager->ContentDefinitions.Add(7,Content);
	FISIDRuntimeIndex Record; Record.ISID=10001; Record.ProductID=21; Record.PackedRuntimeData.RemainingPortions=4;
	Manager->RegisterISID(Record); Manager->ShopCheckout->RegisterShopStock(10001,0,FTransform::Identity);
	auto* Station=World->SpawnActor<AActor>();
	auto* Scanner=NewObject<UShopScannerComponent>(Station); Station->AddInstanceComponent(Scanner); Scanner->RegisterComponent();
	auto* Pawn=World->SpawnActor<ACharacter>(); auto* PC=World->SpawnActor<APlayerController>(); PC->Possess(Pawn);
	auto* Actions=NewObject<UShopPlayerInteractionComponent>(Pawn); Pawn->AddInstanceComponent(Actions); Actions->RegisterComponent();
	Scanner->AssignCustomer(FGuid::NewGuid());
	TestEqual(TEXT("Real bottle enters scanner"),Scanner->ScanProduct(10001),EShopCheckoutResult::Success);
	auto* Widget=CreateWidget<UShopTerminalWidget>(GI,UShopTerminalWidget::StaticClass());
	if (!TestNotNull(TEXT("Native terminal widget initializes"),Widget)) return false;
	Widget->Configure(Actions,Station); const auto Slate=Widget->TakeWidget(); Widget->Refresh();
	if (!TestNotNull(TEXT("Terminal body exists"),Widget->Body.Get())) return false;
	TestTrue(TEXT("First visible frame has checkout controls"),Widget->Body->GetChildrenCount()>3);
	const auto Scanning=Widget->Body->GetAllChildren(); Widget->Refresh();
	TestTrue(TEXT("Repeated scans retain existing controls"),Widget->Body->GetAllChildren()==Scanning);
	if (!TestEqual(TEXT("Real scanner locks bill"),Scanner->BeginCheckout(),EShopCheckoutResult::Success)) return false;
	Widget->Refresh(); const auto Payment=Widget->Body->GetAllChildren();
	TestTrue(TEXT("Phase change replaces controls within same widget"),Payment!=Scanning);
	const auto State=Scanner->GetScannerState();
	ShopCashTests::FPayment Cash(Manager,State.Checkout.CheckoutID,State.Checkout.TotalCents);
	TestEqual(TEXT("Physical tender updates the existing checkout"),Scanner->GetScannerState().Checkout.TenderCents,State.Checkout.TotalCents);
	Widget->Refresh();
	TestTrue(TEXT("Payment text update retains button identities and focus targets"),Widget->Body->GetAllChildren()==Payment);
	TestTrue(TEXT("Duplicate station identity is recognized"),Widget->IsShowingStation(Station));
	Widget->Close();
	return true;
}
#endif
