#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "TimerManager.h"
#include "ShopCashTestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace ShopExpiryTests
{
	UWorld* MakeWorld()
	{
		const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
		return UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	}
	void Configure(APropManager* Manager, float Rate = 4)
	{
		Manager->InitializePropManager();
		Manager->WorldElapsedSeconds = 120;
		FPropProductDefinition Product; Product.ProductID = 21; Product.bEnableInstanceUniqueData = true;
		Product.ConsumableProfile.NumOfPortions = 25; Product.DefaultUniqueData.EventData.ContentID = 7;
		Manager->ProductDefinitions.Add(21, Product);
		FPropContentDefinition Content; Content.ContentID = 7; Content.DisplayName = FText::FromString(TEXT("Lemonade Cola"));
		Content.BasePortionPrice = Rate; Content.IdealServingTemperature = 4; Content.TemperaturePenaltyRange = 20;
		Content.ExpiryProfile.bHasExpiry = true; Content.ExpiryProfile.ExpiryLifetimeMinutes = 1;
		Manager->ContentDefinitions.Add(7, Content);
	}
	void AddBottle(APropManager* Manager, int32 ISID, bool bExpired, float Level = 1)
	{
		FISIDRuntimeIndex Record; Record.ISID = ISID; Record.ProductID = 21;
		Record.PackedRuntimeData.RemainingPortions = 25; Record.PackedRuntimeData.Temperature = 24;
		Manager->RegisterISID(Record);
		auto* Unique = Manager->FindUniqueData(ISID);
		Unique->EventData.ContentLevel = Level; Unique->EventData.ContentCreationTime = bExpired ? 0 : 119;
		Manager->ShopCheckout->RegisterShopStock(ISID, 0, FTransform::Identity);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopExpiryCashTest, "MarryByMidnight.Shop.ExpiryCashCompensation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopExpiryCashTest::RunTest(const FString& Parameters)
{
	UWorld* World = ShopExpiryTests::MakeWorld(); if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	APropManager* Manager = World->SpawnActor<APropManager>(); ShopExpiryTests::Configure(Manager);
	for (int32 ID = 10001; ID <= 10003; ++ID) ShopExpiryTests::AddBottle(Manager, ID, ID == 10001);
	UShopCheckoutComponent* Checkout = Manager->ShopCheckout;
	const FGuid ID = FGuid::NewGuid();
	TestEqual(TEXT("Expired products may be deliberately included"), Checkout->BeginCheckout(ID, FGuid::NewGuid(), {10001, 10002, 10003}), EShopCheckoutResult::Success);
	TestEqual(TEXT("Three discounted bottles lock an original 240 dollar bill"), Checkout->ActiveCheckouts[0].TotalCents, int64(24000));
	TestTrue(TEXT("Invoice is explicitly locked before payment"), Checkout->ActiveCheckouts[0].bInvoiceLocked);
	Manager->SharedWallet->Credit(FGuid::NewGuid(), 24000, TEXT("ExistingBank"));
	ShopCashTests::FPayment Payment(Manager, ID, 24000);
	TestEqual(TEXT("Original payment commits"), Checkout->CompleteCheckout(ID), EShopCheckoutResult::Success);
	if (!TestEqual(TEXT("One expired bottle creates exactly one case"), Checkout->PendingExpiryCases.Num(), 1)) return false;
	TestEqual(TEXT("Mandatory normal-base penalty leaves 140 dollars"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(14000));
	FCompletedCheckoutReceipt Receipt;
	Checkout->GetCompletedReceipt(ID, Receipt);
	TestEqual(TEXT("Original invoice remains 240 dollars"), Receipt.TotalCents, int64(24000));
	TestEqual(TEXT("Original bottle identities remain locked"), Receipt.ItemISIDs.Num(), 3);
	const FGuid CaseID = Checkout->PendingExpiryCases[0].CaseID;
	TestEqual(TEXT("Player chooses separate cash compensation"), Checkout->ChooseCashCompensation(CaseID), EShopCheckoutResult::Success);
	TestEqual(TEXT("Second normal-base deduction leaves 40 dollars"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(4000));
	int64 Adjusted = 0, Owed = 0;
	Checkout->GetAdjustedRevenue(ID, Adjusted, Owed);
	TestEqual(TEXT("Transaction history shows 40 adjusted revenue"), Adjusted, int64(4000));
	TestEqual(TEXT("Paid case owes no money"), Owed, int64(0));
	TestEqual(TEXT("Duplicate choice cannot charge twice"), Checkout->ChooseCashCompensation(CaseID), EShopCheckoutResult::AlreadyResolved);
	TestEqual(TEXT("Duplicate original checkout cannot credit twice"), Checkout->CompleteCheckout(ID), EShopCheckoutResult::AlreadyCompleted);
	FShopCheckoutSaveData Saved = Checkout->ExportCommittedState();
	TestTrue(TEXT("Completed invoice/cases/adjustments validate"), UShopCheckoutComponent::ValidateSaveData(Saved));
	TestEqual(TEXT("Money adjustments are separate history records"), Saved.Adjustments.Num(), 2);
	const FShopPostSaleAdjustment Duplicate = Saved.Adjustments[0];
	Saved.Adjustments.Add(Duplicate);
	TestFalse(TEXT("Duplicate adjustment IDs are rejected on restore"), UShopCheckoutComponent::ValidateSaveData(Saved));
	Saved = Checkout->ExportCommittedState(); Saved.ExpiryCases.Reset();
	TestFalse(TEXT("Removing an expired case cannot erase its obligation"), UShopCheckoutComponent::ValidateSaveData(Saved));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopExpiryReplacementTest, "MarryByMidnight.Shop.ExpiryReplacement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopExpiryReplacementTest::RunTest(const FString& Parameters)
{
	UWorld* World = ShopExpiryTests::MakeWorld(); if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	APropManager* Manager = World->SpawnActor<APropManager>(); ShopExpiryTests::Configure(Manager);
	for (int32 ISID = 10001; ISID <= 10006; ++ISID) ShopExpiryTests::AddBottle(Manager, ISID, ISID == 10001 || ISID == 10005, ISID == 10004 ? .99f : 1);
	UShopCheckoutComponent* Checkout = Manager->ShopCheckout;
	const FGuid ID = FGuid::NewGuid(), Customer = FGuid::NewGuid();
	Checkout->BeginCheckout(ID, Customer, {10001, 10002, 10003});
	Manager->SharedWallet->Credit(FGuid::NewGuid(), 24000, TEXT("ExistingBank"));
	ShopCashTests::FPayment Payment(Manager, ID, 24000); Checkout->CompleteCheckout(ID);
	if (!TestEqual(TEXT("Replacement sale created an expiry case"), Checkout->PendingExpiryCases.Num(), 1)) return false;
	const FGuid CaseID = Checkout->PendingExpiryCases[0].CaseID;
	TestEqual(TEXT("Underfilled compensation is not equivalent"), Checkout->ProvideReplacement(CaseID, 10004), EShopCheckoutResult::InvalidReplacement);
	TestEqual(TEXT("Another expired bottle cannot compensate"), Checkout->ProvideReplacement(CaseID, 10005), EShopCheckoutResult::InvalidReplacement);
	TestEqual(TEXT("A full fresh same-cola bottle settles replacement choice"), Checkout->ProvideReplacement(CaseID, 10006), EShopCheckoutResult::Success);
	TestEqual(TEXT("Free replacement does not take another 100 dollars"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(14000));
	FShopStockEntry Replacement; Checkout->GetStockEntry(10006, Replacement);
	TestEqual(TEXT("Replacement leaves shop inventory for the original customer"), Replacement.CustomerOwner, Customer);
	TestFalse(TEXT("Replacement cannot be sold again"), Checkout->IsAvailableShopStock(10006));
	TestEqual(TEXT("Replacement retry is idempotent"), Checkout->ProvideReplacement(CaseID, 10006), EShopCheckoutResult::AlreadyResolved);
	TestEqual(TEXT("Resolved replacement cannot also charge cash"), Checkout->ChooseCashCompensation(CaseID), EShopCheckoutResult::OperationConflict);
	FCompletedCheckoutReceipt Original; Checkout->GetCompletedReceipt(ID, Original);
	TestEqual(TEXT("Replacement does not edit original invoice identities"), Original.ItemISIDs.Num(), 3);
	const FShopCheckoutSaveData Saved = Checkout->ExportCommittedState();
	TestTrue(TEXT("Zero-price separate replacement history validates"), UShopCheckoutComponent::ValidateSaveData(Saved));
	TestEqual(TEXT("Replacement history records zero additional payment"), Saved.Adjustments.Last().DeductedCents, int64(0));
	TestTrue(TEXT("Replacement case/ownership restore"), Checkout->RestoreCommittedState(Saved));
	TestEqual(TEXT("Restored replacement cannot duplicate inventory transfer"), Checkout->ProvideReplacement(CaseID, 10006), EShopCheckoutResult::AlreadyResolved);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopExpiryDebtTest, "MarryByMidnight.Shop.ExpiryDebtPersistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopExpiryDebtTest::RunTest(const FString& Parameters)
{
	UWorld* World = ShopExpiryTests::MakeWorld(); if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	APropManager* Manager = World->SpawnActor<APropManager>(); ShopExpiryTests::Configure(Manager);
	ShopExpiryTests::AddBottle(Manager, 10001, true, .25f);
	UShopCheckoutComponent* Checkout = Manager->ShopCheckout;
	const FGuid ID = FGuid::NewGuid();
	Manager->SharedWallet->Credit(FGuid::NewGuid(), 2000, TEXT("ExistingBank"));
	Checkout->BeginCheckout(ID, FGuid::NewGuid(), {10001}); ShopCashTests::FPayment Payment(Manager, ID, 2000); Checkout->CompleteCheckout(ID);
	if (!TestEqual(TEXT("Debt sale created an expiry case"), Checkout->PendingExpiryCases.Num(), 1)) return false;
	const FGuid CaseID = Checkout->PendingExpiryCases[0].CaseID;
	Checkout->ChooseCashCompensation(CaseID);
	TestEqual(TEXT("Insufficient wallet never becomes negative"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(0));
	int64 Adjusted = 0, Owed = 0; Checkout->GetAdjustedRevenue(ID, Adjusted, Owed);
	TestEqual(TEXT("Both deductions remain owed less 20 already collected"), Owed, int64(18000));
	TestEqual(TEXT("Recognized revenue includes unpaid mandatory obligations"), Adjusted, int64(-18000));
	UPropDatabaseSaveGame* Save = NewObject<UPropDatabaseSaveGame>();
	Save->WorldSaveData.ShopCheckout = Checkout->ExportCommittedState();
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes); FObjectAndNameAsStringProxyArchive Out(Writer, false); Out.ArIsSaveGame = true; Save->Serialize(Out);
	UPropDatabaseSaveGame* Loaded = NewObject<UPropDatabaseSaveGame>();
	FMemoryReader Reader(Bytes); FObjectAndNameAsStringProxyArchive In(Reader, true); In.ArIsSaveGame = true; Loaded->Serialize(In);
	TestTrue(TEXT("Archive retains valid original invoice and debt history"), UShopCheckoutComponent::ValidateSaveData(Loaded->WorldSaveData.ShopCheckout));
	TestTrue(TEXT("Outstanding compensation restores"), Checkout->RestoreCommittedState(Loaded->WorldSaveData.ShopCheckout));
	Checkout->GetAdjustedRevenue(ID, Adjusted, Owed);
	TestEqual(TEXT("Full debt survives archive and restore"), Owed, int64(18000));
	Manager->SharedWallet->Credit(FGuid::NewGuid(), 5000, TEXT("LaterIncome"));
	World->GetTimerManager().Tick(.1f);
	Checkout->GetAdjustedRevenue(ID, Adjusted, Owed);
	TestEqual(TEXT("Later money automatically pays unavoidable debt"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(0));
	TestEqual(TEXT("Partial collection leaves precise remaining obligation"), Owed, int64(13000));
	Manager->SharedWallet->Credit(FGuid::NewGuid(), 20000, TEXT("LaterIncome")); Checkout->CollectOutstandingDeductions();
	Checkout->GetAdjustedRevenue(ID, Adjusted, Owed);
	TestEqual(TEXT("Debt fully settles once later funds suffice"), Owed, int64(0));
	TestEqual(TEXT("Excess funds remain shared"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(7000));
	TestEqual(TEXT("Underfilled sale can create a real net business loss"), Adjusted, int64(-18000));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopExpiryAfterLockTest, "MarryByMidnight.Shop.ExpiryDuringPaymentDisplay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopExpiryAfterLockTest::RunTest(const FString& Parameters)
{
	UWorld* World = ShopExpiryTests::MakeWorld(); if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Manager = World->SpawnActor<APropManager>(); ShopExpiryTests::Configure(Manager); ShopExpiryTests::AddBottle(Manager,10001,false);
	auto* Station = World->SpawnActor<AActor>(); auto* Scanner = NewObject<UShopScannerComponent>(Station); Station->AddInstanceComponent(Scanner); Scanner->RegisterComponent();
	Scanner->AssignCustomer(FGuid::NewGuid()); Scanner->ScanProduct(10001); Scanner->BeginCheckout();
	const FGuid ID = Scanner->GetScannerState().Checkout.CheckoutID;
	TestFalse(TEXT("Bottle is fresh when original invoice locks"),Scanner->GetScannerState().Checkout.Items[0].Snapshot.Price.bExpired);
	Manager->WorldElapsedSeconds = 180;
	ShopCashTests::FPayment Payment(Manager,ID,8000);
	if (!TestEqual(TEXT("Paying a now-expired invoice remains allowed"),Scanner->CompleteCheckout(),EShopCheckoutResult::Success)) return false;
	const auto State = Scanner->GetScannerState();
	TestTrue(TEXT("Scanner exposes committed expiry-at-sale status"),State.Checkout.Items[0].bExpiredAtSale);
	TestEqual(TEXT("Grouped display counts the affected bottle"),State.Groups[0].ExpiredCount,1);
	TestEqual(TEXT("Original locked price remains unchanged"),State.Checkout.TotalCents,int64(8000));
	TestEqual(TEXT("One mandatory compensation case is retained"),Manager->ShopCheckout->PendingExpiryCases.Num(),1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopLiveSavedTest, "MarryByMidnight.Shop.LiveSavedGroupedScanner", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopLiveSavedTest::RunTest(const FString& Parameters)
{
	UWorld* World = ShopExpiryTests::MakeWorld(); if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	APropManager* Manager = World->SpawnActor<APropManager>(); ShopExpiryTests::Configure(Manager, .0596f);
	Manager->ContentDefinitions.FindChecked(7).IdealServingTemperature = 24;
	ShopExpiryTests::AddBottle(Manager, 10001, false); ShopExpiryTests::AddBottle(Manager, 10002, true);
	AActor* Owner = World->SpawnActor<AActor>(); Owner->SetReplicates(true);
	auto* Scanner = NewObject<UShopScannerComponent>(Owner); Owner->AddInstanceComponent(Scanner); Scanner->RegisterComponent();
	Scanner->SetProductInVolume(10001, true); Scanner->SetProductInVolume(10002, true);
	TestEqual(TEXT("Identical product IDs share one row"), Scanner->GetScannerState().Groups.Num(), 1);
	TestEqual(TEXT("Grouped row retains both bottle identities"), Scanner->GetScannerState().Groups[0].BottleISIDs.Num(), 2);
	TestEqual(TEXT("Grouped row preserves individual expiry flags"), Scanner->GetScannerState().Groups[0].ExpiredCount, 1);
	TestEqual(TEXT("Detailed prices retain cents"), Scanner->GetScannerState().Checkout.DetailedTotalCents, int64(298));
	TestEqual(TEXT("Round aggregate once, not each bottle"), Scanner->GetScannerState().Checkout.TotalCents, int64(300));
	Scanner->SetProductInVolume(10002, false);
	TestEqual(TEXT("Unsaved bottle leaves the live cart with the volume"), Scanner->GetScannerState().Checkout.Items.Num(), 1);
	Scanner->SaveProducts(); Scanner->SetProductInVolume(10001, false);
	TestEqual(TEXT("Saved bottle stays after leaving volume"), Scanner->GetScannerState().Checkout.Items.Num(), 1);
	Scanner->SetProductInVolume(10001, true);
	TestEqual(TEXT("Saved reentry cannot duplicate a bottle"), Scanner->GetScannerState().Checkout.Items.Num(), 1);
	Scanner->RemoveProduct(10001); Scanner->SetProductInVolume(10001, true);
	TestTrue(TEXT("Explicitly cleared bottle is suppressed until it leaves the volume"), Scanner->GetScannerState().Checkout.Items.IsEmpty());
	Scanner->SetProductInVolume(10001, false); Scanner->SetProductInVolume(10001, true);
	TestEqual(TEXT("Later new entry can be scanned again"), Scanner->GetScannerState().Checkout.Items.Num(), 1);
	Scanner->AssignCustomer(FGuid::NewGuid()); Scanner->BeginCheckout();
	TestEqual(TEXT("Locked bill cannot be edited by any teammate"), Scanner->RemoveProduct(10001), EShopCheckoutResult::Unavailable);
	TestEqual(TEXT("Locked bill ignores live-volume exits"), Scanner->SetProductInVolume(10001, false), EShopCheckoutResult::Unavailable);
	return true;
}
#endif
