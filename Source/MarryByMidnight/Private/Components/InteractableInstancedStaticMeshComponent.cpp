#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractablePropManager.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Characters/NPCCharacter.h"
#include "Components/NPCPersonalityComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"
#include "Engine/CollisionProfile.h"
#include "Materials/MaterialInterface.h"

UInteractableISMComponent::UInteractableISMComponent()
{
	SetIsReplicatedByDefault(true);
	SetNotifyRigidBodyCollision(true);
	BodyInstance.bNotifyRigidBodyCollision = true;
	SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	SetGenerateOverlapEvents(false);
	ISMComponent = this;

	// Default ResetTransform places parked ISM instances far below the gameplay world with near-zero scale
	ResetTransform = FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, -100000.0f), FVector(0.0001f));
}

void UInteractableISMComponent::OnRegister()
{
	Super::OnRegister();

	ISMComponent = this;
	SetGenerateOverlapEvents(false);
	SetNotifyRigidBodyCollision(true);
	BodyInstance.bNotifyRigidBodyCollision = true;
	if (GetCollisionEnabled() == ECollisionEnabled::NoCollision)
	{
		SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	}
	OnComponentHit.AddUniqueDynamic(this, &UInteractableISMComponent::HandleComponentHit);
}

void UInteractableISMComponent::BeginPlay()
{
	Super::BeginPlay();

	ISMComponent = this;
	SetGenerateOverlapEvents(false);
	SetNotifyRigidBodyCollision(true);
	BodyInstance.bNotifyRigidBodyCollision = true;
	OnComponentHit.AddUniqueDynamic(this, &UInteractableISMComponent::HandleComponentHit);

	// Section 51: Only clear stale editor/pre-session mappings if this component was not already populated at runtime during BeginPlay
	if (ISIDToISMI.Num() == 0 && ISMIToISID.Num() == 0 && IISMComponentID <= 0)
	{
		EmptyInstances.Empty();
		ISIDToISMI.Empty();
		ISMIToISID.Empty();
		CachedGameplayTransformsByISID.Empty();
	}

	if (DefaultDataAsset && ProductID <= 0)
	{
		ProductID = GetTypeHash(DefaultDataAsset->GetFName()) & 0x7FFFFFFF;
		if (ProductID == 0) ProductID = 1;
		IISMDB = FPropProductDefinition::FromDataAsset(DefaultDataAsset, ProductID, StaticMeshID);
	}
	else if (GetStaticMesh() && ProductID <= 0)
	{
		ProductID = GetTypeHash(GetStaticMesh()->GetFName()) & 0x7FFFFFFF;
		if (ProductID == 0) ProductID = 1;
		IISMDB.ProductID = ProductID;
		IISMDB.StaticMeshID = (StaticMeshID > 0) ? StaticMeshID : ProductID;
		IISMDB.StaticMesh = GetStaticMesh();
		IISMDB.DisplayName = FText::FromName(GetStaticMesh()->GetFName());
	}

	if (IISMDB.ProductID > 0)
	{
		ApplyProductDefinition(IISMDB);
	}

	SyncDatabaseWithInstanceCount();
	if (GetInstanceCount() > 0)
	{
		RebuildLocalIndexMap();
	}

	if (ComponentRuntimeSettings.bAutoRegisterWithPropManager)
	{
		if (APropManager* Manager = APropManager::Get(this))
		{
			Manager->RegisterISMComponent(this);
		}
	}
}

void UInteractableISMComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnComponentHit.RemoveDynamic(this, &UInteractableISMComponent::HandleComponentHit);

	// Remove Empty ISM instances when session ends so no stale empty slots remain
	ClearEmptyInstances(true);
	ISIDToISMI.Empty();
	ISMIToISID.Empty();
	CachedGameplayTransformsByISID.Empty();

	if (APropManager* Manager = APropManager::Get(this))
	{
		Manager->UnregisterISMComponent(this);
	}

	Super::EndPlay(EndPlayReason);
}

void UInteractableISMComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UInteractableISMComponent, ProductID);
	DOREPLIFETIME(UInteractableISMComponent, StaticMeshID);
	DOREPLIFETIME(UInteractableISMComponent, IISMComponentID);
	DOREPLIFETIME(UInteractableISMComponent, IISMDB);
	DOREPLIFETIME(UInteractableISMComponent, PackedInstanceData);
	DOREPLIFETIME(UInteractableISMComponent, PropDatabase);
	DOREPLIFETIME(UInteractableISMComponent, bDebugPrintInstanceHit);
}

void UInteractableISMComponent::OnRep_IISMDB()
{
	ApplyProductDefinition(IISMDB);
	if (IISMDB.ProductID > 0)
	{
		if (APropManager* Manager = APropManager::Get(this))
		{
			if (!Manager->ProductDefinitions.Contains(IISMDB.ProductID))
			{
				Manager->ProductDefinitions.Add(IISMDB.ProductID, IISMDB);
			}
			if (IISMComponentID > 0)
			{
				Manager->RegisterIISMComponent(this, IISMComponentID);
			}
		}
	}
}

void UInteractableISMComponent::ApplyProductDefinition(const FPropProductDefinition& InDefinition)
{
	const bool bSameProductAndMesh = (ProductID == InDefinition.ProductID &&
		InDefinition.ProductID > 0 &&
		InDefinition.StaticMesh != nullptr &&
		GetStaticMesh() == InDefinition.StaticMesh);

	if (InDefinition.ProductID > 0)
	{
		ProductID = InDefinition.ProductID;
	}
	if (InDefinition.StaticMeshID > 0)
	{
		StaticMeshID = InDefinition.StaticMeshID;
	}
	IISMDB = InDefinition;
	if (InDefinition.SourceDataAsset)
	{
		DefaultDataAsset = InDefinition.SourceDataAsset;
	}

	if (!bSameProductAndMesh)
	{
		if (InDefinition.StaticMesh && GetStaticMesh() != InDefinition.StaticMesh)
		{
			SetStaticMesh(InDefinition.StaticMesh);
			MarkRenderStateDirty();
		}
		for (int32 MatIdx = 0; MatIdx < InDefinition.MaterialOverrides.Num(); ++MatIdx)
		{
			if (InDefinition.MaterialOverrides[MatIdx] && GetMaterial(MatIdx) != InDefinition.MaterialOverrides[MatIdx])
			{
				SetMaterial(MatIdx, InDefinition.MaterialOverrides[MatIdx]);
			}
		}
		const int32 NumMats = GetNumMaterials();
		for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
		{
			if (UMaterialInterface* Mat = GetMaterial(MatIdx))
			{
				Mat->CheckMaterialUsage(MATUSAGE_InstancedStaticMeshes);
			}
		}
	}

	if (GetCollisionEnabled() == ECollisionEnabled::NoCollision)
	{
		SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	}
}

