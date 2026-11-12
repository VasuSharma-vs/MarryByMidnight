#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/ShopCheckoutComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopManualCappingTest,"MarryByMidnight.Shop.ManualWheelRotator",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopManualCappingTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	const auto* Template=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/Assest/DataAssets/DA_EmptyBottel.DA_EmptyBottel"));
	if (!TestNotNull(TEXT("Actual bottle source mesh exists"),Template)) return false;
	auto* BottleProduct=DuplicateObject<UInteractablePropDataAsset>(Template,Manager);
	BottleProduct->BottleMaterialType=EBottleMaterialType::Plastic; BottleProduct->DefaultContentID=901; BottleProduct->DefaultContentLevel=1;
	BottleProduct->NumOfPortions=4; BottleProduct->DefaultCapSealed=0; BottleProduct->SupportedCapMask=1; BottleProduct->bHasExpiry=false;
	BottleProduct->CustomActiveActorClass=AColaPropActor::StaticClass(); BottleProduct->FragmentAssembly=nullptr; BottleProduct->FractureFragments.Reset();
	FPropContentDefinition Content; Content.ContentID=901; Content.ExpiryProfile.bHasExpiry=false; Manager->RegisterContentDefinition(Content);
	auto* CapProduct=DuplicateObject<UInteractablePropDataAsset>(Template,Manager);
	CapProduct->bIsBottleCap=true; CapProduct->NumOfPortions=0; CapProduct->DefaultContentID=0; CapProduct->DefaultContentLevel=0;
	CapProduct->DefaultCapType=EBottleCapType::Twist; CapProduct->CustomActiveActorClass=AActivePropActor::StaticClass();
	auto* Actor=World->SpawnActor<AActor>(); auto* Root=NewObject<USceneComponent>(Actor); Actor->SetRootComponent(Root); Root->RegisterComponent();
	Actor->SetActorLocation(FVector(0,0,200));
	auto* Box=NewObject<UBoxComponent>(Actor); Actor->AddInstanceComponent(Box); Box->SetupAttachment(Root);
	Box->SetBoxExtent(FVector(100,100,2000)); Box->SetCollisionEnabled(ECollisionEnabled::NoCollision); Box->RegisterComponent();
	auto* Station=NewObject<UShopProductionStationComponent>(Actor); Actor->AddInstanceComponent(Station); Station->BottleInput=Box;
	Station->bRequiresCooperativeCap=true; Station->RegisterComponent();
	const int32 Bottle=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(BottleProduct),FTransform(FVector(0,0,200)));
	const int32 Cap=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(CapProduct),FTransform(FVector(0,50,200)));
	if (!TestTrue(TEXT("Physical twist cap attaches through existing manager"),Manager->AttachPhysicalCap(Bottle,Cap))) return false;
	// This service test holds the input stationary; rendered physics is a separate acceptance layer.
	if (auto* Active=Manager->GetActiveActorFromISID(Bottle)) if (Active->PropMesh) Active->PropMesh->SetSimulatePhysics(false);
	auto Player=[&](FVector Location)
	{
		auto* Pawn=World->SpawnActor<ACharacter>(); Pawn->SetActorLocation(Location); Pawn->SetActorEnableCollision(false);
		Pawn->GetCharacterMovement()->SetMovementMode(MOVE_None);
		auto* PC=World->SpawnActor<APlayerController>(); PC->Possess(Pawn);
		auto* Actions=NewObject<UShopPlayerInteractionComponent>(Pawn); Pawn->AddInstanceComponent(Actions); Actions->RegisterComponent(); return Actions;
	};
	auto* Holding=Player(FVector(-100,0,200)); auto* Rotating=Player(FVector(100,0,200));
	int64 HoldSequence=0,TurnSequence=0;
	auto Hold=[&](EShopProductionAction Action){ return Holding->ExecuteProductionAction(Station,Station->View.SessionID,Action,Bottle,0,++HoldSequence); };
	auto Turn=[&](EShopProductionAction Action){ return Rotating->ExecuteProductionAction(Station,Station->View.SessionID,Action,Bottle,0,++TurnSequence); };
	TestEqual(TEXT("Single action closure cannot bypass cooperative station"),Hold(EShopProductionAction::CloseCap),EShopCheckoutResult::Unavailable);
	if (!TestTrue(TEXT("Fixture bottle registers as shop stock"),Manager->ShopCheckout->RegisterShopStock(Bottle,0,Manager->GetTransform(Bottle)))) return false;
	const FGuid Pending=FGuid::NewGuid();
	if (!TestEqual(TEXT("Fixture bill locks the same physical bottle"),Manager->ShopCheckout->BeginCheckout(Pending,FGuid::NewGuid(),{Bottle}),EShopCheckoutResult::Success)) return false;
	TestEqual(TEXT("Locked invoice cannot enter manual cap operation"),Hold(EShopProductionAction::HoldCapRole),EShopCheckoutResult::Unavailable);
	TestTrue(TEXT("Canceling fixture bill releases stock"),Manager->ShopCheckout->CancelCheckout(Pending));
	TestEqual(TEXT("First player claims cap holding"),Hold(EShopProductionAction::HoldCapRole),EShopCheckoutResult::Success);
	TestEqual(TEXT("Same player cannot take both roles"),Hold(EShopProductionAction::RotateCapRole),EShopCheckoutResult::OperationConflict);
	TestEqual(TEXT("Second player claims rotation"),Turn(EShopProductionAction::RotateCapRole),EShopCheckoutResult::Success);
	TestEqual(TEXT("No rotation without cap holding force"),Turn(EShopProductionAction::TurnCap),EShopCheckoutResult::OperationConflict);
	TestEqual(TEXT("Other operator cannot inject holding presses"),Turn(EShopProductionAction::PressCap),EShopCheckoutResult::OperationConflict);
	Station->UpdateManualCapping(1);
	TestTrue(TEXT("Unheld cap loses fractional liquid"),Manager->FindUniqueData(Bottle)->EventData.ContentLevel<1);
	TestTrue(TEXT("Holding effectiveness declines before closure"),Station->View.HoldingEfficiency<1);
	Station->UpdateManualCapping(1);
	TestEqual(TEXT("Gradual leakage coalesces one physical release episode"),Manager->ReleasedLiquids.Num(),1);
	const auto* Remaining=Manager->FindUniqueData(Bottle);
	TestTrue(TEXT("Leaked plus remaining portions conserve actual liquid"),FMath::IsNearlyEqual(Manager->ReleasedLiquids[0].Portions+double(Remaining->EventData.ContentLevel)*4,4.,1.e-5));
	const float StartingEfficiency=Station->View.HoldingEfficiency;
	TestEqual(TEXT("Real holder contributes a press"),Hold(EShopProductionAction::PressCap),EShopCheckoutResult::Success);
	TestEqual(TEXT("Same-frame press spam is rejected"),Hold(EShopProductionAction::PressCap),EShopCheckoutResult::OperationConflict);
	for (int32 Step=0;Step<15 && Manager->FindUniqueData(Bottle)->EventData.CapSealed<1;++Step)
	{
		World->Tick(LEVELTICK_All,.12f);
		if (auto* Active=Manager->GetActiveActorFromISID(Bottle)) if (Active->PropMesh) Active->PropMesh->SetSimulatePhysics(false);
		TestEqual(TEXT("Holder maintains cap force"),Hold(EShopProductionAction::PressCap),EShopCheckoutResult::Success);
		TestEqual(TEXT("Independent rotator advances closure"),Turn(EShopProductionAction::TurnCap),EShopCheckoutResult::Success);
		if (Manager->FindUniqueData(Bottle)->EventData.CapSealed<1)
			TestTrue(TEXT("Effectiveness decays despite continued pressing"),Station->View.HoldingEfficiency<=StartingEfficiency);
	}
	TestEqual(TEXT("Two cooperating players complete seal"),Manager->FindUniqueData(Bottle)->EventData.CapSealed,1.f);
	TestEqual(TEXT("Same physical cap identity remains attached"),Manager->FindUniqueData(Bottle)->EventData.AttachedCapISID,Cap);
	TestEqual(TEXT("Completed station releases both operator roles"),Station->View.ManualBottleISID,0);
	Station->UpdateManualCapping(1);
	TestEqual(TEXT("Releasing pressure hold after closure does not reopen cap"),Manager->FindUniqueData(Bottle)->EventData.CapSealed,1.f);
	const int32 Fresh=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(BottleProduct),FTransform(FVector(10,0,200)));
	const int32 FreshCap=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(CapProduct),FTransform(FVector(10,50,200)));
	if (!TestTrue(TEXT("Fresh bottle receives its own physical cap"),Manager->AttachPhysicalCap(Fresh,FreshCap))) return false;
	if (auto* Active=Manager->GetActiveActorFromISID(Fresh)) Active->PropMesh->SetSimulatePhysics(false);
	auto PressFresh=[&](){return Holding->ExecuteProductionAction(Station,Station->View.SessionID,EShopProductionAction::PressCap,Fresh,0,++HoldSequence);};
	auto TurnFresh=[&](){return Rotating->ExecuteProductionAction(Station,Station->View.SessionID,EShopProductionAction::TurnCap,Fresh,0,++TurnSequence);};
	TestEqual(TEXT("First contextual G claims the holder and contributes in one request"),PressFresh(),EShopCheckoutResult::Success);
	TestTrue(TEXT("First press establishes real holding force"),Station->View.CapHolder==Holding->GetOwner() && Station->View.HoldingForce>0);
	TestEqual(TEXT("Auto-claim cannot let the holder rotate too"),Holding->ExecuteProductionAction(Station,Station->View.SessionID,EShopProductionAction::TurnCap,Fresh,0,++HoldSequence),EShopCheckoutResult::OperationConflict);
	TestEqual(TEXT("Proper initial hold avoids mandatory role-selection leakage"),Manager->FindUniqueData(Fresh)->EventData.ContentLevel,1.f);
	TestEqual(TEXT("Teammate R independently claims rotation and turns"),TurnFresh(),EShopCheckoutResult::Success);
	for (int32 Step=0;Step<12 && Manager->FindUniqueData(Fresh)->EventData.CapSealed<1;++Step)
	{
		World->Tick(LEVELTICK_All,.12f);
		if (auto* Active=Manager->GetActiveActorFromISID(Fresh)) Active->PropMesh->SetSimulatePhysics(false);
		TestEqual(TEXT("Fresh bottle holder maintains real force"),PressFresh(),EShopCheckoutResult::Success);
		TestEqual(TEXT("Fresh bottle teammate continues turning"),TurnFresh(),EShopCheckoutResult::Success);
	}
	TestEqual(TEXT("Cooperation can produce a genuinely full sealed replacement"),Manager->FindUniqueData(Fresh)->EventData.CapSealed,1.f);
	TestEqual(TEXT("Full bottle is preserved without artificial top-up or relaxed capacity checks"),Manager->FindUniqueData(Fresh)->EventData.ContentLevel,1.f);
	TestEqual(TEXT("Lossless workflow adds no liquid-release entry"),Manager->ReleasedLiquids.Num(),1);
	return true;
}
#endif
