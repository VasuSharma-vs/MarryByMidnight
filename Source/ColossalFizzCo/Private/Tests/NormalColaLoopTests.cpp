#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropDataAsset.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCashRegisterComponent.h"
#include "Shop/ShopCustomerCashComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopShelfStockComponent.h"
#include "Shop/ShopATMComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNormalColaReinvestmentTest,"ColossalFizzCo.Shop.NormalColaReinvestment",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNormalColaReinvestmentTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	if (!TestTrue(TEXT("Real five-flavor catalog initializes"),Manager->ShopSupplies->InitializeCatalog())) return false;
	TestEqual(TEXT("New bank starts at zero"),Manager->SharedWallet->GetWalletState().BalanceCents,int64(0));
	// Exercise refill conversion into already populated flavor ISMs, as in the real stocked map.
	for (const auto& Reference:Manager->ShopSupplies->Catalog.Get()->FinishedBottleProducts)
	{
		auto* Product=Reference.LoadSynchronous();
		const int32 Existing=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(Product),FTransform(FVector(8000,0,200)));
		if (!TestTrue(TEXT("Each flavor has preexisting resting stock"),Existing>0 && Manager->ShopCheckout->RegisterShopStock(Existing,0,Manager->GetTransform(Existing)))) return false;
	}
	CollectGarbage(RF_NoFlags,true);
	if (!TestNotNull(TEXT("Initialized runtime catalog survives garbage collection"),Manager->ShopSupplies->Catalog.Get())) return false;
	TestTrue(TEXT("Normal content is still recognized after garbage collection"),Manager->ShopSupplies->IsNormalContent(901));
	TestEqual(TEXT("All five stocked flavors remain sellable after garbage collection"),Manager->ShopCheckout->GetAvailableColaStockISIDs().Num(),5);
	auto MakeActor=[&](FVector Location)
	{
		auto* Actor=World->SpawnActor<AActor>(); auto* Root=NewObject<USceneComponent>(Actor);
		Actor->SetRootComponent(Root); Root->RegisterComponent(); Actor->SetActorLocation(Location); return Actor;
	};
	auto Box=[&](AActor* Actor,FVector Offset,FVector Extent)
	{
		auto* Result=NewObject<UBoxComponent>(Actor); Actor->AddInstanceComponent(Result); Result->SetupAttachment(Actor->GetRootComponent());
		Result->SetRelativeLocation(Offset); Result->SetBoxExtent(Extent); Result->SetCollisionEnabled(ECollisionEnabled::NoCollision); Result->RegisterComponent(); return Result;
	};
	auto* Counter=MakeActor(FVector(2000,0,100));
	auto* Scanner=NewObject<UShopScannerComponent>(Counter); Counter->AddInstanceComponent(Scanner);
	Scanner->ScanVolume=Box(Counter,FVector(0,0,100),FVector(300,150,150)); Scanner->RegisterComponent();
	auto* Register=NewObject<UShopCashRegisterComponent>(Counter); Counter->AddInstanceComponent(Register);
	Register->Scanner=Scanner; Register->TenderVolume=Box(Counter,FVector(0,250,0),FVector(80));
	Register->ChangeVolume=Box(Counter,FVector(0,-250,0),FVector(80)); Register->RegisterComponent();
	auto* Customer=MakeActor(FVector(2100,0,100));
	auto* Cash=NewObject<UShopCustomerCashComponent>(Customer); Customer->AddInstanceComponent(Cash); Cash->RegisterComponent();
	if (!TestEqual(TEXT("Customer joins real scanner"),Cash->JoinScanner(Scanner),EShopCheckoutResult::Success)) return false;
	// This is isolated fixture starter stock, not a grant of production bank funds.
	auto* Starter=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/NormalColaPrototype/DA_NormalSpicy.DA_NormalSpicy"));
	if (!TestNotNull(TEXT("Authored Normal starter product"),Starter)) return false;
	const int32 StarterPID=Manager->RegisterProductFromDataAsset(Starter);
	for (int32 I=0; I<10; ++I)
	{
		const FTransform Transform(FRotator::ZeroRotator,FVector(1800+(I%5)*100,(I/5)*100-50,200),FVector::OneVector);
		const int32 ID=Manager->GenerateProductInstance(StarterPID,Transform);
		if (!TestTrue(TEXT("Starter stock registers"),Manager->ShopCheckout->RegisterShopStock(ID,0,Transform))) return false;
	}
	Scanner->RefreshLiveProducts();
	if (!TestEqual(TEXT("Actual scan volume finds ten individual bottles"),Scanner->GetScannerState().Checkout.Items.Num(),10)) return false;
	if (!TestEqual(TEXT("Cashier locks starter invoice"),Scanner->BeginCheckout(),EShopCheckoutResult::Success)) return false;
	const auto Bill=Scanner->GetScannerState().Checkout;
	if (!TestEqual(TEXT("Actual tender volume counts customer coins"),Register->RefreshPayment(),EShopCheckoutResult::Success)) return false;
	if (!TestEqual(TEXT("Physical starter sale completes"),Scanner->CompleteCheckout(),EShopCheckoutResult::Success)) return false;
	TestEqual(TEXT("Sale does not also credit bank"),Manager->SharedWallet->GetWalletState().BalanceCents,int64(0));
	TestEqual(TEXT("Sale becomes real register cash"),Manager->ShopCheckout->GetTeamCashCents(),Bill.TotalCents);
	FCompletedCheckoutReceipt Receipt;
	if (!Manager->ShopCheckout->GetCompletedReceipt(Bill.CheckoutID,Receipt)) return false;
	auto* ATMActor=MakeActor(FVector(4000,0,100));
	auto* ATM=NewObject<UShopATMComponent>(ATMActor); ATMActor->AddInstanceComponent(ATM);
	ATM->DepositVolume=Box(ATMActor,FVector::ZeroVector,FVector(120)); ATM->RegisterComponent();
	for (int32 ID:Receipt.TenderCoinISIDs)
		Manager->InitializeInstanceTransform(ID,FTransform(ATM->DepositVolume->GetComponentLocation()));
	ATM->CollectDeposits();
	TestEqual(TEXT("ATM banks exactly the sale cash"),Manager->SharedWallet->GetWalletState().BalanceCents,Bill.TotalCents);
	TestEqual(TEXT("Deposited coins no longer count as cash"),Manager->ShopCheckout->GetTeamCashCents(),int64(0));
	ATM->CollectDeposits();
	TestEqual(TEXT("ATM cannot deposit the same identities twice"),Manager->SharedWallet->GetWalletState().BalanceCents,Bill.TotalCents);

	UClass* BenchClass=LoadClass<AActor>(nullptr,TEXT("/Game/NormalColaPrototype/BP_NormalColaWorkbench.BP_NormalColaWorkbench_C"));
	if (!TestNotNull(TEXT("Saved workbench class"),BenchClass)) return false;
	auto* Bench=World->SpawnActor<AActor>(BenchClass,FVector(0,0,100),FRotator::ZeroRotator);
	auto* Station=Bench->FindComponentByClass<UShopProductionStationComponent>();
	if (!TestTrue(TEXT("Saved workbench resolves physical equipment inputs"),Station && Station->BottleInput && Station->CylinderInput && Station->CapInput && Station->Filler)) return false;
	if (!TestTrue(TEXT("Saved prototype requires two-player sealing"),Station->bRequiresCooperativeCap)) return false;
	auto Operator=[&](FVector Position)
	{
		auto* Pawn=World->SpawnActor<ACharacter>(); Pawn->SetActorLocation(Position); Pawn->SetActorEnableCollision(false);
		Pawn->GetCharacterMovement()->SetMovementMode(MOVE_None);
		auto* Controller=World->SpawnActor<APlayerController>(); Controller->Possess(Pawn);
		auto* Actions=NewObject<UShopPlayerInteractionComponent>(Pawn); Pawn->AddInstanceComponent(Actions); Actions->RegisterComponent(); return Actions;
	};
	auto* Holder=Operator(FVector(-100,-100,200)); auto* Rotator=Operator(FVector(100,-100,200));
	int64 HoldSequence=0,TurnSequence=0;
	auto* Shelf=MakeActor(FVector(6000,0,200));
	auto* Stock=NewObject<UShopShelfStockComponent>(Shelf); Shelf->AddInstanceComponent(Stock);
	Stock->StockVolume=Box(Shelf,FVector::ZeroVector,FVector(150)); Stock->RegisterComponent();
	int64 Costs=0;
	TArray<int32> FinishedBottles,FinishedCaps,Cylinders;
	TMap<int32,FPropInstanceUniqueData> SavedUniqueData;
	Manager->WorldElapsedSeconds=120;
	for (const TCHAR* Flavor:{TEXT("Lemonade"),TEXT("Mint"),TEXT("Sweet"),TEXT("Spicy"),TEXT("ZeroCalorie")})
	{
		TArray<int32> BottleIDs,CapIDs,CylinderIDs;
		auto Buy=[&](FName Offer,TArray<int32>& IDs)
		{
			int64 Cost=0; if (!Manager->ShopSupplies->Quote(Offer,1,Cost)) return false;
			const auto Result=Manager->ShopSupplies->Purchase(FGuid::NewGuid(),Offer,1,FTransform(FRotator::ZeroRotator,FVector(0,500,200),FVector::OneVector),IDs);
			if (Result==EShopPurchaseResult::Success) Costs+=Cost;
			return Result==EShopPurchaseResult::Success && IDs.Num()==1;
		};
		if (!TestTrue(TEXT("Sale proceeds purchase real bottle, cap and cylinder"),Buy(TEXT("EmptyBottle"),BottleIDs) && Buy(TEXT("TwistCap"),CapIDs) && Buy(FName(*(FString(TEXT("Cylinder"))+Flavor)),CylinderIDs))) return false;
		const int32 Bottle=BottleIDs[0],Cap=CapIDs[0],Cylinder=CylinderIDs[0];
		auto Position=[&](int32 ID,USceneComponent* Input)
		{
			if (auto* Actor=Manager->GetActiveActorFromISID(ID)) Actor->PropMesh->SetSimulatePhysics(false);
			Manager->InitializeInstanceTransform(ID,FTransform(FRotator::ZeroRotator,Input->GetComponentLocation(),FVector::OneVector));
		};
		Position(Bottle,Station->BottleInput); Position(Cap,Station->CapInput); Position(Cylinder,Station->CylinderInput);
		Manager->WorldElapsedSeconds+=1;
		const auto Session=Station->View.SessionID;
		if (!TestEqual(TEXT("Workbench starts the purchased Normal cylinder"),Station->Execute(Session,EShopProductionAction::Fill,Bottle,Cylinder),EShopCheckoutResult::Success)) return false;
		for (int32 I=0; I<12 && Station->Filler->bIsFilling; ++I) Station->Filler->TickComponent(.1f,LEVELTICK_All,nullptr);
		TestFalse(TEXT("Normal fill stops automatically at nominal capacity"),Station->Filler->bIsFilling);
		TestEqual(TEXT("Normal bottle is full without overfill"),Manager->FindUniqueData(Bottle)->EventData.ContentLevel,1.f);
		const float SourceLevel=Manager->FindUniqueData(Cylinder)->EventData.ContentLevel;
		AddInfo(FString::Printf(TEXT("%s cylinder after four portions: %.9f"),Flavor,SourceLevel));
		TestTrue(TEXT("Cylinder loses exactly one bottle's four portions"),FMath::IsNearlyEqual(SourceLevel,.9f,1.e-5f));
		if (!TestEqual(TEXT("Purchased compatible cap attaches"),Station->Execute(Session,EShopProductionAction::AttachCap,Bottle,Cap),EShopCheckoutResult::Success)) return false;
		if (!TestEqual(TEXT("Holder claims real station"),Holder->ExecuteProductionAction(Station,Session,EShopProductionAction::HoldCapRole,Bottle,0,++HoldSequence),EShopCheckoutResult::Success)) return false;
		if (!TestEqual(TEXT("Second operator claims rotation"),Rotator->ExecuteProductionAction(Station,Session,EShopProductionAction::RotateCapRole,Bottle,0,++TurnSequence),EShopCheckoutResult::Success)) return false;
		for (int32 Turn=0;Turn<12 && Manager->FindUniqueData(Bottle)->EventData.CapSealed<1;++Turn)
		{
			if (!TestEqual(TEXT("Holder applies pressure"),Holder->ExecuteProductionAction(Station,Session,EShopProductionAction::PressCap,Bottle,0,++HoldSequence),EShopCheckoutResult::Success)) return false;
			if (!TestEqual(TEXT("Rotator turns under sufficient hold"),Rotator->ExecuteProductionAction(Station,Session,EShopProductionAction::TurnCap,Bottle,0,++TurnSequence),EShopCheckoutResult::Success)) return false;
			if (auto* Active=Manager->GetActiveActorFromISID(Bottle)) Active->PropMesh->SetSimulatePhysics(false);
			World->Tick(LEVELTICK_All,.1f);
		}
		if (!TestEqual(TEXT("Cooperative sealing completes"),Manager->FindUniqueData(Bottle)->EventData.CapSealed,1.f)) return false;
		if (!TestTrue(TEXT("Finished bottle settles to ISM with its cap"),Manager->ConvertASMToISM(Bottle,EPropState::AtRest,true,true))) return false;
		const FTransform StockTransform(FRotator::ZeroRotator,Shelf->GetActorLocation(),FVector::OneVector);
		Manager->InitializeInstanceTransform(Bottle,StockTransform); Stock->RefreshStock();
		TestTrue(TEXT("Finished Normal bottle becomes available shelf stock"),Manager->ShopCheckout->GetAvailableColaStockISIDs().Contains(Bottle));
		TestTrue(TEXT("Registered shelf bottle can be opened"),Manager->DetachPhysicalCap(Bottle));
		TestFalse(TEXT("NPC selection rechecks a bottle opened after registration"),Manager->ShopCheckout->IsAvailableColaStockISID(Bottle));
		TestFalse(TEXT("Opened registered bottle is absent from the shopping list"),Manager->ShopCheckout->GetAvailableColaStockISIDs().Contains(Bottle));
		TestTrue(TEXT("Opening preserves the bottle's shop ownership record"),Manager->ShopCheckout->IsAvailableShopStock(Bottle));
		Position(Cap,Stock->StockVolume);
		if (!TestTrue(TEXT("Same physical cap can reseal registered stock"),Manager->AttachPhysicalCap(Bottle,Cap))) return false;
		const auto* BottleDefinition=Manager->FindProductDefinition(Manager->FindISIDData(Bottle)->ProductID);
		if (!TestTrue(TEXT("Resealing restores finished stock"),Manager->ClosePhysicalCap(Bottle,FMath::Lerp(BottleDefinition->TorqueSweetSpotMin,BottleDefinition->TorqueSweetSpotMax,.5f)))) return false;
		// Move already checked goods aside so each flavor uses the same physical fixture station.
		Manager->InitializeInstanceTransform(Bottle,FTransform(FRotator::ZeroRotator,FVector(6000,500,200),FVector::OneVector));
		if (!TestTrue(TEXT("Relocated physical assembly settles together"),Manager->ConvertASMToISM(Bottle,EPropState::AtRest,true,true))) return false;
		Manager->InitializeInstanceTransform(Cylinder,FTransform(FRotator::ZeroRotator,FVector(0,1000,200),FVector::OneVector));
		Manager->FindUniqueData(Bottle)->ContinuousData.SealedPressure=.25f;
		FinishedBottles.Add(Bottle); FinishedCaps.Add(Cap); Cylinders.Add(Cylinder);
		for (int32 ID:{Bottle,Cap,Cylinder}) SavedUniqueData.Add(ID,*Manager->FindUniqueData(ID));
	}
	TestEqual(TEXT("Reinvestment deducts only configured procurement costs"),Manager->SharedWallet->GetWalletState().BalanceCents,Bill.TotalCents-Costs);
	TestTrue(TEXT("Committed cash state remains save-valid"),UShopCheckoutComponent::ValidateSaveData(Manager->ShopCheckout->ExportCommittedState()));
	const FGuid Unfinished=FGuid::NewGuid();
	if (!TestEqual(TEXT("Create an unfinished physical-cap checkout before saving"),Manager->ShopCheckout->BeginCheckout(Unfinished,FGuid::NewGuid(),{FinishedBottles[0]}),EShopCheckoutResult::Success)) return false;
	const FString Slot=TEXT("Automation_NormalCola_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (!TestTrue(TEXT("Real prototype progress writes to an isolated disk slot"),Manager->SavePropDatabase(Slot,0))) return false;
	AddInfo(TEXT("Retained isolated round-trip evidence slot: ")+Slot);
	auto* LoadedWorld=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!LoadedWorld) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(LoadedWorld);
	ON_SCOPE_EXIT { LoadedWorld->EndPlay(EEndPlayReason::Quit); LoadedWorld->DestroyWorld(false); GEngine->DestroyWorldContext(LoadedWorld); };
	LoadedWorld->InitializeActorsForPlay(FURL()); LoadedWorld->GetWorldSettings()->NotifyBeginPlay();
	auto* Loaded=LoadedWorld->SpawnActor<APropManager>(); Loaded->bEnableAutosave=false; Loaded->InitializePropManager();
	if (!TestTrue(TEXT("A fresh world's manager loads the disk save"),Loaded->LoadPropDatabase(Slot,0))) return false;
	TestEqual(TEXT("Disk load preserves earned bank less procurement"),Loaded->SharedWallet->GetWalletState().BalanceCents,Bill.TotalCents-Costs);
	TestEqual(TEXT("Disk load does not recreate deposited sale coins"),Loaded->ShopCheckout->GetTeamCashCents(),int64(0));
	TestEqual(TEXT("Disk load discards incomplete checkouts"),Loaded->ShopCheckout->ActiveCheckouts.Num(),0);
	TestTrue(TEXT("Canceled checkout returns its bottle to available stock"),Loaded->ShopCheckout->IsAvailableShopStock(FinishedBottles[0]));
	FCompletedCheckoutReceipt LoadedReceipt;
	TestTrue(TEXT("Disk load retains the committed receipt"),Loaded->ShopCheckout->GetCompletedReceipt(Bill.CheckoutID,LoadedReceipt));
	for (int32 Index=0; Index<FinishedBottles.Num(); ++Index)
	{
		const int32 Bottle=FinishedBottles[Index],Cap=FinishedCaps[Index];
		const auto* Restored=Loaded->FindUniqueData(Bottle); const auto* RestoredCap=Loaded->FindUniqueData(Cap);
		const auto* Source=Loaded->FindUniqueData(Cylinders[Index]);
		if (!TestTrue(TEXT("All purchased identities survive disk load"),Restored && RestoredCap && Source)) return false;
		const auto& Expected=SavedUniqueData.FindChecked(Bottle);
		TestEqual(TEXT("Container physical age survives"),Restored->EventData.CreationTime,Expected.EventData.CreationTime);
		TestEqual(TEXT("Liquid creation time survives independently"),Restored->EventData.ContentCreationTime,Expected.EventData.ContentCreationTime);
		TestTrue(TEXT("New content has a later timestamp than its bottle"),Restored->EventData.ContentCreationTime>Restored->EventData.CreationTime);
		TestEqual(TEXT("Persistent pressure survives"),Restored->ContinuousData.SealedPressure,.25f);
		TestEqual(TEXT("Filled quantity survives"),Restored->EventData.ContentLevel,1.f);
		TestEqual(TEXT("Closed cap survives"),Restored->EventData.CapSealed,1.f);
		TestEqual(TEXT("Bottle retains physical cap identity"),Restored->EventData.AttachedCapISID,Cap);
		TestEqual(TEXT("Cap retains its bottle identity"),RestoredCap->EventData.AttachedBottleISID,Bottle);
		TestTrue(TEXT("Remaining cylinder portions survive"),FMath::IsNearlyEqual(Source->EventData.ContentLevel,.9f,1.e-5f));
		const auto* Definition=Loaded->FindProductDefinition(Loaded->FindISIDData(Bottle)->ProductID);
		TestTrue(TEXT("Restored cap is assembled at its bottle, including canceled checkout return"),Loaded->GetTransform(Cap).Equals(Definition->CapAttachmentTransform*Loaded->GetTransform(Bottle),.01f));
	}
	return true;
}
#endif