void UInteractableISMComponent::OnRep_PropDatabase()
{
	RebuildLocalIndexMap();
	if (APropManager* Manager = APropManager::Get(this))
	{
		if (IISMComponentID > 0)
		{
			Manager->RegisterIISMComponent(this, IISMComponentID);
		}
	}
}

// -----------------------------------------------------------------------------
// Section 31 & 65: UInteractableISMComponent Core & Empty Instance API
// -----------------------------------------------------------------------------
int32 UInteractableISMComponent::AddInstanceWithISID(int32 InISID, const FTransform& InstanceTransform, const FPropPackedRuntimeData& InitialData, bool bWorldSpace)
{
	SetNotifyRigidBodyCollision(true);
	BodyInstance.bNotifyRigidBodyCollision = true;
	const int32 NewISMI = AddInstance(InstanceTransform, bWorldSpace);
	if (NewISMI == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	EmptyInstances.Remove(NewISMI);

	if (PackedInstanceData.Num() <= NewISMI)
	{
		PackedInstanceData.SetNum(NewISMI + 1);
	}
	PackedInstanceData[NewISMI] = InitialData;

	FInteractablePropData LegacyEntry = DefaultDataAsset ? CreatePropDataFromAsset(DefaultDataAsset, NewISMI) : FInteractablePropData();
	LegacyEntry.InstanceId = NewISMI;
	LegacyEntry.ISID = InISID;
	LegacyEntry.ProductID = ProductID;
	LegacyEntry.IISMComponentID = IISMComponentID;
	LegacyEntry.RemainingPortions = InitialData.RemainingPortions;
	LegacyEntry.CurrentTemperature = InitialData.Temperature;
	LegacyEntry.bIsGrabbable = InitialData.bIsGrabbable;
	LegacyEntry.SimulationState = EPhysicsSimulationState::AtRest;
	if (LegacyEntry.NumOfPortions > 0)
	{
		LegacyEntry.QuantityLevel = static_cast<float>(LegacyEntry.RemainingPortions) / static_cast<float>(LegacyEntry.NumOfPortions);
	}

	if (PropDatabase.Num() <= NewISMI)
	{
		PropDatabase.SetNum(NewISMI + 1);
	}
	PropDatabase[NewISMI] = LegacyEntry;

	BindISIDToISMI(InISID, NewISMI);
	const FTransform EffectiveReset = GetEffectiveResetTransform();
	if (!InstanceTransform.GetLocation().Equals(EffectiveReset.GetLocation(), 100.0f) && InstanceTransform.GetScale3D().GetMax() >= 0.005f)
	{
		CachedGameplayTransformsByISID.Add(InISID, InstanceTransform);
	}

	return NewISMI;
}

TArray<int32> UInteractableISMComponent::AddInstancesWithISIDs(const TArray<int32>& InISIDs, const TArray<FTransform>& InstanceTransforms, const FPropPackedRuntimeData& InitialData, bool bWorldSpace)
{
	TArray<int32> OutISMIs;
	const int32 Count = FMath::Min(InISIDs.Num(), InstanceTransforms.Num());
	OutISMIs.Reserve(Count);

	for (int32 i = 0; i < Count; ++i)
	{
		const int32 AssignedISMI = ReuseOrAddInstanceWithISID(InISIDs[i], InstanceTransforms[i], InitialData, INDEX_NONE, bWorldSpace);
		if (AssignedISMI != INDEX_NONE)
		{
			OutISMIs.Add(AssignedISMI);
		}
	}
	return OutISMIs;
}

int32 UInteractableISMComponent::ReuseOrAddInstanceWithISID(int32 InISID, const FTransform& InstanceTransform, const FPropPackedRuntimeData& InitialData, int32 PreferredISMI, bool bWorldSpace)
{
	int32 ReusedISMI = INDEX_NONE;

	// 1. Check if PreferredISMI is valid and either Empty or unoccupied
	if (PreferredISMI != INDEX_NONE && IsValidInstance(PreferredISMI))
	{
		const int32 ExistingISID = FindISID(PreferredISMI);
		if (ExistingISID <= 0 || ExistingISID == InISID || IsEmptyInstance(PreferredISMI))
		{
			RemoveEmptyInstance(PreferredISMI);
			ReusedISMI = PreferredISMI;
		}
	}

	// 2. Otherwise check EmptyInstances for an available physical ISM slot (Section 42 & 43)
	if (ReusedISMI == INDEX_NONE && EmptyInstances.Num() > 0)
	{
		ReusedISMI = ConsumeEmptyInstance();
	}

	// 3. If an Empty ISMI slot was found, reuse it without creating a new ISM instance
	if (ReusedISMI != INDEX_NONE && IsValidInstance(ReusedISMI))
	{
		// Preserve existing gameplay transform on the slot before moving if InstanceTransform is ResetTransform
		const FTransform EffectiveReset = GetEffectiveResetTransform();
		FTransform ExistingSlotTransform;
		if (GetInstanceTransform(ReusedISMI, ExistingSlotTransform, true))
		{
			if (!ExistingSlotTransform.GetLocation().Equals(EffectiveReset.GetLocation(), 100.0f) && ExistingSlotTransform.GetScale3D().GetMax() >= 0.005f)
			{
				CachedGameplayTransformsByISID.Add(InISID, ExistingSlotTransform);
			}
		}

		UpdateInstanceTransform(ReusedISMI, InstanceTransform, bWorldSpace, true, true);

		if (PackedInstanceData.Num() <= ReusedISMI)
		{
			PackedInstanceData.SetNum(GetInstanceCount());
		}
		PackedInstanceData[ReusedISMI] = InitialData;

		FInteractablePropData LegacyEntry = DefaultDataAsset ? CreatePropDataFromAsset(DefaultDataAsset, ReusedISMI) : FInteractablePropData();
		LegacyEntry.InstanceId = ReusedISMI;
		LegacyEntry.ISID = InISID;
		LegacyEntry.ProductID = ProductID;
		LegacyEntry.IISMComponentID = IISMComponentID;
		LegacyEntry.RemainingPortions = InitialData.RemainingPortions;
		LegacyEntry.CurrentTemperature = InitialData.Temperature;
		LegacyEntry.bIsGrabbable = InitialData.bIsGrabbable;
		LegacyEntry.SimulationState = EPhysicsSimulationState::AtRest;
		if (LegacyEntry.NumOfPortions > 0)
		{
			LegacyEntry.QuantityLevel = static_cast<float>(LegacyEntry.RemainingPortions) / static_cast<float>(LegacyEntry.NumOfPortions);
		}

		if (PropDatabase.Num() <= ReusedISMI)
		{
			PropDatabase.SetNum(GetInstanceCount());
		}
		PropDatabase[ReusedISMI] = LegacyEntry;

		BindISIDToISMI(InISID, ReusedISMI);
		if (!InstanceTransform.GetLocation().Equals(EffectiveReset.GetLocation(), 100.0f) && InstanceTransform.GetScale3D().GetMax() >= 0.005f)
		{
			CachedGameplayTransformsByISID.Add(InISID, InstanceTransform);
		}
		return ReusedISMI;
	}

	// 4. Otherwise create a new physical ISM instance
	return AddInstanceWithISID(InISID, InstanceTransform, InitialData, bWorldSpace);
}

bool UInteractableISMComponent::ReleaseInstanceByISIDToEmpty(int32 InISID)
{
	const int32 TargetISMI = FindISMI(InISID);
	if (TargetISMI == INDEX_NONE || !IsValidInstance(TargetISMI))
	{
		return false;
	}

	ISIDToISMI.Remove(InISID);
	ISMIToISID.Remove(TargetISMI);
	CachedGameplayTransformsByISID.Remove(InISID);

	return AddEmptyInstance(TargetISMI);
}

bool UInteractableISMComponent::RemoveInstanceByISID(int32 InISID)
{
	TMap<int32, int32> Shifted;
	return RemoveInstanceByISIDWithRepair(InISID, Shifted);
}

bool UInteractableISMComponent::RemoveInstanceByISIDWithRepair(int32 InISID, TMap<int32, int32>& OutShiftedISIDToISMI)
{
	OutShiftedISIDToISMI.Empty();
	const int32 TargetISMI = FindISMI(InISID);
	if (TargetISMI == INDEX_NONE || !IsValidInstance(TargetISMI))
	{
		return false;
	}

	const int32 CountBefore = GetInstanceCount();

	// Capture existing ordered ISIDs before removal so we can repair shifted slots
	TArray<int32> OrderedISIDs;
	OrderedISIDs.SetNumZeroed(CountBefore);
	for (int32 Idx = 0; Idx < CountBefore; ++Idx)
	{
		OrderedISIDs[Idx] = FindISID(Idx);
	}

	const bool bRemoved = RemoveInstance(TargetISMI);
	if (!bRemoved)
	{
		return false;
	}

	if (PackedInstanceData.IsValidIndex(TargetISMI))
	{
		PackedInstanceData.RemoveAt(TargetISMI);
	}
	if (PropDatabase.IsValidIndex(TargetISMI))
	{
		PropDatabase.RemoveAt(TargetISMI);
	}

	OrderedISIDs.RemoveAt(TargetISMI);
	ISIDToISMI.Remove(InISID);
	CachedGameplayTransformsByISID.Remove(InISID);

	// Repair EmptyInstances indices for shifted slots
	EmptyInstances.Remove(TargetISMI);
	for (int32& EmptySlot : EmptyInstances)
	{
		if (EmptySlot > TargetISMI)
		{
			--EmptySlot;
		}
	}

	// Rebuild local index maps for remaining instances (Section 16 Step 26 & Section 24)
	ISMIToISID.Empty();
	for (int32 NewIdx = 0; NewIdx < OrderedISIDs.Num(); ++NewIdx)
	{
		const int32 RemainingISID = OrderedISIDs[NewIdx];
		if (RemainingISID > 0 && !EmptyInstances.Contains(NewIdx))
		{
			ISMIToISID.Add(NewIdx, RemainingISID);
			ISIDToISMI.Add(RemainingISID, NewIdx);

			if (PropDatabase.IsValidIndex(NewIdx))
			{
				PropDatabase[NewIdx].InstanceId = NewIdx;
				PropDatabase[NewIdx].ISID = RemainingISID;
			}

			if (NewIdx >= TargetISMI)
			{
				OutShiftedISIDToISMI.Add(RemainingISID, NewIdx);
			}
		}
	}

	return true;
}

int32 UInteractableISMComponent::FindISMI(int32 InISID) const
{
	if (InISID <= 0)
	{
		return INDEX_NONE;
	}
	if (const int32* FoundISMI = ISIDToISMI.Find(InISID))
	{
		if (IsValidInstance(*FoundISMI))
		{
			return *FoundISMI;
		}
	}
	for (const TPair<int32, int32>& Pair : ISMIToISID)
	{
		if (Pair.Value == InISID && IsValidInstance(Pair.Key) && !EmptyInstances.Contains(Pair.Key))
		{
			return Pair.Key;
		}
	}
	for (int32 Idx = 0; Idx < PropDatabase.Num(); ++Idx)
	{
		if (PropDatabase[Idx].ISID == InISID && IsValidInstance(Idx) && !EmptyInstances.Contains(Idx))
		{
			return Idx;
		}
	}
	return INDEX_NONE;
}

int32 UInteractableISMComponent::FindISID(int32 InISMI) const
{
	if (EmptyInstances.Contains(InISMI))
	{
		return 0;
	}
	if (const int32* FoundISID = ISMIToISID.Find(InISMI))
	{
		return *FoundISID;
	}
	if (PropDatabase.IsValidIndex(InISMI) && PropDatabase[InISMI].ISID > 0)
	{
		return PropDatabase[InISMI].ISID;
	}
	return 0;
}

void UInteractableISMComponent::BindISIDToISMI(int32 InISID, int32 InISMI)
{
	if (InISID <= 0 || InISMI == INDEX_NONE) return;

	// Section 40 & 44: When an ISMI is bound to a logical ISID, it is no longer an Empty Instance
	EmptyInstances.Remove(InISMI);

	// Remove any previous ISID bound to this ISMI
	if (const int32* PrevISID = ISMIToISID.Find(InISMI))
	{
		if (*PrevISID != InISID)
		{
			ISIDToISMI.Remove(*PrevISID);
		}
	}

	ISIDToISMI.Add(InISID, InISMI);
	ISMIToISID.Add(InISMI, InISID);

	if (PropDatabase.IsValidIndex(InISMI))
	{
		PropDatabase[InISMI].ISID = InISID;
		PropDatabase[InISMI].InstanceId = InISMI;
		PropDatabase[InISMI].ProductID = ProductID;
		PropDatabase[InISMI].IISMComponentID = IISMComponentID;
	}
}

bool UInteractableISMComponent::GetInstanceTransformByISID(int32 InISID, FTransform& OutTransform, bool bWorldSpace) const
{
	const int32 ISMI = FindISMI(InISID);
	if (ISMI == INDEX_NONE || !IsValidInstance(ISMI))
	{
		if (bWorldSpace)
		{
			if (const FTransform* Cached = CachedGameplayTransformsByISID.Find(InISID))
			{
				OutTransform = *Cached;
				return true;
			}
		}
		return false;
	}
	if (bWorldSpace && IsInstanceParkedAtResetTransform(ISMI))
	{
		if (const FTransform* Cached = CachedGameplayTransformsByISID.Find(InISID))
		{
			OutTransform = *Cached;
			return true;
		}
	}
	return GetInstanceTransform(ISMI, OutTransform, bWorldSpace);
}

bool UInteractableISMComponent::SetInstanceTransform(int32 InISID, const FTransform& NewTransform, bool bWorldSpace, bool bMarkRenderStateDirty, bool bTeleport)
{
	if (FindISMI(InISID) != INDEX_NONE)
	{
		return SetInstanceTransformByISID(InISID, NewTransform, bWorldSpace, bMarkRenderStateDirty, bTeleport);
	}
	return SetInstanceTransformByISMI(InISID, NewTransform, bWorldSpace, bMarkRenderStateDirty, bTeleport);
}

bool UInteractableISMComponent::SetInstanceTransformByISID(int32 InISID, const FTransform& NewTransform, bool bWorldSpace, bool bMarkRenderStateDirty, bool bTeleport)
{
	const int32 ISMI = FindISMI(InISID);
	if (ISMI == INDEX_NONE || !IsValidInstance(ISMI))
	{
		return false;
	}

	BindISIDToISMI(InISID, ISMI);
	CachedGameplayTransformsByISID.Add(InISID, NewTransform);
	return UpdateInstanceTransform(ISMI, NewTransform, bWorldSpace, bMarkRenderStateDirty, bTeleport);
}

bool UInteractableISMComponent::SetInstanceTransformByISMI(int32 InISMI, const FTransform& NewTransform, bool bWorldSpace, bool bMarkRenderStateDirty, bool bTeleport)
{
	if (!IsValidInstance(InISMI))
	{
		return false;
	}

	const int32 FoundISID = FindISID(InISMI);
	if (FoundISID > 0)
	{
		CachedGameplayTransformsByISID.Add(FoundISID, NewTransform);
	}
	return UpdateInstanceTransform(InISMI, NewTransform, bWorldSpace, bMarkRenderStateDirty, bTeleport);
}

FTransform UInteractableISMComponent::GetEffectiveResetTransform() const
{
	if (ResetTransform.GetLocation().IsNearlyZero(10.0f) || ResetTransform.GetScale3D().GetMax() > 0.01f)
	{
		return FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, -100000.0f), FVector(0.0001f));
	}
	return ResetTransform;
}

