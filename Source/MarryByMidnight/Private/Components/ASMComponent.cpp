#include "Components/ASMComponent.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropManager.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"
#include "Async/Async.h"

UASMComponent::UASMComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	MinASMCount = 100;
	MaxASMCount = 500;
	ExpandThresholdRatio = 0.90f;
	ShrinkThresholdRatio = 0.25f;
	ExpandBatchSize = 25;
	bAutoShrinkExcessASMs = true;

	DefaultResetTransform = FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, -100000.0f), FVector(0.0001f));
	ActivePropActorClass = AActivePropActor::StaticClass();
}

void UASMComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!PropManager)
	{
		PropManager = Cast<APropManager>(GetOwner());
	}
}

void UASMComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ActiveSettlingComponents.Empty();
	Super::EndPlay(EndPlayReason);
}

// =========================================================================
// Custom ASM Actor Class & Dynamic Pool Scaling (100 to 500 ASMs, 90% Auto-Expand)
// =========================================================================
void UASMComponent::SetCustomActivePropActorClass(TSubclassOf<AActivePropActor> NewActorClass, bool bRebuildPoolNow)
{
	ActivePropActorClass = NewActorClass ? NewActorClass : TSubclassOf<AActivePropActor>(AActivePropActor::StaticClass());

	if (!PropManager)
	{
		PropManager = Cast<APropManager>(GetOwner());
	}
	if (PropManager)
	{
		PropManager->ActivePropActorClass = ActivePropActorClass;
	}

	if (bRebuildPoolNow && GetWorld())
	{
		const int32 TargetTotal = FMath::Clamp(FMath::Max(ASMActors.Num(), MinASMCount), MinASMCount, MaxASMCount);
		ClearEmptyASMIDs(true);
		const int32 Needed = TargetTotal - ASMActors.Num();
		if (Needed > 0)
		{
			AddASMUnits(Needed);
		}
		RebuildEmptyASMIDs();
		SyncManagerArrays();
	}
}

void UASMComponent::SetCustomActorClassForProduct(int32 InProductID, TSubclassOf<AActivePropActor> NewActorClass)
{
	if (InProductID <= 0)
	{
		return;
	}

	if (NewActorClass)
	{
		CustomActorClassByProductID.Add(InProductID, NewActorClass);
	}
	else
	{
		CustomActorClassByProductID.Remove(InProductID);
	}
}

TSubclassOf<AActivePropActor> UASMComponent::GetEffectiveActivePropActorClass(int32 InProductID) const
{
	if (InProductID > 0)
	{
		if (const TSubclassOf<AActivePropActor>* CustomProductClass = CustomActorClassByProductID.Find(InProductID))
		{
			if (*CustomProductClass)
			{
				return *CustomProductClass;
			}
		}
		if (PropManager)
		{
			if (const FPropProductDefinition* Def = PropManager->ProductDefinitions.Find(InProductID))
			{
				if (Def->CustomActiveActorClass)
				{
					return Def->CustomActiveActorClass;
				}
			}
		}
	}

	if (ActivePropActorClass && ActivePropActorClass != AActivePropActor::StaticClass())
	{
		return ActivePropActorClass;
	}

	if (PropManager && PropManager->ActivePropActorClass && PropManager->ActivePropActorClass != AActivePropActor::StaticClass())
	{
		return PropManager->ActivePropActorClass;
	}

	return ActivePropActorClass ? ActivePropActorClass : TSubclassOf<AActivePropActor>(AActivePropActor::StaticClass());
}

void UASMComponent::InitializeASMPool(int32 InitialCount)
{
	if (!PropManager)
	{
		PropManager = Cast<APropManager>(GetOwner());
	}

	if (PropManager)
	{
		// Only inherit from PropManager->ActivePropActorClass if ASMComponent doesn't already have a custom Blueprint subclass set
		if (!ActivePropActorClass ||
			(ActivePropActorClass == AActivePropActor::StaticClass() &&
			 PropManager->ActivePropActorClass &&
			 PropManager->ActivePropActorClass != AActivePropActor::StaticClass()))
		{
			ActivePropActorClass = PropManager->ActivePropActorClass;
		}
		else if (ActivePropActorClass)
		{
			PropManager->ActivePropActorClass = ActivePropActorClass;
		}

		if (PropManager->ResetTransforms.Num() > 0 && ResetTransforms.Num() == 0)
		{
			ResetTransforms = PropManager->ResetTransforms;
		}
		DefaultResetTransform = PropManager->DefaultResetTransform;
	}

	if (!ActivePropActorClass)
	{
		ActivePropActorClass = AActivePropActor::StaticClass();
	}

	MinASMCount = FMath::Clamp(MinASMCount, 1, MaxASMCount);
	const int32 TargetCount = (InitialCount > 0)
		? FMath::Clamp(InitialCount, MinASMCount, MaxASMCount)
		: MinASMCount;

	// Clean any invalid actors first
	ASMActors.RemoveAll([](const TObjectPtr<AActivePropActor>& Ptr) { return !IsValid(Ptr); });

	// Replace any existing empty pooled actors whose class doesn't match the effective ActivePropActorClass
	UClass* DesiredDefaultClass = GetEffectiveActivePropActorClass(0).Get();
	if (DesiredDefaultClass)
	{
		TArray<int32> CurrentEmpty = EmptyASMIDs;
		for (int32 EmptyID : CurrentEmpty)
		{
			if (AActivePropActor* ExistingEmpty = GetActiveActorFromASMID(EmptyID))
			{
				if (ExistingEmpty->GetClass() != DesiredDefaultClass)
				{
					RemoveASMByASMID(EmptyID);
				}
			}
		}
	}

	const int32 Needed = TargetCount - ASMActors.Num();
	if (Needed > 0)
	{
		AddASMUnits(Needed);
	}

	RebuildEmptyASMIDs();
	SyncManagerArrays();
}

