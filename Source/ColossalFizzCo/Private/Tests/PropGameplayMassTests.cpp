#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropDataAsset.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropGameplayMassTest,"ColossalFizzCo.Props.GameplayMassPooling",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropGameplayMassTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Prop=World->SpawnActor<AActivePropActor>();
	Prop->PropMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
	Prop->SetActorEnableCollision(true); Prop->PropMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Prop->PropMesh->SetSimulatePhysics(true);
	Prop->PropMesh->SetMassOverrideInKg(NAME_None,12.f,true);
	auto* Asset=NewObject<UInteractablePropDataAsset>(); Asset->GameplayMassKg=5;
	const auto Light=FPropProductDefinition::FromDataAsset(Asset,1,0);
	TestEqual(TEXT("Shared definition retains gameplay mass"),Light.GameplayMassKg,5.f);
	Prop->OnApplySharedDefinitions(&Light,nullptr);
	TestTrue(TEXT("Giant geometry uses configured gameplay mass"),FMath::IsNearlyEqual(Prop->PropMesh->GetMass(),5.f));
	TestTrue(TEXT("Mass does not resize geometry"),Prop->GetActorScale3D().Equals(FVector::OneVector));
	FPropProductDefinition Legacy;
	Prop->OnApplySharedDefinitions(&Legacy,nullptr);
	TestTrue(TEXT("Switching to legacy product restores authored mass"),FMath::IsNearlyEqual(Prop->PropMesh->GetMass(),12.f));
	Prop->OnApplySharedDefinitions(&Light,nullptr); Prop->ResetTransientPoolState();
	TestTrue(TEXT("Pool reset restores original mass"),FMath::IsNearlyEqual(Prop->PropMesh->GetMass(),12.f));
	TestTrue(TEXT("Pool reset restores original override policy"),bool(Prop->PropMesh->BodyInstance.bOverrideMass));
	Prop->PropMesh->SetMassOverrideInKg(NAME_None,0,false);
	Prop->OnApplySharedDefinitions(&Light,nullptr); Prop->ResetTransientPoolState();
	TestFalse(TEXT("Automatically computed legacy mass also restores correctly"),bool(Prop->PropMesh->BodyInstance.bOverrideMass));
	return true;
}
#endif