bool UInteractableISMComponent::IsInstanceParkedAtResetTransform(int32 InISMI) const
{
	if (!IsValidInstance(InISMI))
	{
		return false;
	}
	FTransform InstTransform;
	if (!GetInstanceTransform(InISMI, InstTransform, true))
	{
		return false;
	}
	const FTransform EffectiveReset = GetEffectiveResetTransform();
	return InstTransform.GetLocation().Equals(EffectiveReset.GetLocation(), 100.0f) || InstTransform.GetScale3D().GetMax() < 0.005f;
}

bool UInteractableISMComponent::MoveInstanceToResetTransform(int32 InISID)
{
	int32 ISMI = FindISMI(InISID);
	if (ISMI == INDEX_NONE)
	{
		if (const APropManager* Manager = APropManager::Get(this))
		{
			const int32 ManagerISMI = Manager->GetISMIFromISID(InISID);
			if (IsValidInstance(ManagerISMI))
			{
				ISMI = ManagerISMI;
			}
		}
	}
	if (ISMI == INDEX_NONE || !IsValidInstance(ISMI))
	{
		return false;
	}

	BindISIDToISMI(InISID, ISMI);
	const FTransform EffectiveReset = GetEffectiveResetTransform();

	// Cache current gameplay transform before parking at ResetTransform (Section 7.2 Step 3 & Step 8)
	FTransform CurrentWorldTransform;
	if (GetInstanceTransform(ISMI, CurrentWorldTransform, true))
	{
		const bool bAlreadyAtReset = CurrentWorldTransform.GetLocation().Equals(EffectiveReset.GetLocation(), 100.0f) &&
			CurrentWorldTransform.GetScale3D().GetMax() < 0.005f;
		if (bAlreadyAtReset)
		{
			return true;
		}
		if (!CurrentWorldTransform.GetLocation().Equals(EffectiveReset.GetLocation(), 100.0f) &&
			CurrentWorldTransform.GetScale3D().GetMax() >= 0.005f)
		{
			CachedGameplayTransformsByISID.Add(InISID, CurrentWorldTransform);
		}
	}

	const bool bUpdated = UpdateInstanceTransform(ISMI, EffectiveReset, true, true, true);
	return bUpdated;
}