float UASMComponent::GetPoolUtilizationRatio() const
{
	const int32 TotalCount = ASMActors.Num();
	if (TotalCount <= 0)
	{
		return 0.0f;
	}
	const int32 ActiveCount = TotalCount - EmptyASMIDs.Num();
	return FMath::Clamp(static_cast<float>(ActiveCount) / static_cast<float>(TotalCount), 0.0f, 1.0f);
}

void UASMComponent::EvaluatePoolCapacity()
{
	const int32 ClampedMin = FMath::Clamp(MinASMCount, 1, MaxASMCount);
	int32 TotalCount = ASMActors.Num();
	const float InitialUtilization = GetPoolUtilizationRatio();

	// Fast-path: if pool is within normal operating bounds, skip array scanning and SyncManagerArrays
	if (TotalCount >= ClampedMin &&
		InitialUtilization < ExpandThresholdRatio &&
		(!bAutoShrinkExcessASMs || TotalCount <= ClampedMin || InitialUtilization > ShrinkThresholdRatio))
	{
		return;
	}

	ASMActors.RemoveAll([](const TObjectPtr<AActivePropActor>& Ptr) { return !IsValid(Ptr); });
	TotalCount = ASMActors.Num();

	// 1. Ensure at least MinASMCount (100) ASM units exist
	if (TotalCount < ClampedMin)
	{
		const int32 Added = AddASMUnits(ClampedMin - TotalCount);
		if (Added > 0)
		{
			OnASMPoolScaled.Broadcast(ASMActors.Num(), GetActiveASMCount(), EmptyASMIDs.Num());
		}
		TotalCount = ASMActors.Num();
	}

	if (TotalCount <= 0)
	{
		return;
	}

	const float Utilization = GetPoolUtilizationRatio();

	// 2. If pool is >= 90% full (ExpandThresholdRatio) and below MaxASMCount (500), spawn more ASM units
	if (Utilization >= ExpandThresholdRatio && TotalCount < MaxASMCount)
	{
		const int32 ToAdd = FMath::Min(FMath::Max(1, ExpandBatchSize), MaxASMCount - TotalCount);
		const int32 Added = AddASMUnits(ToAdd);
		if (Added > 0)
		{
			OnASMPoolScaled.Broadcast(ASMActors.Num(), GetActiveASMCount(), EmptyASMIDs.Num());
		}
	}
	// 3. If pool utilization dropped below ShrinkThresholdRatio and total > MinASMCount (100), remove excess empty ASMs
	else if (bAutoShrinkExcessASMs && TotalCount > ClampedMin && Utilization <= ShrinkThresholdRatio)
	{
		const int32 ExcessAboveMin = TotalCount - ClampedMin;
		const int32 ToRemove = FMath::Min(ExcessAboveMin, FMath::Max(1, ExpandBatchSize));
		const int32 Removed = RemoveExcessEmptyASMs(ToRemove);
		if (Removed > 0)
		{
			OnASMPoolScaled.Broadcast(ASMActors.Num(), GetActiveASMCount(), EmptyASMIDs.Num());
		}
	}
}

AActivePropActor* UASMComponent::SpawnPooledASM(TSubclassOf<AActivePropActor> OverrideClass)
{
	if (ASMActors.Num() >= MaxASMCount)
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	if (!PropManager)
	{
		PropManager = Cast<APropManager>(GetOwner());
	}

	TSubclassOf<AActivePropActor> EffectiveClass = OverrideClass ? OverrideClass : GetEffectiveActivePropActorClass(0);
	UClass* SpawnClass = EffectiveClass ? EffectiveClass.Get() : AActivePropActor::StaticClass();
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const int32 SlotIndex = ASMActors.Num();
	FTransform ParkedTransform = GetResetTransformForSlot(SlotIndex);
	ParkedTransform.SetScale3D(FVector::OneVector);

	AActivePropActor* NewASM = World->SpawnActor<AActivePropActor>(SpawnClass, ParkedTransform, SpawnParams);
	if (!NewASM)
	{
		return nullptr;
	}

	NewASM->PropManagerReference = PropManager;
	if (NewASM->ASMID <= 0)
	{
		NewASM->ASMID = AllocateASMID();
	}
	NewASM->ISID = 0;
	NewASM->DeactivateToPoolState(ParkedTransform);

	ASMActors.AddUnique(NewASM);
	ActiveActorByASMID.Add(NewASM->ASMID, NewASM);
	EmptyASMIDs.AddUnique(NewASM->ASMID);

	return NewASM;
}

int32 UASMComponent::AddASMUnits(int32 Count)
{
	if (Count <= 0 || ASMActors.Num() >= MaxASMCount)
	{
		return 0;
	}

	const int32 AllowedToAdd = FMath::Min(Count, MaxASMCount - ASMActors.Num());
	int32 AddedCount = 0;

	for (int32 i = 0; i < AllowedToAdd; ++i)
	{
		if (SpawnPooledASM() != nullptr)
		{
			++AddedCount;
		}
	}

	SyncManagerArrays();
	return AddedCount;
}

