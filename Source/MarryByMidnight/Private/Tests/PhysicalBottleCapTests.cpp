#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractableProp.h"
#include "Components/StaticMeshComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPhysicalBottleCapTest,"MarryByMidnight.Shop.PhysicalBottleCaps",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPhysicalBottleCapTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	auto* BottleAsset=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/Assest/DataAssets/DA_EmptyBottel.DA_EmptyBottel"));
	auto* CapMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Assest/meshs/SKM/SM_BottelCap.SM_BottelCap"));
	if (!TestNotNull(TEXT("Real empty bottle exists"),BottleAsset) || !TestNotNull(TEXT("Supplied cap mesh exists"),CapMesh)) return false;
	auto* CapAsset=NewObject<UInteractablePropDataAsset>(Manager);
	CapAsset->StaticMesh=CapMesh; CapAsset->bIsBottleCap=true; CapAsset->bEnableInstanceUniqueData=true;
	CapAsset->NumOfPortions=0; CapAsset->DefaultContentLevel=0; CapAsset->DefaultContentID=0;
	CapAsset->CustomActiveActorClass=AActivePropActor::StaticClass(); CapAsset->DefaultCapType=EBottleCapType::Twist;
	const int32 BottlePID=Manager->RegisterProductFromDataAsset(BottleAsset), CapPID=Manager->RegisterProductFromDataAsset(CapAsset);
	const FTransform BottleTransform(FRotator::ZeroRotator,FVector(0,0,200),FVector(.15));
	const int32 Bottle=Manager->GenerateProductInstance(BottlePID,BottleTransform);
	FTransform CapTransform=BottleTransform; CapTransform.AddToTranslation(FVector(30,0,0));
	const int32 Cap=Manager->GenerateProductInstance(CapPID,CapTransform);
	const int32 BottleSlot=Manager->FindISIDData(Bottle)->ISMI, CapSlot=Manager->FindISIDData(Cap)->ISMI;
	if (!TestTrue(TEXT("Compatible physical cap attaches"),Manager->AttachPhysicalCap(Bottle,Cap))) return false;
	TestEqual(TEXT("Bottle stores physical cap identity"),Manager->FindUniqueData(Bottle)->EventData.AttachedCapISID,Cap);
	TestEqual(TEXT("Cap stores bottle identity"),Manager->FindUniqueData(Cap)->EventData.AttachedBottleISID,Bottle);
	auto* BottleActor=Manager->GetActiveActorFromISID(Bottle); auto* CapActor=Manager->GetActiveActorFromISID(Cap);
	if (!TestNotNull(TEXT("Bottle ASM exists"),BottleActor) || !TestNotNull(TEXT("Cap ASM exists"),CapActor)) return false;
	TestTrue(TEXT("Cap follows actual bottle actor"),CapActor->GetAttachParentActor()==BottleActor);
	TestFalse(TEXT("Attached cap has no independent physics"),CapActor->PropMesh->IsSimulatingPhysics());
	TestFalse(TEXT("Attached cap cannot be stolen as a loose prop"),Manager->ValidateGrab(Cap,BottleActor));
	TestFalse(TEXT("Same cap cannot attach twice"),Manager->AttachPhysicalCap(Bottle,Cap));
	TestTrue(TEXT("Low torque is processed"),Manager->ClosePhysicalCap(Bottle,.1f));
	TestTrue(TEXT("Low torque does not create a perfect seal"),Manager->FindUniqueData(Bottle)->EventData.CapSealed<1);
	TestTrue(TEXT("Sweet spot torque closes cap"),Manager->ClosePhysicalCap(Bottle,.6f));
	TestEqual(TEXT("Compatible cap seals bottle"),Manager->FindUniqueData(Bottle)->EventData.CapSealed,1.f);
	if (!TestTrue(TEXT("Bottle and cap demote together"),Manager->ConvertASMToISM(Bottle,EPropState::AtRest,true,true))) return false;
	TestEqual(TEXT("Bottle retains original slot"),Manager->FindISIDData(Bottle)->ISMI,BottleSlot);
	TestEqual(TEXT("Cap retains original slot"),Manager->FindISIDData(Cap)->ISMI,CapSlot);
	TestEqual(TEXT("Cap rests as an ISM"),Manager->FindISIDData(Cap)->Representation,EPropRepresentation::ISM);
	TestEqual(TEXT("Demotion preserves cap association"),Manager->FindUniqueData(Cap)->EventData.AttachedBottleISID,Bottle);
	if (!TestTrue(TEXT("Bottle promotes again"),Manager->PromoteInstanceToASM(Bottle,EPropActivationReason::Interaction,false,BottleActor))) return false;
	CapActor=Manager->GetActiveActorFromISID(Cap);
	TestTrue(TEXT("Same cap reattaches on promotion"),CapActor && CapActor->GetAttachParentActor()==BottleActor && CapActor->ISID==Cap);
	TestTrue(TEXT("Physical cap removal succeeds"),Manager->DetachPhysicalCap(Bottle));
	TestEqual(TEXT("Bottle association clears"),Manager->FindUniqueData(Bottle)->EventData.AttachedCapISID,0);
	TestEqual(TEXT("Cap association clears"),Manager->FindUniqueData(Cap)->EventData.AttachedBottleISID,0);
	TestEqual(TEXT("Opening clears seal"),Manager->FindUniqueData(Bottle)->EventData.CapSealed,0.f);
	TestTrue(TEXT("Removed cap still has its original physical identity"),Manager->FindISIDData(Cap)!=nullptr);
	TestTrue(TEXT("Removed cap resumes physics"),CapActor->PropMesh->IsSimulatingPhysics());
	TestNull(TEXT("Removed cap detaches from bottle"),CapActor->GetAttachParentActor());
	return true;
}
#endif
