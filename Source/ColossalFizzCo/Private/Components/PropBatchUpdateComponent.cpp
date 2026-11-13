#include "Components/PropBatchUpdateComponent.h"
#include "Props/InteractablePropManager.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Props/InteractableProp.h"
#include "Props/ColaPropActor.h"

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
	const int32 CountToProcess = FMath::Min(FMath::Clamp(BatchSize, 5, 50), TotalISIDs);

	TArray<FPropBatchEvaluationItem> CurrentBatch;
	CurrentBatch.Reserve(CountToProcess);

	const double BatchBaseWorldSeconds = Manager->WorldElapsedSeconds;
	const int32 CurrentMinutes = Manager->GetCurrentWorldMinutes();

	TArray<FPropContinuousSyncItem> ContinuousSyncItems;
	ContinuousSyncItems.Reserve(CountToProcess);

	struct FDeferredBatchFailure
	{
		int32 ISID = 0;
		bool bOverMaxTemp = false;
		bool bUnderMinTemp = false;
		bool bStrengthZero = false;
		int32 PreferredTargetProductID = 0;
	};
	TArray<FDeferredBatchFailure> DeferredFailures;

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

		const FPropProductDefinition* ProductDef = Manager->FindProductDefinition(Record->ProductID);
		FPropInstanceUniqueData* UniqueData = Manager->FindUniqueData(CheckISID);
		const FPropContentDefinition* ContentDef = (UniqueData && UniqueData->EventData.ContentID > 0)
			? Manager->FindContentDefinition(UniqueData->EventData.ContentID)
			: nullptr;

		// =========================================================================
		// Category 2.1 Continuous Update Sweep (Both Resting ISM and Active ASM)
		// =========================================================================
		if (ProductDef && ProductDef->bEnableInstanceUniqueData && ProductDef->bEnableContinuousUpdateData && UniqueData)
		{
			const double DeltaTSeconds = FMath::Max(0.0, BatchBaseWorldSeconds - UniqueData->ContinuousData.LastUpdatedTime);
			const float PrevTemp = UniqueData->ContinuousData.Temperature;
			float NewTemp = PrevTemp;
			bool bThermalStrengthChanged = false;

			AActivePropActor* ActiveASM = (Record->Representation == EPropRepresentation::ASM) ? Record->ActiveActor.Get() : nullptr;
			const bool bASMHandlesOwnThermal = (ActiveASM && ActiveASM->HasActiveThermalSources());

			if (!bASMHandlesOwnThermal)
			{
				const float EnvTemp = Manager->AmbientTemperature;
				if (DeltaTSeconds > 0.0 && FMath::Abs(EnvTemp - PrevTemp) > KINDA_SMALL_NUMBER)
				{
					const float RateMult = (EnvTemp >= PrevTemp) ? ProductDef->HeatingRateMultiplier : ProductDef->CoolingRateMultiplier;
					const float SpecificHeat = (ContentDef && UniqueData->EventData.ContentLevel > KINDA_SMALL_NUMBER)
						? FMath::Max(0.1f, ContentDef->SpecificHeatCapacity)
						: 1.0f;
					const float K = (ProductDef->ThermalConductivity * RateMult) / (FMath::Max(0.01f, ProductDef->ThermalMass) * SpecificHeat);
					if (K > KINDA_SMALL_NUMBER)
					{
						NewTemp = EnvTemp + (PrevTemp - EnvTemp) * FMath::Exp(-K * static_cast<float>(DeltaTSeconds));
					}
				}

				UniqueData->ContinuousData.Temperature = NewTemp;
				UniqueData->ContinuousData.LastUpdatedTime = BatchBaseWorldSeconds;
			}
			else if (DeltaTSeconds > 0.0)
			{
				UniqueData->ContinuousData.LastUpdatedTime = BatchBaseWorldSeconds;
				NewTemp = UniqueData->ContinuousData.Temperature;
			}

			// Thermal damage accumulation outside safe operating bounds
			if (!bASMHandlesOwnThermal && ProductDef->BottleMaterialType != EBottleMaterialType::Diamond && DeltaTSeconds > 0.0 && ProductDef->ThermalDamageRate > 0.0f)
			{
				float ExcessDegrees = 0.0f;
				float ThermalBand = 1.0f;
				if (NewTemp > ProductDef->HighTempDamageThreshold)
				{
					ExcessDegrees = NewTemp - ProductDef->HighTempDamageThreshold;
					ThermalBand = FMath::Max(1.0f, ProductDef->MaxTemperatureThreshold - ProductDef->HighTempDamageThreshold);
				}
				else if (NewTemp < ProductDef->LowTempDamageThreshold)
				{
					ExcessDegrees = ProductDef->LowTempDamageThreshold - NewTemp;
					ThermalBand = FMath::Max(1.0f, ProductDef->LowTempDamageThreshold - ProductDef->MinTemperatureThreshold);
				}

				if (ExcessDegrees > 0.0f && UniqueData->EventData.Strength > 0.0f)
				{
					const float StressRatio = FMath::Clamp(ExcessDegrees / ThermalBand, 0.1f, 2.0f);
					const float DamageAmount = ProductDef->ThermalDamageRate * StressRatio * static_cast<float>(DeltaTSeconds);
					const float OldStrength = UniqueData->EventData.Strength;
					UniqueData->EventData.Strength = FMath::Max(0.0f, OldStrength - DamageAmount);
					if (!FMath::IsNearlyEqual(OldStrength, UniqueData->EventData.Strength, 0.01f))
					{
						bThermalStrengthChanged = true;
					}
				}
			}

			Record->SyncLegacyPackedFromUniqueData(*UniqueData, ProductDef);

			// Refresh representation visuals (GPU Slots 1..3 on ISM, or push to active ASM)
			if (Record->Representation == EPropRepresentation::ISM)
			{
				if (UInteractableISMComponent* Comp = Manager->FindIISMComponent(Record->IISMComponentID))
				{
					if (Comp->PackedInstanceData.IsValidIndex(Record->ISMI))
					{
						Comp->PackedInstanceData[Record->ISMI] = Record->PackedRuntimeData;
					}
					Comp->UpdateInstanceCustomDataDirect(Record->ISMI, ProductDef, ContentDef, UniqueData, BatchBaseWorldSeconds, true);
				}
			}
			else if (ActiveASM && !bASMHandlesOwnThermal)
			{
				ActiveASM->OnApplyContinuousData(UniqueData->ContinuousData, UniqueData->EventData.Strength, bThermalStrengthChanged);
			}

			// Emit compact Channel A network packet if temperature or strength changed noticeably
			if (!bASMHandlesOwnThermal && (FMath::Abs(NewTemp - PrevTemp) >= TemperatureNetSyncEpsilon || bThermalStrengthChanged))
			{
				FPropContinuousSyncItem SyncItem;
				SyncItem.ISID = CheckISID;
				SyncItem.Temperature = NewTemp;
					SyncItem.SealedPressure = UniqueData->ContinuousData.SealedPressure;
				SyncItem.RelativeTimeOffset = static_cast<float>(UniqueData->ContinuousData.LastUpdatedTime - BatchBaseWorldSeconds);
				SyncItem.bHasThermalStrengthUpdate = bThermalStrengthChanged;
				SyncItem.ThermalStrength = UniqueData->EventData.Strength;
				ContinuousSyncItems.Add(SyncItem);
			}

			// Check extreme thermal / structural failure thresholds (deferred until after loop)
			const bool bIndestructible = ProductDef->BottleMaterialType == EBottleMaterialType::Diamond;
			const bool bBrittle = ProductDef->BottleMaterialType == EBottleMaterialType::Glass || ProductDef->BottleMaterialType == EBottleMaterialType::Concrete;
			const bool bOverMaxTemp = !bIndestructible && (NewTemp >= ProductDef->MaxTemperatureThreshold);
			const bool bUnderMinTemp = !bIndestructible && (NewTemp <= ProductDef->MinTemperatureThreshold);
			const bool bStrengthZero = bBrittle && (UniqueData->EventData.Strength <= 0);
			if (bOverMaxTemp || bUnderMinTemp || bStrengthZero)
			{
				FDeferredBatchFailure Fail;
				Fail.ISID = CheckISID;
				Fail.bOverMaxTemp = bOverMaxTemp;
				Fail.bUnderMinTemp = bUnderMinTemp;
				Fail.bStrengthZero = bStrengthZero;
				if (bOverMaxTemp && ProductDef->MeltedProductID > 0)
				{
					Fail.PreferredTargetProductID = ProductDef->MeltedProductID;
				}
				else if ((bUnderMinTemp || bStrengthZero) && ProductDef->BurstBrokenProductID > 0)
				{
					Fail.PreferredTargetProductID = ProductDef->BurstBrokenProductID;
				}
				DeferredFailures.Add(Fail);
			}
		}
		else if (Record->Representation == EPropRepresentation::ISM && ProductDef && ProductDef->ExpiryProfile.bHasExpiry)
		{
			// Even without continuous thermal ticks, keep Slot 1 (ExpiryAge) updated over time on ISM instances
			if (UInteractableISMComponent* Comp = Manager->FindIISMComponent(Record->IISMComponentID))
			{
				Comp->UpdateInstanceCustomDataDirect(Record->ISMI, ProductDef, ContentDef, UniqueData, BatchBaseWorldSeconds, true);
			}
		}

		// =========================================================================
		// Build the evaluation item (populated from UniqueData or packed fallback)
		// =========================================================================
		FPropBatchEvaluationItem Item;
		Item.ISID = CheckISID;
		Item.ProductID = Record->ProductID;
		Item.Representation = Record->Representation;
		Item.State = Record->State;
		Item.IISMComponentID = Record->IISMComponentID;
		Item.ActiveActor = Record->ActiveActor;

		if (UniqueData)
		{
			const int32 MaxPortions = (ProductDef && ProductDef->ConsumableProfile.NumOfPortions > 0)
				? ProductDef->ConsumableProfile.NumOfPortions
				: 4;
			Item.CurrentTemperature = UniqueData->ContinuousData.Temperature;
			Item.RemainingPortions = FMath::Max(0, FMath::RoundToInt(UniqueData->EventData.ContentLevel * static_cast<float>(MaxPortions)));
			Item.AgeMinutes = FMath::Max(0.0f, static_cast<float>((BatchBaseWorldSeconds - UniqueData->EventData.ContentCreationTime) / 60.0));
			Item.bIsGrabbable = UniqueData->EventData.bIsGrabbable;
		}
		else
		{
			Item.CurrentTemperature = Record->PackedRuntimeData.Temperature;
			Item.RemainingPortions = Record->PackedRuntimeData.RemainingPortions;
			Item.AgeMinutes = FMath::Max(0.0f, static_cast<float>(CurrentMinutes - Record->PackedRuntimeData.CreationMinute));
			Item.bIsGrabbable = Record->PackedRuntimeData.bIsGrabbable;
		}

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
		else if (Record->Representation == EPropRepresentation::CompositeFragment)
		{
			Item.WorldTransform = Manager->GetTransform(CheckISID);
			Item.WorldLocation = Item.WorldTransform.GetLocation();
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

	// Flush Channel A unreliable continuous updates and any listener-triggered deltas
	if (ContinuousSyncItems.Num() > 0)
	{
		Manager->Multicast_SyncContinuousBatch(BatchBaseWorldSeconds, ContinuousSyncItems);
	}
	FlushFieldDeltas();

	// Safely execute deferred failure transitions outside loop/broadcast
	for (const FDeferredBatchFailure& Fail : DeferredFailures)
	{
		FISIDRuntimeIndex* FailRecord = Manager->FindISIDData(Fail.ISID);
		if (!FailRecord)
		{
			continue;
		}
		const FPropProductDefinition* FailProductDef = Manager->FindProductDefinition(FailRecord->ProductID);
		if (!FailProductDef)
		{
			continue;
		}

		if (FailRecord->Representation == EPropRepresentation::ISM)
		{
			if (FailProductDef->bPromoteISMOnContinuousTempFailure)
			{
				AActivePropActor* PromotedActor = nullptr;
				if (Manager->PromoteInstanceToASM(Fail.ISID, EPropActivationReason::Damage, true, PromotedActor) && PromotedActor)
				{
					if (AColaPropActor* BottleActor = Cast<AColaPropActor>(PromotedActor))
					{
						FContainmentFailureRequest Req;
						Req.FailureType = Fail.bStrengthZero
							? EContainmentFailureType::BottleBreakage
							: EContainmentFailureType::ThermalFailure;
						Req.FailureLocation = BottleActor->GetActorLocation();
						Req.FailureDirection = FVector::UpVector;
						Req.Severity = 1.0f;
						BottleActor->SubmitFailureRequest(Req);
					}
					else if (Fail.PreferredTargetProductID > 0)
					{
						Manager->ChangePropProductWithMode(Fail.ISID, Fail.PreferredTargetProductID, EPropProductResetMode::ContentReset);
					}
				}
			}
			else if (Fail.PreferredTargetProductID > 0)
			{
				Manager->ChangePropProductWithMode(Fail.ISID, Fail.PreferredTargetProductID, EPropProductResetMode::ContentReset);
			}
		}
		else if (AColaPropActor* BottleActor = Cast<AColaPropActor>(FailRecord->ActiveActor.Get()))
		{
			FContainmentFailureRequest Req;
			Req.FailureType = Fail.bStrengthZero
				? EContainmentFailureType::BottleBreakage
				: EContainmentFailureType::ThermalFailure;
			Req.FailureLocation = BottleActor->GetActorLocation();
			Req.FailureDirection = FVector::UpVector;
			Req.Severity = 1.0f;
			BottleActor->SubmitFailureRequest(Req);
		}
	}
}