int32 UASMComponent::RemoveExcessEmptyASMs(int32 MaxToRemove)
{
	const int32 ClampedMin = FMath::Clamp(MinASMCount, 1, MaxASMCount);
	if (ASMActors.Num() <= ClampedMin || EmptyASMIDs.Num() == 0)
	{
		return 0;
	}

	const int32 MaxRemovable = ASMActors.Num() - ClampedMin;
	const int32 TargetRemove = (MaxToRemove > 0) ? FMath::Min(MaxToRemove, MaxRemovable) : MaxRemovable;

	int32 RemovedCount = 0;
	while (RemovedCount < TargetRemove && EmptyASMIDs.Num() > 0 && ASMActors.Num() > ClampedMin)
	{
		const int32 CandidateASMID = EmptyASMIDs.Last();
		if (RemoveASMByASMID(CandidateASMID))
		{
			++RemovedCount;
		}
		else
		{
			EmptyASMIDs.Pop(EAllowShrinking::No);
		}
	}

	SyncManagerArrays();
	return RemovedCount;
}

bool UASMComponent::RemoveASMByASMID(int32 InASMID)
{
	if (InASMID <= 0)
	{
		return false;
	}

	AActivePropActor* TargetASM = GetActiveActorFromASMID(InASMID);
	if (const int32* BoundISID = ASMIDToISID.Find(InASMID))
	{
		ActiveActorByISID.Remove(*BoundISID);
		ISIDToASMID.Remove(*BoundISID);
	}
	ASMIDToISID.Remove(InASMID);
	EmptyASMIDs.Remove(InASMID);
	ActiveActorByASMID.Remove(InASMID);

	if (TargetASM)
	{
		if (TargetASM->PhysicsSimComponent)
		{
			UnregisterSettlingComponent(TargetASM->PhysicsSimComponent);
		}
		ASMActors.Remove(TargetASM);
		if (IsValid(TargetASM))
		{
			TargetASM->Destroy();
		}
		SyncManagerArrays();
		return true;
	}

	return false;
}

FTransform UASMComponent::GetResetTransformForSlot(int32 SlotIndex) const
{
	if (ResetTransforms.Num() > 0)
	{
		return ResetTransforms[FMath::Abs(SlotIndex) % ResetTransforms.Num()];
	}
	FVector OffsetLoc = DefaultResetTransform.GetLocation() + FVector(
		static_cast<float>(SlotIndex % 256) * 50.0f,
		static_cast<float>((SlotIndex / 256) % 256) * 50.0f,
		0.0f);
	return FTransform(DefaultResetTransform.GetRotation(), OffsetLoc, DefaultResetTransform.GetScale3D());
}

// =========================================================================
// Empty [ASMID] Management Functions
// =========================================================================
bool UASMComponent::AddEmptyASMID(int32 InASMID)
{
	AActivePropActor* ASM = GetActiveActorFromASMID(InASMID);
	if (!IsValid(ASM))
	{
		return false;
	}

	// Unbind any old ISID association
	if (const int32* OldISID = ASMIDToISID.Find(InASMID))
	{
		const int32 RemovedISID = *OldISID;
		ISIDToASMID.Remove(RemovedISID);
		ActiveActorByISID.Remove(RemovedISID);
		if (PropManager)
		{
			PropManager->ActiveActorByISID.Remove(RemovedISID);
		}
	}
	if (ASM->ISID > 0)
	{
		ISIDToASMID.Remove(ASM->ISID);
		ActiveActorByISID.Remove(ASM->ISID);
		if (PropManager)
		{
			PropManager->ActiveActorByISID.Remove(ASM->ISID);
		}
	}
	ASMIDToISID.Remove(InASMID);

	const int32 SlotIdx = EmptyASMIDs.Num();
	const FTransform ParkedTransform = GetResetTransformForSlot(SlotIdx);

	ASM->DeactivateToPoolState(ParkedTransform);
	ASM->ISID = 0;

	EmptyASMIDs.AddUnique(InASMID);
	if (PropManager)
	{
		PropManager->EmptyASMIDs.AddUnique(InASMID);
	}
	return true;
}

bool UASMComponent::RemoveEmptyASMID(int32 InASMID)
{
	const bool bRemoved = EmptyASMIDs.Remove(InASMID) > 0;
	if (bRemoved && PropManager)
	{
		PropManager->EmptyASMIDs.Remove(InASMID);
	}
	return bRemoved;
}

bool UASMComponent::IsEmptyASMID(int32 InASMID) const
{
	return EmptyASMIDs.Contains(InASMID);
}

int32 UASMComponent::GetEmptyASMID(int32 Index) const
{
	return EmptyASMIDs.IsValidIndex(Index) ? EmptyASMIDs[Index] : 0;
}

int32 UASMComponent::ConsumeEmptyASMID()
{
	while (EmptyASMIDs.Num() > 0)
	{
		const int32 CandidateASMID = EmptyASMIDs[0];
		EmptyASMIDs.RemoveAt(0, EAllowShrinking::No);
		if (PropManager)
		{
			PropManager->EmptyASMIDs.Remove(CandidateASMID);
		}
		if (AActivePropActor* ASM = GetActiveActorFromASMID(CandidateASMID))
		{
			if (IsValid(ASM))
			{
				return CandidateASMID;
			}
		}
	}
	return 0;
}

void UASMComponent::ClearEmptyASMIDs(bool bDestroyPhysicalActors)
{
	if (bDestroyPhysicalActors && EmptyASMIDs.Num() > 0)
	{
		TArray<int32> CopyEmpty = EmptyASMIDs;
		for (int32 EmptyID : CopyEmpty)
		{
			RemoveASMByASMID(EmptyID);
		}
	}
	EmptyASMIDs.Empty();
	SyncManagerArrays();
}

void UASMComponent::RebuildEmptyASMIDs()
{
	EmptyASMIDs.Empty();
	ASMActors.RemoveAll([](const TObjectPtr<AActivePropActor>& Ptr) { return !IsValid(Ptr); });

	for (AActivePropActor* ASM : ASMActors)
	{
		if (!IsValid(ASM)) continue;

		if (ASM->ASMID <= 0)
		{
			ASM->ASMID = AllocateASMID();
		}
		ActiveActorByASMID.Add(ASM->ASMID, ASM);

		const int32* ActiveISID = ASMIDToISID.Find(ASM->ASMID);
		if ((!ActiveISID || *ActiveISID <= 0) && ASM->ISID <= 0 && !ASM->bIsActive)
		{
			EmptyASMIDs.AddUnique(ASM->ASMID);
		}
	}
	SyncManagerArrays();
}

