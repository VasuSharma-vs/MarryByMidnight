#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Shop/SharedWalletTypes.h"
#include "SharedTeamWalletComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSharedWalletChanged, FSharedWalletState, State);

/** Lives on the always-relevant Prop Manager; clients receive the same coherent balance/revision. */
UCLASS(ClassGroup = (Shop), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API USharedTeamWalletComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USharedTeamWalletComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category = "Wallet")
	FOnSharedWalletChanged OnWalletChanged;

	UFUNCTION(BlueprintPure, Category = "Wallet")
	FSharedWalletState GetWalletState() const { return ReplicatedState; }

	/** Call only after validating the gameplay operation. Reuse its ID when retrying. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Wallet")
	EWalletOperationResult Credit(const FGuid& OperationID, int64 AmountCents, FName Reason);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Wallet")
	EWalletOperationResult Debit(const FGuid& OperationID, int64 AmountCents, FName Reason);

	FSharedWalletSaveData ExportCommittedState() const;
	bool FindReceipt(const FGuid& OperationID, FWalletReceipt& OutReceipt) const { return Ledger.FindReceipt(OperationID, OutReceipt); }
	bool RestoreCommittedState(const FSharedWalletSaveData& Saved);
	static bool ValidateSaveData(const FSharedWalletSaveData& Saved);
	/** Internal game-thread conversion boundary. Failed resources leave no bank receipt or published balance. */
	EWalletOperationResult CommitWithResources(const FGuid& OperationID, int64 DeltaCents, FName Reason, TFunctionRef<bool()> CommitResources);

private:
	UPROPERTY(ReplicatedUsing = OnRep_WalletState)
	FSharedWalletState ReplicatedState;

	FSharedWalletLedger Ledger;
	bool bCommittingResources = false;
	EWalletOperationResult Commit(const FGuid& OperationID, int64 DeltaCents, FName Reason);

	UFUNCTION()
	void OnRep_WalletState();
};
