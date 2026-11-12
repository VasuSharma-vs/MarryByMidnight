#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Components/PropActivationVolumeComponent.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractablePropManager.h"
#include "Props/PropActivationVolume.h"
#include "Shop/ShopProcurementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOriginalBottleRocketTest,"MarryByMidnight.Props.OriginalBottleRocket",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOriginalBottleRocketTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	if (!TestTrue(TEXT("Real Normal product/cap catalog initializes"),Manager->ShopSupplies->InitializeCatalog())) return false;
	auto* Floor=World->SpawnActor<AActor>(); auto* FloorBox=NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(FloorBox);
	FloorBox->SetBoxExtent(FVector(20000,20000,50)); FloorBox->SetCollisionProfileName(TEXT("BlockAll")); FloorBox->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-50));
	auto* Policy=World->SpawnActor<APropActivationVolume>(); Policy->ActivationVolume->SetBoxExtent(FVector(25000));
	auto* Product=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/NormalColaPrototype/DA_NormalLemonade.DA_NormalLemonade"));
	if (!TestNotNull(TEXT("Real prototype lemonade product"),Product)) return false;
	const int32 ID=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(Product),FTransform(FVector(0,0,5)));
	if (!TestTrue(TEXT("Real bottle activates"),Manager->SetInstanceToUnsettled(ID))) return false;
	auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(ID));
	if (!TestNotNull(TEXT("Configured bottle representation"),Bottle) || !TestNotNull(TEXT("Authored rocket component"),Bottle->RocketComp.Get())) return false;
	TestTrue(TEXT("Prototype defaults to original BP_Cola flight"),Bottle->bUseOriginalRocketFlight);
	const auto* Initial=Manager->FindUniqueData(ID); if (!Initial) return false;
	const int32 CapID=Initial->EventData.AttachedCapISID;
	if (!TestTrue(TEXT("Authored bottle has its sealed physical cap"),CapID>0 && Initial->EventData.CapSealed>=1)) return false;
	Policy->ActivationVolume->RefreshProps();
	World->Tick(LEVELTICK_All,.05f); // Give the launch snapshot a real, nonzero server timestamp.
	Bottle->RocketRandomSeed=77;
	Bottle->RocketMinDistance=Bottle->RocketMaxDistance=2000;
	Bottle->RocketComp->TippingSettings.bEnableTipping=false;
	Bottle->RocketComp->DraggingSettings.bEnableDragging=false;
	// Exercise mode switching on the same retained ASM, without a pool reset.
	if (!TestTrue(TEXT("Previous optional physics flight starts"),Bottle->RocketComp->StartPressurePhysicsFlight())) return false;
	Bottle->RocketComp->StopRocket(true);
	TestTrue(TEXT("Settled body remains a live ASM in the volume"),Bottle->bIsActive && Bottle->PropMesh->IsSimulatingPhysics() && Bottle->PropMesh->IsGravityEnabled());
	const float InitialFill=Bottle->GetContentLevel(), Strength=Bottle->GetStrength();
	Manager->SetBottleAgitationPressure(ID,Bottle->GetBottlePressureTolerance()+1.f);
	Bottle->AddAgitationPressure(.01f);
	if (!TestTrue(TEXT("Overpressure starts original component flight"),Bottle->RocketComp->IsPoweredFlight())) return false;
	TestFalse(TEXT("Old physics-flight flag cannot freeze the original flight"),Bottle->RocketComp->GetReplicatedFlightState().bPressurePhysicsFlight);
	TestFalse(TEXT("Component owns kinematic powered movement"),Bottle->PropMesh->IsSimulatingPhysics());
	TestFalse(TEXT("Actor movement does not fight the replicated component flight plan"),Bottle->IsReplicatingMovement());
	TestEqual(TEXT("Component moves the canonical ASM root"),Bottle->RocketComp->TargetPrimitive.Get(),static_cast<UPrimitiveComponent*>(Bottle->GetRootComponent()));
	TestTrue(TEXT("Server snapshot contains a deterministic launch seed"),Bottle->RocketComp->GetReplicatedFlightState().LaunchSeed>0);
	TestEqual(TEXT("Physical cap identity is preserved after release"),Manager->FindUniqueData(CapID)->EventData.AttachedBottleISID,0);
	TestEqual(TEXT("Rocket launch preserves bottle strength"),Bottle->GetStrength(),Strength);
	TestFalse(TEXT("Same release cannot launch twice"),Bottle->StartPlasticPressureRelease());
	const auto PoweredSnapshot=Bottle->RocketComp->GetReplicatedFlightState();
	const float LaunchDistance=Bottle->RocketComp->GetMaxFuelDistance();
	const auto PoweredRecord=*Manager->FindISIDData(ID);
	const auto PoweredUnique=*Manager->FindUniqueData(ID);
	static_cast<UActorComponent*>(Bottle->RocketComp.Get())->TickComponent(.1f,LEVELTICK_All,nullptr);
	Bottle->UpdatePlasticPressure(.1f);
	TestEqual(TEXT("Server records the requested deterministic seed"),Bottle->RocketLaunchConfiguration.Seed,77);
	TestEqual(TEXT("Chosen travel distance is applied to component"),Bottle->RocketLaunchConfiguration.Distance,LaunchDistance);
	TestTrue(TEXT("Original flight consumes canonical liquid"),Bottle->GetContentLevel()<InitialFill);
	double Released=0; for (const auto& Liquid:Manager->ReleasedLiquids) if (Liquid.SourceISID==ID) Released+=Liquid.Portions;
	TestTrue(TEXT("Retained plus emitted liquid is conserved"),FMath::IsNearlyEqual(Released+Bottle->GetContentLevel()*Bottle->GetNumPortions(),double(InitialFill)*Bottle->GetNumPortions(),1.e-4));
	const FVector Before=Bottle->GetActorLocation();
	for (int32 Frame=0;Frame<15;++Frame) static_cast<UActorComponent*>(Bottle->RocketComp.Get())->TickComponent(.05f,LEVELTICK_All,nullptr);
	TestTrue(TEXT("Original component moves the bottle root"),FVector::Distance(Before,Bottle->GetActorLocation())>5);
	TestEqual(TEXT("Flight never changes permanent ISID"),Bottle->ISID,ID);
	TestEqual(TEXT("Flight retains permanent ISM slot"),Manager->FindISIDData(ID)->ISMI,PoweredRecord.ISMI);
	Manager->ReleaseBottleLiquid(ID); Bottle->UpdatePlasticPressure(.01f);
	TestFalse(TEXT("Empty bottle cannot continue powered flight"),Bottle->RocketComp->IsPoweredFlight());
	TestEqual(TEXT("Empty original flight uses the component's falling handoff"),Bottle->RocketComp->GetSimulationState(),ERocketSimulationState::SpentFalling);
	TestTrue(TEXT("Falling root is authoritative physics"),Bottle->PropMesh->IsSimulatingPhysics() && Bottle->IsReplicatingMovement());
	const auto FallingSnapshot=Bottle->RocketComp->GetReplicatedFlightState();
	const auto FallingRecord=*Manager->FindISIDData(ID); const auto FallingUnique=*Manager->FindUniqueData(ID);
	const int32 LedgerRows=Manager->ReleasedLiquids.Num();
	auto Receive=[&](const FRocketFlightNetState& Snapshot,const FISIDRuntimeIndex& Record,const FPropInstanceUniqueData& Unique)
	{
		Bottle->SwapRoles(); ON_SCOPE_EXIT { Bottle->SwapRoles(); };
		Bottle->RocketComp->RestoreReplicatedFlightState(Snapshot);
		Bottle->InitializeFromISIDRecord(Record,Manager->FindProductDefinition(Record.ProductID),Manager->FindContentDefinition(Unique.EventData.ContentID),&Unique,Manager);
		Bottle->HandleOnBecomeActive(Bottle,ID,Record.ProductID,Bottle->GetActorTransform());
		TestEqual(TEXT("Client binding retains received component phase"),Bottle->RocketComp->GetSimulationState(),Snapshot.State);
		Bottle->UpdatePlasticPressure(.2f);
		TestEqual(TEXT("Receiving client cannot discharge authoritative liquid"),Manager->ReleasedLiquids.Num(),LedgerRows);
		TestFalse(TEXT("Receiving client cannot launch a pressure release"),Bottle->StartPlasticPressureRelease());
	};
	Receive(PoweredSnapshot,PoweredRecord,PoweredUnique);
	TestEqual(TEXT("Snapshot fallback uses replicated distance rather than fuel seconds"),Bottle->RocketComp->GetMaxFuelDistance(),LaunchDistance);
	TestTrue(TEXT("Received original mode stays powered after ISID binding"),Bottle->RocketComp->IsPoweredFlight());
	TestFalse(TEXT("Received original mode does not select Chaos thrust"),Bottle->RocketComp->IsPressureThrustActive());
	Receive(FallingSnapshot,FallingRecord,FallingUnique);
	TestTrue(TEXT("Received falling handoff enables replicated root physics"),Bottle->PropMesh->IsSimulatingPhysics() && Bottle->IsReplicatingMovement());
	Bottle->ResetTransientPoolState();
	TestEqual(TEXT("Pooling clears both flight modes"),Bottle->RocketComp->GetSimulationState(),ERocketSimulationState::Idle);
	return true;
}
#endif
