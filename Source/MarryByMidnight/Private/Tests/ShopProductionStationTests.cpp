#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "Components/BoxComponent.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ShopInstanceVolumeQuery.h"
#include "Shop/ShopProcurementComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopProductionStationTest,"MarryByMidnight.Shop.ProductionStation",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopProductionStationTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	Manager->ShopSupplies->Catalog=NewObject<UShopSupplyCatalog>(Manager);
	auto* StationActor=World->SpawnActor<AActor>();
	auto* Root=NewObject<USceneComponent>(StationActor); StationActor->SetRootComponent(Root); Root->RegisterComponent(); StationActor->SetActorLocation(FVector(0,0,200));
	auto MakeBox=[&](FVector Offset)
	{
		auto* Box=NewObject<UBoxComponent>(StationActor); StationActor->AddInstanceComponent(Box);
		Box->SetupAttachment(Root); Box->SetBoxExtent(FVector(25)); Box->SetCollisionEnabled(ECollisionEnabled::NoCollision); Box->SetRelativeLocation(Offset); Box->RegisterComponent(); return Box;
	};
	auto* BottleBox=MakeBox(FVector::ZeroVector); auto* CylinderBox=MakeBox(FVector(0,150,0)); auto* CapBox=MakeBox(FVector(100,0,0));
	auto* Filler=NewObject<UBottleFillingComponent>(StationActor); StationActor->AddInstanceComponent(Filler); Filler->RegisterComponent();
	auto* Station=NewObject<UShopProductionStationComponent>(StationActor); StationActor->AddInstanceComponent(Station);
	Station->BottleInput=BottleBox; Station->CylinderInput=CylinderBox; Station->CapInput=CapBox; Station->Filler=Filler; Station->RegisterComponent();
	auto* Empty=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/Assest/DataAssets/DA_EmptyBottel.DA_EmptyBottel"));
	if (!TestNotNull(TEXT("Empty bottle asset"),Empty)) return false;
	auto* Source=DuplicateObject<UInteractablePropDataAsset>(Empty,Manager,TEXT("TestPremixedCylinder")); Source->DefaultContentID=901; Source->DefaultContentLevel=1; Source->NumOfPortions=Empty->NumOfPortions*10;
	FShopSupplyOffer SourceOffer; SourceOffer.OfferID=TEXT("TestCylinder"); SourceOffer.Kind=EShopSupplyKind::ColaCylinder; SourceOffer.Product=Source;
	Manager->ShopSupplies->Catalog.Get()->Offers.Add(SourceOffer);
	FPropContentDefinition Content; Content.ContentID=901; Content.ExpiryProfile.bHasExpiry=false; Manager->RegisterContentDefinition(Content);
	const int32 BottlePID=Manager->RegisterProductFromDataAsset(Empty), SourcePID=Manager->RegisterProductFromDataAsset(Source);
	const int32 Bottle=Manager->GenerateProductInstance(BottlePID,FTransform(FRotator::ZeroRotator,BottleBox->GetComponentLocation(),FVector(.15)));
	const int32 Cylinder=Manager->GenerateProductInstance(SourcePID,FTransform(FRotator::ZeroRotator,CylinderBox->GetComponentLocation(),FVector(.15)));
	TestTrue(TEXT("Actual bottle volume sees persistent ISID"),ShopInstanceVolumeQuery::Query(BottleBox).Contains(Bottle));
	TestTrue(TEXT("Actual cylinder volume sees persistent ISID"),ShopInstanceVolumeQuery::Query(CylinderBox).Contains(Cylinder));
	auto* Pawn=World->SpawnActor<ACharacter>(); auto* PC=World->SpawnActor<APlayerController>(); PC->Possess(Pawn);
	auto* Actions=NewObject<UShopPlayerInteractionComponent>(Pawn); Pawn->AddInstanceComponent(Actions); Actions->RegisterComponent();
	const FGuid Session=Station->View.SessionID;
	if (!TestTrue(TEXT("Server equipment session initialized"),Session.IsValid())) return false;
	TestEqual(TEXT("Stale equipment request rejected"),Actions->ExecuteProductionAction(Station,FGuid::NewGuid(),EShopProductionAction::Fill,Bottle,Cylinder,1),EShopCheckoutResult::OperationConflict);
	TestFalse(TEXT("Rejected request does not start filler"),Filler->bIsFilling);
	TestEqual(TEXT("Wrong source identity outside physical cylinder volume rejected"),Actions->ExecuteProductionAction(Station,Session,EShopProductionAction::Fill,Bottle,Bottle,2),EShopCheckoutResult::Unavailable);
	Pawn->SetActorLocation(FVector(5000,0,0));
	TestEqual(TEXT("Remote station use rejected"),Actions->ExecuteProductionAction(Station,Session,EShopProductionAction::Fill,Bottle,Cylinder,3),EShopCheckoutResult::Unavailable);
	Pawn->SetActorLocation(FVector(0,0,100));
	if (!TestEqual(TEXT("Owned player starts physical fill"),Actions->ExecuteProductionAction(Station,Session,EShopProductionAction::Fill,Bottle,Cylinder,4),EShopCheckoutResult::Success)) return false;
	const float Before=Manager->FindUniqueData(Cylinder)->EventData.ContentLevel;
	Filler->TickComponent(.1f,LEVELTICK_All,nullptr);
	TestTrue(TEXT("Filler credits actual target liquid"),Manager->FindUniqueData(Bottle)->EventData.ContentLevel>0);
	TestTrue(TEXT("Filler debits the purchased source"),Manager->FindUniqueData(Cylinder)->EventData.ContentLevel<Before);
	TestEqual(TEXT("Duplicate action sequence rejected"),Actions->ExecuteProductionAction(Station,Session,EShopProductionAction::StopFilling,0,0,4),EShopCheckoutResult::OperationConflict);
	TestTrue(TEXT("Duplicate request does not stop a valid fill"),Filler->bIsFilling);
	Manager->InitializeInstanceTransform(Cylinder,FTransform(FRotator::ZeroRotator,FVector(5000,0,200),FVector(.15)));
	Station->RefreshInputs();
	TestFalse(TEXT("Removing cylinder from physical input stops transfer"),Filler->bIsFilling);
	return true;
}
#endif
