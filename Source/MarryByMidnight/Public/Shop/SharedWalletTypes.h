#pragma once

#include "CoreMinimal.h"
#include "SharedWalletTypes.generated.h"

UENUM(BlueprintType)
enum class EWalletOperationResult : uint8
{
	Applied,
	AlreadyApplied,
	InsufficientFunds,
	InvalidOperation,
	OperationConflict,
	NotAuthority
};

USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FSharedWalletState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Wallet")
	int64 BalanceCents = 0;

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Wallet")
	int64 Revision = 0;
};

/** A receipt exists only after the operation has committed. IDs survive saving and loading. */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FWalletReceipt
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Wallet")
	FGuid OperationID;

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Wallet")
	int64 DeltaCents = 0;

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Wallet")
	FName Reason;
};

USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FSharedWalletSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	int32 SchemaVersion = 1;

	UPROPERTY(SaveGame)
	FSharedWalletState State;

	UPROPERTY(SaveGame)
	TArray<FWalletReceipt> Receipts;
};

/** Integer accounting shared by runtime and tests; callers must serialize access on the game thread. */
class MARRYBYMIDNIGHT_API FSharedWalletLedger
{
public:
	EWalletOperationResult Apply(const FGuid& OperationID, int64 DeltaCents, FName Reason);
	const FSharedWalletState& GetState() const { return State; }
	bool FindReceipt(const FGuid& OperationID, FWalletReceipt& OutReceipt) const;
	FSharedWalletSaveData Export() const;
	bool Import(const FSharedWalletSaveData& Saved);
	/** Roll back only the most recent unpublished operation; used by atomic physical-cash conversion. */
	bool RollbackLast(const FGuid& OperationID);

private:
	FSharedWalletState State;
	TArray<FWalletReceipt> Receipts;
	TMap<FGuid, int32> ReceiptIndex;
};
