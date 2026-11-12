#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Components/ASMComponent.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"

const FPropContentDefinition* APropManager::FindContentDefinition(int32 ContentID) const
{
	return ContentDefinitions.Find(ContentID);
}

bool APropManager::RegisterContentDefinition(const FPropContentDefinition& Definition)
{
	if (!HasAuthority() || !Definition.IsValidDefinition() || !FMath::IsFinite(Definition.BasePortionPrice) || Definition.BasePortionPrice < 0
		|| !FMath::IsFinite(Definition.DensityGPerMl) || Definition.DensityGPerMl<=0) return false;
	ContentDefinitions.Add(Definition.ContentID, Definition);
	Multicast_RegisterContentDefinition(Definition);
	return true;
}

void APropManager::Multicast_RegisterContentDefinition_Implementation(const FPropContentDefinition& Definition)
{
	if (!HasAuthority()) ContentDefinitions.Add(Definition.ContentID, Definition);
}

void APropManager::Multicast_RegisterProductDefinition_Implementation(const FPropProductDefinition& Definition)
{
	if (!HasAuthority()) RegisterProductDefinition(Definition);
}

const FPropInstanceUniqueData* APropManager::FindUniqueData(int32 InISID) const
{
	return UniqueDataByISID.Find(InISID);
}

FPropInstanceUniqueData* APropManager::FindUniqueData(int32 InISID)
{
	if (FPropInstanceUniqueData* Existing = UniqueDataByISID.Find(InISID)) return Existing;
	const FISIDRuntimeIndex* Record = FindISIDData(InISID);
	const FPropProductDefinition* Def = Record ? FindProductDefinition(Record->ProductID) : nullptr;
	if (!Def || !Def->bEnableInstanceUniqueData) return nullptr;
	FPropInstanceUniqueData Unique = Def->DefaultUniqueData;
	Unique.EventData.ContentLevel = Def->ConsumableProfile.NumOfPortions > 0 ? static_cast<float>(Record->PackedRuntimeData.RemainingPortions) / Def->ConsumableProfile.NumOfPortions : 0.0f;
	Unique.EventData.CreationTime = FMath::Max(0, Record->PackedRuntimeData.CreationMinute) * 60.0;
	Unique.EventData.ContentCreationTime = Unique.EventData.CreationTime;
	Unique.ContinuousData.LastUpdatedTime = WorldElapsedSeconds;
	Unique.ContinuousData.Temperature = Record->PackedRuntimeData.Temperature;
	return &UniqueDataByISID.Add(InISID, Unique);
}

void APropManager::RefreshUniqueRepresentation(int32 InISID, uint8 DirtyMask)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	FPropInstanceUniqueData* Unique = UniqueDataByISID.Find(InISID);
	if (!Record || !Unique) return;
	const FPropProductDefinition* Def = FindProductDefinition(Record->ProductID);
	Record->SyncLegacyPackedFromUniqueData(*Unique, Def);
	if (AActivePropActor* Actor = Record->ActiveActor.Get())
	{
		Actor->LastDiscreteEventTime = Record->LastDiscreteEventTime;
		Actor->OnApplyEventData(Unique->EventData, DirtyMask);
		Actor->OnApplyContinuousData(Unique->ContinuousData, Unique->EventData.Strength, true);
	}
	if (UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
	{
		if (Comp->PackedInstanceData.IsValidIndex(Record->ISMI)) Comp->PackedInstanceData[Record->ISMI] = Record->PackedRuntimeData;
		Comp->UpdateInstanceCustomDataDirect(Record->ISMI, Def, FindContentDefinition(Unique->EventData.ContentID), Unique, WorldElapsedSeconds, true);
	}
}