bool UPropBatchUpdateComponent::SetInstanceTemperature(int32 InISID, float NewTemperature)
{
	if (InISID <= 0) return false;

	APropManager* Manager = GetPropManager();
	if (!Manager) return false;

	FISIDRuntimeIndex* Record = Manager->FindISIDData(InISID);
	if (!Record) return false;

	FPropInstanceUniqueData* Unique = Manager->FindUniqueData(InISID);
	const float CurrentTemp = Unique ? Unique->ContinuousData.Temperature : Record->PackedRuntimeData.Temperature;
	if (FMath::IsNearlyEqual(CurrentTemp, NewTemperature, TemperatureNetSyncEpsilon))
	{
		return false;
	}

	const int32 CurrentMinutes = Manager->GetCurrentWorldMinutes();
	Record->PackedRuntimeData.Temperature = NewTemperature;
	Record->RuntimeData.Temperature = NewTemperature;
	Record->PackedRuntimeData.LastUpdatedMinute = CurrentMinutes;

	if (Unique)
	{
		Unique->ContinuousData.Temperature = NewTemperature;
		Unique->ContinuousData.LastUpdatedTime = Manager->WorldElapsedSeconds;
	}

	if (Record->Representation == EPropRepresentation::ISM)
	{
		if (UInteractableISMComponent* Comp = Manager->FindIISMComponent(Record->IISMComponentID))
		{
			Comp->SetInstanceData(InISID, Record->PackedRuntimeData);
		}
	}
	else if (AActivePropActor* PropActor = Record->ActiveActor)
	{
		if (Unique)
		{
			PropActor->OnApplyContinuousData(Unique->ContinuousData, Unique->EventData.Strength, false);
		}
		else
		{
			FPropContinuousUpdateData TempContinuous;
			TempContinuous.Temperature = NewTemperature;
			TempContinuous.LastUpdatedTime = Manager->WorldElapsedSeconds;
			PropActor->OnApplyContinuousData(TempContinuous, 100.0f, false);
		}
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

	const FPropProductDefinition* ProductDef = Manager->FindProductDefinition(Record->ProductID);
	const int32 MaxPortions = (ProductDef && ProductDef->ConsumableProfile.NumOfPortions > 0)
		? ProductDef->ConsumableProfile.NumOfPortions
		: 4;
	const int32 ClampedPortions = FMath::Clamp(NewPortions, 0, 255);

	FPropInstanceUniqueData* Unique = Manager->FindUniqueData(InISID);
	const int32 ExistingPortions = Unique
		? FMath::RoundToInt(Unique->EventData.ContentLevel * static_cast<float>(MaxPortions))
		: Record->PackedRuntimeData.RemainingPortions;

	if (ExistingPortions == ClampedPortions)
	{
		return false;
	}

	const int32 CurrentMinutes = Manager->GetCurrentWorldMinutes();
	Record->PackedRuntimeData.RemainingPortions = ClampedPortions;
	Record->RuntimeData.RemainingPortions = ClampedPortions;
	Record->PackedRuntimeData.LastUpdatedMinute = CurrentMinutes;

	if (Unique)
	{
		FPropEventSyncDelta EventDelta;
		EventDelta.ISID = InISID;
		EventDelta.SetFlag(EPropEventDirtyMask::ContentLevel);
		EventDelta.ContentLevel = static_cast<float>(ClampedPortions) / static_cast<float>(MaxPortions);
		Manager->SubmitInstanceEventUpdate(InISID, EventDelta);
		return true;
	}

	if (Record->Representation == EPropRepresentation::ISM)
	{
		if (UInteractableISMComponent* Comp = Manager->FindIISMComponent(Record->IISMComponentID))
		{
			Comp->SetInstanceData(InISID, Record->PackedRuntimeData);
		}
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

		const FPropProductDefinition* ProductDef = Manager->FindProductDefinition(Record->ProductID);
		FPropInstanceUniqueData* Unique = Manager->FindUniqueData(Delta.ISID);
		bool bNeedUpdateComp = false;

		// 1. Only apply Temperature if flagged dirty
		if (Delta.HasFlag(EPropRuntimeDirtyMask::Temperature))
		{
			Record->PackedRuntimeData.Temperature = Delta.Temperature;
			Record->RuntimeData.Temperature = Delta.Temperature;
			if (Unique)
			{
				Unique->ContinuousData.Temperature = Delta.Temperature;
				Unique->ContinuousData.LastUpdatedTime = Manager->WorldElapsedSeconds;
			}
			if (AActivePropActor* PropActor = Record->ActiveActor)
			{
				FPropContinuousUpdateData ContData;
				ContData.Temperature = Delta.Temperature;
				ContData.LastUpdatedTime = Manager->WorldElapsedSeconds;
				PropActor->OnApplyContinuousData(ContData, Unique ? Unique->EventData.Strength : 100.0f, false);
			}
			bNeedUpdateComp = true;
		}

		// 2. Only apply Portions if flagged dirty
		if (Delta.HasFlag(EPropRuntimeDirtyMask::Portions))
		{
			Record->PackedRuntimeData.RemainingPortions = Delta.RemainingPortions;
			Record->RuntimeData.RemainingPortions = Delta.RemainingPortions;
			if (Unique)
			{
				const int32 MaxPortions = (ProductDef && ProductDef->ConsumableProfile.NumOfPortions > 0)
					? ProductDef->ConsumableProfile.NumOfPortions
					: 4;
				Unique->EventData.ContentLevel = static_cast<float>(Delta.RemainingPortions) / static_cast<float>(MaxPortions);
				if (AActivePropActor* PropActor = Record->ActiveActor)
				{
					PropActor->OnApplyEventData(Unique->EventData, EPropEventDirtyMask::ContentLevel);
				}
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
