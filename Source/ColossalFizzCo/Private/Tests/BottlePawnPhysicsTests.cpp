#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PropGrabInputComponent.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace BottlePawnPhysicsTests
{
struct FFixture
{
    UWorld* World = nullptr;
    ACharacter* Character = nullptr;
    APropManager* Manager = nullptr;
    AColaPropActor* Bottle = nullptr;
    int32 ISID = 0;
    bool bContactOnly = false;
    ~FFixture()
    {
        if (!World) return;
        World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    }
};
class FContactCheck : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    TSharedPtr<FFixture> Fixture;
    int32 Frame = 0;
    float WalkSpeed = 0, GentleSpeed = 0, StrongSpeed = 0, LightSpeed = 0;
public:
    FContactCheck(FAutomationTestBase* InTest, TSharedPtr<FFixture> InFixture) : Test(InTest), Fixture(InFixture) {}
    virtual bool Update() override
    {
        auto* Mesh = Fixture->Bottle->PropMesh.Get();
        auto* Movement = Fixture->Character->GetCharacterMovement();
        if (Frame < 40) Fixture->Character->AddMovementInput(FVector::ForwardVector);
        else
        {
            if (Fixture->bContactOnly)
            {
                Test->TestTrue(TEXT("Walking contact does not launch the lightweight bottle"),WalkSpeed>0 && WalkSpeed<30.f);
                Test->TestTrue(TEXT("Lightweight shell retains approved mass and original scale"),FMath::IsNearlyEqual(Fixture->Bottle->BottleMass,.05f) && Fixture->Bottle->GetActorScale3D().Equals(FVector::OneVector));
                Test->AddInfo(FString::Printf(TEXT("Lightweight contact max %.3f cm/s"),WalkSpeed));
                return true;
            }
            Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
            if (Frame == 40)
            {
                Test->TestTrue(TEXT("Actual character contact gives only a small passive push"), WalkSpeed > 0 && WalkSpeed < 30.f);
                Fixture->Character->SetActorLocation(FVector::ZeroVector, false, nullptr, ETeleportType::TeleportPhysics);
                Movement->StopMovementImmediately(); Movement->DisableMovement();
                Fixture->Manager->SetInstanceToUnsettled(Fixture->ISID, FVector(400,0,0), Mesh->GetComponentLocation(), 60);
            }
            else if (Frame == 41)
                Fixture->Manager->SetInstanceToUnsettled(Fixture->ISID, FVector(40000,0,0), Mesh->GetComponentLocation(), 60);
            else if (Frame == 42)
            {
				FPropProductDefinition Light; Light.BottleMass = 5;
                Fixture->Bottle->OnApplySharedDefinitions(&Light, nullptr);
                Fixture->Manager->SetInstanceToUnsettled(Fixture->ISID, FVector(400,0,0), Mesh->GetComponentLocation(), 60);
            }
            else if (Frame == 43)
            {
				FPropProductDefinition Heavy; Heavy.BottleMass = 80;
                Fixture->Bottle->OnApplySharedDefinitions(&Heavy, nullptr);
                Fixture->Manager->SwapRoles();
                Fixture->Manager->SetInstanceToUnsettled(Fixture->ISID, FVector(400,0,0), Mesh->GetComponentLocation(), 60);
                Fixture->Manager->SwapRoles();
            }
        }
        // One update per engine frame: synchronous repeated Tick calls cannot advance Chaos reliably.
        Fixture->World->Tick(LEVELTICK_All, 1.f/60);
        const float Speed = Mesh->GetPhysicsLinearVelocity().Size();
        if (Frame < 40) WalkSpeed = FMath::Max(WalkSpeed, Speed);
        else if (Frame == 40)
        {
            GentleSpeed = Speed;
            Test->TestTrue(TEXT("Manager wake-up impulse respects heavy bottle mass"), Speed > 0 && Speed < 10.f);
        }
        else if (Frame == 41)
        {
            StrongSpeed = Speed;
            Test->TestTrue(TEXT("Strong hit retains substantial physical response"), Speed > 400.f && Speed > GentleSpeed*50);
        }
        else if (Frame == 42)
        {
            LightSpeed = Speed;
            Test->TestTrue(TEXT("Same momentum moves lighter bottle much more"), Speed > GentleSpeed*10);
        }
        else if (Frame == 43)
        {
            Test->TestTrue(TEXT("Client-side existing ASM uses the same mass-aware kick"), FMath::IsNearlyEqual(Speed, GentleSpeed, .1f));
            Test->TestTrue(TEXT("Physical agitation remains calibrated after mass tuning"), Fixture->Bottle->NormalizePhysicalContactImpulse(FVector(40000,0,0)).Equals(FVector(2500,0,0), .01));
            Test->TestEqual(TEXT("Released bottle still blocks pawns"), Mesh->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
            Test->TestTrue(TEXT("Bottle geometry is never resized"), Fixture->Bottle->GetActorScale3D().Equals(FVector::OneVector));
            Test->AddInfo(FString::Printf(TEXT("Contact max %.3f cm/s; heavy kick %.3f; strong hit %.3f; light kick %.3f; client kick %.3f"), WalkSpeed, GentleSpeed, StrongSpeed, LightSpeed, Speed));
            return true;
        }
        ++Frame;
        return false;
    }
};
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FBottlePawnPhysicsTest, "ColossalFizzCo.Props.BottlePawnPhysics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FBottlePawnPhysicsTest::GetTests(TArray<FString>& Names,TArray<FString>& Commands) const
{
    Names.Add(TEXT("Heavy")); Commands.Add(TEXT("Heavy"));
    Names.Add(TEXT("Lightweight")); Commands.Add(TEXT("Lightweight"));
}
bool FBottlePawnPhysicsTest::RunTest(const FString& Parameters)
{
    auto Fixture = MakeShared<BottlePawnPhysicsTests::FFixture>();
    Fixture->bContactOnly=Parameters==TEXT("Lightweight");
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    Fixture->World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
    auto* World = Fixture->World;
    if (!World) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
    Fixture->Manager = World->SpawnActor<APropManager>(); Fixture->Manager->bEnableAutosave = false; Fixture->Manager->InitializePropManager();
    auto* Product = NewObject<UInteractablePropDataAsset>(Fixture->Manager);
    Product->StaticMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    Product->CustomActiveActorClass = AColaPropActor::StaticClass(); Product->GameplayMassKg = 0;
    Product->BottleMass=Fixture->bContactOnly ? .05f : 80.f;
    Product->BottleMaterialType = EBottleMaterialType::Plastic; Product->DefaultContentLevel = 0; Product->DefaultContentID = 0; Product->bHasExpiry = false;
    Fixture->ISID = Fixture->Manager->GenerateProductInstance(Fixture->Manager->RegisterProductFromDataAsset(Product), FTransform(FVector(2000,0,500)));
    if (!TestTrue(TEXT("Real manager promotes resting bottle"), Fixture->Manager->SetInstanceToUnsettled(Fixture->ISID, FVector::ZeroVector, FVector::ZeroVector, 60))) return false;
    Fixture->Bottle = Cast<AColaPropActor>(Fixture->Manager->GetActiveActorFromISID(Fixture->ISID));
    if (!TestNotNull(TEXT("Native bottle representation"), Fixture->Bottle)) return false;
    Fixture->Bottle->PropMesh->SetEnableGravity(false);
    TestTrue(TEXT("Explicit bottle mass is preserved"), FMath::IsNearlyEqual(Fixture->Bottle->PropMesh->GetMass(), Product->BottleMass));
    Fixture->Character = World->SpawnActor<ACharacter>(FVector(1800,0,500), FRotator::ZeroRotator);
    auto* Input = NewObject<UPropGrabInputComponent>(Fixture->Character); Fixture->Character->AddInstanceComponent(Input); Input->RegisterComponent();
    auto* Movement = Fixture->Character->GetCharacterMovement();
    TestTrue(TEXT("Character dimensions and mass are preserved"), FMath::IsNearlyEqual(Movement->Mass,100.f) && Fixture->Character->GetActorScale3D().Equals(FVector::OneVector));
    Movement->bRunPhysicsWithNoController = true; Movement->SetMovementMode(MOVE_Flying); Movement->Velocity = FVector(600,0,0);
    ADD_LATENT_AUTOMATION_COMMAND(BottlePawnPhysicsTests::FContactCheck(this, Fixture));
    return true;
}
#endif
