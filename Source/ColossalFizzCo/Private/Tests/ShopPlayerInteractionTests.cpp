#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopPlayerInteractionComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopPlayerActionTest, "ColossalFizzCo.Shop.PlayerActionBoundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopPlayerActionTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	APropManager* Manager = World->SpawnActor<APropManager>();
	FPropProductDefinition Product; Product.ProductID = 21; Product.bEnableInstanceUniqueData = true;
	Product.ConsumableProfile.NumOfPortions = 25; Product.DefaultUniqueData.EventData.ContentID = 7; Manager->ProductDefinitions.Add(21, Product);
	FPropContentDefinition Content; Content.ContentID = 7; Content.BasePortionPrice = 4; Content.IdealServingTemperature = 4;
	Content.ExpiryProfile.bHasExpiry = false; Manager->ContentDefinitions.Add(7, Content);
	FISIDRuntimeIndex Record; Record.ISID = 10001; Record.ProductID = 21; Record.PackedRuntimeData.RemainingPortions = 25;
	Manager->RegisterISID(Record); Manager->ShopCheckout->RegisterShopStock(10001, 0, FTransform::Identity);
	AActor* Station = World->SpawnActor<AActor>(); Station->SetReplicates(true);
	auto* Scanner = NewObject<UShopScannerComponent>(Station); Station->AddInstanceComponent(Scanner); Scanner->RegisterComponent();
	Scanner->ScanProduct(10001);
	const FGuid FirstSession = Scanner->GetScannerState().SessionID;
	ACharacter* Pawn = World->SpawnActor<ACharacter>();
	APlayerController* Controller = World->SpawnActor<APlayerController>(); Controller->Possess(Pawn);
	auto* Actions = NewObject<UShopPlayerInteractionComponent>(Pawn); Pawn->AddInstanceComponent(Actions); Actions->RegisterComponent();
	auto Act = [&](FGuid Session, EShopScannerAction Action, int64 Sequence)
	{
		return Actions->ExecuteScannerAction(Scanner, Session, Action, 10001, FGuid(), Sequence);
	};
	TestEqual(TEXT("Owned nearby player can save a shared cart"), Act(FirstSession, EShopScannerAction::Save, 1), EShopCheckoutResult::Success);
	TestEqual(TEXT("Replayed player command is rejected"), Act(FirstSession, EShopScannerAction::Save, 1), EShopCheckoutResult::OperationConflict);
	Pawn->SetActorLocation(FVector(10000, 0, 0));
	TestEqual(TEXT("Remote player cannot control station"), Act(FirstSession, EShopScannerAction::Clear, 2), EShopCheckoutResult::Unavailable);
	Pawn->SetActorLocation(FVector::ZeroVector);
	TestEqual(TEXT("Current nearby player can clear cart"), Act(FirstSession, EShopScannerAction::Clear, 3), EShopCheckoutResult::Success);
	Scanner->ScanProduct(10001);
	const FGuid NextSession = Scanner->GetScannerState().SessionID;
	TestNotEqual(TEXT("Next cart has a different identity"), FirstSession, NextSession);
	TestEqual(TEXT("Delayed clear cannot erase next customer's items"), Act(FirstSession, EShopScannerAction::Clear, 4), EShopCheckoutResult::OperationConflict);
	TestEqual(TEXT("Next cart survives delayed command"), Scanner->GetScannerState().Checkout.Items.Num(), 1);
	TestEqual(TEXT("Invalid action is rejected"), Act(NextSession, static_cast<EShopScannerAction>(255), 5), EShopCheckoutResult::InvalidRequest);
	ACharacter* Teammate = World->SpawnActor<ACharacter>();
	APlayerController* OtherController = World->SpawnActor<APlayerController>(); OtherController->Possess(Teammate);
	auto* OtherActions = NewObject<UShopPlayerInteractionComponent>(Teammate); Teammate->AddInstanceComponent(OtherActions); OtherActions->RegisterComponent();
	TestEqual(TEXT("Another teammate may continue the same transaction"), OtherActions->ExecuteScannerAction(Scanner, NextSession, EShopScannerAction::Save, 0, FGuid(), 1), EShopCheckoutResult::Success);
	Controller->UnPossess();
	TestEqual(TEXT("Unpossessed/NPC actor cannot impersonate a cashier"), Act(NextSession, EShopScannerAction::Clear, 6), EShopCheckoutResult::InvalidRequest);
	return true;
}
#endif
