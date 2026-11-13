#include "Misc/AutomationTest.h"
#include "Shop/SharedWalletTypes.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedWalletAccountingTest, "ColossalFizzCo.Shop.SharedWallet", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSharedWalletAccountingTest::RunTest(const FString& Parameters)
{
	FSharedWalletLedger Wallet;
	const FGuid Sale = FGuid::NewGuid(), Expense = FGuid::NewGuid();
	TestEqual(TEXT("Committed sale credits wallet"), Wallet.Apply(Sale, 7000, TEXT("Sale")), EWalletOperationResult::Applied);
	TestEqual(TEXT("Repeated sale cannot duplicate revenue"), Wallet.Apply(Sale, 7000, TEXT("Sale")), EWalletOperationResult::AlreadyApplied);
	TestEqual(TEXT("ID reuse with a changed amount rejected"), Wallet.Apply(Sale, 9000, TEXT("Sale")), EWalletOperationResult::OperationConflict);
	TestEqual(TEXT("Overspending rejected"), Wallet.Apply(Expense, -8000, TEXT("Supplies")), EWalletOperationResult::InsufficientFunds);
	TestEqual(TEXT("Rejected operation leaves balance unchanged"), Wallet.GetState().BalanceCents, int64(7000));
	TestEqual(TEXT("Valid expense commits once"), Wallet.Apply(Expense, -2000, TEXT("Supplies")), EWalletOperationResult::Applied);
	const FSharedWalletSaveData Saved = Wallet.Export();
	FSharedWalletLedger Restored;
	TestTrue(TEXT("Committed ledger restores"), Restored.Import(Saved));
	TestEqual(TEXT("Restored balance matches"), Restored.GetState().BalanceCents, int64(5000));
	TestEqual(TEXT("Retry after load cannot repeat payment"), Restored.Apply(Expense, -2000, TEXT("Supplies")), EWalletOperationResult::AlreadyApplied);
	FSharedWalletSaveData Corrupt = Saved;
	Corrupt.State.BalanceCents++;
	TestFalse(TEXT("Inconsistent saved balance rejected"), Restored.Import(Corrupt));
	TestEqual(TEXT("Failed restore preserves live balance"), Restored.GetState().BalanceCents, int64(5000));
	Corrupt = Saved; Corrupt.Receipts.Add(Saved.Receipts[0]); Corrupt.State.Revision++;
	TestFalse(TEXT("Duplicate saved receipt rejected"), Restored.Import(Corrupt));
	FSharedWalletLedger Large;
	TestEqual(TEXT("Maximum balance supported exactly"), Large.Apply(FGuid::NewGuid(), MAX_int64, TEXT("Test")), EWalletOperationResult::Applied);
	TestEqual(TEXT("Integer overflow rejected"), Large.Apply(FGuid::NewGuid(), 1, TEXT("Test")), EWalletOperationResult::InvalidOperation);
	TestEqual(TEXT("Invalid request ID rejected"), Large.Apply(FGuid(), -1, TEXT("Test")), EWalletOperationResult::InvalidOperation);
	return true;
}
#endif