bool UInteractableISMComponent::RestoreInstanceToGameplayTransform(int32 InISID, const FTransform& GameplayTransform)
{
	int32 ISMI = FindISMI(InISID);
	if (ISMI == INDEX_NONE)
	{
		if (const APropManager* Manager = APropManager::Get(this))
		{
			const int32 ManagerISMI = Manager->GetISMIFromISID(InISID);
			if (IsValidInstance(ManagerISMI))
			{
				ISMI = ManagerISMI;
			}
		}
	}
	if (ISMI == INDEX_NONE || !IsValidInstance(ISMI))
	{
		return false;
	}

	BindISIDToISMI(InISID, ISMI);
	CachedGameplayTransformsByISID.Add(InISID, GameplayTransform);

	FTransform CurrentWorldTransform;
	if (GetInstanceTransform(ISMI, CurrentWorldTransform, true))
	{
		if (CurrentWorldTransform.Equals(GameplayTransform, 0.1f))
		{
			return true;
		}
	}

	const bool bUpdated = UpdateInstanceTransform(ISMI, GameplayTransform, true, true, true);
	return bUpdated;
}

bool UInteractableISMComponent::GetInstanceData(int32 InISID, FPropPackedRuntimeData& OutData) const
{
	const int32 ISMI = FindISMI(InISID);
	return GetInstanceDataByISMI(ISMI, OutData);
}

