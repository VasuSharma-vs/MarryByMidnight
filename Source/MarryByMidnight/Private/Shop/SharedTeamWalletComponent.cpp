#include "Shop/SharedTeamWalletComponent.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

USharedTeamWalletComponent::USharedTeamWalletComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void USharedTeamWalletComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USharedTeamWalletComponent, ReplicatedState);
}

EWalletOperationResult USharedTeamWalletComponent::Credit(const FGuid& OperationID, int64 AmountCents, FName Reason)
{
	return AmountCents > 0 ? Commit(OperationID, AmountCents, Reason) : EWalletOperationResult::InvalidOperation;
}

EWalletOperationResult USharedTeamWalletComponent::Debit(const FGuid& OperationID, int64 AmountCents, FName Reason)
{
	return AmountCents > 0 ? Commit(OperationID, -AmountCents, Reason) : EWalletOperationResult::InvalidOperation;
}

EWalletOperationResult USharedTeamWalletComponent::Commit(const FGuid& OperationID, int64 DeltaCents, FName Reason)
{
	check(IsInGameThread());
	if (!GetOwner() || !GetOwner()->HasAuthority()) return EWalletOperationResult::NotAuthority;
	const EWalletOperationResult Result = Ledger.Apply(OperationID, DeltaCents, Reason);
	if (Result == EWalletOperationResult::Applied)
	{
		ReplicatedState = Ledger.GetState();
		OnWalletChanged.Broadcast(ReplicatedState);
		GetOwner()->ForceNetUpdate();
	}
	return Result;
}

void USharedTeamWalletComponent::OnRep_WalletState()
{
	OnWalletChanged.Broadcast(ReplicatedState);
}

FSharedWalletSaveData USharedTeamWalletComponent::ExportCommittedState() const { return Ledger.Export(); }

bool USharedTeamWalletComponent::ValidateSaveData(const FSharedWalletSaveData& Saved)
{
	FSharedWalletLedger Candidate;
	return Candidate.Import(Saved);
}

bool USharedTeamWalletComponent::RestoreCommittedState(const FSharedWalletSaveData& Saved)
{
	check(IsInGameThread());
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Ledger.Import(Saved)) return false;
	ReplicatedState = Ledger.GetState();
	OnWalletChanged.Broadcast(ReplicatedState);
	GetOwner()->ForceNetUpdate();
	return true;
}
