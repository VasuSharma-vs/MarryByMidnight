#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColaLiquidIntegrationTest,"ColossalFizzCo.Props.LiquidRepresentationContinuity",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FColaLiquidIntegrationTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	auto* Source=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/NormalColaPrototype/DA_NormalMint.DA_NormalMint"));
	if (!TestNotNull(TEXT("Real Normal bottle product"),Source)) return false;
	auto* Product=DuplicateObject<UInteractablePropDataAsset>(Source,Manager); Product->DefaultContentLevel=.25f;
	const int32 ID=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(Product),FTransform(FRotator(65,0,0),FVector(0,0,500)));
	auto* ISM=Manager->GetComponentFromISID(ID);
	if (!TestNotNull(TEXT("Resting liquid ISM"),ISM)) return false;
	const int32 Index=ISM->FindISMI(ID), Slots=ISM->GetInstanceCount();
	if (!TestTrue(TEXT("Core instance slots remain with appended visual channels"),ISM->NumCustomDataFloats>=6 && Index>=0)) return false;
	const float RestOffset=ISM->PerInstanceSMCustomData[Index*ISM->NumCustomDataFloats+4];
	TestEqual(TEXT("Resting volume solution is valid"),ISM->PerInstanceSMCustomData[Index*ISM->NumCustomDataFloats+5],1.f);
	float InstanceMode=0,PreviewLevel=0,InstanceHeight=0;
	auto* InstanceMID=Cast<UMaterialInstanceDynamic>(ISM->GetMaterial(1));
	if (!TestNotNull(TEXT("One shared runtime liquid MID per ISM component"),InstanceMID)) return false;
	InstanceMID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Use Instance Liquid Data")),InstanceMode);
	InstanceMID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Liquid Level")),PreviewLevel);
	InstanceMID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Liquid Container Height")),InstanceHeight);
	TestEqual(TEXT("Runtime ISM explicitly selects its own instance data"),InstanceMode,1.f);
	float AuthoredPreviewLevel=0;
	if (UMaterialInterface* SourceLiquid=ISM->GetProductMaterial(1))
		SourceLiquid->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Liquid Level")),AuthoredPreviewLevel);
	TestEqual(TEXT("Runtime wrapper preserves the artist's configurable preview value"),PreviewLevel,AuthoredPreviewLevel);
	TestEqual(TEXT("Preview never replaces the actual quarter-full contents"),ISM->PerInstanceSMCustomData[Index*ISM->NumCustomDataFloats],.25f);
	auto* Compatible=DuplicateObject<UInteractablePropDataAsset>(Source,Manager,TEXT("LiquidCompatibleProduct"));
	const int32 CompatiblePID=Manager->RegisterProductFromDataAsset(Compatible);
	TestNotEqual(TEXT("Compatibility test uses a distinct product"),CompatiblePID,ISM->ProductID);
	TestTrue(TEXT("Shared liquid MID preserves compatible cross-product reuse"),Manager->CanComponentReuseForProduct(ISM,CompatiblePID));
	TestTrue(TEXT("Product identity retains the original material asset"),ISM->GetProductMaterial(1)!=InstanceMID);
	if (!TestTrue(TEXT("Same ISID promotes"),Manager->SetInstanceToUnsettled(ID,FVector::ZeroVector,FVector::ZeroVector,60))) return false;
	auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(ID));
	if (!TestNotNull(TEXT("Bottle ASM"),Bottle)) return false;
	Bottle->UpdateDynamicMaterial(false);
	auto* MID=Bottle->DynamicMaterialInstance.Get();
	if (!TestNotNull(TEXT("Existing liquid material has a per-active-bottle MID"),MID)) return false;
	float Mode=0,ActiveOffset=0,Level=0,ActiveSource=1,CachedFill=-1,ActiveHeight=0;
	TestTrue(TEXT("Native material input exists"),MID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Native Liquid Motion")),Mode));
	MID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Liquid Plane Offset")),ActiveOffset);
	MID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Liquid Level")),Level);
	MID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Use Instance Liquid Data")),ActiveSource);
	MID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Liquid Cached Fill")),CachedFill);
	MID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Liquid Container Height")),ActiveHeight);
	TestEqual(TEXT("Native motion is enabled only on ASM"),Mode,1.f);
	TestEqual(TEXT("ASM explicitly selects dynamic material contents"),ActiveSource,0.f);
	TestEqual(TEXT("Native cached plane is stamped with the same fill"),CachedFill,.25f);
	TestEqual(TEXT("Both representations use identical body dimensions"),ActiveHeight,InstanceHeight);
	TestTrue(TEXT("Body height replaces the old 150cm proxy"),FMath::IsNearlyEqual(ActiveHeight,186.58856f,.01f));
	TestEqual(TEXT("Actual bottle content reaches the material"),Level,.25f);
	TestTrue(TEXT("Static and newly promoted clipping planes agree"),FMath::IsNearlyEqual(RestOffset,ActiveOffset,.02f));
	Bottle->LiquidMotion.Tilt=FVector2D(.3,0); Bottle->LiquidMotion.WaveEnergy=.3f;
	TestFalse(TEXT("Visible retained slosh prevents a discontinuous pool return"),Manager->SetInstanceToRest(ID));
	Bottle->LiquidMotion.Reset();
	TestTrue(TEXT("Settled fluid can return to the existing ISM slot"),Manager->SetInstanceToRest(ID));
	TestEqual(TEXT("Promotion/demotion never removes the ISM"),ISM->GetInstanceCount(),Slots);
	TestEqual(TEXT("Original instance identity is retained"),ISM->FindISMI(ID),Index);
	TestTrue(TEXT("Resting plane remains continuous"),FMath::IsNearlyEqual(ISM->PerInstanceSMCustomData[Index*ISM->NumCustomDataFloats+4],RestOffset,.02f));
	TestTrue(TEXT("Pool reset clears previous occupant's fluid state"),Bottle->LiquidMotion.IsSettled() && !Bottle->LiquidMotion.bHasHistory);
	TestEqual(TEXT("Demotion retains the shared ISM material instead of the actor MID"),ISM->GetMaterial(1),static_cast<UMaterialInterface*>(InstanceMID));
	return true;
}
#endif
