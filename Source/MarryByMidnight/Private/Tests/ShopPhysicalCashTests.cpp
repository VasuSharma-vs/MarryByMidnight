#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Kismet/GameplayStatics.h"
#include "Props/InteractablePropDataAsset.h"
#include "ShopCashTestHelpers.h"
#include "Shop/ShopATMComponent.h"
#include "Shop/ShopCashRegisterComponent.h"
#include "Props/InteractableProp.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace PhysicalCashTests
{
	UWorld* MakeWorld(bool Physics = false)
	{
		const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(Physics).CreateNavigation(false).CreateAISystem(false);
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
		if (World && GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		return World;
	}
	void Cleanup(UWorld* World) { World->DestroyWorld(false); if (GEngine) GEngine->DestroyWorldContext(World); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopATMConservationTest, "MarryByMidnight.Shop.ATMCashConservation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopATMConservationTest::RunTest(const FString& Parameters)
{
	UWorld* World = PhysicalCashTests::MakeWorld(); if (!World) return false;
	ON_SCOPE_EXIT { PhysicalCashTests::Cleanup(World); };
	auto* Manager = World->SpawnActor<APropManager>(); Manager->bEnableAutosave = false;
	auto* Cash = Manager->ShopCheckout.Get();
	TestEqual(TEXT("Register starts empty"), Cash->GetTeamCashCents(), int64(0));
	Manager->SharedWallet->Credit(FGuid::NewGuid(), 50000, TEXT("InitialBank"));
	const FGuid Withdraw = FGuid::NewGuid(); TArray<int32> IDs, Retry;
	TestEqual(TEXT("Withdraw all supplied denominations"), Cash->WithdrawCoins(Withdraw, {1,2,5,10,20,50,100}, FTransform::Identity, IDs), EShopCheckoutResult::Success);
	if (!TestEqual(TEXT("Seven actual coin identities"), IDs.Num(), 7)) return false;
	TestEqual(TEXT("Bank is debited by 188 dollars"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(31200));
	TestEqual(TEXT("Physical cash holds exactly the withdrawal"), Cash->GetTeamCashCents(), int64(18800));
	TestEqual(TEXT("Withdrawal retry succeeds"), Cash->WithdrawCoins(Withdraw, {1,2,5,10,20,50,100}, FTransform::Identity, Retry), EShopCheckoutResult::Success);
	TestTrue(TEXT("Retry reuses identities"), IDs == Retry);
	TestEqual(TEXT("Changed retry conflicts"), Cash->WithdrawCoins(Withdraw, {100}, FTransform::Identity, Retry), EShopCheckoutResult::OperationConflict);
	for (int32 I = 0; I < IDs.Num(); ++I)
	{
		FShopCashCoin Coin; TestTrue(TEXT("Coin has authoritative monetary identity"), Cash->GetCoin(IDs[I], Coin));
		const auto* Definition = Manager->FindProductDefinition(Coin.ProductID);
		TestTrue(TEXT("Coin uses supplied denomination mesh"), Definition && Definition->StaticMesh == Cash->CurrencyRecipes[I].Product->StaticMesh);
		TestTrue(TEXT("Coin collision supports physics"), Definition && Definition->StaticMesh->GetBodySetup()->AggGeom.GetElementCount() > 0);
		TestTrue(TEXT("Gameplay scale preserves source meshes"), Manager->GetTransform(IDs[I]).GetScale3D().Equals(FVector(.04), .00001));
	}
	const FGuid Deposit = FGuid::NewGuid(); const TArray<int32> Deposited = {IDs[5], IDs[6]};
	TestEqual(TEXT("Deposit retires actual team coins"), Cash->DepositCoins(Deposit, Deposited), EShopCheckoutResult::Success);
	TestEqual(TEXT("Only deposited value returns to bank"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(46200));
	TestEqual(TEXT("Remaining physical cash"), Cash->GetTeamCashCents(), int64(3800));
	TestFalse(TEXT("Deposited coin no longer has live prop identity"), Manager->FindISIDData(IDs[6]) != nullptr);
	TestEqual(TEXT("Deposit retry does not double credit"), Cash->DepositCoins(Deposit, Deposited), EShopCheckoutResult::Success);
	TestEqual(TEXT("Same coin cannot be deposited with new operation"), Cash->DepositCoins(FGuid::NewGuid(), Deposited), EShopCheckoutResult::Unavailable);
	const FString Slot = TEXT("CashTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { UGameplayStatics::DeleteGameInSlot(Slot, 0); };
	TestTrue(TEXT("Real world SaveGame succeeds"), Manager->SavePropDatabase(Slot, 0));
	TestTrue(TEXT("Cash/bank/world cross-references validate"), UShopCheckoutComponent::ValidateCashWorldSaveData(Manager->WorldSaveData));
	TestTrue(TEXT("Reload preserves team cash and completed ATM operations"), Manager->LoadPropDatabase(Slot, 0));
	TestEqual(TEXT("Bank plus cash conserved across reload"), Manager->SharedWallet->GetWalletState().BalanceCents + Cash->GetTeamCashCents(), int64(50000));
	TestEqual(TEXT("Deposit retry remains idempotent after reload"), Cash->DepositCoins(Deposit, Deposited), EShopCheckoutResult::Success);
	auto Corrupt = Cash->ExportCommittedState(); Corrupt.Cash.Coins[0].Dollars = 100;
	TestFalse(TEXT("Currency value tampering is rejected"), UShopCheckoutComponent::ValidateSaveData(Corrupt));
	Corrupt = Cash->ExportCommittedState(); Corrupt.Cash.Coins.RemoveAt(0);
	TestFalse(TEXT("Dropping a team coin from snapshot is rejected"), UShopCheckoutComponent::ValidateSaveData(Corrupt));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopGiantCoinSpacingTest, "MarryByMidnight.Shop.GiantCoinSpacing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopGiantCoinSpacingTest::RunTest(const FString& Parameters)
{
	UWorld* World=PhysicalCashTests::MakeWorld(); if (!World) return false;
	ON_SCOPE_EXIT { PhysicalCashTests::Cleanup(World); };
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false;
	auto* Cash=Manager->ShopCheckout.Get();
	for (auto& Recipe:Cash->CurrencyRecipes) Recipe.MeshScale=1;
	Manager->SharedWallet->Credit(FGuid::NewGuid(),18800,TEXT("IsolatedGiantCoinFunding"));
	TArray<int32> IDs;
	if (!TestEqual(TEXT("Full sized supplied coins withdraw successfully"),Cash->WithdrawCoins(FGuid::NewGuid(),{1,2,5,10,20,50,100},FTransform(FVector(300,0,200)),IDs),EShopCheckoutResult::Success)) return false;
	for (int32 I=0;I<IDs.Num();++I)
	{
		TestTrue(TEXT("Supplied giant coin scale is retained"),Manager->GetTransform(IDs[I]).GetScale3D().Equals(FVector::OneVector));
		FShopCashCoin A; Cash->GetCoin(IDs[I],A);
		const auto* ProductA=Manager->FindProductDefinition(A.ProductID);
		if (!TestNotNull(TEXT("Coin mesh product remains registered"),ProductA)) return false;
		const FBox BoundsA=ProductA->StaticMesh->GetBoundingBox().TransformBy(Manager->GetTransform(IDs[I]));
		for (int32 J=I+1;J<IDs.Num();++J)
		{
			FShopCashCoin B; Cash->GetCoin(IDs[J],B);
			const auto* ProductB=Manager->FindProductDefinition(B.ProductID);
			if (!TestNotNull(TEXT("Other coin retains mesh definition"),ProductB)) return false;
			TestFalse(TEXT("Giant coin delivery bounds do not intersect"),BoundsA.Intersect(ProductB->StaticMesh->GetBoundingBox().TransformBy(Manager->GetTransform(IDs[J]))));
		}
	}
	TestEqual(TEXT("Giant geometry does not create or lose monetary value"),Cash->GetTeamCashCents()+Manager->SharedWallet->GetWalletState().BalanceCents,int64(18800));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopATMAtomicFailureTest, "MarryByMidnight.Shop.ATMAtomicFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopATMAtomicFailureTest::RunTest(const FString& Parameters)
{
	UWorld* World = PhysicalCashTests::MakeWorld(); if (!World) return false;
	ON_SCOPE_EXIT { PhysicalCashTests::Cleanup(World); };
	auto* Manager = World->SpawnActor<APropManager>(); auto* Bank = Manager->SharedWallet.Get(); auto* Cash = Manager->ShopCheckout.Get();
	Bank->Credit(FGuid::NewGuid(), 10000, TEXT("InitialBank"));
	const auto Before = Bank->GetWalletState(); const FGuid AtomicID = FGuid::NewGuid();
	TestEqual(TEXT("Failed resource commit rolls back unpublished bank debit"), Bank->CommitWithResources(AtomicID, -100, TEXT("AtomicTest"), [] { return false; }), EWalletOperationResult::InvalidOperation);
	TestEqual(TEXT("Failed atomic operation preserves balance"), Bank->GetWalletState().BalanceCents, Before.BalanceCents);
	TestEqual(TEXT("Failed atomic operation preserves revision"), Bank->GetWalletState().Revision, Before.Revision);
	TestEqual(TEXT("Same ID is reusable after complete rollback"), Bank->Debit(AtomicID, 100, TEXT("AtomicTest")), EWalletOperationResult::Applied);
	TArray<int32> Coins;
	TestEqual(TEXT("Unsupported denomination is rejected"), Cash->WithdrawCoins(FGuid::NewGuid(), {3}, FTransform::Identity, Coins), EShopCheckoutResult::InvalidRequest);
	Cash->CurrencyRecipes.Reset();
	TestEqual(TEXT("Missing recipe prevents withdrawal"), Cash->WithdrawCoins(FGuid::NewGuid(), {100}, FTransform::Identity, Coins), EShopCheckoutResult::CashUnavailable);
	TestEqual(TEXT("Missing assets cannot destroy bank money"), Bank->GetWalletState().BalanceCents, int64(9900));
	TestTrue(TEXT("Failed withdrawal creates no currency"), Coins.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopAuthoredATMTest, "MarryByMidnight.Shop.AuthoredATMIntegration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopAuthoredATMTest::RunTest(const FString& Parameters)
{
	UWorld* World = PhysicalCashTests::MakeWorld(true); if (!World) return false;
	ON_SCOPE_EXIT { PhysicalCashTests::Cleanup(World); };
	auto* ATMClass = LoadClass<AActor>(nullptr, TEXT("/Game/DirtyActors/BP_ATM.BP_ATM_C"));
	if (!TestNotNull(TEXT("Authored ATM Blueprint loads from disk"), ATMClass)) return false;
	auto* Manager = World->SpawnActor<APropManager>(); Manager->bEnableAutosave = false;
	auto* Terminal = World->SpawnActor<AActor>(ATMClass, FVector(1000, 0, 0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Authored ATM spawns"), Terminal)) return false;
	Terminal->DispatchBeginPlay();
	auto* ATM = Terminal->FindComponentByClass<UShopATMComponent>();
	if (!TestNotNull(TEXT("Authored terminal contains native ATM service"), ATM)
		|| !TestNotNull(TEXT("Tagged deposit volume resolves at BeginPlay"), ATM->DepositVolume.Get())
		|| !TestNotNull(TEXT("Tagged coin output resolves at BeginPlay"), ATM->CoinOutput.Get())) return false;
	TestTrue(TEXT("ATM actor replicates"), Terminal->GetIsReplicated());
	const auto* Housing = Terminal->FindComponentByClass<UStaticMeshComponent>();
	TestTrue(TEXT("ATM uses supplied housing mesh"), Housing && Housing->GetStaticMesh()
		&& Housing->GetStaticMesh()->GetPathName() == TEXT("/Game/Assest/meshs/SM_ATM.SM_ATM"));
	TestFalse(TEXT("Output cannot immediately redeposit withdrawals"), ATM->DepositVolume->Bounds.GetBox().IsInside(ATM->CoinOutput->GetComponentLocation()));
	TestEqual(TEXT("Fresh game bank starts at zero"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(0));
	TestEqual(TEXT("Empty bank cannot produce coins"), ATM->Withdraw(FGuid::NewGuid(), {1}), EShopCheckoutResult::WalletFailure);
	TestEqual(TEXT("Failed withdrawal leaves physical cash empty"), Manager->ShopCheckout->GetTeamCashCents(), int64(0));
	// Funding exists only in this isolated test world; production starting balance remains zero.
	Manager->SharedWallet->Credit(FGuid::NewGuid(), 18800, TEXT("AuthoredATMTestFunding"));
	TestEqual(TEXT("Authored output withdraws seven real denominations"), ATM->Withdraw(FGuid::NewGuid(), {1,2,5,10,20,50,100}), EShopCheckoutResult::Success);
	TestEqual(TEXT("Withdrawal debits bank exactly once"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(0));
	ATM->CollectDeposits();
	TestEqual(TEXT("Withdrawal remains outside deposit mouth"), Manager->ShopCheckout->GetTeamCashCents(), int64(18800));
	const auto Snapshot = Manager->ShopCheckout->ExportCommittedState();
	if (!TestEqual(TEXT("All seven physical identities exist"), Snapshot.Cash.Coins.Num(), 7)) return false;
	for (int32 Index = 0; Index < Snapshot.Cash.Coins.Num(); ++Index)
	{
		const int32 ISID = Snapshot.Cash.Coins[Index].ISID;
		const AActivePropActor* Dispensed = Manager->GetActiveActorFromISID(ISID);
		if (!TestTrue(TEXT("Authored ATM dispenses independently simulating coins"), Dispensed && Dispensed->PropMesh && Dispensed->PropMesh->IsSimulatingPhysics())) return false;
		AActivePropActor* Active = nullptr;
		if (!TestTrue(TEXT("Coin can be picked into the existing active pool"), Manager->PromoteInstanceToASM(ISID, EPropActivationReason::Drop, true, Active))) return false;
		Active->SetActorLocation(ATM->DepositVolume->GetComponentLocation() + FVector((Index - 3) * 6, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
		Manager->SetPropState(ISID, EPropState::Settling);
		if (!TestTrue(TEXT("Dropped coin settles with same ISID in deposit mouth"), Manager->DemoteActorToISM(ISID))) return false;
	}
	ATM->CollectDeposits();
	TestEqual(TEXT("Physical deposit returns the exact withdrawn value"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(18800));
	TestEqual(TEXT("Deposited world coins are retired"), Manager->ShopCheckout->GetTeamCashCents(), int64(0));
	ATM->CollectDeposits();
	TestEqual(TEXT("Polling deposited volume cannot double-credit"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(18800));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopCoinPhysicalVolumeTest, "MarryByMidnight.Shop.CoinPhysicalVolume", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopCoinPhysicalVolumeTest::RunTest(const FString& Parameters)
{
	UWorld* World = PhysicalCashTests::MakeWorld(true); if (!World) return false;
	ON_SCOPE_EXIT { PhysicalCashTests::Cleanup(World); };
	auto* Manager = World->SpawnActor<APropManager>(); Manager->SharedWallet->Credit(FGuid::NewGuid(), 18800, TEXT("InitialBank"));
	TArray<int32> Coins;
	if (!TestEqual(TEXT("Withdraw actual physical coin props"), Manager->ShopCheckout->WithdrawCoins(FGuid::NewGuid(), {1,2,5,10,20,50,100}, FTransform(FVector(0,0,100)), Coins), EShopCheckoutResult::Success)) return false;
	auto* Station = World->SpawnActor<AActor>();
	auto* Box = NewObject<UBoxComponent>(Station); Station->SetRootComponent(Box); Box->SetBoxExtent(FVector(40,10,10)); Box->RegisterComponent(); Station->SetActorLocation(FVector(24,0,100));
	const auto IDs = UShopCashRegisterComponent::QueryCoinsInVolume(Box);
	TestEqual(TEXT("Server volume resolves all seven ISM coin identities"), IDs.Num(), 7);
	for (int32 Coin : Coins) TestTrue(TEXT("Each actual coin is detected once"), IDs.Contains(Coin));
	AActivePropActor* Active = nullptr;
	if (TestTrue(TEXT("Dropped coin promotes into normal physics pool"), Manager->PromoteInstanceToASM(Coins[0], EPropActivationReason::Drop, true, Active)))
	{
		TestTrue(TEXT("Promotion keeps same monetary identity in volume"), UShopCashRegisterComponent::QueryCoinsInVolume(Box).Contains(Coins[0]));
		Manager->SetPropState(Coins[0], EPropState::Settling);
		TestTrue(TEXT("Coin returns to preserved ISM"), Manager->DemoteActorToISM(Coins[0]));
		TestTrue(TEXT("Demotion keeps identity visible to volume"), UShopCashRegisterComponent::QueryCoinsInVolume(Box).Contains(Coins[0]));
		TestEqual(TEXT("Representation transitions cannot create money"), Manager->ShopCheckout->GetTeamCashCents(), int64(18800));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopATMPlayerBoundaryTest, "MarryByMidnight.Shop.ATMPlayerBoundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopATMPlayerBoundaryTest::RunTest(const FString& Parameters)
{
	UWorld* World = PhysicalCashTests::MakeWorld(true); if (!World) return false;
	ON_SCOPE_EXIT { PhysicalCashTests::Cleanup(World); };
	auto* Manager = World->SpawnActor<APropManager>(); Manager->SharedWallet->Credit(FGuid::NewGuid(), 10000, TEXT("InitialBank"));
	auto* Terminal = World->SpawnActor<AActor>();
	auto* Root = NewObject<USceneComponent>(Terminal); Terminal->SetRootComponent(Root); Root->RegisterComponent();
	auto* Deposit = NewObject<UBoxComponent>(Terminal); Deposit->SetupAttachment(Root); Deposit->SetBoxExtent(FVector(10)); Deposit->RegisterComponent();
	auto* Output = NewObject<USceneComponent>(Terminal); Output->SetupAttachment(Root); Output->SetRelativeLocation(FVector(100,0,100)); Output->RegisterComponent();
	auto* ATM = NewObject<UShopATMComponent>(Terminal); Terminal->AddInstanceComponent(ATM); ATM->DepositVolume = Deposit; ATM->CoinOutput = Output; ATM->RegisterComponent();
	auto* Pawn = World->SpawnActor<ACharacter>(); auto* Controller = World->SpawnActor<APlayerController>(); Controller->Possess(Pawn);
	auto* Actions = NewObject<UShopPlayerInteractionComponent>(Pawn); Pawn->AddInstanceComponent(Actions); Actions->RegisterComponent();
	const FGuid ID = FGuid::NewGuid();
	TestEqual(TEXT("Owned nearby player can withdraw"), Actions->ExecuteATMWithdrawal(ATM, {5,5}, ID, 1), EShopCheckoutResult::Success);
	TestEqual(TEXT("Replayed RPC sequence is rejected"), Actions->ExecuteATMWithdrawal(ATM, {5,5}, ID, 1), EShopCheckoutResult::OperationConflict);
	Pawn->SetActorLocation(FVector(10000,0,0));
	TestEqual(TEXT("Remote player cannot withdraw"), Actions->ExecuteATMWithdrawal(ATM, {100}, FGuid::NewGuid(), 2), EShopCheckoutResult::Unavailable);
	Controller->UnPossess();
	TestEqual(TEXT("Unpossessed actor cannot withdraw"), Actions->ExecuteATMWithdrawal(ATM, {100}, FGuid::NewGuid(), 3), EShopCheckoutResult::Unavailable);
	TestEqual(TEXT("Only authorized withdrawal touched bank"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(9000));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopFiniteDrawerTest, "MarryByMidnight.Shop.FiniteDrawerCashExchange", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopFiniteDrawerTest::RunTest(const FString& Parameters)
{
	UWorld* World = PhysicalCashTests::MakeWorld(); if (!World) return false;
	ON_SCOPE_EXIT { PhysicalCashTests::Cleanup(World); };
	auto* Manager = World->SpawnActor<APropManager>(); Manager->InitializePropManager(); auto* Cash = Manager->ShopCheckout.Get();
	FPropProductDefinition Product; Product.ProductID = 21; Product.bEnableInstanceUniqueData = true; Product.ConsumableProfile.NumOfPortions = 25; Product.DefaultUniqueData.EventData.ContentID = 7;
	Manager->ProductDefinitions.Add(21, Product);
	FPropContentDefinition Content; Content.ContentID = 7; Content.BasePortionPrice = 4; Content.IdealServingTemperature = 4; Content.ExpiryProfile.bHasExpiry = false; Manager->ContentDefinitions.Add(7, Content);
	FISIDRuntimeIndex Record; Record.ISID = 10001; Record.ProductID = 21; Record.PackedRuntimeData.Temperature = 4; Record.PackedRuntimeData.RemainingPortions = 25;
	Manager->RegisterISID(Record); Manager->FindUniqueData(10001)->EventData.ContentLevel = .7f;
	Cash->RegisterShopStock(10001, 0, FTransform::Identity);
	Manager->SharedWallet->Credit(FGuid::NewGuid(), 50000, TEXT("InitialBank"));
	TArray<int32> Float; Cash->WithdrawCoins(FGuid::NewGuid(), {20,10}, FTransform::Identity, Float);
	const FGuid Customer = FGuid::NewGuid(), ID = FGuid::NewGuid(), Register = FGuid::NewGuid();
	Cash->BeginCheckout(ID, Customer, {10001}); TArray<int32> Tender;
	Cash->IssueCustomerCoins(FGuid::NewGuid(), Customer, {100}, FTransform::Identity, Tender);
	TestEqual(TEXT("Fabricated coin identity cannot pay"), Cash->ReconcilePhysicalCash(ID, Register, {999999}, {}), EShopCheckoutResult::Unavailable);
	Cash->ReconcilePhysicalCash(ID, Register, Tender, {Float[0]});
	TestEqual(TEXT("Finite drawer cannot invent missing change"), Cash->CompleteCheckout(ID), EShopCheckoutResult::IncorrectChange);
	Cash->ReconcilePhysicalCash(ID, Register, Tender, Float);
	TestEqual(TEXT("Payment coin cannot be banked while escrowed"), Cash->DepositCoins(FGuid::NewGuid(), {Float[0]}), EShopCheckoutResult::Unavailable);
	TestEqual(TEXT("Cancel releases incomplete physical cash"), Cash->CancelCheckout(ID), true);
	FShopCashCoin Coin; Cash->GetCoin(Tender[0], Coin);
	TestEqual(TEXT("Cancel returns tender to its original customer"), Coin.CustomerOwner, Customer);
	TestFalse(TEXT("Cancel releases change escrow"), (Cash->GetCoin(Float[0], Coin), Coin.CheckoutID.IsValid()));
	const FGuid Next = FGuid::NewGuid(); Cash->BeginCheckout(Next, Customer, {10001});
	TestEqual(TEXT("Actual tender and finite change reconcile"), Cash->ReconcilePhysicalCash(Next, Register, Tender, Float), EShopCheckoutResult::Success);
	TestEqual(TEXT("Correct physical payment commits"), Cash->CompleteCheckout(Next), EShopCheckoutResult::Success);
	TestEqual(TEXT("Cash sale leaves bank unchanged after withdrawal"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(47000));
	TestEqual(TEXT("Register owns customer hundred; change belongs to customer"), Cash->GetTeamCashCents(), int64(10000));
	TestEqual(TEXT("Customer change cannot be deposited as shop cash"), Cash->DepositCoins(FGuid::NewGuid(), Float), EShopCheckoutResult::Unavailable);
	TestEqual(TEXT("Sale tender can now be deposited once"), Cash->DepositCoins(FGuid::NewGuid(), Tender), EShopCheckoutResult::Success);
	TestEqual(TEXT("Initial 500 plus 70 sale, without duplicated bank credit"), Manager->SharedWallet->GetWalletState().BalanceCents, int64(57000));
	TestEqual(TEXT("Register emptied by real deposit"), Cash->GetTeamCashCents(), int64(0));
	TestTrue(TEXT("Exchange/cancel history has coherent ownership"), UShopCheckoutComponent::ValidateSaveData(Cash->ExportCommittedState()));
	return true;
}
#endif
