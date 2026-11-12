#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Character.h"
#include "Components/BoxComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCashRegisterComponent.h"
#include "Shop/ShopCustomerCashComponent.h"
#include "Shop/ShopCustomerFlow.h"
#include "Shop/ShopCustomerVisitComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/SharedTeamWalletComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopCustomerLifecycleTest,"MarryByMidnight.Shop.CustomerCashLifecycle",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopCustomerLifecycleTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	if (!Manager->ShopSupplies->InitializeCatalog()) return false;
	auto* Product=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/NormalColaPrototype/DA_NormalLemonade.DA_NormalLemonade"));
	if (!Product) return false;
	const int32 ID=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(Product),FTransform(FRotator::ZeroRotator,FVector(0,0,100),FVector(.15)));
	Manager->ShopCheckout->RegisterShopStock(ID,0,Manager->GetTransform(ID));
	auto* Counter=World->SpawnActor<AActor>(); auto* Root=NewObject<USceneComponent>(Counter); Counter->SetRootComponent(Root); Root->RegisterComponent();
	auto* Scanner=NewObject<UShopScannerComponent>(Counter); Counter->AddInstanceComponent(Scanner); Scanner->RegisterComponent();
	auto Box=[&](FVector Offset)
	{
		auto* B=NewObject<UBoxComponent>(Counter); Counter->AddInstanceComponent(B); B->SetupAttachment(Root);
		B->SetRelativeLocation(Offset); B->SetBoxExtent(FVector(100)); B->SetCollisionEnabled(ECollisionEnabled::NoCollision); B->RegisterComponent(); return B;
	};
	auto* Register=NewObject<UShopCashRegisterComponent>(Counter); Counter->AddInstanceComponent(Register);
	Register->Scanner=Scanner; Register->TenderVolume=Box(FVector(0,200,100)); Register->ChangeVolume=Box(FVector(0,-200,100)); Register->RegisterComponent();
	auto* Actor=World->SpawnActor<ACharacter>(); auto* Cash=NewObject<UShopCustomerCashComponent>(Actor); Actor->AddInstanceComponent(Cash); Cash->RegisterComponent();
	const FGuid Customer=Cash->CustomerID;
	Cash->TenderRoundUpDollars=10;
	TestEqual(TEXT("Customer joins scanner"),Cash->JoinScanner(Scanner),EShopCheckoutResult::Success);
	Scanner->ScanProduct(ID); Cash->VisitBudgetCents=Scanner->GetScannerState().Checkout.TotalCents;
	if (!TestEqual(TEXT("First invoice receives physical tender"),Scanner->BeginCheckout(),EShopCheckoutResult::Success)) return false;
	Register->RefreshPayment();
	TestEqual(TEXT("Empty register bootstraps with exact payment"),Scanner->GetScannerState().Checkout.TenderCents,Scanner->GetScannerState().Checkout.TotalCents);
	const auto OldCoins=Manager->ShopCheckout->GetCustomerCoinISIDs(Customer);
	if (!TestTrue(TEXT("Customer really issued coins"),!OldCoins.IsEmpty())) return false;
	const auto OldSession=Scanner->GetScannerState().SessionID;
	TestEqual(TEXT("Cancel unpaid invoice"),Scanner->ClearCart(),EShopCheckoutResult::Success);
	Scanner->ScanProduct(ID);
	if (!TestEqual(TEXT("Same visit can retry after cancellation"),Scanner->BeginCheckout(),EShopCheckoutResult::Success)) return false;
	const auto NewCoins=Manager->ShopCheckout->GetCustomerCoinISIDs(Customer);
	TestEqual(TEXT("Cancel/rebill does not duplicate spendable customer coins"),NewCoins.Num(),OldCoins.Num());
	for (int32 Coin:OldCoins) TestNull(TEXT("Canceled tender identity retired"),Manager->FindISIDData(Coin));
	TestEqual(TEXT("Retry retains sufficient visit budget"),Register->RefreshPayment(),EShopCheckoutResult::Success);
	const auto NewSession=Scanner->GetScannerState().SessionID;
	TestEqual(TEXT("Old session cannot release retry"),Scanner->ReleaseCustomer(Customer,OldSession),EShopCheckoutResult::OperationConflict);
	if (!TestEqual(TEXT("Retry sale completes once"),Scanner->CompleteCheckout(),EShopCheckoutResult::Success)) return false;
	const int64 CashAfter=Manager->ShopCheckout->GetTeamCashCents();
	TestFalse(TEXT("Customer cleanup cannot reclaim now-team-owned tender"),Manager->ShopCheckout->ReclaimCustomerCoins(Customer,NewCoins));
	TestTrue(TEXT("Completed visit releases its station"),Cash->ReleaseStation(false));
	TestFalse(TEXT("Released scanner has no customer assignment"),Scanner->GetScannerState().Checkout.CustomerID.IsValid());
	TestTrue(TEXT("Counter session rotates after completion"),Scanner->GetScannerState().SessionID!=NewSession);
	TestTrue(TEXT("Visit can reset after releasing resources"),Cash->ResetVisit());
	TestTrue(TEXT("Reused customer gets a new visit identity"),Cash->CustomerID!=Customer);
	TestEqual(TEXT("Reset preserves completed team cash"),Manager->ShopCheckout->GetTeamCashCents(),CashAfter);
	TestEqual(TEXT("Reset never credits bank"),Manager->SharedWallet->GetWalletState().BalanceCents,int64(0));
	// Explicit isolated test float: withdrawal offsets fixture funding; no production starting money changes.
	Manager->SharedWallet->Credit(FGuid::NewGuid(),900,TEXT("TestChangeFloat")); TArray<int32> Float;
	TestEqual(TEXT("Finite change coins withdrawn"),Manager->ShopCheckout->WithdrawCoins(FGuid::NewGuid(),{1,1,1,1,1,1,1,1,1},FTransform(Register->TenderVolume->GetComponentLocation()),Float),EShopCheckoutResult::Success);
	const int32 Next=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(Product),FTransform(FRotator::ZeroRotator,FVector(0,0,100),FVector(.15)));
	FPropEventSyncDelta Fill; Fill.ISID=Next; Fill.SetFlag(EPropEventDirtyMask::ContentLevel); Fill.ContentLevel=.75f; Manager->SubmitInstanceEventUpdate(Next,Fill);
	Manager->ShopCheckout->RegisterShopStock(Next,0,Manager->GetTransform(Next));
	Cash->VisitBudgetCents=10000; Cash->JoinScanner(Scanner); Scanner->ScanProduct(Next);
	if (!TestEqual(TEXT("Next visit locks another bill"),Scanner->BeginCheckout(),EShopCheckoutResult::Success)) return false;
	Register->RefreshPayment(); const auto RoundedBill=Scanner->GetScannerState().Checkout;
	TestTrue(TEXT("Available real float permits rounded tender"),RoundedBill.TenderCents>RoundedBill.TotalCents);
	TestEqual(TEXT("Rounded tender alone cannot finish without physical change"),Scanner->CompleteCheckout(),EShopCheckoutResult::IncorrectChange);
	const int32 ChangeDollars=int32((RoundedBill.TenderCents-RoundedBill.TotalCents)/100);
	for (int32 I=0; I<ChangeDollars; ++I) Manager->InitializeInstanceTransform(Float[I],FTransform(Register->ChangeVolume->GetComponentLocation()));
	Register->RefreshPayment();
	TestEqual(TEXT("Correct real change completes rounded payment"),Scanner->CompleteCheckout(),EShopCheckoutResult::Success);
	TestEqual(TEXT("Rounded payment nets only the actual bill"),Manager->ShopCheckout->GetTeamCashCents(),CashAfter+900+RoundedBill.TotalCents);
	TestTrue(TEXT("Cleanup preserves cash/receipt save validity"),UShopCheckoutComponent::ValidateSaveData(Manager->ShopCheckout->ExportCommittedState()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopVisitClaimsTest,"MarryByMidnight.Shop.CustomerShoppingClaims",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopVisitClaimsTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	auto* Product=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/NormalColaPrototype/DA_NormalLemonade.DA_NormalLemonade")); if (!Product) return false;
	if (!Manager->ShopSupplies->InitializeCatalog()) return false;
	const int32 ID=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(Product),FTransform(FVector(0,0,100)));
	Manager->ShopCheckout->RegisterShopStock(ID,0,Manager->GetTransform(ID));
	auto* Flow=World->SpawnActor<AShopCustomerFlow>(); Flow->EntrancePoint=World->SpawnActor<AActor>();
	auto MakeVisit=[&]()
	{
		auto* Actor=World->SpawnActor<ACharacter>(); auto* Cash=NewObject<UShopCustomerCashComponent>(Actor); Actor->AddInstanceComponent(Cash); Cash->RegisterComponent();
		auto* Visit=NewObject<UShopCustomerVisitComponent>(Actor); Actor->AddInstanceComponent(Visit); Visit->RegisterComponent();
		TestTrue(TEXT("Visit initializes existing cash/stock plan"),Visit->StartVisit(Flow,World->SpawnActor<AActor>())); return Visit;
	};
	auto* First=MakeVisit(); auto* Second=MakeVisit();
	TestTrue(TEXT("First visitor claims a real stock identity"),Flow->ClaimStock(ID,First));
	TestFalse(TEXT("Second visitor cannot claim same bottle"),Flow->ClaimStock(ID,Second));
	Flow->ReleaseClaims(Second); TestTrue(TEXT("Unrelated cancellation keeps original claim"),Flow->StockIsClaimed(ID));
	Flow->ReleaseClaims(First); TestTrue(TEXT("Released claim becomes available to next visitor"),Flow->ClaimStock(ID,Second));
	Flow->JoinQueue(First); Flow->JoinQueue(First); Flow->JoinQueue(Second);
	TestEqual(TEXT("First-in queue order"),Flow->QueuePosition(First),0); TestEqual(TEXT("Duplicate join does not add another slot"),Flow->QueuePosition(Second),1);
	Flow->LeaveQueue(First); TestEqual(TEXT("Queue advances when first visitor leaves"),Flow->QueuePosition(Second),0);
	AddInfo(TEXT("This fixture verifies visit coordination only; it has no navigation and does not playtest NPC movement."));
	return true;
}
#endif