namespace
{
	bool ValidEvent(const FPropEventSyncDelta& D)
	{
		return (!D.HasFlag(EPropEventDirtyMask::ContentLevel) || (FMath::IsFinite(D.ContentLevel) && D.ContentLevel >= 0))
			&& (!D.HasFlag(EPropEventDirtyMask::Strength) || (FMath::IsFinite(D.Strength) && D.Strength >= 0 && D.Strength <= 1))
			&& (!D.HasFlag(EPropEventDirtyMask::CapSealed) || (FMath::IsFinite(D.CapSealed) && D.CapSealed >= 0 && D.CapSealed <= 1))
			&& (!D.HasFlag(EPropEventDirtyMask::CreationTime) || (FMath::IsFinite(D.CreationTime) && D.CreationTime >= 0))
			&& (!(D.ExtendedDirtyMask & EPropExtendedEventMask::ContentCreationTime) || (FMath::IsFinite(D.ContentCreationTime) && D.ContentCreationTime >= 0))
			&& (!(D.ExtendedDirtyMask & EPropExtendedEventMask::AttachedCap) || D.AttachedCapISID >= 0)
			&& (!(D.ExtendedDirtyMask & EPropExtendedEventMask::AttachedBottle) || D.AttachedBottleISID >= 0);
	}
	void ApplyEvent(FPropEventUpdateData& E, const FPropEventSyncDelta& D)
	{
		if (D.HasFlag(EPropEventDirtyMask::ContentLevel)) E.ContentLevel = D.ContentLevel;
		if (D.HasFlag(EPropEventDirtyMask::ContentID)) E.ContentID = D.ContentID;
		if (D.HasFlag(EPropEventDirtyMask::Strength)) E.Strength = D.Strength;
		if (D.HasFlag(EPropEventDirtyMask::CapSealed)) E.CapSealed = D.CapSealed;
		if (D.HasFlag(EPropEventDirtyMask::CreationTime)) E.CreationTime = D.CreationTime;
		if (D.HasFlag(EPropEventDirtyMask::CapType)) E.CapType = D.CapType;
		if (D.HasFlag(EPropEventDirtyMask::Grabbable)) E.bIsGrabbable = D.bIsGrabbable;
		if (D.ExtendedDirtyMask & EPropExtendedEventMask::ContentCreationTime) E.ContentCreationTime = D.ContentCreationTime;
		if (D.ExtendedDirtyMask & EPropExtendedEventMask::Clean) E.bClean = D.bClean;
		if (D.ExtendedDirtyMask & EPropExtendedEventMask::AttachedCap) E.AttachedCapISID = D.AttachedCapISID;
		if (D.ExtendedDirtyMask & EPropExtendedEventMask::AttachedBottle) E.AttachedBottleISID = D.AttachedBottleISID;
		E.EventSequence = D.EventSequence;
	}
}

bool APropManager::SubmitInstanceEventUpdate(int32 InISID, const FPropEventSyncDelta& Delta)
{
	if (!HasAuthority() || !ValidEvent(Delta)) return false;
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	FPropInstanceUniqueData* Unique = FindUniqueData(InISID);
	if (!Record || !Unique) return false;
	if (Delta.HasFlag(EPropEventDirtyMask::ContentID) && Delta.ContentID != 0 && !FindContentDefinition(Delta.ContentID)) return false;
	FPropEventSyncDelta Applied = Delta;
	Applied.ISID = InISID;
	Applied.EventSequence = Unique->EventData.EventSequence + 1;
	if (Applied.HasFlag(EPropEventDirtyMask::ProductID) && !ChangePropProductWithMode(InISID, Applied.NewProductID, EPropProductResetMode::PreserveUniqueState)) return false;
	Unique = FindUniqueData(InISID);
	if (!Unique) return false;
	ApplyEvent(Unique->EventData, Applied);
	Record = FindISIDData(InISID);
	Record->LastDiscreteEventTime = WorldElapsedSeconds;
	Record->Version++;
	RefreshUniqueRepresentation(InISID, Applied.DirtyMask);
	MarkInstanceDirty(InISID, EPropDirtyFlags::RuntimeData);
	Multicast_SyncEventDeltaBatch(WorldElapsedSeconds, {Applied});
	return true;
}

void APropManager::Multicast_SyncEventDeltaBatch_Implementation(double EventTime, const TArray<FPropEventSyncDelta>& Deltas)
{
	if (HasAuthority()) return;
	for (const FPropEventSyncDelta& D : Deltas)
	{
		FISIDRuntimeIndex* Record = FindISIDData(D.ISID);
		FPropInstanceUniqueData* Unique = FindUniqueData(D.ISID);
		if (!Record || !Unique || !ValidEvent(D) || static_cast<int32>(D.EventSequence - Unique->EventData.EventSequence) <= 0) continue;
		if (D.HasFlag(EPropEventDirtyMask::ProductID)) ChangePropProduct(D.ISID, D.NewProductID, false);
		ApplyEvent(Unique->EventData, D);
		Record->LastDiscreteEventTime = FMath::Max(Record->LastDiscreteEventTime, EventTime);
		RefreshUniqueRepresentation(D.ISID, D.DirtyMask);
	}
}

void APropManager::Multicast_SyncContinuousBatch_Implementation(double BaseWorldSeconds, const TArray<FPropContinuousSyncItem>& Items)
{
	if (HasAuthority()) return;
	for (const FPropContinuousSyncItem& Item : Items)
	{
		FISIDRuntimeIndex* Record = FindISIDData(Item.ISID);
		FPropInstanceUniqueData* Unique = FindUniqueData(Item.ISID);
		const double Time = BaseWorldSeconds + Item.RelativeTimeOffset;
		if (!Record || !Unique || !FMath::IsFinite(Time) || !FMath::IsFinite(Item.Temperature) || !FMath::IsFinite(Item.SealedPressure) || Item.SealedPressure < 0 || Time < Record->LastDiscreteEventTime || Time < Unique->ContinuousData.LastUpdatedTime) continue;
		Unique->ContinuousData.Temperature = Item.Temperature;
		Unique->ContinuousData.SealedPressure = Item.SealedPressure;
		Unique->ContinuousData.LastUpdatedTime = Time;
		if (Item.bHasThermalStrengthUpdate && FMath::IsFinite(Item.ThermalStrength)) Unique->EventData.Strength = FMath::Clamp(Item.ThermalStrength, 0.0f, 1.0f);
		RefreshUniqueRepresentation(Item.ISID, 0);
	}
}

