#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Components/ASMComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfiguredBottleAssetTest, "MarryByMidnight.Shop.ConfiguredBottleRefill", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConfiguredBottleAssetTest::RunTest(const FString& Parameters)
{
	const auto* Empty = LoadObject<UInteractablePropDataAsset>(nullptr, TEXT("/Game/Assest/DataAssets/DA_EmptyBottel.DA_EmptyBottel"));
	const auto* Filled = LoadObject<UInteractablePropDataAsset>(nullptr, TEXT("/Game/Assest/DataAssets/DA_Cola.DA_Cola"));
	if (!TestNotNull(TEXT("Configured empty bottle"), Empty) || !TestNotNull(TEXT("Configured filled bottle"), Filled)) return false;
	TestTrue(TEXT("Empty bottle allocates persistent physical history"), Empty->bEnableInstanceUniqueData);
	TestTrue(TEXT("Empty bottle retains nominal capacity"), Empty->NumOfPortions > 0 && Empty->NumOfPortions == Filled->NumOfPortions);
	TestEqual(TEXT("Empty product starts with no liquid"), Empty->DefaultContentLevel, 0.0f);
	TestEqual(TEXT("Empty product starts without a liquid identity"), Empty->DefaultContentID, 0);
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	APropManager* Manager = World->SpawnActor<APropManager>();
	Manager->ASMComponent->MinASMCount = 1; Manager->ASMComponent->MaxASMCount = 1; Manager->ASMComponent->ExpandBatchSize = 1;
	Manager->WorldElapsedSeconds = 100;
	const int32 EmptyPID = Manager->RegisterProductFromDataAsset(Empty), FilledPID = Manager->RegisterProductFromDataAsset(Filled);
	TestEqual(TEXT("Registration does not invent contents for empty container"), Manager->FindProductDefinition(EmptyPID)->DefaultUniqueData.EventData.ContentID, 0);
	const int32 Target = Manager->GenerateProductInstance(EmptyPID, FTransform(FVector(0, 0, 200)));
	const int32 Source = Manager->GenerateProductInstance(FilledPID, FTransform(FVector(100, 0, 200)));
	auto* History = Manager->FindUniqueData(Target);
	if (!TestNotNull(TEXT("Real empty asset receives canonical state"), History)) return false;
	History->EventData.CreationTime = 10; History->EventData.Strength = .8f;
	History->EventData.bClean = true; History->EventData.CapSealed = 0;
	const FGuid Session = Manager->BeginLiquidTransfer(Source, Target);
	TestTrue(TEXT("Real configured bottle passes reuse admission"), Session.IsValid());
	TestTrue(TEXT("Real configured bottle accepts fractional refill"), Manager->TransferLiquid(Session, .5f) > 0);
	const auto* Result = Manager->FindUniqueData(Target);
	TestEqual(TEXT("Refill preserves old container creation"), Result->EventData.CreationTime, 10.0);
	TestEqual(TEXT("Refill preserves physical damage"), Result->EventData.Strength, .8f);
	TestEqual(TEXT("Refill starts a fresh liquid lifecycle"), Result->EventData.ContentCreationTime, 100.0);
	TestTrue(TEXT("New contents have actual source identity"), Result->EventData.ContentID > 0 && Result->EventData.ContentID == Manager->FindUniqueData(Source)->EventData.ContentID);
	Manager->EndLiquidTransfer(Session);
	return true;
}
#endif
