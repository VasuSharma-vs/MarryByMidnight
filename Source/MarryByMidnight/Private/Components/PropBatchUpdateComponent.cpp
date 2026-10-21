#include "Components/PropBatchUpdateComponent.h"
#include "Props/InteractablePropManager.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Props/InteractableProp.h"

UPropBatchUpdateComponent::UPropBatchUpdateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPropBatchUpdateComponent::BeginPlay()
{
	Super::BeginPlay();
	GetPropManager();
}

APropManager* UPropBatchUpdateComponent::GetPropManager()
{
	if (!PropManagerRef.IsValid())
	{
		if (APropManager* Manager = Cast<APropManager>(GetOwner()))
		{
			PropManagerRef = Manager;
		}
		else
		{
			PropManagerRef = APropManager::Get(this);
		}
	}
	return PropManagerRef.Get();
}

void UPropBatchUpdateComponent::ProcessBatchUpdate(float DeltaSeconds)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	APropManager* Manager = GetPropManager();
	if (!Manager || Manager->AllActiveISIDs.Num() == 0)
	{
		return;
	}

	const int32 TotalISIDs = Manager->AllActiveISIDs.Num();
	const int32 CountToProcess = FMath::Clamp(BatchSize, 5, 50);

	TArray<FPropBatchEvaluationItem> CurrentBatch;
	CurrentBatch.Reserve(CountToProcess);

	const int32 CurrentMinutes = Manager->GetCurrentWorldMinutes();
	const int32 StartIndex = NextBatchIndex;
	bool bCycleCompleted = false;

	for (int32 i = 0; i < CountToProcess; ++i)
	{
		if (NextBatchIndex >= TotalISIDs)
		{
			NextBatchIndex = 0;
			bCycleCompleted = true;
		}

		const int32 CheckISID = Manager->AllActiveISIDs[NextBatchIndex++];
		FISIDRuntimeIndex* Record = Manager->FindISIDData(CheckISID);
		if (!Record)
		{
			continue;
		}

		// Build the evaluation item
		FPropBatchEvaluationItem Item;
		Item.ISID = CheckISID;
		Item.ProductID = Record->ProductID;
		Item.Representation = Record->Representation;
		Item.State = Record->State;
		Item.CurrentTemperature = Record->PackedRuntimeData.Temperature;
		Item.RemainingPortions = Record->PackedRuntimeData.RemainingPortions;
		Item.AgeMinutes = FMath::Max(0.0f, static_cast<float>(CurrentMinutes - Record->PackedRuntimeData.CreationMinute));
		Item.IISMComponentID = Record->IISMComponentID;
		Item.ActiveActor = Record->ActiveActor;
		Item.bIsGrabbable = Record->PackedRuntimeData.bIsGrabbable;
		if (Record->ActiveActor)
		{
			Item.bIsGrabbable = Record->ActiveActor->bIsGrabbable;
		}
		if (Record->State == EPropState::Held)
		{
			Item.bIsGrabbable = false;
		}

		// Resolve location & transform
		if (Record->Representation == EPropRepresentation::ISM)
		{
			if (UInteractableISMComponent* Comp = Manager->FindIISMComponent(Record->IISMComponentID))
			{
				FTransform InstanceTransform;
				if (Comp->GetInstanceTransform(Record->ISMI, InstanceTransform, true))
				{
					Item.WorldTransform = InstanceTransform;
					Item.WorldLocation = InstanceTransform.GetLocation();
				}
			}
		}
		else if (Record->ActiveActor)
		{
			Item.WorldTransform = Record->ActiveActor->GetActorTransform();
			Item.WorldLocation = Record->ActiveActor->GetActorLocation();
		}

		CurrentBatch.Add(Item);
	}

	FPropBatchMetadata Metadata;
	Metadata.BatchStartIndex = StartIndex;
	Metadata.BatchEndIndex = NextBatchIndex;
	Metadata.TotalActiveISIDs = TotalISIDs;
	Metadata.bCycleCompleted = bCycleCompleted;

	// Broadcast batch to subscribed listeners (weather, cooking/cooling zones, AI perception)
	if (CurrentBatch.Num() > 0)
	{
		OnBatchEvaluatePropsNative.Broadcast(CurrentBatch, Metadata);
		if (OnBatchEvaluateProps.IsBound())
		{
			OnBatchEvaluateProps.Broadcast(CurrentBatch, Metadata);
		}
	}

	// Flush dirty field deltas accumulated this frame
	FlushFieldDeltas();
}