bool UInteractableISMComponent::SetInstanceData(int32 InISID, const FPropPackedRuntimeData& InData)
{
	const int32 ISMI = FindISMI(InISID);
	return SetInstanceDataByISMI(ISMI, InData);
}

bool UInteractableISMComponent::GetInstanceDataByISMI(int32 InISMI, FPropPackedRuntimeData& OutData) const
{
	if (PackedInstanceData.IsValidIndex(InISMI))
	{
		OutData = PackedInstanceData[InISMI];
		return true;
	}
	return false;
}

bool UInteractableISMComponent::SetInstanceDataByISMI(int32 InISMI, const FPropPackedRuntimeData& InData)
{
	if (InISMI < 0 || InISMI >= GetInstanceCount())
	{
		return false;
	}

	if (PackedInstanceData.Num() <= InISMI)
	{
		PackedInstanceData.SetNum(GetInstanceCount());
	}
	PackedInstanceData[InISMI] = InData;

	if (PropDatabase.IsValidIndex(InISMI))
	{
		PropDatabase[InISMI].RemainingPortions = InData.RemainingPortions;
		PropDatabase[InISMI].CurrentTemperature = InData.Temperature;
		PropDatabase[InISMI].bIsGrabbable = InData.bIsGrabbable;
		PropDatabase[InISMI].SimulationState = EPhysicsSimulationState::AtRest;
		if (PropDatabase[InISMI].NumOfPortions > 0)
		{
			PropDatabase[InISMI].QuantityLevel = static_cast<float>(InData.RemainingPortions) / static_cast<float>(PropDatabase[InISMI].NumOfPortions);
		}
	}

	return true;
}

bool UInteractableISMComponent::GetInstanceRuntimeInfo(int32 InISMI, FISMInstanceRuntimeInfo& OutInfo) const
{
	if (InISMI < 0 || InISMI >= GetInstanceCount() || !PackedInstanceData.IsValidIndex(InISMI))
	{
		return false;
	}
	OutInfo.ISMI = InISMI;
	OutInfo.ISID = FindISID(InISMI);
	OutInfo.ProductID = ProductID;
	OutInfo.RuntimeDefaults = PackedInstanceData[InISMI];
	return true;
}

bool UInteractableISMComponent::GetInstanceRuntimeInfoByISID(int32 InISID, FISMInstanceRuntimeInfo& OutInfo) const
{
	const int32 ResolvedISMI = FindISMI(InISID);
	if (ResolvedISMI == INDEX_NONE)
	{
		return false;
	}
	return GetInstanceRuntimeInfo(ResolvedISMI, OutInfo);
}

bool UInteractableISMComponent::UpdateRuntimeDefaults(int32 InISMI, const FPropPackedRuntimeData& NewData, bool bAutoUpdateTimestamp)
{
	if (!SetInstanceDataByISMI(InISMI, NewData))
	{
		return false;
	}

	if (bAutoUpdateTimestamp && GetOwner() && GetOwner()->HasAuthority())
	{
		if (APropManager* Manager = Cast<APropManager>(GetOwner()))
		{
			PackedInstanceData[InISMI].LastUpdatedMinute = Manager->GetCurrentWorldMinutes();
		}
	}
	return true;
}

// -----------------------------------------------------------------------------
// Section 40 & 65: Empty Instance Functions
// -----------------------------------------------------------------------------
bool UInteractableISMComponent::AddEmptyInstance(int32 InISMI)
{
	if (!IsValidInstance(InISMI))
	{
		return false;
	}

	// Section 40 Rules:
	// - Must not retain an old ISID association
	if (const int32* OldISID = ISMIToISID.Find(InISMI))
	{
		ISIDToISMI.Remove(*OldISID);
		CachedGameplayTransformsByISID.Remove(*OldISID);
	}
	ISMIToISID.Remove(InISMI);

	// - Must not retain stale runtime gameplay data
	if (PackedInstanceData.IsValidIndex(InISMI))
	{
		PackedInstanceData[InISMI] = FPropPackedRuntimeData();
		PackedInstanceData[InISMI].RemainingPortions = 0;
		PackedInstanceData[InISMI].bIsGrabbable = false;
	}
	if (PropDatabase.IsValidIndex(InISMI))
	{
		PropDatabase[InISMI] = FInteractablePropData();
		PropDatabase[InISMI].InstanceId = InISMI;
		PropDatabase[InISMI].ISID = 0;
		PropDatabase[InISMI].ProductID = 0;
		PropDatabase[InISMI].RemainingPortions = 0;
		PropDatabase[InISMI].QuantityLevel = 0.0f;
		PropDatabase[InISMI].bIsGrabbable = false;
	}

	// - Should remain at the component's Reset/Parking transform
	UpdateInstanceTransform(InISMI, GetEffectiveResetTransform(), true, true, true);
	MarkRenderStateDirty();

	// - Add ISMI to EmptyInstances
	EmptyInstances.AddUnique(InISMI);
	return true;
}

bool UInteractableISMComponent::RemoveEmptyInstance(int32 InISMI)
{
	return EmptyInstances.Remove(InISMI) > 0;
}

bool UInteractableISMComponent::IsEmptyInstance(int32 InISMI) const
{
	return EmptyInstances.Contains(InISMI);
}

int32 UInteractableISMComponent::GetEmptyInstanceCount() const
{
	return EmptyInstances.Num();
}

int32 UInteractableISMComponent::GetEmptyInstance(int32 Index) const
{
	return EmptyInstances.IsValidIndex(Index) ? EmptyInstances[Index] : INDEX_NONE;
}

int32 UInteractableISMComponent::ConsumeEmptyInstance()
{
	while (EmptyInstances.Num() > 0)
	{
		const int32 CandidateISMI = EmptyInstances[0];
		EmptyInstances.RemoveAt(0, EAllowShrinking::No);
		if (IsValidInstance(CandidateISMI))
		{
			return CandidateISMI;
		}
	}
	return INDEX_NONE;
}

void UInteractableISMComponent::ClearEmptyInstances(bool bRemovePhysicalISMInstances)
{
	if (bRemovePhysicalISMInstances && EmptyInstances.Num() > 0)
	{
		// Sort descending so removing physical ISM slots only shifts indices higher than the removed slot
		TArray<int32> SortedEmpty = EmptyInstances;
		SortedEmpty.Sort([](const int32 A, const int32 B) { return A > B; });

		for (int32 EmptyISMI : SortedEmpty)
		{
			if (IsValidInstance(EmptyISMI))
			{
				RemoveInstance(EmptyISMI);
				if (PackedInstanceData.IsValidIndex(EmptyISMI))
				{
					PackedInstanceData.RemoveAt(EmptyISMI);
				}
				if (PropDatabase.IsValidIndex(EmptyISMI))
				{
					PropDatabase.RemoveAt(EmptyISMI);
				}
			}
		}

		EmptyInstances.Empty();
		RebuildLocalIndexMap();
		return;
	}

	EmptyInstances.Empty();
}

