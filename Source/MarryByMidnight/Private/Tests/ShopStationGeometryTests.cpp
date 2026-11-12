#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Shop/ShopInteractionGeometry.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopStationGeometryTest,"MarryByMidnight.Shop.GiantStationReach",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopStationGeometryTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Counter=World->SpawnActor<AActor>(); auto* Root=NewObject<USceneComponent>(Counter);
	Counter->SetRootComponent(Root); Root->RegisterComponent(); Counter->SetActorLocation(FVector(0,700,460));
	auto* Customer=World->SpawnActor<ACharacter>(); Customer->GetCapsuleComponent()->SetCapsuleSize(204,528);
	Customer->SetActorLocation(FVector(-550,-250,530));
	TestTrue(TEXT("Giant counter origin is beyond the customer's legitimate reach"),ShopInteractionGeometry::CustomerDistanceToStation(Customer,Counter)>350);
	auto Control=[&](FName Tag,FVector Location)
	{
		auto* Point=NewObject<USceneComponent>(Counter); Counter->AddInstanceComponent(Point); Point->SetupAttachment(Root);
		Point->ComponentTags.Add(Tag); Point->SetRelativeLocation(Location); Point->RegisterComponent();
	};
	Control(TEXT("ShopCustomerInteractionPoint"),FVector(-450,-610,0));
	TestTrue(TEXT("Giant handover uses capsule surface and authored customer point"),ShopInteractionGeometry::CustomerDistanceToStation(Customer,Counter)<350);
	const FVector Player(350,0,556);
	TestTrue(TEXT("A tagged customer point cannot grant a player remote access"),ShopInteractionGeometry::DistanceToStation(Counter,Player)>350);
	Control(TEXT("ShopInteractionPoint"),FVector(350,-610,20));
	TestTrue(TEXT("Player deck reaches its own controls without increasing range"),ShopInteractionGeometry::DistanceToStation(Counter,Player)<350);
	TestTrue(TEXT("Distant player still fails the existing range limit"),ShopInteractionGeometry::DistanceToStation(Counter,FVector(3000,0,556))>350);
	Customer->SetActorLocation(FVector(-3000,-250,530));
	TestTrue(TEXT("Distant customer cannot pay through a handover marker"),ShopInteractionGeometry::CustomerDistanceToStation(Customer,Counter)>350);
	return true;
}
#endif
