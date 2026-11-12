#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include <limits>
#include "Kismet/GameplayStatics.h"
#if WITH_EDITOR
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBottleRocketTemplateTest,"MarryByMidnight.Props.BottleRocketTemplate",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBottleRocketTemplateTest::RunTest(const FString& Parameters)
{
	auto* Blueprint=LoadObject<UBlueprint>(nullptr,TEXT("/Game/Props/Colas/BP_Cola.BP_Cola"));
	if (!TestNotNull(TEXT("Bottle Blueprint exists"),Blueprint) || !Blueprint->SimpleConstructionScript) return false;
	int32 Rockets=0;
	for (auto* Node:Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		auto* Template=Node ? Cast<URocketProjectileComponent>(Node->ComponentTemplate) : nullptr;
		if (!Template) continue;
		++Rockets;
		AddInfo(FString::Printf(TEXT("Rocket template=%s flags=%08x archetype=%s class=%s variable=%s"),*Template->GetPathName(),
			uint32(Template->GetFlags()),*GetPathNameSafe(Template->GetArchetype()),*Template->GetClass()->GetPathName(),*Node->GetVariableName().ToString()));
		TestFalse(TEXT("Component template is not a class default object"),Template->HasAnyFlags(RF_ClassDefaultObject));
		TestTrue(TEXT("Cooked component archetype has the component class"),Template->GetArchetype() && Template->GetArchetype()->IsA(Template->GetClass()));
	}
	TestEqual(TEXT("Bottle has one authored rocket component"),Rockets,1);
	return true;
}
#endif
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlasticPressureTest,"MarryByMidnight.Props.PlasticPressure",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPlasticPressureTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	const auto* Template=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/Assest/DataAssets/DA_EmptyBottel.DA_EmptyBottel"));
	if (!TestNotNull(TEXT("Existing bottle source"),Template)) return false;
	auto* Product=DuplicateObject<UInteractablePropDataAsset>(Template,Manager);
	Product->BottleMaterialType=EBottleMaterialType::Plastic; Product->DefaultContentID=901; Product->DefaultContentLevel=1;
	Product->NumOfPortions=4; Product->DefaultCapSealed=0; Product->SupportedCapMask=1; Product->bHasExpiry=false;
	Product->CustomActiveActorClass=AColaPropActor::StaticClass(); Product->FragmentAssembly=nullptr; Product->FractureFragments.Reset();
	Product->PressureFailureThreshold=2; Product->PressureResistanceMultiplier=1;
	FPropContentDefinition Content; Content.ContentID=901; Content.PressureStrengthMultiplier=1; Content.ExpiryProfile.bHasExpiry=false;
	Manager->RegisterContentDefinition(Content);
	auto* CapProduct=DuplicateObject<UInteractablePropDataAsset>(Template,Manager);
	CapProduct->bIsBottleCap=true; CapProduct->NumOfPortions=0; CapProduct->DefaultContentID=0; CapProduct->DefaultContentLevel=0;
	CapProduct->DefaultCapType=EBottleCapType::Twist; CapProduct->CustomActiveActorClass=AActivePropActor::StaticClass();
	const int32 ID=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(Product),FTransform(FVector(0,0,500)));
	const int32 Cap=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(CapProduct),FTransform(FVector(0,50,500)));
	if (!TestTrue(TEXT("Real physical cap attaches"),Manager->AttachPhysicalCap(ID,Cap))) return false;
	const auto* Def=Manager->FindProductDefinition(Manager->FindISIDData(ID)->ProductID);
	if (!TestTrue(TEXT("Shared cap service seals"),Manager->ClosePhysicalCap(ID,FMath::Lerp(Def->TorqueSweetSpotMin,Def->TorqueSweetSpotMax,.5f)))) return false;
	auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(ID));
	if (!TestNotNull(TEXT("Native plastic representation"),Bottle) || !TestNotNull(TEXT("Shared rocket component exists"),Bottle->RocketComp.Get())) return false;
	// Retain explicit coverage of the optional Chaos pressure mode. The prototype
	// now defaults to the original projectile mode, covered by its own tests.
	Bottle->bUseOriginalRocketFlight=false;
	TestTrue(TEXT("Bottle geometry retains native unit scale"),Bottle->GetActorScale3D().Equals(FVector::OneVector));
	TestFalse(TEXT("Invalid pressure cannot mutate canonical state"),Manager->SetBottleAgitationPressure(ID,std::numeric_limits<float>::quiet_NaN()));
	const FString Slot=TEXT("Automation_PlasticPressure_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (!TestTrue(TEXT("Initial cache snapshot saves"),Manager->SavePropDatabase(Slot,0))) return false;
	const double ThermalClock=Manager->FindUniqueData(ID)->ContinuousData.LastUpdatedTime;
	Manager->WorldElapsedSeconds+=1;
	Manager->SetBottleAgitationPressure(ID,.5f);
	TestEqual(TEXT("Pressure does not consume elapsed thermal integration time"),Manager->FindUniqueData(ID)->ContinuousData.LastUpdatedTime,ThermalClock);
	if (!TestTrue(TEXT("Pressure-only incremental save succeeds"),Manager->SavePropDatabase(Slot,0))) return false;
	const auto* Snapshot=Cast<UPropDatabaseSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
	const auto* Saved=Snapshot ? Snapshot->WorldSaveData.SavedInstances.FindByPredicate([ID](const auto& Item){return Item.ISID==ID;}) : nullptr;
	if (!TestNotNull(TEXT("Pressure bottle retained in incremental snapshot"),Saved)) return false;
	TestEqual(TEXT("Pressure-only mutation refreshes cached save data"),Saved->UniqueData.ContinuousData.SealedPressure,.5f);
	Manager->SetBottleAgitationPressure(ID,0);
	Bottle->ApplyBottleImpact(FVector(0,0,Bottle->DropImpactThreshold*1.1f),1234);
	const float ImpactPressure=Manager->FindUniqueData(ID)->ContinuousData.SealedPressure;
	TestTrue(TEXT("Impact increases saved pressure"),ImpactPressure>0);
	Bottle->ApplyBottleImpact(FVector(0,0,Bottle->DropImpactThreshold*1.1f),1234);
	TestEqual(TEXT("Same hit identity cannot charge pressure twice"),Manager->FindUniqueData(ID)->ContinuousData.SealedPressure,ImpactPressure);
	const float Threshold=Bottle->GetBottlePressureTolerance();
	Manager->SetBottleAgitationPressure(ID,Threshold-1.f);
	TestFalse(TEXT("Exactly at tolerance is not a release"),Bottle->StartPlasticPressureRelease());
	Bottle->AddAgitationPressure(.1f);
	TestTrue(TEXT("Exceeding tolerance starts physics jet"),Bottle->RocketComp->IsPressureThrustActive());
	const auto ActiveReceived=Bottle->RocketComp->GetReplicatedFlightState();
	const auto ActiveRecord=*Manager->FindISIDData(ID);
	const auto ActiveUnique=*Manager->FindUniqueData(ID);
	auto RebindAsClient=[&](const FRocketFlightNetState& Received,const FISIDRuntimeIndex& Record,const FPropInstanceUniqueData& Unique,ERocketSimulationState Expected)
	{
		Bottle->SwapRoles();
		ON_SCOPE_EXIT { Bottle->SwapRoles(); };
		TestFalse(TEXT("Fixture represents a receiving client"),Bottle->HasAuthority());
		Bottle->RocketComp->RestoreReplicatedFlightState(Received);
		Bottle->InitializeFromISIDRecord(Record,Manager->FindProductDefinition(Record.ProductID),
			Manager->FindContentDefinition(Unique.EventData.ContentID),&Unique,Manager);
		Bottle->HandleOnBecomeActive(Bottle,ID,Record.ProductID,Bottle->GetActorTransform());
		TestEqual(TEXT("Client binding retains received flight state"),Bottle->RocketComp->GetSimulationState(),Expected);
	};
	TestTrue(TEXT("Physics body simulates with authoritative actor movement"),Bottle->PropMesh->IsSimulatingPhysics() && Bottle->IsReplicatingMovement());
	TestEqual(TEXT("Original cap ISID is preserved after ejection"),Manager->FindUniqueData(Cap)->EventData.AttachedBottleISID,0);
	TestNotNull(TEXT("Ejected cap remains a managed instance"),Manager->FindISIDData(Cap));
	TestFalse(TEXT("Second launch cannot repeat same release"),Bottle->StartPlasticPressureRelease());
	Bottle->RocketComp->NozzleAxis=ERocketMeshAxis::NegativeZ;
	Bottle->SetActorRotation(FRotator::ZeroRotator,ETeleportType::TeleportPhysics);
	TestTrue(TEXT("Upright reaction follows physical cap opening despite legacy negative-Z rocket axis"),Bottle->RocketComp->GetPressureThrustDirection().Equals(-FVector::UpVector,.001));
	Bottle->SetActorRotation(FRotator(180,0,0),ETeleportType::TeleportPhysics);
	TestTrue(TEXT("Inverted bottle thrust points upward"),Bottle->RocketComp->GetPressureThrustDirection().Equals(FVector::UpVector,.001));
	const float InitialLevel=Bottle->GetContentLevel();
	Bottle->UpdatePlasticPressure(.2f);
	TestTrue(TEXT("Jet consumes actual retained liquid"),Bottle->GetContentLevel()<InitialLevel);
	if (!TestEqual(TEXT("Jet has one coalesced release episode"),Manager->ReleasedLiquids.Num(),1)) return false;
	TestTrue(TEXT("Jet conserves retained and released portions"),FMath::IsNearlyEqual(Manager->ReleasedLiquids[0].Portions+double(Bottle->GetContentLevel())*4,4.,1.e-5));
	const float PressureBefore=Manager->FindUniqueData(ID)->ContinuousData.SealedPressure;
	TestFalse(TEXT("Active jet cannot demote"),Bottle->CanDemoteToISM());
	Bottle->UpdatePlasticPressure(.2f);
	TestTrue(TEXT("Jet pressure falls while venting"),Manager->FindUniqueData(ID)->ContinuousData.SealedPressure<PressureBefore);
	TestEqual(TEXT("Repeated jet steps do not append ledger rows"),Manager->ReleasedLiquids.Num(),1);
	Manager->ReleaseBottleLiquid(ID); Bottle->UpdatePlasticPressure(.2f);
	TestFalse(TEXT("Empty bottle stops powered thrust"),Bottle->RocketComp->IsPressureThrustActive());
	TestEqual(TEXT("Flight retains momentum in physical falling"),Bottle->RocketComp->GetSimulationState(),ERocketSimulationState::SpentFalling);
	auto* Floor=World->SpawnActor<AActor>();
	auto* FloorMesh=NewObject<UStaticMeshComponent>(Floor); Floor->SetRootComponent(FloorMesh); FloorMesh->RegisterComponent();
	FloorMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
	FloorMesh->SetWorldScale3D(FVector(20,20,1)); FloorMesh->SetWorldLocation(FVector(0,0,-50));
	FloorMesh->SetCollisionProfileName(TEXT("BlockAll"));
	Bottle->SetActorRotation(FRotator(180,0,0),ETeleportType::TeleportPhysics);
	const float InvertedBottomOffset=Bottle->PropMesh->Bounds.GetBox().Min.Z-Bottle->GetActorLocation().Z;
	Bottle->SetActorLocation(FVector(0,0,180-InvertedBottomOffset),false,nullptr,ETeleportType::TeleportPhysics);
	Bottle->PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector); Bottle->PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	static_cast<UActorComponent*>(Bottle->RocketComp.Get())->TickComponent(.3f,LEVELTICK_All,nullptr);
	TestEqual(TEXT("Motionless body above floor cannot settle in midair"),Bottle->RocketComp->GetSimulationState(),ERocketSimulationState::SpentFalling);
	Bottle->SetActorLocation(FVector(0,0,1-InvertedBottomOffset),false,nullptr,ETeleportType::TeleportPhysics);
	static_cast<UActorComponent*>(Bottle->RocketComp.Get())->TickComponent(.3f,LEVELTICK_All,nullptr);
	TestEqual(TEXT("Inverted native-size bottle settles with its root above the old trace range"),Bottle->RocketComp->GetSimulationState(),ERocketSimulationState::AtRest);
	// Reuse the fixture as a receiver only after all authoritative jet accounting
	// assertions. Client binding intentionally clears server-only release episodes.
	const auto RestReceived=Bottle->RocketComp->GetReplicatedFlightState();
	const auto RestRecord=*Manager->FindISIDData(ID);
	const auto RestUnique=*Manager->FindUniqueData(ID);
	RebindAsClient(ActiveReceived,ActiveRecord,ActiveUnique,ERocketSimulationState::Rocketing);
	TestTrue(TEXT("Client activation retains received pressure mode"),Bottle->RocketComp->IsPressureThrustActive());
	RebindAsClient(RestReceived,RestRecord,RestUnique,ERocketSimulationState::AtRest);
	Bottle->ResetTransientPoolState();
	TestEqual(TEXT("Pool reset clears rocket state"),Bottle->RocketComp->GetSimulationState(),ERocketSimulationState::Idle);
	TestFalse(TEXT("No liquid means no phantom propulsion"),Bottle->StartPlasticPressureRelease());
	return true;
}
#endif
