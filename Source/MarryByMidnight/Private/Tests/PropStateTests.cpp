#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Props/ColaPropActor.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropStateRoundTripTest, "MarryByMidnight.Props.StateRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropStateRoundTripTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AColaPropActor* Bottle = World->SpawnActor<AColaPropActor>();
	if (!TestNotNull(TEXT("Bottle actor"), Bottle)) return false;
	Bottle->NumOfPortions = 25;
	FPropEventUpdateData Event;
	Event.ContentLevel = 1.337f; Event.ContentID = 7; Event.Strength = 0.81f;
	Event.CapSealed = 1; Event.CapType = 1; Event.CreationTime = 123.5; Event.ContentCreationTime = 150.5; Event.EventSequence = 12;
	Bottle->OnApplyEventData(Event);
	FPropInstanceUniqueData Saved;
	FISIDRuntimeIndex Record;
	Bottle->WriteToISIDRecord(Record, &Saved);
	TestEqual(TEXT("Fractional overfill survives writeback"), Saved.EventData.ContentLevel, 1.337f);
	TestEqual(TEXT("Content identity survives writeback"), Saved.EventData.ContentID, 7);
	TestEqual(TEXT("Strength survives writeback"), Saved.EventData.Strength, 0.81f);
	TestEqual(TEXT("Container creation timestamp survives writeback"), Saved.EventData.CreationTime, 123.5);
	TestEqual(TEXT("Independent content timestamp survives writeback"), Saved.EventData.ContentCreationTime, 150.5);
	Event.EventSequence = 11; Event.ContentLevel = 0.5;
	Bottle->OnApplyEventData(Event);
	TestEqual(TEXT("Older discrete update cannot roll back fill"), Bottle->GetContentLevel(), 1.337f);
	Bottle->LastDiscreteEventTime = 200;
	FPropContinuousUpdateData Continuous;
	Continuous.LastUpdatedTime = 199; Continuous.Temperature = 80;
	Bottle->OnApplyContinuousData(Continuous);
	TestNotEqual(TEXT("Thermal packet predating event rejected"), Bottle->GetCurrentTemperature(), 80.0f);
	Continuous.LastUpdatedTime = 201; Continuous.Temperature = 6;
	Bottle->OnApplyContinuousData(Continuous);
	TestEqual(TEXT("Fresh thermal update accepted"), Bottle->GetCurrentTemperature(), 6.0f);
	TestEqual(TEXT("Thermal update preserves creation timestamp"), Bottle->GetCreationTime(), 123.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBottleDemotionTest, "MarryByMidnight.Props.DemotionPolicy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBottleDemotionTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AColaPropActor* Bottle = World->SpawnActor<AColaPropActor>();
	if (!Bottle) return false;
	FPropEventUpdateData Event;
	Event.ContentLevel = 0.4f; Event.CapSealed = 0;
	Bottle->OnApplyEventData(Event);
	TestFalse(TEXT("Open filled bottle cannot demote"), Bottle->CanDemoteToISM());
	Event.ContentLevel = 0; Event.EventSequence++;
	Bottle->OnApplyEventData(Event);
	TestTrue(TEXT("Empty uncapped stationary bottle may demote"), Bottle->CanDemoteToISM());
	Bottle->bIsHeld = true;
	TestFalse(TEXT("Held bottle cannot demote"), Bottle->CanDemoteToISM());
	Bottle->bIsHeld = false;
	const int32 Generation = Bottle->GetPoolGeneration();
	Bottle->ResetTransientPoolState();
	TestEqual(TEXT("Pool reset advances generation"), Bottle->GetPoolGeneration(), Generation + 1);
	return true;
}
#endif