// =========================================================================
// Identity, Lookup & Representation Conversion (ISM <-> ASM)
// =========================================================================
int32 UASMComponent::AllocateASMID()
{
	while (ActiveActorByASMID.Contains(NextASMID) || NextASMID <= 0)
	{
		++NextASMID;
	}
	return NextASMID++;
}

AActivePropActor* UASMComponent::GetActiveActorFromASMID(int32 InASMID) const
{
	if (InASMID <= 0)
	{
		return nullptr;
	}
	if (const TObjectPtr<AActivePropActor>* Found = ActiveActorByASMID.Find(InASMID))
	{
		if (IsValid(Found->Get()))
		{
			return Found->Get();
		}
	}
	for (AActivePropActor* Candidate : ASMActors)
	{
		if (IsValid(Candidate) && Candidate->ASMID == InASMID)
		{
			return Candidate;
		}
	}
	return nullptr;
}

AActivePropActor* UASMComponent::GetActiveActorFromISID(int32 InISID) const
{
	if (const TObjectPtr<AActivePropActor>* Found = ActiveActorByISID.Find(InISID))
	{
		return Found->Get();
	}
	if (const int32* MappedASMID = ISIDToASMID.Find(InISID))
	{
		return GetActiveActorFromASMID(*MappedASMID);
	}
	return nullptr;
}

int32 UASMComponent::GetASMIDFromISID(int32 InISID) const
{
	if (const int32* MappedASMID = ISIDToASMID.Find(InISID))
	{
		return *MappedASMID;
	}
	if (const TObjectPtr<AActivePropActor>* Found = ActiveActorByISID.Find(InISID))
	{
		if (*Found)
		{
			return (*Found)->ASMID;
		}
	}
	return 0;
}

int32 UASMComponent::GetISIDFromASMID(int32 InASMID) const
{
	if (const int32* MappedISID = ASMIDToISID.Find(InASMID))
	{
		return *MappedISID;
	}
	if (AActivePropActor* ASM = GetActiveActorFromASMID(InASMID))
	{
		return ASM->ISID;
	}
	return 0;
}

void UASMComponent::RegisterActiveActor(int32 InISID, AActivePropActor* InActor)
{
	if (InISID <= 0 || !IsValid(InActor))
	{
		return;
	}

	RegisterProp(InActor);
	EmptyASMIDs.Remove(InActor->ASMID);

	ActiveActorByISID.Add(InISID, InActor);
	ISIDToASMID.Add(InISID, InActor->ASMID);
	ASMIDToISID.Add(InActor->ASMID, InISID);

	if (PropManager)
	{
		PropManager->EmptyASMIDs.Remove(InActor->ASMID);
		PropManager->ActiveActorByISID.Add(InISID, InActor);
		if (FISIDRuntimeIndex* Record = PropManager->FindISIDData(InISID))
		{
			Record->ActiveActor = InActor;
			Record->ASMID = InActor->ASMID;
			Record->Representation = EPropRepresentation::ASM;
		}
	}
}

void UASMComponent::UnregisterActiveActor(int32 InISID)
{
	if (const int32* MappedASMID = ISIDToASMID.Find(InISID))
	{
		ASMIDToISID.Remove(*MappedASMID);
	}
	ISIDToASMID.Remove(InISID);
	ActiveActorByISID.Remove(InISID);

	if (PropManager)
	{
		PropManager->ActiveActorByISID.Remove(InISID);
		if (FISIDRuntimeIndex* Record = PropManager->FindISIDData(InISID))
		{
			Record->ActiveActor = nullptr;
			Record->ASMID = 0;
		}
	}
}

namespace
{
	// Dedicated ASM pool partitioning (4 player slots x 25 ASMs per 100-ASM block):
	//   Slot 0 (Server / Host / AI): ASMIDs 1..25   (and 101..125, ...)
	//   Slot 1 (Client Player 1):    ASMIDs 26..50  (and 126..150, ...)
	//   Slot 2 (Client Player 2):    ASMIDs 51..75  (and 151..175, ...)
	//   Slot 3 (Client Player 3):    ASMIDs 76..100 (and 176..200, ...)
	constexpr int32 GNumDedicatedPlayerSlots = 4;
	constexpr int32 GASMsPerPlayerSlot = 25;

	int32 GetPlayerSlotForASMID(int32 InASMID)
	{
		if (InASMID <= 0)
		{
			return 0;
		}
		return ((InASMID - 1) / GASMsPerPlayerSlot) % GNumDedicatedPlayerSlots;
	}

	int32 ResolveLocalDedicatedPlayerSlot(UWorld* World, bool bIsClient)
	{
		if (!bIsClient || !World)
		{
			return 0; // Server / Listen-Server Host / AI uses Slot 0 (ASMIDs 1..25)
		}

		APlayerController* LocalPC = World->GetFirstPlayerController();
		APlayerState* LocalPS = LocalPC ? LocalPC->PlayerState.Get() : nullptr;
		AGameStateBase* GameState = World->GetGameState();
		if (LocalPS && GameState && GameState->PlayerArray.Num() > 1)
		{
			TArray<int32> SortedPlayerIds;
			for (APlayerState* PS : GameState->PlayerArray)
			{
				if (PS)
				{
					SortedPlayerIds.AddUnique(PS->GetPlayerId());
				}
			}
			SortedPlayerIds.Sort();
			const int32 Rank = SortedPlayerIds.IndexOfByKey(LocalPS->GetPlayerId());
			if (Rank != INDEX_NONE)
			{
				const int32 ClientSlot = (Rank > 0) ? (1 + ((Rank - 1) % (GNumDedicatedPlayerSlots - 1))) : 1;
				return ClientSlot;
			}
		}

		// Default single remote client gets Slot 1 (ASMIDs 26..50)
		return 1;
	}
}

