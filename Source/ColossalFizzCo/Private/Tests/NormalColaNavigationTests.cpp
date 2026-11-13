#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Shop/NormalColaNavigationConfig.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNormalColaNavigationTest,"ColossalFizzCo.Shop.GiantNavigationBootstrap",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNormalColaNavigationTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Config=NewObject<UNormalColaNavigationConfig>(World->GetWorldSettings());
	if (!TestFalse(TEXT("Profile specifies a concrete engine navigation service"),Config->NavigationSystemClass.IsNull())) return false;
	World->GetWorldSettings()->SetNavigationSystemConfigOverride(Config);
	FNavigationSystem::AddNavigationSystemToWorld(*World,FNavigationSystemRunMode::GameMode,Config,true,true);
	auto* Nav=UNavigationSystemV1::GetCurrent(World);
	if (!TestNotNull(TEXT("World bootstrap creates the navigation service"),Nav)) return false;
	const auto& Agents=Nav->GetSupportedAgents();
	if (!TestEqual(TEXT("Prototype has one world-local customer agent"),Agents.Num(),1)) return false;
	UClass* Customer=LoadClass<ACharacter>(nullptr,TEXT("/Game/NormalColaPrototype/BP_NormalColaCustomer.BP_NormalColaCustomer_C"));
	if (!TestNotNull(TEXT("Actual prototype customer class"),Customer)) return false;
	const auto* Capsule=Customer->GetDefaultObject<ACharacter>()->GetCapsuleComponent();
	TestEqual(TEXT("Agent radius matches actual giant collision"),Agents[0].AgentRadius,Capsule->GetScaledCapsuleRadius());
	TestEqual(TEXT("Agent height matches actual giant collision"),Agents[0].AgentHeight,Capsule->GetScaledCapsuleHalfHeight()*2);
	TestTrue(TEXT("Giant navigation uses walking"),bool(Agents[0].bCanWalk));
	TestEqual(TEXT("Inherited static engine config cannot override the world-local dynamic profile"),
		GetDefault<ANormalColaNavigationData>()->GetRuntimeGenerationMode(),ERuntimeGenerationType::Dynamic);
	auto* Data=World->SpawnActor<ANormalColaNavigationData>();
	if (TestNotNull(TEXT("Native giant navigation data spawns"),Data))
		TestEqual(TEXT("Spawned navigation remains dynamically generated"),Data->GetRuntimeGenerationMode(),ERuntimeGenerationType::Dynamic);
	return true;
}
#endif
