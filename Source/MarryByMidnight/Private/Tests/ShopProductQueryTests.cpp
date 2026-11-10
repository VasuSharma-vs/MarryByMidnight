#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Components/ASMComponent.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopProductQueryTest, "MarryByMidnight.Shop.ProductScanQuery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopProductQueryTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	APropManager* Manager = World->SpawnActor<APropManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	FPropProductDefinition Product;
	Product.ProductID = 21; Product.bEnableInstanceUniqueData = true;
	Product.ConsumableProfile.NumOfPortions = 25; Product.DefaultUniqueData.EventData.ContentID = 7;
	Manager->ProductDefinitions.Add(21, Product);
	FPropContentDefinition Content;
	Content.ContentID = 7; Content.DisplayName = FText::FromString(TEXT("Configured Cola"));
	Content.BasePortionPrice = 4; Content.IdealServingTemperature = 4; Content.TemperaturePenaltyRange = 10;
	Content.ExpiryProfile.bHasExpiry = true; Content.ExpiryProfile.ExpiryLifetimeMinutes = 2;
	Manager->ContentDefinitions.Add(7, Content);
	FISIDRuntimeIndex Record;
	Record.ISID = 10001; Record.ProductID = 21; Record.Representation = EPropRepresentation::ISM;
	Record.PackedRuntimeData.RemainingPortions = 25; Record.PackedRuntimeData.Temperature = 9;
	Manager->RegisterISID(Record);
	FPropInstanceUniqueData* Unique = Manager->FindUniqueData(10001);
	Unique->EventData.ContentLevel = 0.7f; Unique->EventData.CreationTime = 100; Unique->EventData.ContentCreationTime = 990;
	Unique->EventData.Strength = 0.81f; Unique->EventData.CapSealed = 1;
	Manager->WorldElapsedSeconds = 1000;
	const int32 UniqueCount = Manager->UniqueDataByISID.Num();
	const int32 PoolCount = Manager->ASMComponent->ASMActors.Num();
	FShopProductScanData Scan;
	TestTrue(TEXT("Resting product can be scanned"), Manager->GetProductScanData(10001, Scan));
	TestEqual(TEXT("Content-defined range is used instead of hardcoded range"), Scan.Price.SaleCents, int64(6300));
	TestEqual(TEXT("Content age is independent of old container age"), Scan.ContentAgeSeconds, 10.0);
	TestEqual(TEXT("Container history is included unchanged"), Scan.ContainerCreationTime, 100.0);
	TestEqual(TEXT("Scanner includes actual content identity"), Scan.ContentID, 7);
	TestEqual(TEXT("Integrity is exposed without changing price rules"), Scan.Strength, 0.81f);
	TestEqual(TEXT("Scanning preserves ISM representation"), Manager->FindISIDData(10001)->Representation, EPropRepresentation::ISM);
	TestEqual(TEXT("Scanning does not allocate an active actor"), Manager->ASMComponent->ASMActors.Num(), PoolCount);
	TestEqual(TEXT("Scanning does not allocate unique state"), Manager->UniqueDataByISID.Num(), UniqueCount);
	Manager->ShopCheckout->RegisterShopStock(10001, 0, FTransform::Identity);
	const FGuid CheckoutID = FGuid::NewGuid();
	TestEqual(TEXT("Checkout accepts canonical scanner quote"), Manager->ShopCheckout->BeginCheckout(CheckoutID, FGuid::NewGuid(), {10001}), EShopCheckoutResult::Success);
	TestEqual(TEXT("Scanner and checkout total agree"), Manager->ShopCheckout->ActiveCheckouts[0].TotalCents, Scan.Price.SaleCents);
	Manager->ShopCheckout->CancelCheckout(CheckoutID);
	Unique->EventData.ContentCreationTime = 880;
	TestTrue(TEXT("Expired product still yields inspectable details"), Manager->GetProductScanData(10001, Scan));
	TestTrue(TEXT("Exact expiry boundary is expired"), Scan.Price.bExpired);
	TestEqual(TEXT("Expired product revenue is zero"), Scan.Price.SaleCents, int64(0));
	TestEqual(TEXT("Expired-product compensation uses full nominal amount"), Scan.Price.CompensationCents, int64(10000));
	TestEqual(TEXT("Checkout refuses an expired quote"), Manager->ShopCheckout->BeginCheckout(FGuid::NewGuid(), FGuid::NewGuid(), {10001}), EShopCheckoutResult::ExpiredProduct);
	TestFalse(TEXT("Unknown identity fails inspection"), Manager->GetProductScanData(99999, Scan));
	TestEqual(TEXT("Failed read clears previous identity"), Scan.ISID, 0);
	TestFalse(TEXT("Failed read cannot retain a previously valid quote"), Scan.Price.bValid);
	TestEqual(TEXT("Unknown identity does not allocate state"), Manager->UniqueDataByISID.Num(), UniqueCount);
	Unique->EventData.ContentCreationTime = 1001;
	TestFalse(TEXT("Future content timestamps cannot hide expiry"), Manager->GetProductScanData(10001, Scan));
	Unique->EventData.ContentCreationTime = 990;
	Manager->ContentDefinitions.FindChecked(7).ExpiryProfile.ExpiryLifetimeMinutes = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("Invalid expiry configuration fails safely"), Manager->GetProductScanData(10001, Scan));
	return true;
}
#endif