AActivePropActor* UASMComponent::AcquireActiveActor(int32 InProductID)
{
	UWorld* World = GetWorld();
	const bool bIsClient = (GetOwner() && !GetOwner()->HasAuthority() && World && World->IsNetMode(NM_Client));
	const int32 DedicatedSlot = ResolveLocalDedicatedPlayerSlot(World, bIsClient);

	if (bIsClient)
	{
		// Pass 1: Prefer an inactive ASM in this Client's dedicated 25-ASM partition (e.g., ASMIDs 26..50 for Client 1)
		for (int32 i = 0; i < EmptyASMIDs.Num(); ++i)
		{
			const int32 CandidateID = EmptyASMIDs[i];
			if (GetPlayerSlotForASMID(CandidateID) != DedicatedSlot)
			{
				continue;
			}
			if (AActivePropActor* CandidateASM = GetActiveActorFromASMID(CandidateID))
			{
				if (IsValid(CandidateASM) && !CandidateASM->bIsActive && CandidateASM->ISID <= 0)
				{
					EmptyASMIDs.RemoveAt(i, EAllowShrinking::No);
					if (PropManager)
					{
						PropManager->EmptyASMIDs.Remove(CandidateID);
					}
					return CandidateASM;
				}
			}
		}

		for (AActivePropActor* CandidateASM : ASMActors)
		{
			if (IsValid(CandidateASM) && CandidateASM->ASMID > 0 && GetPlayerSlotForASMID(CandidateASM->ASMID) == DedicatedSlot)
			{
				if (!CandidateASM->bIsActive && CandidateASM->ISID <= 0 && !ASMIDToISID.Contains(CandidateASM->ASMID))
				{
					EmptyASMIDs.Remove(CandidateASM->ASMID);
					if (PropManager)
					{
						PropManager->EmptyASMIDs.Remove(CandidateASM->ASMID);
					}
					return CandidateASM;
				}
			}
		}

		// Pass 2 (Overflow): If all 25 dedicated ASMs for this Client are currently active, fall back to any free non-Server-slot ASM first, then any free ASM
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			for (AActivePropActor* CandidateASM : ASMActors)
			{
				if (IsValid(CandidateASM) && !CandidateASM->bIsActive && CandidateASM->ISID <= 0 && (CandidateASM->ASMID <= 0 || !ASMIDToISID.Contains(CandidateASM->ASMID)))
				{
					if (Pass == 0 && CandidateASM->ASMID > 0 && GetPlayerSlotForASMID(CandidateASM->ASMID) == 0)
					{
						continue;
					}
					if (CandidateASM->ASMID > 0)
					{
						EmptyASMIDs.Remove(CandidateASM->ASMID);
						if (PropManager)
						{
							PropManager->EmptyASMIDs.Remove(CandidateASM->ASMID);
						}
					}
					return CandidateASM;
				}
			}
		}
		return nullptr;
	}

	// 1. Ensure pool is initialized to at least MinASMCount (100)
	if (ASMActors.Num() < FMath::Clamp(MinASMCount, 1, MaxASMCount))
	{
		EvaluatePoolCapacity();
	}

	const TSubclassOf<AActivePropActor> DesiredClass = GetEffectiveActivePropActorClass(InProductID);
	UClass* DesiredUClass = DesiredClass ? DesiredClass.Get() : AActivePropActor::StaticClass();

	// 2. Server prefers an available Empty ASMID in the Server's dedicated Slot 0 (ASMIDs 1..25) first, then falls back to any empty slot
	int32 AcquiredASMID = 0;
	AActivePropActor* AcquiredASM = nullptr;
	for (int32 Pass = 0; Pass < 2 && !IsValid(AcquiredASM); ++Pass)
	{
		for (int32 i = 0; i < EmptyASMIDs.Num(); ++i)
		{
			const int32 CandidateID = EmptyASMIDs[i];
			if (Pass == 0 && GetPlayerSlotForASMID(CandidateID) != DedicatedSlot)
			{
				continue;
			}
			if (AActivePropActor* CandidateASM = GetActiveActorFromASMID(CandidateID))
			{
				if (IsValid(CandidateASM) && !CandidateASM->bIsActive && CandidateASM->ISID <= 0 && CandidateASM->GetClass() == DesiredUClass)
				{
					EmptyASMIDs.RemoveAt(i, EAllowShrinking::No);
					if (PropManager)
					{
						PropManager->EmptyASMIDs.Remove(CandidateID);
					}
					AcquiredASMID = CandidateID;
					AcquiredASM = CandidateASM;
					break;
				}
			}
		}
	}

	// If no exact-class empty ASM was found, consume any empty ASM and replace it with the desired BP class
	while (!IsValid(AcquiredASM) && EmptyASMIDs.Num() > 0)
	{
		AcquiredASMID = ConsumeEmptyASMID();
		AActivePropActor* CandidateASM = (AcquiredASMID > 0) ? GetActiveActorFromASMID(AcquiredASMID) : nullptr;
		if (!IsValid(CandidateASM) || CandidateASM->bIsActive || CandidateASM->ISID > 0)
		{
			continue;
		}
		AcquiredASM = CandidateASM;
		if (AcquiredASM->GetClass() != DesiredUClass)
		{
			RemoveASMByASMID(AcquiredASMID);
			AcquiredASM = SpawnPooledASM(DesiredClass);
			if (AcquiredASM)
			{
				EmptyASMIDs.Remove(AcquiredASM->ASMID);
				SyncManagerArrays();
			}
		}
		break;
	}

	// 3. If no empty ASM was available and we are below MaxASMCount (500), spawn with DesiredClass
	if (!IsValid(AcquiredASM) && ASMActors.Num() < MaxASMCount)
	{
		AcquiredASM = SpawnPooledASM(DesiredClass);
		if (AcquiredASM)
		{
			EmptyASMIDs.Remove(AcquiredASM->ASMID);
			SyncManagerArrays();
		}
	}

	// 4. Evaluate pool utilization — if pool is now >= 90% full, automatically add more ASM units up to MaxASMCount (500)
	EvaluatePoolCapacity();

	return AcquiredASM;
}