void UInteractableISMComponent::RebuildEmptyInstances()
{
	EmptyInstances.Empty();

	const int32 NumInstances = GetInstanceCount();
	for (int32 ISMI = 0; ISMI < NumInstances; ++ISMI)
	{
		const int32* ActiveISID = ISMIToISID.Find(ISMI);
		if (!ActiveISID || *ActiveISID <= 0)
		{
			AddEmptyInstance(ISMI);
		}
	}
}

void UInteractableISMComponent::RebuildLocalIndexMap()
{
	TMap<int32, int32> PreviousISMIToISID = ISMIToISID;
	ISIDToISMI.Empty();
	ISMIToISID.Empty();

	const int32 NumInstances = GetInstanceCount();
	for (int32 ISMI = 0; ISMI < NumInstances; ++ISMI)
	{
		if (EmptyInstances.Contains(ISMI))
		{
			continue;
		}

		int32 EntryISID = 0;
		if (PropDatabase.IsValidIndex(ISMI) && PropDatabase[ISMI].ISID > 0)
		{
			EntryISID = PropDatabase[ISMI].ISID;
			PropDatabase[ISMI].InstanceId = ISMI;
		}
		else if (const int32* PrevISID = PreviousISMIToISID.Find(ISMI))
		{
			EntryISID = *PrevISID;
		}

		if (EntryISID > 0)
		{
			ISIDToISMI.Add(EntryISID, ISMI);
			ISMIToISID.Add(ISMI, EntryISID);
		}
	}
}

bool UInteractableISMComponent::Validate() const
{
	const int32 NumInstances = GetInstanceCount();
	if (PackedInstanceData.Num() != NumInstances)
	{
		return false;
	}

	for (const TPair<int32, int32>& Pair : ISIDToISMI)
	{
		const int32 MappedISID = Pair.Key;
		const int32 MappedISMI = Pair.Value;
		if (MappedISID <= 0 || MappedISMI < 0 || MappedISMI >= NumInstances)
		{
			return false;
		}
		if (EmptyInstances.Contains(MappedISMI))
		{
			return false;
		}
		const int32* ReverseISID = ISMIToISID.Find(MappedISMI);
		if (!ReverseISID || *ReverseISID != MappedISID)
		{
			return false;
		}
	}

	for (int32 EmptyISMI : EmptyInstances)
	{
		if (EmptyISMI < 0 || EmptyISMI >= NumInstances || ISMIToISID.Contains(EmptyISMI))
		{
			return false;
		}
	}

	return true;
}

// -----------------------------------------------------------------------------
// Legacy & Convenience API
// -----------------------------------------------------------------------------
int32 UInteractableISMComponent::AddInteractableInstance(const FTransform& InstanceTransform, const FInteractablePropData& InitialData)
{
	const int32 NewIndex = AddInstance(InstanceTransform);
	if (NewIndex != INDEX_NONE)
	{
		FInteractablePropData DataCopy = InitialData;
		DataCopy.InstanceId = NewIndex;
		DataCopy.ProductID = ProductID;
		DataCopy.IISMComponentID = IISMComponentID;

		if (NewIndex < PropDatabase.Num())
		{
			PropDatabase[NewIndex] = DataCopy;
		}
		else
		{
			PropDatabase.Add(DataCopy);
		}

		FPropPackedRuntimeData Packed;
		Packed.RemainingPortions = DataCopy.RemainingPortions;
		Packed.Temperature = DataCopy.CurrentTemperature;
		Packed.bIsGrabbable = DataCopy.bIsGrabbable;
		if (NewIndex < PackedInstanceData.Num())
		{
			PackedInstanceData[NewIndex] = Packed;
		}
		else
		{
			PackedInstanceData.Add(Packed);
		}

		if (DataCopy.ISID > 0)
		{
			BindISIDToISMI(DataCopy.ISID, NewIndex);
		}
	}
	return NewIndex;
}

bool UInteractableISMComponent::RemoveInteractableInstance(int32 InstanceIndex)
{
	if (!IsValidInstance(InstanceIndex)) return false;

	const int32 BoundISID = FindISID(InstanceIndex);
	if (BoundISID > 0)
	{
		return RemoveInstanceByISID(BoundISID);
	}

	const bool bRemoved = RemoveInstance(InstanceIndex);
	if (bRemoved)
	{
		if (PropDatabase.IsValidIndex(InstanceIndex))
		{
			PropDatabase.RemoveAt(InstanceIndex);
			for (int32 i = InstanceIndex; i < PropDatabase.Num(); ++i)
			{
				PropDatabase[i].InstanceId = i;
			}
		}
		if (PackedInstanceData.IsValidIndex(InstanceIndex))
		{
			PackedInstanceData.RemoveAt(InstanceIndex);
		}
		RebuildLocalIndexMap();
	}
	return bRemoved;
}

bool UInteractableISMComponent::GetPropDataForInstance(int32 InstanceIndex, FInteractablePropData& OutData) const
{
	if (PropDatabase.IsValidIndex(InstanceIndex))
	{
		OutData = PropDatabase[InstanceIndex];
		return true;
	}
	return false;
}

bool UInteractableISMComponent::SetPropDataForInstance(int32 InstanceIndex, const FInteractablePropData& NewData)
{
	if (PropDatabase.IsValidIndex(InstanceIndex))
	{
		PropDatabase[InstanceIndex] = NewData;
		if (PackedInstanceData.IsValidIndex(InstanceIndex))
		{
			PackedInstanceData[InstanceIndex].RemainingPortions = NewData.RemainingPortions;
			PackedInstanceData[InstanceIndex].Temperature = NewData.CurrentTemperature;
			PackedInstanceData[InstanceIndex].bIsGrabbable = NewData.bIsGrabbable;
		}
		return true;
	}
	return false;
}

