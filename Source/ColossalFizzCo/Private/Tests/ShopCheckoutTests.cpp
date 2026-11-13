#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "ShopCashTestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopCheckoutCommitTest, "ColossalFizzCo.Shop.CheckoutCommit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopCheckoutCommitTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	APropManager* Manager = World->SpawnActor<APropManager>();
	if (!Manager || !Manager->ShopCheckout || !Manager->SharedWallet) return false;
	Manager->InitializePropManager();
	FPropProductDefinition Product; Product.ProductID = 21; Product.bEnableInstanceUniqueData = true; Product.ConsumableProfile.NumOfPortions = 25;
	Product.DefaultUniqueData.EventData.ContentID = 7;
	Manager->ProductDefinitions.Add(21, Product);
	FPropContentDefinition Content; Content.ContentID = 7; Content.BasePortionPrice = 4; Content.IdealServingTemperature = 4; Content.ExpiryProfile.bHasExpiry = false;
	Manager->ContentDefinitions.Add(7, Content);
	FISIDRuntimeIndex Record; Record.ISID = 10001; Record.ProductID = 21; Record.PackedRuntimeData.RemainingPortions = 25; Record.PackedRuntimeData.Temperature = 4;
	Manager->RegisterISID(Record);
	Manager->FindUniqueData(10001)->EventData.ContentLevel = 0.7f;
	UShopCheckoutComponent* Checkout = Manager->ShopCheckout;
	TestTrue(TEXT("Shop can register logical stock"), Checkout->RegisterShopStock(10001, 200, FTransform::Identity));
	const FGuid Transaction = FGuid::NewGuid(), Customer = FGuid::NewGuid(), Tender = FGuid::NewGuid();
	TestEqual(TEXT("Checkout uses fixed configured quote"), Checkout->BeginCheckout(Transaction, Customer, {10001}), EShopCheckoutResult::Success);
	TestEqual(TEXT("Server total uses actual quantity"), Checkout->ActiveCheckouts[0].TotalCents, int64(7000));
	TestEqual(TEXT("Simultaneous checkout cannot reserve same product"), Checkout->BeginCheckout(FGuid::NewGuid(), FGuid::NewGuid(), {10001}), EShopCheckoutResult::Unavailable);
	TestEqual(TEXT("Amount-only tender cannot fabricate cash"), Checkout->ReceiveTender(Transaction, Tender, 10000), EShopCheckoutResult::InvalidRequest);
	ShopCashTests::FPayment Payment(Manager, Transaction, 10000);
	TestEqual(TEXT("Same physical tender recount is idempotent"), Payment.Recount(), EShopCheckoutResult::Success);
	TestEqual(TEXT("Pending tender does not credit shared wallet"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(0));
	TestEqual(TEXT("Incomplete change prevents commit"), Checkout->CompleteCheckout(Transaction), EShopCheckoutResult::IncorrectChange);
	Payment.AddChange(2900);
	TestEqual(TEXT("Wrong coin change prevents completion"), Checkout->CompleteCheckout(Transaction), EShopCheckoutResult::IncorrectChange);
	Payment.AddChange(100);
	TestEqual(TEXT("Correct payment and change commits"), Checkout->CompleteCheckout(Transaction), EShopCheckoutResult::Success);
	TestEqual(TEXT("Sale cash does not enter bank"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(0));
	TestEqual(TEXT("Customer tender stays as physical team cash"), Checkout->GetTeamCashCents(), int64(10000));
	TestEqual(TEXT("Commit retry cannot duplicate payment"), Checkout->CompleteCheckout(Transaction), EShopCheckoutResult::AlreadyCompleted);
	FShopStockEntry Sold; Checkout->GetStockEntry(10001, Sold);
	TestEqual(TEXT("Product transfers to customer once"), Sold.CustomerOwner, Customer);
	TestFalse(TEXT("Sold product unavailable for another sale"), Checkout->IsAvailableShopStock(10001));
	const FShopCheckoutSaveData Saved = Checkout->ExportCommittedState();
	TestTrue(TEXT("Committed inventory restores"), Checkout->RestoreCommittedState(Saved));
	TestTrue(TEXT("No unfinished transactions restore"), Checkout->ActiveCheckouts.IsEmpty());
	TestEqual(TEXT("Completed transaction remains idempotent after restore"), Checkout->CompleteCheckout(Transaction), EShopCheckoutResult::AlreadyCompleted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopCheckoutCancelTest, "ColossalFizzCo.Shop.CheckoutCancellation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopCheckoutCancelTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	APropManager* Manager = World->SpawnActor<APropManager>();
	if (!Manager) return false;
	Manager->InitializePropManager();
	FPropProductDefinition Product; Product.ProductID = 21; Product.bEnableInstanceUniqueData = true; Product.ConsumableProfile.NumOfPortions = 25; Product.DefaultUniqueData.EventData.ContentID = 7;
	Manager->ProductDefinitions.Add(21, Product);
	FPropContentDefinition Content; Content.ContentID = 7; Content.ExpiryProfile.bHasExpiry = false;
	Manager->ContentDefinitions.Add(7, Content);
	FISIDRuntimeIndex Record; Record.ISID = 10001; Record.ProductID = 21; Record.PackedRuntimeData.RemainingPortions = 25;
	Manager->RegisterISID(Record);
	UShopCheckoutComponent* Checkout = Manager->ShopCheckout;
	const FTransform StockTransform(FVector(100, 200, 300));
	Checkout->RegisterShopStock(10001, 200, StockTransform);
	const FGuid Transaction = FGuid::NewGuid();
	Checkout->BeginCheckout(Transaction, FGuid::NewGuid(), {10001});
	ShopCashTests::FPayment Payment(Manager, Transaction, 10000);
	FTransform CanceledTransform;
	TestTrue(TEXT("Pending stock has a save-time return location"), Checkout->GetCanceledStockTransform(10001, CanceledTransform));
	TestTrue(TEXT("Return location matches original stock"), CanceledTransform.Equals(StockTransform));
	const FShopCheckoutSaveData Saved = Checkout->ExportCommittedState();
	TestTrue(TEXT("Unfinished tender is excluded from snapshot receipts"), Saved.Completed.IsEmpty());
	TestTrue(TEXT("Reload cancels unfinished checkout"), Checkout->RestoreCommittedState(Saved));
	TestTrue(TEXT("Canceled product returns to available inventory"), Checkout->IsAvailableShopStock(10001));
	TestEqual(TEXT("Canceled cash never enters shared wallet"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(0));
	TestEqual(TEXT("Canceled transaction cannot later complete"), Checkout->CompleteCheckout(Transaction), EShopCheckoutResult::NotFound);
	return true;
}
#endif
