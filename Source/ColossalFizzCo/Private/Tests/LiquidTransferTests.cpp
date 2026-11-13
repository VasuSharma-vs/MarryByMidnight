#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Props/InteractablePropManager.h"
#include "Props/ColaPropActor.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiquidTransferRulesTest, "ColossalFizzCo.Shop.LiquidTransfer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLiquidTransferRulesTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	APropManager* Manager = World->SpawnActor<APropManager>();
	AColaPropActor* ActiveBottle = World->SpawnActor<AColaPropActor>();
	if (!Manager || !ActiveBottle) return false;
	FPropProductDefinition SupplyDef; SupplyDef.ProductID = 21; SupplyDef.bEnableInstanceUniqueData = true; SupplyDef.ConsumableProfile.NumOfPortions = 100;
	SupplyDef.DefaultUniqueData.EventData.ContentID = 7;
	FPropProductDefinition BottleDef; BottleDef.ProductID = 22; BottleDef.bEnableInstanceUniqueData = true; BottleDef.ConsumableProfile.NumOfPortions = 25;
	BottleDef.BottleMaterialType = EBottleMaterialType::Plastic; BottleDef.MaxOverfillLevel = 1.7f;
	Manager->ProductDefinitions.Add(21, SupplyDef); Manager->ProductDefinitions.Add(22, BottleDef);
	FPropContentDefinition Content; Content.ContentID = 7; Content.ExpiryProfile.bHasExpiry = false;
	Manager->ContentDefinitions.Add(7, Content);
	FISIDRuntimeIndex Supply; Supply.ISID = 10001; Supply.ProductID = 21; Supply.PackedRuntimeData.RemainingPortions = 50;
	FISIDRuntimeIndex Bottle; Bottle.ISID = 10002; Bottle.ProductID = 22; Bottle.PackedRuntimeData.RemainingPortions = 0;
	Bottle.Representation = EPropRepresentation::ASM; Bottle.ActiveActor = ActiveBottle;
	Manager->RegisterISID(Supply); Manager->RegisterISID(Bottle);
	Manager->FindUniqueData(10002)->EventData.Strength = 0.81f;
	Manager->FindUniqueData(10002)->EventData.CreationTime = 10;
	Manager->WorldElapsedSeconds = 100;
	const FGuid Session = Manager->BeginLiquidTransfer(10001, 10002);
	TestTrue(TEXT("Eligible empty bottle starts a fill session"), Session.IsValid());
	TestFalse(TEXT("Second filler cannot reserve same bottle"), Manager->BeginLiquidTransfer(10001, 10002).IsValid());
	TestEqual(TEXT("Accepted liquid reported in actual portions"), Manager->TransferLiquid(Session, 10), 10.0f);
	TestEqual(TEXT("Refill preserves bottle creation"), Manager->FindUniqueData(10002)->EventData.CreationTime, 10.0);
	TestEqual(TEXT("Refill preserves strength"), Manager->FindUniqueData(10002)->EventData.Strength, 0.81f);
	TestEqual(TEXT("First accepted liquid stamps content age"), Manager->FindUniqueData(10002)->EventData.ContentCreationTime, 100.0);
	Manager->WorldElapsedSeconds = 140;
	TestEqual(TEXT("Continuous fill accepts further portions"), Manager->TransferLiquid(Session, 5), 5.0f);
	TestEqual(TEXT("Ongoing fill does not refresh expiry"), Manager->FindUniqueData(10002)->EventData.ContentCreationTime, 100.0);
	const float Total = Manager->FindUniqueData(10001)->EventData.ContentLevel * 100 + Manager->FindUniqueData(10002)->EventData.ContentLevel * 25;
	TestTrue(TEXT("Transfer conserves liquid across different nominal capacities"), FMath::IsNearlyEqual(Total, 50.0f, 0.0001f));
	Manager->TransferLiquid(Session, 1000);
	TestTrue(TEXT("Damaged capacity limits fill"), FMath::IsNearlyEqual(Manager->FindUniqueData(10002)->EventData.ContentLevel, 1.7f * 0.81f, 0.0001f));
	Manager->EndLiquidTransfer(Session);
	TestFalse(TEXT("Nonempty bottle cannot start a new refill cycle"), Manager->BeginLiquidTransfer(10001, 10002).IsValid());
	const float Before = Manager->FindUniqueData(10001)->EventData.ContentLevel;
	FPropEventSyncDelta Debit, Invalid;
	Debit.ISID = 10001; Debit.SetFlag(EPropEventDirtyMask::ContentLevel); Debit.ContentLevel = 0;
	Invalid.ISID = 10002; Invalid.SetFlag(EPropEventDirtyMask::Strength); Invalid.Strength = 2;
	TestFalse(TEXT("Invalid half of atomic transfer rejects entire batch"), Manager->SubmitAtomicInstanceEventUpdates({Debit, Invalid}));
	TestEqual(TEXT("Rejected batch cannot debit supply"), Manager->FindUniqueData(10001)->EventData.ContentLevel, Before);
	return true;
}
#endif
