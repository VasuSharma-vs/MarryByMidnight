#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "Props/InteractablePropDatabase.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropPersistentSchemaTest, "MarryByMidnight.Props.PersistentSchema", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropPersistentSchemaTest::RunTest(const FString& Parameters)
{
	UPropDatabaseSaveGame* Save = NewObject<UPropDatabaseSaveGame>();
	FPropSerializedInstance Bottle;
	Bottle.ISID = 10001; Bottle.ProductID = 21; Bottle.bHasUniqueData = true;
	Bottle.UniqueData.EventData.CreationTime = 15;
	Bottle.UniqueData.EventData.ContentCreationTime = 225.5;
	Bottle.UniqueData.EventData.ContentLevel = 1.337f;
	Bottle.UniqueData.EventData.Strength = 0.81f;
	Bottle.UniqueData.EventData.AttachedCapISID = 10002;
	Bottle.UniqueData.EventData.bClean = false;
	Bottle.UniqueData.ContinuousData.SealedPressure = 1.85f;
	Save->WorldSaveData.SavedInstances.Add(Bottle);
	FPropSerializedInstance Cap;
	Cap.ISID = 10002; Cap.ProductID = 22; Cap.bHasUniqueData = true;
	Cap.UniqueData.EventData.AttachedBottleISID = Bottle.ISID;
	Save->WorldSaveData.SavedInstances.Add(Cap);
	FSharedWalletLedger Wallet;
	const FGuid SaleID = FGuid::NewGuid();
	Wallet.Apply(SaleID, 7000, TEXT("Sale"));
	Save->WorldSaveData.SharedWallet = Wallet.Export();
	TArray<uint8> Bytes;
	TestTrue(TEXT("World snapshot serializes through UE save archive"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
	UPropDatabaseSaveGame* Loaded = Cast<UPropDatabaseSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("Snapshot reloads"), Loaded) || Loaded->WorldSaveData.SavedInstances.Num() != 2) return false;
	const FPropInstanceUniqueData& Restored = Loaded->WorldSaveData.SavedInstances[0].UniqueData;
	TestEqual(TEXT("Container creation preserved separately"), Restored.EventData.CreationTime, 15.0);
	TestEqual(TEXT("Fresh liquid creation preserved"), Restored.EventData.ContentCreationTime, 225.5);
	TestEqual(TEXT("Fractional fill preserved"), Restored.EventData.ContentLevel, 1.337f);
	TestEqual(TEXT("Damage preserved"), Restored.EventData.Strength, 0.81f);
	TestEqual(TEXT("Sealed pressure preserved"), Restored.ContinuousData.SealedPressure, 1.85f);
	TestFalse(TEXT("Cleanliness preserved"), Restored.EventData.bClean);
	TestEqual(TEXT("Cap identity preserved"), Restored.EventData.AttachedCapISID, 10002);
	TestEqual(TEXT("Reverse bottle relationship preserved"), Loaded->WorldSaveData.SavedInstances[1].UniqueData.EventData.AttachedBottleISID, 10001);
	FSharedWalletLedger RestoredWallet;
	TestTrue(TEXT("Saved committed ledger validates"), RestoredWallet.Import(Loaded->WorldSaveData.SharedWallet));
	TestEqual(TEXT("Duplicate revenue rejected after archive reload"), RestoredWallet.Apply(SaleID, 7000, TEXT("Sale")), EWalletOperationResult::AlreadyApplied);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropDeltaSchemaTest, "MarryByMidnight.Props.DeltaSchema", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropDeltaSchemaTest::RunTest(const FString& Parameters)
{
	FPropEventSyncDelta Original;
	Original.ISID = 10001; Original.EventSequence = 22;
	Original.DirtyMask = static_cast<uint8>(EPropEventDirtyMask::ContentLevel);
	Original.ContentLevel = 1.337f;
	Original.ExtendedDirtyMask = EPropExtendedEventMask::Clean | EPropExtendedEventMask::ContentCreationTime | EPropExtendedEventMask::AttachedCap | EPropExtendedEventMask::AttachedBottle;
	Original.bClean = false; Original.ContentCreationTime = 225.5; Original.AttachedCapISID = 10002; Original.AttachedBottleISID = 10003;
	FPropContinuousSyncItem Continuous;
	Continuous.ISID = 10001; Continuous.SealedPressure = 1.85f; Continuous.Temperature = 6.5f;
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	bool Success = false;
	Original.NetSerialize(Writer, nullptr, Success);
	TestTrue(TEXT("Event serializes"), Success);
	Continuous.NetSerialize(Writer, nullptr, Success);
	FMemoryReader Reader(Bytes);
	FPropEventSyncDelta Decoded;
	FPropContinuousSyncItem DecodedContinuous;
	Decoded.NetSerialize(Reader, nullptr, Success);
	TestTrue(TEXT("Event deserializes"), Success);
	DecodedContinuous.NetSerialize(Reader, nullptr, Success);
	TestEqual(TEXT("Precise fill survives event wire format"), Decoded.ContentLevel, Original.ContentLevel);
	TestEqual(TEXT("Independent content clock survives wire format"), Decoded.ContentCreationTime, Original.ContentCreationTime);
	TestEqual(TEXT("Both cap links survive wire format"), Decoded.AttachedCapISID, Original.AttachedCapISID);
	TestEqual(TEXT("Reverse cap link survives wire format"), Decoded.AttachedBottleISID, Original.AttachedBottleISID);
	TestFalse(TEXT("Cleanliness survives event wire format"), Decoded.bClean);
	TestEqual(TEXT("Pressure survives continuous wire format"), DecodedContinuous.SealedPressure, Continuous.SealedPressure);
	TestEqual(TEXT("Temperature survives continuous wire format"), DecodedContinuous.Temperature, Continuous.Temperature);
	TestFalse(TEXT("Archive has no read errors"), Reader.IsError());
	return true;
}
#endif