void UASMComponent::ReturnActiveActorToPool(AActivePropActor* InActor)
{
	if (!IsValid(InActor))
	{
		return;
	}

	if (InActor->bIsActive || InActor->ISID > 0)
	{
		FTransform ResetTransform = DefaultResetTransform;
		if (PropManager)
		{
			ResetTransform = PropManager->GetResetTransformForSlot(InActor->ASMID);
		}
		InActor->ISID = 0;
		InActor->DeactivateToPoolState(ResetTransform);
	}

	RegisterProp(InActor);
	AddEmptyASMID(InActor->ASMID);

	// Check if pool should shrink excess empty ASMs while keeping >= MinASMCount (100)
	EvaluatePoolCapacity();
}

bool UASMComponent::PromoteRecordToASM(
	FISIDRuntimeIndex& Record,
	const FPropProductDefinition* ProductDef,
	const FTransform& GameplayTransform,
	EPropActivationReason Reason,
	bool bEnableReplication,
	AActivePropActor*& OutActor)
{
	OutActor = nullptr;
	const int32 SignedISID = Record.ISID;

	// If already in ASM representation with a valid actor, return it
	if (Record.Representation == EPropRepresentation::ASM)
	{
		if (AActivePropActor* ExistingASM = GetActiveActorFromISID(SignedISID))
		{
			OutActor = ExistingASM;
			return true;
		}
	}

	// If Record.ASMID was pre-selected (e.g., matching a client-predicted ASMID on the server), prefer that exact pooled actor if free
	AActivePropActor* ASM = nullptr;
	if (Record.ASMID > 0)
	{
		if (AActivePropActor* PreferredASM = GetActiveActorFromASMID(Record.ASMID))
		{
			if (IsValid(PreferredASM) && !PreferredASM->bIsActive && PreferredASM->ISID <= 0)
			{
				EmptyASMIDs.Remove(PreferredASM->ASMID);
				if (PropManager)
				{
					PropManager->EmptyASMIDs.Remove(PreferredASM->ASMID);
				}
				ASM = PreferredASM;
			}
		}
	}

	// Otherwise acquire an ASM unit from EmptyASMIDs (or auto-expand up to MaxASMCount = 500)
	if (!ASM)
	{
		ASM = AcquireActiveActor(Record.ProductID);
	}
	if (!ASM)
	{
		return false;
	}

	// Determine target initial ASM state based on activation reason
	EPropState InitialASMState = Record.State;
	if (InitialASMState == EPropState::AtRest)
	{
		switch (Reason)
		{
		case EPropActivationReason::Grab:
			InitialASMState = EPropState::Held;
			break;
		case EPropActivationReason::Throw:
		case EPropActivationReason::PhysicsImpulse:
		case EPropActivationReason::Movement:
			InitialASMState = EPropState::InMotion;
			break;
		case EPropActivationReason::Drop:
		case EPropActivationReason::Damage:
			InitialASMState = EPropState::Unsettled;
			break;
		default:
			break;
		}
	}
	Record.State = InitialASMState;

	// Update Record, Copy ISID/PID/State/Runtime Data/Version to ASM, and register active binding BEFORE activating representation
	Record.Representation = EPropRepresentation::ASM;
	Record.ActiveActor = ASM;
	Record.ASMID = ASM->ASMID;
	Record.Version++;

	ASM->InitializeFromISIDRecord(Record, ProductDef, PropManager);
	ASM->Version = Record.Version;
	RegisterActiveActor(SignedISID, ASM);

	// Activate ASM representation at gameplay transform (fires OnBecomeActive before collision/physics/tick start)
	ASM->ActivateASMRepresentation(GameplayTransform, InitialASMState, bEnableReplication);

	OutActor = ASM;
	return true;
}

bool UASMComponent::DemoteRecordFromASM(
	FISIDRuntimeIndex& Record,
	EPropState NewState,
	bool bPoolActor,
	bool bWriteBackTransform)
{
	const int32 SignedISID = Record.ISID;
	AActivePropActor* ASM = GetActiveActorFromISID(SignedISID);
	if (!ASM && IsValid(Record.ActiveActor))
	{
		ASM = Record.ActiveActor.Get();
	}

	if (ASM)
	{
		// Fire Blueprint pre-return events while the actor is still active at its gameplay transform
		ASM->NotifyBeforeReturnToRestPosition();

		FTransform SavedTransform = Record.ActiveTransform;
		ASM->WriteToISIDRecord(Record);
		if (!bWriteBackTransform)
		{
			Record.ActiveTransform = SavedTransform;
		}
	}

	Record.State = NewState;
	Record.PackedRuntimeData.bIsGrabbable = (NewState != EPropState::Held);
	Record.RuntimeData.InteractionState = EPropInteractionLockState::Free;
	Record.RuntimeData.InteractionOwner = nullptr;
	Record.RuntimeData.CurrentInteractor = nullptr;
	Record.Representation = EPropRepresentation::ISM;
	Record.ActiveActor = nullptr;
	Record.ASMID = 0;
	Record.Version++;

	UnregisterActiveActor(SignedISID);

	if (ASM)
	{
		FTransform ResetTransform = DefaultResetTransform;
		if (PropManager)
		{
			ResetTransform = PropManager->GetResetTransformForSlot(ASM->ASMID);
		}
		ASM->ISID = 0;
		ASM->DeactivateToPoolState(ResetTransform);

		if (bPoolActor)
		{
			ReturnActiveActorToPool(ASM);
		}
		else
		{
			RemoveASMByASMID(ASM->ASMID);
		}
	}

	return true;
}

