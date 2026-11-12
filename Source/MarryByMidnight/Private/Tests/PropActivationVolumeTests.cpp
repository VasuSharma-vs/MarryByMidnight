#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "Components/ASMComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Components/PropActivationVolumeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropManager.h"
#include "Props/PropActivationVolume.h"
#include "Props/PropInstanceVolumeQuery.h"
#include "Props/FragmentDestructionActor.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    struct FActivationWorld
    {
        UWorld* World=nullptr;
        APropManager* Manager=nullptr;
        FActivationWorld()
        {
            const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
            World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
            if (!World) return;
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Manager=World->SpawnActor<APropManager>();
            Manager->bEnableAutosave=false; Manager->ASMComponent->MinASMCount=1; Manager->InitializePropManager();
            FPropProductDefinition Product; Product.ProductID=1001;
            Product.StaticMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
            Product.bEnableInstanceUniqueData=true;
            Product.PhysicsProfile.UnsettledDelay=.05f; Product.InteractionProfile.bCanDamage=false;
            Manager->RegisterProductDefinition(Product);
            Product.ProductID=1002; Product.CustomActiveActorClass=AFragmentPropActor::StaticClass();
            Manager->RegisterProductDefinition(Product);
            auto* Floor=World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(Box);
            Box->SetBoxExtent(FVector(10000,10000,50)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
            World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
        }
        ~FActivationWorld()
        {
            if (World) { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
        }
        int32 Add(FVector Location) { return Manager->GenerateProductInstance(1001,FTransform(Location)); }
        APropActivationVolume* Volume(FVector Location=FVector(0,0,100),FVector Extent=FVector(300,150,200))
        {
            auto* Actor=World->SpawnActorDeferred<APropActivationVolume>(APropActivationVolume::StaticClass(),FTransform(Location),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            Actor->ActivationVolume->bKeepPropsActive=false;
            Actor->ActivationVolume->SetBoxExtent(Extent);
            Actor->FinishSpawning(FTransform(Location));
            return Actor;
        }
    };

    // Physics scheduling and timers use engine frame numbers. Run one fixture
    // tick per latent update, rather than pretending a synchronous loop is frames.
    class FActivationFrames : public IAutomationLatentCommand
    {
        TSharedRef<FActivationWorld> Fixture;
        int32 Remaining;
        TFunction<void()> After;
    public:
        FActivationFrames(TSharedRef<FActivationWorld> InFixture,int32 Count,TFunction<void()> InAfter)
            : Fixture(InFixture),Remaining(Count),After(MoveTemp(InAfter)) {}
        virtual bool Update() override
        {
            Fixture->World->Tick(LEVELTICK_All,.05f);
            if (--Remaining>0) return false;
            After(); return true;
        }
    };
    void AfterFrames(TSharedRef<FActivationWorld> Fixture,int32 Count,TFunction<void()> After)
    {
        FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FActivationFrames>(Fixture,Count,MoveTemp(After)));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropActivationLifecycleTest,"MarryByMidnight.Props.ActivationVolume.Lifecycle",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropActivationLifecycleTest::RunTest(const FString& Parameters)
{
    auto F=MakeShared<FActivationWorld>(); if (!F->World) return false;
    const int32 First=F->Add(FVector(-100,0,100)), Second=F->Add(FVector(100,0,100)), Outside=F->Add(FVector(1500,0,100));
    auto* ISM=F->Manager->GetComponentFromISID(First); if (!TestNotNull(TEXT("Registered permanent ISM"),ISM)) return false;
    const int32 Slots=ISM->GetInstanceCount(), FirstIndex=ISM->FindISMI(First);
    F->Manager->FindUniqueData(First)->EventData.Strength=.61f;
    auto* Volume=F->Volume()->ActivationVolume.Get(); Volume->MaxPromotionsPerRefresh=1;
    const auto IDs=PropInstanceVolumeQuery::Query(Volume,F->Manager);
    TestTrue(TEXT("One ISM component contributes both overlapping instance identities"),IDs.Contains(First) && IDs.Contains(Second));
    TestFalse(TEXT("Outside instance excluded"),IDs.Contains(Outside));
    Volume->SetKeepingPropsActive(true);
    const int32 Count=int32(F->Manager->GetRepresentation(First)==EPropRepresentation::ASM)+int32(F->Manager->GetRepresentation(Second)==EPropRepresentation::ASM);
    TestEqual(TEXT("Promotion budget is respected"),Count,1);
    Volume->RefreshProps();
    TestTrue(TEXT("Deferred instance promotes on next refresh"),F->Manager->GetRepresentation(First)==EPropRepresentation::ASM && F->Manager->GetRepresentation(Second)==EPropRepresentation::ASM);
    auto* Actor=F->Manager->GetActiveActorFromISID(First); if (!TestNotNull(TEXT("Active prop"),Actor)) return false;
    AfterFrames(F,80,[this,F,Actor,Volume,First]()
    {
        TestTrue(TEXT("Stationary props remain ASMs beyond normal rest delay"),F->Manager->GetRepresentation(First)==EPropRepresentation::ASM);
        TestEqual(TEXT("Protected free props remain Unsettled"),Actor->PhysicsSimComponent->CurrentState,EPhysicsSimulationState::Unsettled);
        TestFalse(TEXT("No unsettled countdown is repeatedly restarted"),Actor->PhysicsSimComponent->IsUnsettledTimerActive());
        TestFalse(TEXT("Direct manager demotion is blocked"),F->Manager->SetInstanceToRest(First));
        TestFalse(TEXT("Explicit settling cannot bypass the policy"),F->Manager->SetInstanceToSettling(First));
        Actor->PhysicsSimComponent->SetStateAtRest();
        TestEqual(TEXT("Physics state cannot bypass the policy"),Actor->PhysicsSimComponent->CurrentState,EPhysicsSimulationState::Unsettled);
        Actor->SetActorLocation(FVector(900,0,100),false,nullptr,ETeleportType::TeleportPhysics);
        TestFalse(TEXT("Live geometry recognizes exit before next refresh"),F->Manager->IsInstanceKeptActive(First));
        Volume->RefreshProps();
    });
    AfterFrames(F,40,[this,F,First,Outside,ISM,FirstIndex,Slots]()
    {
        TestEqual(TEXT("Exit resumes automatic settlement and demotion"),F->Manager->GetRepresentation(First),EPropRepresentation::ISM);
        TestEqual(TEXT("Persistent instance slot is preserved"),ISM->FindISMI(First),FirstIndex);
        TestEqual(TEXT("Activation never removes ISM slots"),ISM->GetInstanceCount(),Slots);
        TestEqual(TEXT("Unique physical history survives activation and rest"),F->Manager->FindUniqueData(First)->EventData.Strength,.61f);
        TestEqual(TEXT("Unrelated outside stock stays instanced"),F->Manager->GetRepresentation(Outside),EPropRepresentation::ISM);
    });
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropActivationOverlapTest,"MarryByMidnight.Props.ActivationVolume.OverlapAndCleanup",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropActivationOverlapTest::RunTest(const FString& Parameters)
{
    auto F=MakeShared<FActivationWorld>(); if (!F->World) return false;
    const int32 ID=F->Add(FVector(0,0,100)); F->Manager->SetInstanceToUnsettled(ID);
    auto* A=F->Volume(); auto* B=F->Volume(); auto ProbeIDs=MakeShared<TArray<int32>>();
    A->ActivationVolume->SetKeepingPropsActive(true); B->ActivationVolume->SetKeepingPropsActive(true);
    A->ActivationVolume->SetKeepingPropsActive(false);
    TestTrue(TEXT("Second volume retains protection when first disables"),F->Manager->IsInstanceKeptActive(ID));
    TestFalse(TEXT("Overlapping volume cannot be bypassed by direct rest"),F->Manager->SetInstanceToRest(ID));
    B->Destroy();
    AfterFrames(F,40,[this,F,A,ID]()
    {
        TestEqual(TEXT("Destroying last enabled volume releases the active prop"),F->Manager->GetRepresentation(ID),EPropRepresentation::ISM);
        A->ActivationVolume->SetKeepingPropsActive(true);
        TestEqual(TEXT("Re-enabling rediscovers and promotes resting identities"),F->Manager->GetRepresentation(ID),EPropRepresentation::ASM);
        A->ActivationVolume->Deactivate();
    });
    AfterFrames(F,40,[this,F,A,ID]()
    {
        TestEqual(TEXT("Component deactivation releases protection"),F->Manager->GetRepresentation(ID),EPropRepresentation::ISM);
        A->ActivationVolume->Activate();
        TestEqual(TEXT("Component activation restores the policy"),F->Manager->GetRepresentation(ID),EPropRepresentation::ASM);
        A->SetActorLocation(FVector(3000,0,100)); A->ActivationVolume->RefreshProps();
    });
    AfterFrames(F,40,[this,F,A,ID,ProbeIDs]()
    {
        TestEqual(TEXT("Moving volume releases props left behind"),F->Manager->GetRepresentation(ID),EPropRepresentation::ISM);
        A->SetActorRotation(FRotator(0,45,0)); A->SetActorScale3D(FVector(2,1,.5)); A->ActivationVolume->SetBoxExtent(FVector(200,40,200));
        ProbeIDs->Add(F->Add(A->GetActorTransform().TransformPosition(FVector(150,0,0))));
        ProbeIDs->Add(F->Add(A->GetActorTransform().TransformPosition(FVector(0,170,0))));
        A->ActivationVolume->RefreshProps();
        TestEqual(TEXT("Rotated scaled volume promotes the overlapping instance"),F->Manager->GetRepresentation((*ProbeIDs)[0]),EPropRepresentation::ASM);
        TestEqual(TEXT("Rotated box does not use a loose world AABB"),F->Manager->GetRepresentation((*ProbeIDs)[1]),EPropRepresentation::ISM);
        A->ActivationVolume->DestroyComponent();
    });
    AfterFrames(F,40,[this,F,ProbeIDs]()
    {
        TestEqual(TEXT("Component removal releases its remaining identities"),F->Manager->GetRepresentation((*ProbeIDs)[0]),EPropRepresentation::ISM);
    });
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropActivationInteractionTest,"MarryByMidnight.Props.ActivationVolume.HeldAndMotion",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropActivationInteractionTest::RunTest(const FString& Parameters)
{
    FActivationWorld F; if (!F.World) return false;
    const int32 ID=F.Add(FVector(0,0,100)); auto* Volume=F.Volume()->ActivationVolume.Get(); Volume->SetKeepingPropsActive(true);
    auto* Actor=F.Manager->GetActiveActorFromISID(ID); if (!TestNotNull(TEXT("Promoted prop"),Actor)) return false;
    TestTrue(TEXT("Normal grab is allowed in the active volume"),F.Manager->SetInstanceToHeld(ID)); Volume->RefreshProps();
    TestEqual(TEXT("Volume refresh preserves Held state"),F.Manager->GetPropState(ID),EPropState::Held);
    Volume->SetKeepingPropsActive(false);
    TestTrue(TEXT("Disabling volume does not drop a held prop"),Actor->bIsHeld);
    F.Manager->DropProp(ID); Volume->SetKeepingPropsActive(true);
    Actor=F.Manager->GetActiveActorFromISID(ID); if (!TestNotNull(TEXT("Released prop is active again"),Actor)) return false;
    Actor->SetPropToInMotion(FVector(20,0,0)); Volume->RefreshProps();
    TestTrue(TEXT("Explicit motion remains independent of volume policy"),Actor->bIsInMotion);
    Volume->SetKeepingPropsActive(false);
    TestTrue(TEXT("Leaving the policy does not interrupt explicit motion"),Actor->bIsInMotion);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropActivationFragmentTest,"MarryByMidnight.Props.ActivationVolume.FragmentCompatibility",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropActivationFragmentTest::RunTest(const FString& Parameters)
{
    auto F=MakeShared<FActivationWorld>(); if (!F->World) return false;
    auto* Volume=F->Volume()->ActivationVolume.Get(); Volume->SetKeepingPropsActive(true);
    FPropFractureFragmentEntry Entry; Entry.FragmentProductID=1002; Entry.MinImpulseSpeed=Entry.MaxImpulseSpeed=0; TArray<int32> IDs;
    if (!TestTrue(TEXT("Existing original fragment path starts"),F->Manager->CreateFragmentDestruction({Entry},FTransform(FVector(0,0,100)),FVector::ZeroVector,FVector::ZeroVector,IDs))) return false;
    const int32 ID=IDs[0]; Volume->RefreshProps();
    AfterFrames(F,80,[this,F,Volume,ID]()
    {
        TestEqual(TEXT("Original component is not demoted while inside"),F->Manager->GetRepresentation(ID),EPropRepresentation::CompositeFragment);
        TestFalse(TEXT("Direct original-fragment settlement cannot bypass volume"),F->Manager->SettleOriginalFragment(ID));
        Volume->SetKeepingPropsActive(false);
    });
    AfterFrames(F,40,[this,F,Volume,ID]()
    {
        TestEqual(TEXT("Original fragment settles after release"),F->Manager->GetRepresentation(ID),EPropRepresentation::ISM);
        Volume->SetKeepingPropsActive(true);
        TestTrue(TEXT("Settled fragment reactivates through its dedicated ASM"),Cast<AFragmentPropActor>(F->Manager->GetActiveActorFromISID(ID))!=nullptr);
    });
    return true;
}
#endif