bool UPropBatchUpdateComponent::SetInstanceTemperature(int32 InISID, float NewTemperature)
{
	if (InISID <= 0) return false;

	APropManager* Manager = GetPropManager();
	if (!Manager) return false;

	FISIDRuntimeIndex* Record = Manager->FindISIDData(InISID);
	if (!Record) return false;

	if (FMath::IsNearlyEqual(Record->PackedRuntimeData.Temperature, NewTemperature, 0.05f))
	{
		return false;
	}

	const int32 CurrentMinutes = Manager->GetCurrentWorldMinutes();
	Record->PackedRuntimeData.Temperature = NewTemperature;
	Record->RuntimeData.Temperature = NewTemperature;
	Record->PackedRuntimeData.LastUpdatedMinute = CurrentMinutes;

	if (Record->Representation == EPropRepresentation::ISM)
	{
		if (UInteractableISMComponent* Comp = Manager->FindIISMComponent(Record->IISMComponentID))
		{
			Comp->SetInstanceData(InISID, Record->PackedRuntimeData);
		}
	}
	else if (AActivePropActor* PropActor = Record->ActiveActor)
	{
		PropActor->CurrentTemperature = NewTemperature;
	}

	FPropRuntimeFieldDelta& Delta = PendingFieldDeltas.FindOrAdd(InISID);
	Delta.ISID = InISID;
	Delta.SetFlag(EPropRuntimeDirtyMask::Temperature);
	Delta.Temperature = NewTemperature;
	Delta.LastUpdatedMinute = CurrentMinutes;

	return true;
}

bool UPropBatchUpdateComponent::SetInstancePortions(int32 InISID, int32 NewPortions)
{
	if (InISID <= 0) return false;

	APropManager* Manager = GetPropManager();
	if (!Manager) return false;

	FISIDRuntimeIndex* Record = Manager->FindISIDData(InISID);
	if (!Record) return false;

	const int32 ClampedPortions = FMath::Clamp(NewPortions, 0, 255);
	if (Record->PackedRuntimeData.RemainingPortions == ClampedPortions)
	{
		return false;
	}

	const int32 CurrentMinutes = Manager->GetCurrentWorldMinutes();
	Record->PackedRuntimeData.RemainingPortions = ClampedPortions;
	Record->RuntimeData.RemainingPortions = ClampedPortions;
	Record->PackedRuntimeData.LastUpdatedMinute = CurrentMinutes;

	if (Record->Representation == EPropRepresentation::ISM)
	{
		if (UInteractableISMComponent* Comp = Manager->FindIISMComponent(Record->IISMComponentID))
		{
			Comp->SetInstanceData(InISID, Record->PackedRuntimeData);
		}
	}
	else if (AActivePropActor* PropActor = Record->ActiveActor)
	{
		PropActor->RemainingPortions = ClampedPortions;
		PropActor->OnRep_RemainingPortions();
	}

	FPropRuntimeFieldDelta& Delta = PendingFieldDeltas.FindOrAdd(InISID);
	Delta.ISID = InISID;
	Delta.SetFlag(EPropRuntimeDirtyMask::Portions);
	Delta.RemainingPortions = ClampedPortions;
	Delta.LastUpdatedMinute = CurrentMinutes;

	return true;
}

bool UPropBatchUpdateComponent::SetInstanceState(int32 InISID, EPropState NewState)
{
	if (InISID <= 0) return false;

	APropManager* Manager = GetPropManager();
	if (!Manager) return false;

	FISIDRuntimeIndex* Record = Manager->FindISIDData(InISID);
	if (!Record || Record->State == NewState)
	{
		return false;
	}

	Record->State = NewState;
	if (AActivePropActor* PropActor = Record->ActiveActor)
	{
		PropActor->SetPropState(NewState);
	}

	const int32 CurrentMinutes = Manager->GetCurrentWorldMinutes();
	FPropRuntimeFieldDelta& Delta = PendingFieldDeltas.FindOrAdd(InISID);
	Delta.ISID = InISID;
	Delta.SetFlag(EPropRuntimeDirtyMask::State);
	Delta.State = NewState;
	Delta.LastUpdatedMinute = CurrentMinutes;

	return true;
}

bool UPropBatchUpdateComponent::ApplyFieldDelta(const FPropRuntimeFieldDelta& Delta)
{
	bool bChanged = false;
	if (Delta.HasFlag(EPropRuntimeDirtyMask::Temperature))
	{
		bChanged |= SetInstanceTemperature(Delta.ISID, Delta.Temperature);
	}
	if (Delta.HasFlag(EPropRuntimeDirtyMask::Portions))
	{
		bChanged |= SetInstancePortions(Delta.ISID, Delta.RemainingPortions);
	}
	if (Delta.HasFlag(EPropRuntimeDirtyMask::State))
	{
		bChanged |= SetInstanceState(Delta.ISID, Delta.State);
	}
	return bChanged;
}