void UASMComponent::ResetActiveASMsToPool()
{
	TArray<int32> CurrentActiveISIDs;
	ActiveActorByISID.GetKeys(CurrentActiveISIDs);
	for (int32 ActiveISID : CurrentActiveISIDs)
	{
		if (AActivePropActor* ASM = GetActiveActorFromISID(ActiveISID))
		{
			AddEmptyASMID(ASM->ASMID);
		}
	}
	ActiveActorByISID.Empty();
	ISIDToASMID.Empty();
	ASMIDToISID.Empty();
	SyncManagerArrays();
}

// =========================================================================
// Registration, Distributed Temperature Updates & Physics Settling
// =========================================================================
void UASMComponent::RegisterProp(AActivePropActor* Prop)
{
	if (!IsValid(Prop))
	{
		return;
	}

	if (!PropManager)
	{
		PropManager = Cast<APropManager>(GetOwner());
	}
	Prop->PropManagerReference = PropManager;

	const bool bIsClient = (Prop->GetWorld() && Prop->GetWorld()->IsNetMode(NM_Client) && !Prop->HasAuthority());
	if (Prop->ASMID <= 0 && !bIsClient)
	{
		Prop->ASMID = AllocateASMID();
	}

	ASMActors.AddUnique(Prop);
	if (PropManager)
	{
		PropManager->ASMActors.AddUnique(Prop);
	}

	if (Prop->ASMID > 0)
	{
		ActiveActorByASMID.Add(Prop->ASMID, Prop);
		if (PropManager)
		{
			PropManager->ActiveActorByASMID.Add(Prop->ASMID, Prop);
			PropManager->NextASMID = FMath::Max(PropManager->NextASMID, NextASMID);
		}

		if (!Prop->bIsActive && Prop->ISID <= 0)
		{
			EmptyASMIDs.AddUnique(Prop->ASMID);
			if (PropManager)
			{
				PropManager->EmptyASMIDs.AddUnique(Prop->ASMID);
			}
		}
	}
}

void UASMComponent::UnregisterProp(AActivePropActor* Prop)
{
	if (!Prop)
	{
		return;
	}

	if (Prop->ASMID > 0)
	{
		if (const int32* BoundISID = ASMIDToISID.Find(Prop->ASMID))
		{
			ISIDToASMID.Remove(*BoundISID);
			ActiveActorByISID.Remove(*BoundISID);
		}
		ASMIDToISID.Remove(Prop->ASMID);
		EmptyASMIDs.Remove(Prop->ASMID);
		ActiveActorByASMID.Remove(Prop->ASMID);
	}

	ASMActors.Remove(Prop);
	SyncManagerArrays();
}

void UASMComponent::RegisterSettlingComponent(UStaticMeshPhysicsSimulationComponent* SimComp)
{
	if (SimComp && !ActiveSettlingComponents.Contains(SimComp))
	{
		ActiveSettlingComponents.Add(SimComp);
	}
}

void UASMComponent::UnregisterSettlingComponent(UStaticMeshPhysicsSimulationComponent* SimComp)
{
	ActiveSettlingComponents.Remove(SimComp);
}

void UASMComponent::UpdateDistributedASMs(float DeltaSeconds, float AmbientTemperature, int32 PropsUpdatedPerFrame)
{
	ASMActors.RemoveAll([](const TObjectPtr<AActivePropActor>& Ptr) { return !IsValid(Ptr); });

	if (ASMActors.Num() == 0)
	{
		return;
	}

	const int32 CountToUpdate = FMath::Min(FMath::Max(1, PropsUpdatedPerFrame), ASMActors.Num());
	for (int32 i = 0; i < CountToUpdate; ++i)
	{
		if (CurrentASMUpdateIndex >= ASMActors.Num())
		{
			CurrentASMUpdateIndex = 0;
		}

		if (ASMActors.IsValidIndex(CurrentASMUpdateIndex))
		{
			if (AActivePropActor* Prop = ASMActors[CurrentASMUpdateIndex])
			{
				if (Prop->bIsActive && Prop->bEnableTemperatureUpdate)
				{
					const float EffectiveDelta = DeltaSeconds * static_cast<float>(ASMActors.Num());
					Prop->UpdateTemperature(AmbientTemperature, EffectiveDelta);
					if (Prop->ISID > 0 && PropManager)
					{
						if (FISIDRuntimeIndex* Record = PropManager->FindISIDData(Prop->ISID))
						{
							Record->PackedRuntimeData.Temperature = Prop->CurrentTemperature;
							Record->RuntimeData.Temperature = Prop->CurrentTemperature;
						}
					}
				}
			}
		}

		CurrentASMUpdateIndex = (CurrentASMUpdateIndex + 1) % ASMActors.Num();
	}
}

