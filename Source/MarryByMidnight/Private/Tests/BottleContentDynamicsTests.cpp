#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/WorldSettings.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Shop/ShopProcurementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBottleContentDynamicsTest,"MarryByMidnight.Props.BottleContentDynamics",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBottleContentDynamicsTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	if (!TestTrue(TEXT("Normal content catalog initializes"),Manager->ShopSupplies->InitializeCatalog())) return false;
	auto* Product=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/NormalColaPrototype/DA_NormalMint.DA_NormalMint"));
	if (!Product) return false;
	const int32 ID=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(Product),FTransform(FVector(0,0,2000)));
	auto* ISM=Manager->GetComponentFromISID(ID); const int32 Index=ISM->FindISMI(ID),Slots=ISM->GetInstanceCount();
	if (!Manager->SetInstanceToUnsettled(ID)) return false;
	auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(ID));
	if (!Bottle) return false;
	TestTrue(TEXT("Approved empty plastic mass reaches real product"),FMath::IsNearlyEqual(Bottle->BottleMass,.05f));
	TestTrue(TEXT("Four 20ml portions at density1.04 weigh 0.0832kg"),FMath::IsNearlyEqual(Bottle->ContentMass,.0832f));
	TestTrue(TEXT("Physics receives bottle plus content mass"),FMath::IsNearlyEqual(Bottle->PropMesh->BodyInstance.GetBodyMass(),.1332f));
	Bottle->PropMesh->SetPhysicsLinearVelocity(FVector(123,45,67));
	Bottle->PropMesh->SetPhysicsAngularVelocityInDegrees(FVector(12,23,34));
	const FVector Velocity=Bottle->PropMesh->GetPhysicsLinearVelocity(),Angular=Bottle->PropMesh->GetPhysicsAngularVelocityInDegrees();
	if (!Manager->ReleaseBottleLiquidPortions(ID,2,FGuid::NewGuid())) return false;
	TestEqual(TEXT("Half release keeps fractional canonical content"),Bottle->GetContentLevel(),.5f);
	TestTrue(TEXT("Half full bottle becomes lighter"),FMath::IsNearlyEqual(Bottle->PropMesh->BodyInstance.GetBodyMass(),.0916f));
	TestTrue(TEXT("Mass update preserves linear velocity"),Bottle->PropMesh->GetPhysicsLinearVelocity().Equals(Velocity,.01));
	TestTrue(TEXT("Mass update preserves angular velocity"),Bottle->PropMesh->GetPhysicsAngularVelocityInDegrees().Equals(Angular,.01));
	float Level=-1,Cached=-1;
	Bottle->DynamicMaterialInstance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Liquid Level")),Level);
	Bottle->DynamicMaterialInstance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Liquid Cached Fill")),Cached);
	TestEqual(TEXT("Active material tracks fractional content"),Level,.5f); TestEqual(TEXT("Cached clipping plane uses same content"),Cached,.5f);
	auto Dense=*Manager->FindContentDefinition(Bottle->GetContentID()); Dense.DensityGPerMl=2.f;
	Manager->RegisterContentDefinition(Dense); Bottle->UpdateBottleMass();
	TestTrue(TEXT("Per-content density affects physical liquid mass"),FMath::IsNearlyEqual(Bottle->ContentMass,.08f));
	Manager->SetBottleAgitationPressure(ID,FMath::Max(0.f,Bottle->GetBottlePressureTolerance()*.8f-Bottle->GetBottlePressure()));
	World->Tick(LEVELTICK_All,.05f);
	for (int32 I=0;I<20;++I) Bottle->UpdateMorphTargets();
	const float Bulge=Bottle->SmoothedPressureExpansion;
	AddInfo(FString::Printf(TEXT("Morph fixture plastic=%d pressure=%.4f gauge=%.4f tolerance=%.4f dt=%.5f expansion=%.5f skeletal=%s active=%d"),Bottle->UsesPlasticPressure(),Bottle->GetBottlePressure(),Bottle->LocalUniqueData.ContinuousData.SealedPressure,Bottle->GetBottlePressureTolerance(),World->GetDeltaSeconds(),Bulge,*GetNameSafe(Bottle->SkeletalMeshComponent),Bottle->bIsActive));
	TestTrue(TEXT("Canonical pressure expands the plastic morph"),Bulge>.05f);
	TestNotNull(TEXT("The real plastic bottle has its expansion morph"),Bottle->SkeletalMeshComponent->GetSkeletalMeshAsset()->FindMorphTarget(FName(TEXT("OverfullLevel"))));
	TestTrue(TEXT("Pressure is applied to the actual skeletal morph"),Bottle->SkeletalMeshComponent->GetMorphTarget(FName(TEXT("OverfullLevel")))>.05f);
	Manager->SetBottleAgitationPressure(ID,0);
	TestTrue(TEXT("Pressure release decays rather than snapping"),Bottle->SmoothedPressureExpansion>0);
	if (!Manager->ReleaseBottleLiquid(ID)) return false;
	Bottle->UpdateDynamicMaterial(false);
	Bottle->DynamicMaterialInstance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Liquid Level")),Level);
	Bottle->DynamicMaterialInstance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Liquid Cached Fill")),Cached);
	TestEqual(TEXT("Empty ASM material receives exact zero"),Level,0.f); TestEqual(TEXT("Empty cached fill is zero"),Cached,0.f);
	TestEqual(TEXT("No contents retain zero content mass"),Bottle->ContentMass,0.f);
	TestTrue(TEXT("Empty physics mass is shell only"),FMath::IsNearlyEqual(Bottle->PropMesh->BodyInstance.GetBodyMass(),.05f));
	for (int32 I=0;I<100;++I) Bottle->UpdateMorphTargets();
	TestTrue(TEXT("Expansion relaxes after pressure release"),Bottle->SmoothedPressureExpansion<Bulge*.01f);
	Bottle->LiquidMotion.Reset(); Bottle->ColaLevel=.7f; // Simulate an obsolete legacy visual mirror.
	if (!TestTrue(TEXT("Empty bottle returns to permanent ISM"),Manager->SetInstanceToRest(ID))) return false;
	TestEqual(TEXT("Pool return cannot restore stale liquid"),Manager->FindUniqueData(ID)->EventData.ContentLevel,0.f);
	TestEqual(TEXT("ISM slot remains allocated"),ISM->GetInstanceCount(),Slots);
	TestEqual(TEXT("ISM fill data is exact zero"),ISM->PerInstanceSMCustomData[Index*ISM->NumCustomDataFloats],0.f);
	if (!TestTrue(TEXT("Empty instance promotes again"),Manager->SetInstanceToUnsettled(ID))) return false;
	Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(ID));
	TestEqual(TEXT("Reactivation preserves emptiness"),Bottle->GetContentLevel(),0.f);
	TestTrue(TEXT("Reactivation restores the light empty mass"),FMath::IsNearlyEqual(Bottle->PropMesh->BodyInstance.GetBodyMass(),.05f));
	// Receiving-role fixture exercises the same replicated-event callbacks without
	// claiming an actual remote network connection.
	Bottle->SwapRoles();
	FPropEventUpdateData Incoming=Bottle->LocalUniqueData.EventData;
	Incoming.ContentID=Dense.ContentID; Incoming.ContentLevel=.5f; ++Incoming.EventSequence;
	Bottle->OnApplyEventData(Incoming);
	TestTrue(TEXT("Receiving client derives mass from replicated content and density"),FMath::IsNearlyEqual(Bottle->ContentMass,.08f));
	Bottle->DynamicMaterialInstance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Liquid Level")),Level);
	TestEqual(TEXT("Receiving client drives the same material fill"),Level,.5f);
	FPropContinuousUpdateData Continuous=Bottle->LocalUniqueData.ContinuousData;
	Continuous.SealedPressure=1; Continuous.LastUpdatedTime+=1;
	Bottle->OnApplyContinuousData(Continuous);
	const float FirstExpansion=Bottle->SmoothedPressureExpansion;
	for (int32 I=0;I<20;++I) Bottle->UpdateMorphTargets();
	TestTrue(TEXT("Receiving client interpolates canonical pressure into morphs"),Bottle->SmoothedPressureExpansion>FirstExpansion && Bottle->SmoothedPressureExpansion>0);
	const float PressureBefore=Bottle->GetBottlePressure(); Bottle->AddAgitationPressure(10);
	TestEqual(TEXT("Receiving client cannot author pressure"),Bottle->GetBottlePressure(),PressureBefore);
	Bottle->SwapRoles();
	return true;
}
#endif