bool APropManager::ChangePropProductWithMode(int32 InISID, int32 NewProductID, EPropProductResetMode ResetMode)
{
	if (!HasAuthority()) return false;
	const FPropProductDefinition* NewDef = FindProductDefinition(NewProductID);
	FPropInstanceUniqueData* Existing = FindUniqueData(InISID);
	if (!NewDef || !FindISIDData(InISID)) return false;
	FPropInstanceUniqueData Preserved = Existing ? *Existing : NewDef->DefaultUniqueData;
	const uint32 NextEventSequence = Preserved.EventData.EventSequence + 1;
	AActivePropActor* Reserved = nullptr;
	FISIDRuntimeIndex* Before = FindISIDData(InISID);
	if (Before->Representation == EPropRepresentation::ASM && ASMComponent)
	{
		const TSubclassOf<AActivePropActor> Desired = ASMComponent->GetEffectiveActivePropActorClass(NewProductID);
		AActivePropActor* Live = Before->ActiveActor.Get();
		if (!Live || Live->GetClass() != (Desired ? Desired.Get() : AActivePropActor::StaticClass()))
		{
			Reserved = ASMComponent->AcquireActiveActor(NewProductID);
			if (!Reserved) return false;
		}
	}
	if (!ChangePropProduct(InISID, NewProductID, false)) { if (Reserved) ASMComponent->ReturnActiveActorToPool(Reserved); return false; }
	if (ResetMode == EPropProductResetMode::FullReset)
	{
		Preserved = NewDef->DefaultUniqueData;
		Preserved.EventData.CreationTime = WorldElapsedSeconds;
		Preserved.EventData.ContentCreationTime = WorldElapsedSeconds;
		Preserved.ContinuousData.LastUpdatedTime = WorldElapsedSeconds;
	}
	else if (ResetMode == EPropProductResetMode::ContentReset)
	{
		Preserved.EventData.ContentLevel = 0;
		Preserved.EventData.ContentID = 0;
		Preserved.EventData.CapType = 0;
		Preserved.EventData.CapSealed = 0;
	}
	Preserved.EventData.EventSequence = NextEventSequence;
	if (NewDef->bEnableInstanceUniqueData) UniqueDataByISID.Add(InISID, Preserved);
	else UniqueDataByISID.Remove(InISID);
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (Record->Representation == EPropRepresentation::ASM && ASMComponent && !ASMComponent->SwapActiveActorClass(*Record, NewDef, Reserved)) return false;
	Record->LastDiscreteEventTime = WorldElapsedSeconds;
	RefreshUniqueRepresentation(InISID);
	MarkInstanceDirty(InISID, EPropDirtyFlags::RuntimeData | EPropDirtyFlags::Representation);
	return true;
}

bool APropManager::SubmitAtomicInstanceEventUpdates(const TArray<FPropEventSyncDelta>& Deltas)
{
	if (!HasAuthority() || Deltas.IsEmpty() || Deltas.Num() > 64) return false;
	TSet<int32> IDs;
	for (const FPropEventSyncDelta& D : Deltas)
	{
		if (IDs.Contains(D.ISID) || !ValidEvent(D) || D.HasFlag(EPropEventDirtyMask::ProductID) || !FindISIDData(D.ISID) || !FindUniqueData(D.ISID)) return false;
		if (D.HasFlag(EPropEventDirtyMask::ContentID) && D.ContentID && !FindContentDefinition(D.ContentID)) return false;
		IDs.Add(D.ISID);
	}
	TArray<FPropEventSyncDelta> Applied = Deltas;
	for (FPropEventSyncDelta& D : Applied)
	{
		FPropInstanceUniqueData* Unique = FindUniqueData(D.ISID);
		D.EventSequence = Unique->EventData.EventSequence + 1;
		ApplyEvent(Unique->EventData, D);
		FISIDRuntimeIndex* Record = FindISIDData(D.ISID);
		Record->LastDiscreteEventTime = WorldElapsedSeconds; ++Record->Version;
		MarkInstanceDirty(D.ISID, EPropDirtyFlags::RuntimeData);
	}
	// Commit every canonical record before updating any visual representation.
	for (const FPropEventSyncDelta& D : Applied) RefreshUniqueRepresentation(D.ISID, D.DirtyMask);
	Multicast_SyncEventDeltaBatch(WorldElapsedSeconds, Applied);
	return true;
}