void UASMComponent::CheckSettlingQueue(bool bUseAsyncSettlingCheck)
{
	ActiveSettlingComponents.RemoveAll([](const TWeakObjectPtr<UStaticMeshPhysicsSimulationComponent>& Ptr) { return !Ptr.IsValid(); });

	if (ActiveSettlingComponents.Num() == 0)
	{
		return;
	}

	if (bUseAsyncSettlingCheck)
	{
		ProcessSettlingQueueAsync();
	}
	else
	{
		TArray<UStaticMeshPhysicsSimulationComponent*> RestedComponents;
		for (auto It = ActiveSettlingComponents.CreateIterator(); It; ++It)
		{
			if (UStaticMeshPhysicsSimulationComponent* SimComp = It->Get())
			{
				if (SimComp->CurrentState == EPhysicsSimulationState::Settling)
				{
					if (SimComp->CheckSettled())
					{
						RestedComponents.Add(SimComp);
						It.RemoveCurrent();
					}
				}
				else
				{
					It.RemoveCurrent();
				}
			}
		}

		for (UStaticMeshPhysicsSimulationComponent* SimComp : RestedComponents)
		{
			SimComp->SetStateAtRest();
			if (AActivePropActor* PropOwner = Cast<AActivePropActor>(SimComp->GetOwner()))
			{
				if (PropOwner->bIsActive && PropOwner->ISID > 0 && PropManager)
				{
					PropManager->ConvertASMToISM(static_cast<uint32>(PropOwner->ISID), EPropState::AtRest, true, true);
				}
			}
			OnSettlingObjectRested.Broadcast(SimComp, SimComp->GetOwner());
			if (PropManager)
			{
				PropManager->OnSettlingObjectRested.Broadcast(SimComp, SimComp->GetOwner());
			}
		}
	}
}

void UASMComponent::ProcessSettlingQueueAsync()
{
	struct FSettlingData
	{
		TWeakObjectPtr<UStaticMeshPhysicsSimulationComponent> Comp;
		FVector LinearVelocity;
		FVector AngularVelocityDegrees;
		float LinearTolerance;
		float AngularTolerance;
		bool bIsSleeping;
	};

	TArray<FSettlingData> Snapshots;
	Snapshots.Reserve(ActiveSettlingComponents.Num());

	for (const auto& WeakComp : ActiveSettlingComponents)
	{
		if (UStaticMeshPhysicsSimulationComponent* SimComp = WeakComp.Get())
		{
			if (SimComp->CurrentState == EPhysicsSimulationState::Settling)
			{
				UPrimitiveComponent* Prim = SimComp->TargetMesh;
				const FVector LinVel = Prim ? Prim->GetComponentVelocity() : FVector::ZeroVector;
				const FVector AngVel = Prim ? Prim->GetPhysicsAngularVelocityInDegrees() : FVector::ZeroVector;
				const bool bSleeping = Prim ? (!Prim->IsSimulatingPhysics() || !Prim->IsAnyRigidBodyAwake()) : true;
				Snapshots.Add({ WeakComp, LinVel, AngVel, SimComp->VelocityTolerance, SimComp->AngularVelocityTolerance, bSleeping });
			}
		}
	}

	if (Snapshots.Num() == 0)
	{
		return;
	}

	TWeakObjectPtr<UASMComponent> WeakThis(this);

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Snapshots, WeakThis]()
	{
		TArray<TWeakObjectPtr<UStaticMeshPhysicsSimulationComponent>> SettledComps;
		for (const FSettlingData& Item : Snapshots)
		{
			if (Item.bIsSleeping ||
				(Item.LinearVelocity.Size() <= Item.LinearTolerance &&
				 Item.AngularVelocityDegrees.Size() <= Item.AngularTolerance))
			{
				SettledComps.Add(Item.Comp);
			}
		}

		if (SettledComps.Num() > 0)
		{
			AsyncTask(ENamedThreads::GameThread, [SettledComps, WeakThis]()
			{
				if (UASMComponent* StrongThis = WeakThis.Get())
				{
					for (const auto& WeakComp : SettledComps)
					{
						if (UStaticMeshPhysicsSimulationComponent* SimComp = WeakComp.Get())
						{
							if (SimComp->CurrentState == EPhysicsSimulationState::Settling)
							{
								SimComp->SetStateAtRest();
								StrongThis->ActiveSettlingComponents.Remove(WeakComp);
								if (AActivePropActor* PropOwner = Cast<AActivePropActor>(SimComp->GetOwner()))
								{
									if (PropOwner->bIsActive && PropOwner->ISID > 0 && StrongThis->PropManager)
									{
										StrongThis->PropManager->ConvertASMToISM(static_cast<uint32>(PropOwner->ISID), EPropState::AtRest, true, true);
									}
								}
								StrongThis->OnSettlingObjectRested.Broadcast(SimComp, SimComp->GetOwner());
								if (StrongThis->PropManager)
								{
									StrongThis->PropManager->OnSettlingObjectRested.Broadcast(SimComp, SimComp->GetOwner());
								}
							}
						}
					}
				}
			});
		}
	});
}

void UASMComponent::FlushActiveActorState()
{
	if (!PropManager)
	{
		return;
	}

	for (TPair<int32, TObjectPtr<AActivePropActor>>& Pair : ActiveActorByISID)
	{
		if (AActivePropActor* ASM = Pair.Value.Get())
		{
			if (FISIDRuntimeIndex* Record = PropManager->FindISIDData(Pair.Key))
			{
				ASM->WriteToISIDRecord(*Record);
			}
		}
	}
}

void UASMComponent::SyncManagerArrays()
{
	if (!PropManager)
	{
		return;
	}

	PropManager->ASMActors = ASMActors;
	PropManager->EmptyASMIDs = EmptyASMIDs;
	PropManager->ActiveActorByASMID = ActiveActorByASMID;
	PropManager->ActiveActorByISID = ActiveActorByISID;
	PropManager->NextASMID = NextASMID;
}
