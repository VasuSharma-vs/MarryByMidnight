#include "Shop/SharedWalletTypes.h"

EWalletOperationResult FSharedWalletLedger::Apply(const FGuid& OperationID, int64 DeltaCents, FName Reason)
{
	if (!OperationID.IsValid() || DeltaCents == 0 || DeltaCents == MIN_int64 || Reason.IsNone()) return EWalletOperationResult::InvalidOperation;
	if (const int32* Index = ReceiptIndex.Find(OperationID))
	{
		const FWalletReceipt& Previous = Receipts[*Index];
		return Previous.DeltaCents == DeltaCents && Previous.Reason == Reason
			? EWalletOperationResult::AlreadyApplied : EWalletOperationResult::OperationConflict;
	}
	if (DeltaCents < 0 && State.BalanceCents < -DeltaCents) return EWalletOperationResult::InsufficientFunds;
	if ((DeltaCents > 0 && State.BalanceCents > MAX_int64 - DeltaCents) || State.Revision == MAX_int64) return EWalletOperationResult::InvalidOperation;

	FWalletReceipt Receipt;
	Receipt.OperationID = OperationID; Receipt.DeltaCents = DeltaCents; Receipt.Reason = Reason;
	ReceiptIndex.Add(OperationID, Receipts.Add(Receipt));
	State.BalanceCents += DeltaCents;
	++State.Revision;
	return EWalletOperationResult::Applied;
}

FSharedWalletSaveData FSharedWalletLedger::Export() const
{
	FSharedWalletSaveData Saved;
	Saved.State = State;
	Saved.Receipts = Receipts;
	return Saved;
}

bool FSharedWalletLedger::Import(const FSharedWalletSaveData& Saved)
{
	if (Saved.SchemaVersion != 1 || Saved.State.BalanceCents < 0 || Saved.State.Revision != Saved.Receipts.Num()) return false;
	FSharedWalletLedger Candidate;
	for (const FWalletReceipt& Receipt : Saved.Receipts)
	{
		if (Candidate.Apply(Receipt.OperationID, Receipt.DeltaCents, Receipt.Reason) != EWalletOperationResult::Applied) return false;
	}
	if (Candidate.State.BalanceCents != Saved.State.BalanceCents) return false;
	*this = MoveTemp(Candidate);
	return true;
}
