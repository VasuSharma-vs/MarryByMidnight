#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Shop/ShopProcurementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBottleRocketTravelTest,"ColossalFizzCo.Props.BottleRocketTravel",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBottleRocketTravelTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	if (!Manager->ShopSupplies->InitializeCatalog()) return false;
	auto* Product=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/NormalColaPrototype/DA_NormalLemonade.DA_NormalLemonade"));
	if (!Product) return false;
	const int32 ID=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(Product),FTransform(FVector(0,0,2000)));
	if (!Manager->SetInstanceToUnsettled(ID)) return false;
	auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(ID)); if (!Bottle) return false;
	Bottle->RocketRandomSeed=12345; Bottle->RocketDirectionVariation=0;
	Bottle->RocketMinDistance=Bottle->RocketMaxDistance=600;
	Bottle->RocketMinSpeed=Bottle->RocketMaxSpeed=1500; Bottle->RocketAcceleration=0;
	Bottle->RocketComp->TippingSettings.bEnableTipping=false;
	Bottle->RocketComp->DraggingSettings.bEnableDragging=false;
	Manager->SetBottleAgitationPressure(ID,Bottle->GetBottlePressureTolerance()+1); Bottle->AddAgitationPressure(.01f);
	if (!TestTrue(TEXT("Real cap release starts projectile flight"),Bottle->RocketComp->IsPoweredFlight())) return false;
	TestEqual(TEXT("Configured distance is respected"),Bottle->RocketLaunchConfiguration.Distance,600.f);
	TestTrue(TEXT("Zero angular variation selects the bottle's up direction"),Bottle->RocketLaunchConfiguration.Direction.Equals(FVector::UpVector,.001));
	FTransform LastPose; FVector LastVelocity,LastAngular; float PreviousFill=Bottle->GetContentLevel();
	bool WasPartial=false;
	Bottle->RocketComp->OnMovementStep.AddLambda([&](float)
	{
		LastPose=Bottle->GetActorTransform(); LastVelocity=Bottle->RocketComp->GetRocketVelocity(); LastAngular=Bottle->RocketComp->GetRocketAngularVelocityDegrees();
		const float Fill=Bottle->GetContentLevel(); WasPartial|=Fill>0 && Fill<.99f;
		TestTrue(TEXT("Distance-driven discharge never refills the bottle"),Fill<=PreviousFill); PreviousFill=Fill;
	});
	for (int32 I=0;I<200 && Bottle->RocketComp->IsPoweredFlight();++I)
		static_cast<UActorComponent*>(Bottle->RocketComp.Get())->TickComponent(.02f,LEVELTICK_All,nullptr);
	TestTrue(TEXT("Flight includes gradual fractional depletion"),WasPartial);
	TestTrue(TEXT("Flight reaches its configured distance"),Bottle->RocketComp->GetTotalFlightDistanceTraveled()>=600.f);
	TestEqual(TEXT("Bottle is exactly empty at travel limit"),Bottle->GetContentLevel(),0.f);
	TestEqual(TEXT("Material is exactly empty at travel limit"),Bottle->ColaLevel,0.f);
	TestTrue(TEXT("Travel limit hands off to physics falling"),Bottle->PropMesh->IsSimulatingPhysics() && Bottle->IsReplicatingMovement());
	TestTrue(TEXT("Handoff preserves root position and rotation"),Bottle->GetActorTransform().Equals(LastPose,.001));
	TestTrue(TEXT("Handoff preserves projectile linear velocity"),Bottle->PropMesh->GetPhysicsLinearVelocity().Equals(LastVelocity,.1));
	TestTrue(TEXT("Handoff preserves measured angular velocity"),Bottle->PropMesh->GetPhysicsAngularVelocityInDegrees().Equals(LastAngular,.1));
	TestTrue(TEXT("Depleted bottle uses lightweight empty mass"),FMath::IsNearlyEqual(Bottle->PropMesh->BodyInstance.GetBodyMass(),.05f));
	double Released=0; for (const auto& Row:Manager->ReleasedLiquids) if (Row.SourceISID==ID) Released+=Row.Portions;
	TestTrue(TEXT("All expelled liquid is conserved in one release ledger"),FMath::IsNearlyEqual(Released,4.0,1.e-4));
	TestEqual(TEXT("Repeated requests cannot relaunch an uncapped empty bottle"),Bottle->StartPlasticPressureRelease(),false);
	return true;
}
#endif
