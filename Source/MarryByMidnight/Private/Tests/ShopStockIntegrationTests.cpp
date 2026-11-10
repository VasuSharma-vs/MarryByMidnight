#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropDataAsset.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopCustomerCashComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace ShopStockIntegration
{
	UWorld* World()
	{
		const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
		if (World && GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		return World;
	}
	void Cleanup(UWorld* World) { World->DestroyWorld(false); if (GEngine) GEngine->DestroyWorldContext(World); }
	FPropProductDefinition Configure(APropManager* Manager)
	{
		Manager->InitializePropManager();
		FPropProductDefinition P; P.ProductID = 21; P.bEnableInstanceUniqueData = true;
		P.ConsumableProfile.NumOfPortions = 135; P.DefaultUniqueData.EventData.ContentID = 7;
		P.DefaultUniqueData.EventData.ContentLevel = 1;
		Manager->ProductDefinitions.Add(21, P);
		FPropContentDefinition C; C.ContentID = 7; C.BasePortionPrice = 25; C.IdealServingTemperature = 4; C.ExpiryProfile.bHasExpiry = false;
		Manager->ContentDefinitions.Add(7, C); return P;
	}
	void Add(APropManager* Manager, int32 ID, int32 PID, float Level = 1)
	{
		FISIDRuntimeIndex R; R.ISID = ID; R.ProductID = PID; R.PackedRuntimeData.RemainingPortions = 135; R.PackedRuntimeData.Temperature = 4;
		Manager->RegisterISID(R); if (auto* U = Manager->FindUniqueData(ID)) U->EventData.ContentLevel = Level;
		Manager->ShopCheckout->RegisterShopStock(ID, 0, FTransform::Identity);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopCatalogBudgetTest, "MarryByMidnight.Shop.ColaCatalogAndBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopCatalogBudgetTest::RunTest(const FString& Parameters)
{
	UWorld* World = ShopStockIntegration::World(); if (!World) return false;
	ON_SCOPE_EXIT { ShopStockIntegration::Cleanup(World); };
	auto* Manager = World->SpawnActor<APropManager>(); auto P = ShopStockIntegration::Configure(Manager);
	for (int32 ID : {10001,10002,10003}) ShopStockIntegration::Add(Manager, ID, 21);
	P.ProductID = 22; P.ConsumableProfile.ConsumableType = EConsumablePropType::Food; Manager->ProductDefinitions.Add(22, P);
	ShopStockIntegration::Add(Manager,10004,22);
	P.ProductID = 23; P.ConsumableProfile.NumOfPortions = 0; P.InteractionProfile.bCanConsume = false; Manager->ProductDefinitions.Add(23, P);
	ShopStockIntegration::Add(Manager,10005,23);
	P.ProductID = 24; Manager->ProductDefinitions.Add(24, P); ShopStockIntegration::Add(Manager,10006,24);
	P = Manager->ProductDefinitions.FindChecked(21); P.ProductID = 25; Manager->ProductDefinitions.Add(25,P); ShopStockIntegration::Add(Manager,10007,25,0);
	const auto Available = Manager->ShopCheckout->GetAvailableColaStockISIDs();
	TestTrue(TEXT("Cola catalog excludes food, coins, fragments and empty bottles"), Available == TArray<int32>({10001,10002,10003}));
	const auto All = Manager->GetAllProductIDs();
	TestTrue(TEXT("Generic registry still contains non-cola products"), All.Contains(22) && All.Contains(23) && All.Contains(24) && All.Contains(25));
	auto* Customer = World->SpawnActor<AActor>(); auto* Cash = NewObject<UShopCustomerCashComponent>(Customer); Customer->AddInstanceComponent(Cash); Cash->RegisterComponent();
	Cash->VisitBudgetCents = 1000000;
	const auto Plan = Cash->BuildShoppingPlan(3);
	TestTrue(TEXT("135-portion authored cola is retained in plan"), Plan == TArray<int32>({21}));
	TestEqual(TEXT("Customer can afford two 3375-dollar bottles, not three"), Cash->GetPlannedQuantity(21), 2);
	TestEqual(TEXT("Planning preserves nominal capacity"), Manager->FindProductDefinition(21)->ConsumableProfile.NumOfPortions, 135);
	TestEqual(TEXT("Planning preserves authored rate"), Manager->FindContentDefinition(7)->BasePortionPrice, 25.f);
	Manager->ShopCheckout->MaxCoinsPerOperation = 1;
	TestTrue(TEXT("Plan respects physical coin issuance limit"), Cash->BuildShoppingPlan(3).IsEmpty());
	TestEqual(TEXT("Rejected plan has no stale quantity"), Cash->GetPlannedQuantity(21), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopAuthoredStockRestoreTest, "MarryByMidnight.Shop.AuthoredStockRestore", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopAuthoredStockRestoreTest::RunTest(const FString& Parameters)
{
	UWorld* World = ShopStockIntegration::World(); if (!World) return false;
	ON_SCOPE_EXIT { ShopStockIntegration::Cleanup(World); };
	auto* Manager = World->SpawnActor<APropManager>(); ShopStockIntegration::Configure(Manager); Manager->bEnableAutosave = false;
	auto* Asset = LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/Assest/DataAssets/DA_BasicCola.DA_BasicCola"));
	if (!TestNotNull(TEXT("Authored bottle asset"),Asset)) return false;
	Manager->ProductDefinitions.FindChecked(21).StaticMesh = Asset->StaticMesh;
	auto SpawnSource = [&]()
	{
		auto* Actor = World->SpawnActor<AActivePropActor>(); Actor->ProductID = 21; Actor->PropMesh->SetStaticMesh(Asset->StaticMesh);
		Actor->Tags.Add(TEXT("ShopInitialStock")); Actor->InitialShopPurchaseCents = 275; Actor->RemainingPortions = 135; return Actor;
	};
	auto* Source = SpawnSource(); const int32 ID = Manager->ConvertPlacedActorToISM(Source,true);
	if (!TestTrue(TEXT("Placed stock receives a real ISID"),ID > 0)) return false;
	FShopStockEntry Stock;
	TestTrue(TEXT("Explicitly tagged authored prop registers after conversion"),Manager->ShopCheckout->GetStockEntry(ID,Stock));
	TestEqual(TEXT("Purchase cost is independent of retail price"),Stock.OriginalPurchaseCents,int64(275));
	const FString Slot = TEXT("StockRestore_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { UGameplayStatics::DeleteGameInSlot(Slot,0); };
	if (!TestTrue(TEXT("Stock snapshot saves"),Manager->SavePropDatabase(Slot,0))) return false;
	auto* OriginalLevelActor = SpawnSource();
	Manager->InitializeLevelProps(Slot,0); Manager->ProcessQueuedPlacedConversions(1000);
	TestEqual(TEXT("Loading does not duplicate level stock over saved inventory"),Manager->ISIDData.Num(),1);
	TestTrue(TEXT("Restored stock retains its identity"),Manager->ShopCheckout->GetStockEntry(ID,Stock));
	TestEqual(TEXT("Restored acquisition cost is not replaced with retail"),Stock.OriginalPurchaseCents,int64(275));
	TestEqual(TEXT("Redundant authored source is pooled after load"),OriginalLevelActor->GetOwner(),static_cast<AActor*>(Manager));
	return true;
}
#endif