bool UInteractableISMComponent::ConsumeInstancePortions(int32 InstanceIndex, int32 SipsToConsume, AActor* ConsumerActor, int32& OutSipsConsumed)
{
	OutSipsConsumed = 0;
	if (!PropDatabase.IsValidIndex(InstanceIndex)) return false;

	FInteractablePropData& Data = PropDatabase[InstanceIndex];
	if (Data.IsEmpty()) return false;

	OutSipsConsumed = Data.ConsumePortions(SipsToConsume);
	if (OutSipsConsumed <= 0) return false;

	if (PackedInstanceData.IsValidIndex(InstanceIndex))
	{
		PackedInstanceData[InstanceIndex].RemainingPortions = Data.RemainingPortions;
	}

	const float Ratio = (Data.NumOfPortions > 0) ? static_cast<float>(OutSipsConsumed) / static_cast<float>(Data.NumOfPortions) : 1.0f;
	const bool bItemExpired = Data.IsExpired();

	if (ANPCCharacter* NPC = Cast<ANPCCharacter>(ConsumerActor))
	{
		if (bItemExpired)
		{
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::Sickness, Data.SicknessLevel * Ratio);
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::Dopamine, -Data.ExpiredDopaminePenalty * Ratio);
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::Stress, 25.0f * Ratio);

			for (const auto& Mod : Data.ExpiryStimulusProfile)
			{
				NPC->ApplySimulationStatDelta(Mod.Stat, Mod.ChangeRate * Ratio);
			}
		}
		else
		{
			const float ConsumerTolerance = (NPC->PersonalityComponent) ? NPC->PersonalityComponent->Personality.TemperatureTolerance : 0.5f;
			const float TempDopamine = FInteractablePropData::CalculateTemperatureDopamine(Data.CurrentTemperature, Data.ConsumableTemperature, Data.TemperatureDopamineBonus, ConsumerTolerance) * Ratio;
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::Dopamine, TempDopamine);

			for (const auto& Mod : Data.StimuliPerPortion)
			{
				NPC->ApplySimulationStatDelta(Mod.Stat, Mod.ChangeRate * Ratio);
			}

			if (NPC->PersonalityComponent)
			{
				const float Compatibility = NPC->PersonalityComponent->EvaluateTaste(Data.TasteProfile);
				if (Compatibility >= 0.65f)
				{
					NPC->ApplySimulationStatDelta(ENPCSimulationStat::Dopamine, 10.0f * Compatibility * Ratio);
				}
				else if (Compatibility <= 0.30f)
				{
					NPC->ApplySimulationStatDelta(ENPCSimulationStat::Stress, 8.0f * (1.0f - Compatibility) * Ratio);
				}
			}
		}

		const float AutoBodyTempDelta = FInteractablePropData::CalculateBodyTemperatureDelta(Data.CurrentTemperature) * Ratio;
		if (FMath::Abs(AutoBodyTempDelta) > KINDA_SMALL_NUMBER)
		{
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::BodyTemperature, AutoBodyTempDelta);
		}
	}

	OnInstancePortionConsumed.Broadcast(InstanceIndex, Data.RemainingPortions, ConsumerActor);
	return true;
}

void UInteractableISMComponent::UpdateInstanceTemperature(int32 InstanceIndex, float AmbientTemperature, float DeltaSeconds)
{
	if (PropDatabase.IsValidIndex(InstanceIndex))
	{
		PropDatabase[InstanceIndex].UpdateTemperature(AmbientTemperature, DeltaSeconds);
		if (PackedInstanceData.IsValidIndex(InstanceIndex))
		{
			PackedInstanceData[InstanceIndex].Temperature = PropDatabase[InstanceIndex].CurrentTemperature;
		}
	}
}

void UInteractableISMComponent::SetInstanceSimulationState(int32 InstanceIndex, EPhysicsSimulationState NewState)
{
	if (PropDatabase.IsValidIndex(InstanceIndex))
	{
		PropDatabase[InstanceIndex].SimulationState = NewState;
	}
}

void UInteractableISMComponent::HandleComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	const int32 HitISMI = ResolveHitISMI(Hit, OtherActor, OtherComp);
	if (HitISMI == INDEX_NONE)
	{
		return;
	}

	if (LastProcessedHitFrame == GFrameCounter && LastProcessedHitISMI == HitISMI)
	{
		return;
	}
	LastProcessedHitFrame = GFrameCounter;
	LastProcessedHitISMI = HitISMI;

	OnInstanceHit(HitISMI, OtherActor, OtherComp, NormalImpulse, Hit);
}

int32 UInteractableISMComponent::ResolveHitISMI(const FHitResult& Hit, AActor* OtherActor, UPrimitiveComponent* OtherComp) const
{
	// Only resolve explicit instance indices reported in Hit.Item or Hit.MyItem — never guess a nearby instance.
	if (Hit.Item != INDEX_NONE && IsValidInstance(Hit.Item))
	{
		if (IsEmptyInstance(Hit.Item) || IsInstanceParkedAtResetTransform(Hit.Item))
		{
			return INDEX_NONE;
		}
		return Hit.Item;
	}

	if (Hit.MyItem != INDEX_NONE && IsValidInstance(Hit.MyItem))
	{
		if (IsEmptyInstance(Hit.MyItem) || IsInstanceParkedAtResetTransform(Hit.MyItem))
		{
			return INDEX_NONE;
		}
		return Hit.MyItem;
	}

	return INDEX_NONE;
}

bool UInteractableISMComponent::OnInstanceHit(int32 InISMI, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!IsValidInstance(InISMI) || IsEmptyInstance(InISMI) || IsInstanceParkedAtResetTransform(InISMI))
	{
		return false;
	}

	APropManager* Manager = APropManager::Get(this);

	// 1. Resolve logical ISID from the hit instance index (ISMI)
	int32 HitISID = FindISID(InISMI);
	if (HitISID <= 0 && Manager)
	{
		HitISID = Manager->GetISIDFromISMI(IISMComponentID, InISMI);
	}

	// Ignore hits from an ASM that is already representing the exact same ISID
	if (const AActivePropActor* OtherProp = Cast<AActivePropActor>(OtherActor))
	{
		if (HitISID > 0 && OtherProp->ISID == HitISID)
		{
			return false;
		}
	}

	OnISMInstanceHit.Broadcast(InISMI, HitISID, OtherActor, NormalImpulse, Hit);

	if (!bUnsettleOnInstanceHit)
	{
		return false;
	}

	// Skip client-side cascade contacts from already-active AActivePropActors (handled authoritatively on server)
	const AActor* CompOwner = GetOwner();
	if (CompOwner && !CompOwner->HasAuthority() && OtherActor && OtherActor->IsA<AActivePropActor>())
	{
		return false;
	}

	FVector EffectiveImpulse = FVector::ZeroVector;
	if (OtherComp && OtherComp->IsSimulatingPhysics())
	{
		EffectiveImpulse = OtherComp->GetComponentVelocity() * 0.65f;
	}
	else if (OtherActor)
	{
		EffectiveImpulse = OtherActor->GetVelocity() * 0.55f;
	}

	if (EffectiveImpulse.IsNearlyZero() && !NormalImpulse.IsNearlyZero())
	{
		EffectiveImpulse = NormalImpulse.GetSafeNormal() * FMath::Min(NormalImpulse.Size() * 0.05f, 180.0f);
	}

	// Clamp impulse magnitude and vertical pop so cans topple/roll smoothly without a popcorn bounce
	EffectiveImpulse.Z = FMath::Clamp(EffectiveImpulse.Z, -40.0f, 35.0f);
	EffectiveImpulse = EffectiveImpulse.GetClampedToMaxSize(220.0f);

	// 2. Transition the hit ISID into Unsettled state (promoted to ASM)
	return SetInstanceUnsettledByISMI(InISMI, EffectiveImpulse, Hit.ImpactPoint);
}

