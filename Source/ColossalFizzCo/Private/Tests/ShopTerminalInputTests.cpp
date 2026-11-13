#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/ShopScannerComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopTerminalViewTest,"ColossalFizzCo.Shop.CheckoutViewInteraction",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopTerminalViewTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Pawn=World->SpawnActor<ACharacter>(); auto* PC=World->SpawnActor<APlayerController>(); PC->Possess(Pawn);
	auto* Actions=NewObject<UShopPlayerInteractionComponent>(Pawn); Pawn->AddInstanceComponent(Actions); Actions->RegisterComponent();
	auto* Counter=World->SpawnActor<AActor>(); auto* Root=NewObject<USceneComponent>(Counter);
	Counter->SetRootComponent(Root); Root->RegisterComponent(); Counter->SetActorLocation(FVector(0,700,460));
	auto* Housing=NewObject<UBoxComponent>(Counter); Counter->AddInstanceComponent(Housing); Housing->SetupAttachment(Root);
	Housing->SetRelativeLocation(FVector(0,0,-230)); Housing->SetBoxExtent(FVector(850,620,230));
	Housing->SetCollisionProfileName(TEXT("BlockAll")); Housing->RegisterComponent();
	auto* Control=NewObject<USceneComponent>(Counter); Counter->AddInstanceComponent(Control); Control->SetupAttachment(Root);
	Control->ComponentTags.Add(TEXT("ShopInteractionPoint")); Control->SetRelativeLocation(FVector(350,-610,20)); Control->RegisterComponent();
	auto* Scanner=NewObject<UShopScannerComponent>(Counter); Counter->AddInstanceComponent(Scanner); Scanner->RegisterComponent();
	const FVector Position(350,-80,556), Eye(350,-80,620), Direction=FVector(0,200,-160).GetSafeNormal();
	Pawn->SetActorLocation(Position);
	FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(CheckoutLegacyTrace),false,Pawn);
	TestFalse(TEXT("Legacy 150cm trace cannot hit the counter from the player access deck"),World->LineTraceSingleByChannel(Hit,Eye,Eye+Direction*150,ECC_Visibility,Params));
	TestTrue(TEXT("Existing station reach accepts the front controls"),Actions->IsStationReachable(Counter));
	TestEqual(TEXT("Station-specific view trace reaches the actual giant desk collision"),Actions->FindTerminalInView(Eye,Direction),Counter);
	TestNull(TEXT("Looking away cannot select a nearby checkout"),Actions->FindTerminalInView(Eye,-Direction));
	TestNull(TEXT("Invalid view direction is rejected"),Actions->FindTerminalInView(Eye,FVector::ZeroVector));
	Pawn->SetActorLocation(FVector(-700,-80,556));
	TestNull(TEXT("Looking at a distant desk corner does not bypass the authored control reach"),Actions->FindTerminalInView(FVector(-700,-80,620),Direction));
	Pawn->SetActorLocation(Position);
	auto* Blocker=World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Blocker); Blocker->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(40,20,40)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
	Blocker->SetActorLocation(Eye+Direction*70);
	TestNull(TEXT("An obstructing object prevents opening through it"),Actions->FindTerminalInView(Eye,Direction));
	Blocker->Destroy();
	PC->UnPossess();
	TestNull(TEXT("An unpossessed pawn cannot open a shop station"),Actions->FindTerminalInView(Eye,Direction));
	return true;
}
#endif
