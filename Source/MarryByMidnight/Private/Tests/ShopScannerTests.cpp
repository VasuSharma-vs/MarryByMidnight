#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "ShopCashTestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopScannerTest, "MarryByMidnight.Shop.ScannerLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopScannerTest::RunTest(const FString& Parameters)
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
	FPropContentDefinition Content; Content.ContentID = 7; Content.BasePortionPrice = 4; Content.IdealServingTemperature = 4; Content.ExpiryProfile.bHasExpiry = false;
	Manager->ContentDefinitions.Add(7, Content);
	FISIDRuntimeIndex Record; Record.ISID = 10001; Record.ProductID = 21; Record.PackedRuntimeData.RemainingPortions = 25; Record.PackedRuntimeData.Temperature = 4;
	Manager->RegisterISID(Record);
	Manager->FindUniqueData(10001)->EventData.ContentLevel = .7f;
	Manager->ShopCheckout->RegisterShopStock(10001, 0, FTransform::Identity);
	auto MakeScanner = [World]()
	{
		AActor* Owner = World->SpawnActor<AActor>();
		Owner->SetReplicates(true);
		UShopScannerComponent* Scanner = NewObject<UShopScannerComponent>(Owner);
		Owner->AddInstanceComponent(Scanner); Scanner->RegisterComponent();
		return Scanner;
	};
	UShopScannerComponent* Scanner = MakeScanner();
	UShopScannerComponent* Other = MakeScanner();
	TestEqual(TEXT("Unregistered item cannot enter cart"), Scanner->ScanProduct(99999), EShopCheckoutResult::InvalidProduct);
	TestEqual(TEXT("Resting shop stock enters cart"), Scanner->ScanProduct(10001), EShopCheckoutResult::Success);
	TestEqual(TEXT("Rescanning does not duplicate item"), Scanner->ScanProduct(10001), EShopCheckoutResult::Success);
	TestEqual(TEXT("Cart retains one logical item"), Scanner->GetScannerState().Checkout.Items.Num(), 1);
	TestEqual(TEXT("Cart uses canonical price"), Scanner->GetScannerState().Checkout.TotalCents, int64(7000));
	Manager->FindUniqueData(10001)->ContinuousData.Temperature = 100;
	Scanner->ScanProduct(10001);
	TestEqual(TEXT("Rescan replaces old price"), Scanner->GetScannerState().Checkout.TotalCents, int64(5600));
	Manager->FindUniqueData(10001)->ContinuousData.Temperature = 4;
	Scanner->RemoveProduct(10001);
	TestEqual(TEXT("Removal subtracts scanned price before deleting entry"), Scanner->GetScannerState().Checkout.TotalCents, int64(0));
	Scanner->MaxScanDistance = 0;
	TestEqual(TEXT("Invalid scanner range rejects scan"), Scanner->ScanProduct(10001), EShopCheckoutResult::Unavailable);
	Scanner->MaxScanDistance = 300;
	Scanner->ScanProduct(10001); Other->ScanProduct(10001);
	TestEqual(TEXT("Customer identity is required"), Scanner->BeginCheckout(), EShopCheckoutResult::InvalidRequest);
	const FGuid Customer = FGuid::NewGuid();
	Scanner->AssignCustomer(Customer); Other->AssignCustomer(FGuid::NewGuid());
	TestEqual(TEXT("Scanner reserves staged stock"), Scanner->BeginCheckout(), EShopCheckoutResult::Success);
	const FGuid FirstID = Scanner->GetScannerState().Checkout.CheckoutID;
	Scanner->BeginCheckout();
	TestEqual(TEXT("Begin retry keeps transaction identity"), Scanner->GetScannerState().Checkout.CheckoutID, FirstID);
	TestEqual(TEXT("Other scanner cannot reserve same stock"), Other->BeginCheckout(), EShopCheckoutResult::Unavailable);
	ShopCashTests::FPayment FirstPayment(Manager, FirstID, 10000);
	TestEqual(TEXT("Cancel succeeds after receiving pending tender"), Scanner->ClearCart(), EShopCheckoutResult::Success);
	TestEqual(TEXT("Canceled tender never reaches wallet"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(0));
	TestEqual(TEXT("Other scanner can reserve returned stock"), Other->BeginCheckout(), EShopCheckoutResult::Success);
	ShopCashTests::FPayment Payment(Manager, Other->GetScannerState().Checkout.CheckoutID, 9000); Payment.Recount();
	TestEqual(TEXT("Cash retry is not duplicated in scanner view"), Other->GetScannerState().Checkout.TenderCents, int64(9000));
	Payment.AddChange(1900);
	TestEqual(TEXT("Scanner cannot finish with incorrect coin change"), Other->CompleteCheckout(), EShopCheckoutResult::IncorrectChange);
	Payment.AddChange(100);
	TestEqual(TEXT("Scanner commits exact payment"), Other->CompleteCheckout(), EShopCheckoutResult::Success);
	TestEqual(TEXT("Completion changes scanner phase"), Other->GetScannerState().Phase, EShopScannerPhase::Completed);
	TestEqual(TEXT("Sale does not automatically bank register cash"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(0));
	TestEqual(TEXT("Completion retry is harmless"), Other->CompleteCheckout(), EShopCheckoutResult::AlreadyCompleted);
	Other->ClearCart();
	TestFalse(TEXT("Next customer does not inherit completed customer's identity"), Other->GetScannerState().Checkout.CustomerID.IsValid());
	Scanner->GetOwner()->SwapRoles();
	TestEqual(TEXT("Client cannot scan"), Scanner->ScanProduct(10001), EShopCheckoutResult::NotAuthority);
	TestEqual(TEXT("Client cannot assign customer"), Scanner->AssignCustomer(FGuid::NewGuid()), EShopCheckoutResult::NotAuthority);
	TestEqual(TEXT("Client cannot commit"), Scanner->CompleteCheckout(), EShopCheckoutResult::NotAuthority);
	Scanner->GetOwner()->SwapRoles();
	return true;
}
#endif