bool UPropBatchUpdateComponent::FindClosestItemInBatch(const TArray<FPropBatchEvaluationItem>& Batch, const FVector& Origin, FPropBatchEvaluationItem& OutClosestItem, float MaxDistance)
{
	float MinDistSq = FMath::Square(MaxDistance);
	bool bFound = false;

	for (const FPropBatchEvaluationItem& Item : Batch)
	{
		const float DistSq = FVector::DistSquared(Origin, Item.WorldLocation);
		if (DistSq < MinDistSq)
		{
			MinDistSq = DistSq;
			OutClosestItem = Item;
			bFound = true;
		}
	}

	return bFound;
}

void UPropBatchUpdateComponent::FilterItemsByProduct(const TArray<FPropBatchEvaluationItem>& Batch, int32 InProductID, TArray<FPropBatchEvaluationItem>& OutItems)
{
	OutItems.Empty();
	for (const FPropBatchEvaluationItem& Item : Batch)
	{
		if (Item.ProductID == InProductID)
		{
			OutItems.Add(Item);
		}
	}
}

void UPropBatchUpdateComponent::FilterItemsInBox(const TArray<FPropBatchEvaluationItem>& Batch, const FBox& WorldBounds, TArray<FPropBatchEvaluationItem>& OutItems)
{
	OutItems.Empty();
	for (const FPropBatchEvaluationItem& Item : Batch)
	{
		if (WorldBounds.IsInsideOrOn(Item.WorldLocation))
		{
			OutItems.Add(Item);
		}
	}
}

void UPropBatchUpdateComponent::FlushFieldDeltas()
{
	if (PendingFieldDeltas.Num() == 0) return;

	TArray<FPropRuntimeFieldDelta> BatchToSend;
	BatchToSend.Reserve(PendingFieldDeltas.Num());

	for (const auto& Pair : PendingFieldDeltas)
	{
		if (Pair.Value.DirtyFlags != 0)
		{
			BatchToSend.Add(Pair.Value);
		}
	}
	PendingFieldDeltas.Empty();

	if (BatchToSend.Num() > 0)
	{
		Multicast_SyncFieldDeltas(BatchToSend);
	}
}

void UPropBatchUpdateComponent::Multicast_SyncFieldDeltas_Implementation(const TArray<FPropRuntimeFieldDelta>& Deltas)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->HasAuthority())
	{
		return;
	}
	ApplyFieldDeltasOnClient(Deltas);
}

void UPropBatchUpdateComponent::ApplyFieldDeltasOnClient(const TArray<FPropRuntimeFieldDelta>& Deltas)
{
	APropManager* Manager = GetPropManager();
	if (!Manager) return;

	for (const FPropRuntimeFieldDelta& Delta : Deltas)
	{
		FISIDRuntimeIndex* Record = Manager->FindISIDData(Delta.ISID);
		if (!Record) continue;

		bool bNeedUpdateComp = false;

		// 1. Only apply Temperature if flagged dirty
		if (Delta.HasFlag(EPropRuntimeDirtyMask::Temperature))
		{
			Record->PackedRuntimeData.Temperature = Delta.Temperature;
			Record->RuntimeData.Temperature = Delta.Temperature;
			if (AActivePropActor* PropActor = Record->ActiveActor)
			{
				PropActor->CurrentTemperature = Delta.Temperature;
			}
			bNeedUpdateComp = true;
		}

		// 2. Only apply Portions if flagged dirty
		if (Delta.HasFlag(EPropRuntimeDirtyMask::Portions))
		{
			Record->PackedRuntimeData.RemainingPortions = Delta.RemainingPortions;
			Record->RuntimeData.RemainingPortions = Delta.RemainingPortions;
			if (AActivePropActor* PropActor = Record->ActiveActor)
			{
				PropActor->RemainingPortions = Delta.RemainingPortions;
				PropActor->OnRep_RemainingPortions();
			}
			bNeedUpdateComp = true;
		}

		// 3. Only apply State if flagged dirty
		if (Delta.HasFlag(EPropRuntimeDirtyMask::State))
		{
			Record->State = Delta.State;
			if (AActivePropActor* PropActor = Record->ActiveActor)
			{
				PropActor->SetPropState(Delta.State);
			}
		}

		Record->PackedRuntimeData.LastUpdatedMinute = Delta.LastUpdatedMinute;

		// If this is an ISM instance, write updated packed runtime data into the ISM component
		if (bNeedUpdateComp && Record->Representation == EPropRepresentation::ISM)
		{
			if (UInteractableISMComponent* Comp = Manager->FindIISMComponent(Record->IISMComponentID))
			{
				Comp->SetInstanceData(Delta.ISID, Record->PackedRuntimeData);
			}
		}
	}
}
