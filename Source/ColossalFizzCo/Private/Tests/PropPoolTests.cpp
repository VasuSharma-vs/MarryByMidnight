#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Components/ASMComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropPoolGrowthTest, "ColossalFizzCo.Props.PoolGrowth", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropPoolGrowthTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	APropManager* Manager = World->SpawnActor<APropManager>();
	if (!Manager || !Manager->ASMComponent) return false;
	UASMComponent* Pool = Manager->ASMComponent;
	Pool->MinASMCount = 1; Pool->MaxASMCount = 1; Pool->ExpandBatchSize = 2; Pool->bAutoShrinkExcessASMs = false;
	Pool->InitializeASMPool(1);
	AActivePropActor* First = Pool->AcquireActiveActor();
	if (!TestNotNull(TEXT("Initial pool unit"), First)) return false;
	First->ISID = 10001; First->bIsActive = true;
	Pool->RegisterActiveActor(10001, First);
	AActivePropActor* Second = Pool->AcquireActiveActor();
	if (!TestNotNull(TEXT("Exhausted pool allocates more units"), Second)) return false;
	TestTrue(TEXT("Soft ceiling grows by the configured batch"), Pool->MaxASMCount == 3);
	TestEqual(TEXT("Exactly one small batch allocated"), Pool->ASMActors.Num(), 3);
	TestNotEqual(TEXT("Expanded actor uses distinct ASM identity"), First->ASMID, Second->ASMID);
	const int32 OldGeneration = Second->GetPoolGeneration();
	Second->ISID = 10002; Second->bIsActive = true;
	Pool->RegisterActiveActor(10002, Second);
	Pool->UnregisterActiveActor(10002);
	Pool->ReturnActiveActorToPool(Second);
	TestEqual(TEXT("Returned unit resets its logical identity"), Second->ISID, 0);
	TestFalse(TEXT("Returned unit is inactive"), Second->bIsActive);
	TestTrue(TEXT("Returned unit resets generation"), Second->GetPoolGeneration() > OldGeneration);
	TestTrue(TEXT("Returned ASM identity is available for reuse"), Pool->EmptyASMIDs.Contains(Second->ASMID));
	return true;
}
#endif