bool UInteractableISMComponent::SetInstanceUnsettledByISMI(int32 InISMI, FVector HitImpulse, FVector HitLocation)
{
	return SetInstanceToUnsettled(InISMI, HitImpulse, HitLocation, -1.0f);
}

bool UInteractableISMComponent::SetInstanceToRest(int32 InstanceIndex)
{
	APropManager* Manager = APropManager::Get(this);
	return Manager ? Manager->SetMeshInstanceToRest(this, InstanceIndex) : false;
}

bool UInteractableISMComponent::SetInstanceToUnsettled(int32 InstanceIndex, FVector HitImpulse, FVector ImpactPoint, float UnsettledDelay)
{
	APropManager* Manager = APropManager::Get(this);
	return Manager ? Manager->SetMeshInstanceToUnsettled(this, InstanceIndex, HitImpulse, ImpactPoint, UnsettledDelay) : false;
}

bool UInteractableISMComponent::SetInstanceToSettling(int32 InstanceIndex, float VelocityTolerance)
{
	APropManager* Manager = APropManager::Get(this);
	return Manager ? Manager->SetMeshInstanceToSettling(this, InstanceIndex, VelocityTolerance) : false;
}

bool UInteractableISMComponent::SetInstanceToHeld(int32 InstanceIndex, AActor* HolderActor, USceneComponent* InAttachParent, FName SocketName)
{
	APropManager* Manager = APropManager::Get(this);
	return Manager ? Manager->SetMeshInstanceToHeld(this, InstanceIndex, HolderActor, InAttachParent, SocketName) : false;
}

bool UInteractableISMComponent::SetInstanceToInMotion(int32 InstanceIndex, FVector HitImpulse, FVector ImpactPoint)
{
	APropManager* Manager = APropManager::Get(this);
	return Manager ? Manager->SetMeshInstanceToInMotion(this, InstanceIndex, HitImpulse, ImpactPoint) : false;
}

void UInteractableISMComponent::SetInstanceGrabbable(int32 InstanceIndex, bool bNewGrabbable)
{
	if (PropDatabase.IsValidIndex(InstanceIndex))
	{
		PropDatabase[InstanceIndex].bIsGrabbable = bNewGrabbable;
	}
	if (PackedInstanceData.IsValidIndex(InstanceIndex))
	{
		PackedInstanceData[InstanceIndex].bIsGrabbable = bNewGrabbable;
	}
}

void UInteractableISMComponent::SyncDatabaseWithInstanceCount()
{
	const int32 NumInstances = GetInstanceCount();
	if (PropDatabase.Num() < NumInstances)
	{
		const int32 PrevNum = PropDatabase.Num();
		PropDatabase.SetNum(NumInstances);
		for (int32 i = PrevNum; i < NumInstances; ++i)
		{
			if (DefaultDataAsset)
			{
				PropDatabase[i] = CreatePropDataFromAsset(DefaultDataAsset, i);
			}
			else
			{
				PropDatabase[i].InstanceId = i;
			}
		}
	}

	if (PackedInstanceData.Num() < NumInstances)
	{
		const int32 PrevPackedNum = PackedInstanceData.Num();
		PackedInstanceData.SetNum(NumInstances);
		for (int32 i = PrevPackedNum; i < NumInstances; ++i)
		{
			if (PropDatabase.IsValidIndex(i))
			{
				PackedInstanceData[i].RemainingPortions = PropDatabase[i].RemainingPortions;
				PackedInstanceData[i].Temperature = PropDatabase[i].CurrentTemperature;
				PackedInstanceData[i].bIsGrabbable = PropDatabase[i].bIsGrabbable;
			}
		}
	}
}

FInteractablePropData UInteractableISMComponent::CreatePropDataFromAsset(const UInteractablePropDataAsset* InAsset, int32 InInstanceId) const
{
	FInteractablePropData NewData;
	NewData.InstanceId = InInstanceId;
	NewData.ProductID = ProductID;
	NewData.IISMComponentID = IISMComponentID;

	if (InAsset)
	{
		NewData.ItemDisplayName = InAsset->ItemDisplayName;
		NewData.ConsumableType = InAsset->ConsumableType;
		NewData.StaticMesh = InAsset->StaticMesh;
		NewData.DataAsset = const_cast<UInteractablePropDataAsset*>(InAsset);
		NewData.bEnableTemperatureUpdate = InAsset->bEnableTemperatureUpdate;
		NewData.ConsumableTemperature = InAsset->ConsumableTemperature;
		NewData.CurrentTemperature = InAsset->ConsumableTemperature;
		NewData.TemperatureDopamineBonus = InAsset->TemperatureDopamineBonus;
		NewData.ThermalExchangeRate = InAsset->ThermalExchangeRate;
		NewData.Price = InAsset->Price;
		NewData.NumOfPortions = InAsset->NumOfPortions;
		NewData.RemainingPortions = InAsset->NumOfPortions;
		NewData.QuantityLevel = 1.0f;
		NewData.bHasExpiry = InAsset->bHasExpiry;
		NewData.ExpiryLifetimeMinutes = InAsset->ExpiryLifetimeMinutes;
		NewData.SicknessLevel = InAsset->SicknessLevel;
		NewData.ExpiredDopaminePenalty = InAsset->ExpiredDopaminePenalty;
		NewData.ExpiryStimulusProfile = InAsset->ExpiryStimulusProfile;
		NewData.StimuliPerPortion = InAsset->StimuliPerPortion;
		NewData.TasteProfile = InAsset->BuildTasteVector(false);
	}

	return NewData;
}
