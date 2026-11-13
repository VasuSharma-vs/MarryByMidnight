#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Props/InteractableProp.h"
#include "Props/ColaPropActor.h"
#include "Props/ColaLiquidMotion.h"
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
#include "Materials/MaterialInstanceDynamic.h"

UInteractableISMComponent::UInteractableISMComponent()
{
	SetIsReplicatedByDefault(true);
	SetNotifyRigidBodyCollision(true);
	BodyInstance.bNotifyRigidBodyCollision = true;
	SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	SetGenerateOverlapEvents(false);
	ISMComponent = this;
	NumCustomDataFloats = 6;

	// Default ResetTransform places parked ISM instances far below the gameplay world with near-zero scale
	ResetTransform = FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, -100000.0f), FVector(0.0001f));
}

void UInteractableISMComponent::OnRegister()
{
	Super::OnRegister();

	ISMComponent = this;
	if (NumCustomDataFloats < 6)
	{
		SetNumCustomDataFloats(6);
	}
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

	APropManager* Manager = APropManager::Get(this);

	// If ProductID is not explicitly set, try to find an existing registered ProductID from PropManager
	if (ProductID <= 0 && Manager)
	{
		if (DefaultDataAsset)
		{
			const int32 FoundPID = Manager->FindProductIDByDataAsset(DefaultDataAsset);
			if (FoundPID > 0)
			{
				ProductID = FoundPID;
			}
		}
		else if (GetStaticMesh())
		{
			TArray<UMaterialInterface*> CompMats;
			const int32 NumMats = GetNumMaterials();
			for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
			{
				CompMats.Add(GetMaterial(MatIdx));
			}
			const int32 FoundPID = Manager->FindProductIDByMeshAndMaterials(GetStaticMesh(), CompMats);
			if (FoundPID > 0)
			{
				ProductID = FoundPID;
			}
		}
	}

	if (ProductID > 0 && Manager)
	{
		if (const FPropProductDefinition* SharedDef = Manager->FindProductDefinition(ProductID))
		{
			ApplyProductDefinition(*SharedDef);
		}
		else if (DefaultDataAsset)
		{
			const FPropProductDefinition TempDef = FPropProductDefinition::FromDataAsset(DefaultDataAsset, ProductID, StaticMeshID);
			ApplyProductDefinition(TempDef);
		}
	}
	else if (DefaultDataAsset)
	{
		const FPropProductDefinition TempDef = FPropProductDefinition::FromDataAsset(DefaultDataAsset, ProductID, StaticMeshID);
		ApplyProductDefinition(TempDef);
	}

	SyncDatabaseWithInstanceCount();
	if (GetInstanceCount() > 0)
	{
		RebuildLocalIndexMap();
	}

	if (ComponentRuntimeSettings.bAutoRegisterWithPropManager)
	{
		if (Manager)
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
	DOREPLIFETIME(UInteractableISMComponent, PackedInstanceData);
	DOREPLIFETIME(UInteractableISMComponent, bDebugPrintInstanceHit);
}

void UInteractableISMComponent::ApplyProductDefinition(const FPropProductDefinition& InDefinition)
{
	if (InDefinition.ProductID > 0)
	{
		ProductID = InDefinition.ProductID;
	}
	if (InDefinition.StaticMeshID > 0)
	{
		StaticMeshID = InDefinition.StaticMeshID;
	}
	if (InDefinition.SourceDataAsset)
	{
		DefaultDataAsset = InDefinition.SourceDataAsset;
	}

	// 1. Resolve and apply target static mesh
	UStaticMesh* TargetMesh = InDefinition.StaticMesh;
	if (!TargetMesh && InDefinition.SourceDataAsset)
	{
		TargetMesh = InDefinition.SourceDataAsset->StaticMesh;
	}
	if (TargetMesh && GetStaticMesh() != TargetMesh)
	{
		SetStaticMesh(TargetMesh);
		MarkRenderStateDirty();
	}

	// 2. Resolve source material overrides
	const TArray<TObjectPtr<UMaterialInterface>>& SourceOverrides = (InDefinition.SourceDataAsset && InDefinition.SourceDataAsset->MaterialOverrides.Num() > 0)
		? InDefinition.SourceDataAsset->MaterialOverrides
		: InDefinition.MaterialOverrides;

	// 3. Always apply materials to all mesh slots
	if (TargetMesh)
	{
		const int32 NumSlots = TargetMesh->GetStaticMaterials().Num();
		for (int32 MatIdx = 0; MatIdx < NumSlots; ++MatIdx)
		{
			UMaterialInterface* TargetMat = nullptr;
			if (SourceOverrides.IsValidIndex(MatIdx) && IsValid(SourceOverrides[MatIdx]))
			{
				TargetMat = SourceOverrides[MatIdx];
			}
			else
			{
				TargetMat = TargetMesh->GetMaterial(MatIdx);
			}

			if (TargetMat && GetMaterial(MatIdx) != TargetMat)
			{
				UE_LOG(LogTemp, Verbose, TEXT("[PropConversionDebug] [UInteractableISMComponent::ApplyProductDefinition] Slot %d on Comp '%s' updated: '%s' -> '%s'"),
					MatIdx, *GetName(), GetMaterial(MatIdx) ? *GetMaterial(MatIdx)->GetName() : TEXT("nullptr"), *TargetMat->GetName());
				SetMaterial(MatIdx, TargetMat);
			}
		}

		// Clear any excess override material slots beyond the mesh slot count
		const int32 ExistingNumMats = GetNumMaterials();
		for (int32 ExcessIdx = NumSlots; ExcessIdx < ExistingNumMats; ++ExcessIdx)
		{
			SetMaterial(ExcessIdx, nullptr);
		}
	}

	// 4. Verify material usage for instanced static meshes and dirty render state
	const int32 FinalNumMats = TargetMesh ? TargetMesh->GetStaticMaterials().Num() : GetNumMaterials();
	for (int32 MatIdx = 0; MatIdx < FinalNumMats; ++MatIdx)
	{
		if (UMaterialInterface* Mat = GetMaterial(MatIdx))
		{
			Mat->CheckMaterialUsage(MATUSAGE_InstancedStaticMeshes);
		}
	}
	bUsesBottleLiquidVolume=InDefinition.CustomActiveActorClass && InDefinition.CustomActiveActorClass->IsChildOf(AColaPropActor::StaticClass());
	RefreshBottleLiquidMaterial();
	MarkRenderStateDirty();

	if (GetCollisionEnabled() == ECollisionEnabled::NoCollision)
	{
		SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	}
}

void UInteractableISMComponent::RefreshBottleLiquidMaterial()
{
	if (bUsesBottleLiquidVolume)
	{
		// Outside play, material previews must respect the artist's Liquid Level.
		// During play, every resting bottle uses its authoritative per-instance fill.
		if (GetWorld() && GetWorld()->IsGameWorld())
		{
			UMaterialInterface* Source=GetMaterial(1);
			if (Source && Source!=LiquidInstanceMaterial)
			{
				if (!LiquidInstanceMaterial || Source!=LiquidInstanceMaterialSource)
				{
					LiquidInstanceMaterialSource=Source;
					LiquidInstanceMaterial=UMaterialInstanceDynamic::Create(Source,this);
				}
				if (LiquidInstanceMaterial) SetMaterial(1,LiquidInstanceMaterial);
			}
			if (LiquidInstanceMaterial)
			{
				LiquidInstanceMaterial->SetScalarParameterValue(TEXT("Use Instance Liquid Data"),1);
				LiquidInstanceMaterial->SetScalarParameterValue(TEXT("Native Liquid Motion"),0);
			}
		}
		if (UMaterialInterface* Liquid=GetMaterial(1))
		{
			Liquid->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Liquid Container Height")),LiquidVolumeHeightCm);
			Liquid->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Liquid Container Radius")),LiquidVolumeRadiusCm);
		}
		LiquidVolumeHeightCm=FMath::Max(1.f,LiquidVolumeHeightCm); LiquidVolumeRadiusCm=FMath::Max(1.f,LiquidVolumeRadiusCm);
	}
}

UMaterialInterface* UInteractableISMComponent::GetProductMaterial(int32 MaterialIndex) const
{
	UMaterialInterface* Material=GetMaterial(MaterialIndex);
	return MaterialIndex==1 && Material && Material==LiquidInstanceMaterial
		? LiquidInstanceMaterialSource.Get() : Material;
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

	BindISIDToISMI(InISID, NewISMI);
	UpdateInstanceCustomData(NewISMI, true);
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

		BindISIDToISMI(InISID, ReusedISMI);
		UpdateInstanceCustomData(ReusedISMI, true);
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
	const bool bUpdated=UpdateInstanceTransform(ISMI, NewTransform, bWorldSpace, bMarkRenderStateDirty, bTeleport);
	if (bUpdated && bUsesBottleLiquidVolume) UpdateInstanceCustomData(ISMI,bMarkRenderStateDirty);
	return bUpdated;
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
	const bool bUpdated=UpdateInstanceTransform(InISMI, NewTransform, bWorldSpace, bMarkRenderStateDirty, bTeleport);
	if (bUpdated && bUsesBottleLiquidVolume) UpdateInstanceCustomData(InISMI,bMarkRenderStateDirty);
	return bUpdated;
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

	UpdateInstanceCustomData(InISMI, true);

	return true;
}

void UInteractableISMComponent::UpdateInstanceCustomData(int32 InISMI, bool bMarkRenderStateDirty)
{
	if (InISMI < 0 || InISMI >= GetInstanceCount())
	{
		return;
	}

	APropManager* Manager = APropManager::Get(this);
	const FPropProductDefinition* ProductDef = Manager ? Manager->FindProductDefinition(ProductID) : nullptr;
	FPropProductDefinition FallbackDef;
	if (!ProductDef && DefaultDataAsset)
	{
		FallbackDef = FPropProductDefinition::FromDataAsset(DefaultDataAsset, ProductID, StaticMeshID);
		ProductDef = &FallbackDef;
	}

	const int32 BoundISID = FindISID(InISMI);
	const FPropInstanceUniqueData* UniqueData = (Manager && BoundISID > 0) ? Manager->FindUniqueData(BoundISID) : nullptr;
	const FPropContentDefinition* ContentDef = (Manager && UniqueData && UniqueData->EventData.ContentID > 0)
		? Manager->FindContentDefinition(UniqueData->EventData.ContentID)
		: nullptr;
	const double WorldSeconds = Manager
		? Manager->WorldElapsedSeconds
		: (GetWorld() ? static_cast<double>(GetWorld()->GetTimeSeconds()) : 0.0);

	UpdateInstanceCustomDataDirect(InISMI, ProductDef, ContentDef, UniqueData, WorldSeconds, bMarkRenderStateDirty);
}

void UInteractableISMComponent::UpdateInstanceCustomDataDirect(
	int32 InISMI,
	const FPropProductDefinition* ProductDef,
	const FPropContentDefinition* ContentDef,
	const FPropInstanceUniqueData* UniqueData,
	double CurrentWorldSeconds,
	bool bMarkRenderStateDirty)
{
	if (InISMI < 0 || InISMI >= GetInstanceCount())
	{
		return;
	}

	if (NumCustomDataFloats < 6)
	{
		SetNumCustomDataFloats(6);
	}

	// Slot 0: ContentLevel [0.0 .. MaxOverfillLevel]
	float ContentLevelValue = 1.0f;
	if (UniqueData)
	{
		ContentLevelValue = FMath::Max(0.0f, UniqueData->EventData.ContentLevel);
	}
	else if (PackedInstanceData.IsValidIndex(InISMI))
	{
		const int32 TotalPortions = (ProductDef && ProductDef->ConsumableProfile.NumOfPortions > 0)
			? ProductDef->ConsumableProfile.NumOfPortions
			: (DefaultDataAsset ? FMath::Max(1, DefaultDataAsset->NumOfPortions) : 4);
		ContentLevelValue = FMath::Clamp(static_cast<float>(PackedInstanceData[InISMI].RemainingPortions) / static_cast<float>(TotalPortions), 0.0f, 1.0f);
	}

	// Slot 1: ExpiryAge normalized [0.0 = Fresh, 1.0 = Expired]
	float ExpiryAge = 0.0f;
	bool bHasExpiry = false;
	float LifetimeMinutes = 60.0f;
	if (ContentDef && ContentDef->ContentID > 0)
	{
		bHasExpiry = ContentDef->ExpiryProfile.bHasExpiry;
		LifetimeMinutes = ContentDef->ExpiryProfile.ExpiryLifetimeMinutes;
	}
	else if (ProductDef)
	{
		bHasExpiry = ProductDef->ExpiryProfile.bHasExpiry;
		LifetimeMinutes = ProductDef->ExpiryProfile.ExpiryLifetimeMinutes;
	}
	else if (DefaultDataAsset)
	{
		bHasExpiry = DefaultDataAsset->bHasExpiry;
		LifetimeMinutes = DefaultDataAsset->ExpiryLifetimeMinutes;
	}

	if (bHasExpiry && LifetimeMinutes > KINDA_SMALL_NUMBER)
	{
		if (UniqueData)
		{
			const double AgeSeconds = FMath::Max(0.0, CurrentWorldSeconds - UniqueData->EventData.ContentCreationTime);
			ExpiryAge = FMath::Clamp(static_cast<float>(AgeSeconds / (static_cast<double>(LifetimeMinutes) * 60.0)), 0.0f, 1.0f);
		}
		else if (PackedInstanceData.IsValidIndex(InISMI))
		{
			const int32 CurrentMinutes = FMath::FloorToInt(CurrentWorldSeconds / 60.0);
			const float AgeMinutes = FMath::Max(0.0f, static_cast<float>(CurrentMinutes - PackedInstanceData[InISMI].CreationMinute));
			ExpiryAge = FMath::Clamp(AgeMinutes / LifetimeMinutes, 0.0f, 1.0f);
		}
	}

	// Slot 2: Temperature in Celsius (raw float, drives shader frost/heat directly)
	float TempCelsius = 21.0f;
	if (UniqueData)
	{
		TempCelsius = UniqueData->ContinuousData.Temperature;
	}
	else if (PackedInstanceData.IsValidIndex(InISMI))
	{
		TempCelsius = PackedInstanceData[InISMI].Temperature;
	}
	else if (ProductDef)
	{
		TempCelsius = ProductDef->TemperatureProfile.ConsumableTemperature;
	}
	else if (DefaultDataAsset)
	{
		TempCelsius = DefaultDataAsset->ConsumableTemperature;
	}

	// Slot 3: Normalized Structural Strength [0.0 = Broken/Deformed, 1.0 = Intact]
	float StrengthRatio = 1.0f;
	if (UniqueData)
	{
		const float MaxStr = (ProductDef && ProductDef->MaxStrength > KINDA_SMALL_NUMBER) ? ProductDef->MaxStrength : 100.0f;
		StrengthRatio = FMath::Clamp(UniqueData->EventData.Strength / MaxStr, 0.0f, 1.0f);
	}

	SetCustomDataValue(InISMI, 0, ContentLevelValue, false);
	SetCustomDataValue(InISMI, 1, ExpiryAge, false);
	SetCustomDataValue(InISMI, 2, TempCelsius, false);
	SetCustomDataValue(InISMI, 3, StrengthRatio, false);
	float PlaneOffset=0;
	if (bUsesBottleLiquidVolume)
	{
		FTransform Transform;
		if (!GetInstanceTransformByISID(FindISID(InISMI),Transform,true)) GetInstanceTransform(InISMI,Transform,true);
		const FVector LocalNormal=Transform.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
		PlaneOffset=FColaLiquidMotion::CylinderPlaneOffset(FMath::Clamp(ContentLevelValue,0.f,1.f),LocalNormal,LiquidVolumeHeightCm*.5f,LiquidVolumeRadiusCm);
	}
	SetCustomDataValue(InISMI,4,PlaneOffset,false);
	SetCustomDataValue(InISMI,5,bUsesBottleLiquidVolume ? 1.f : 0.f,bMarkRenderStateDirty);
}

void UInteractableISMComponent::UpdateAllInstancesCustomData()
{
	const int32 Total = GetInstanceCount();
	if (Total <= 0)
	{
		return;
	}

	APropManager* Manager = APropManager::Get(this);
	const FPropProductDefinition* ProductDef = Manager ? Manager->FindProductDefinition(ProductID) : nullptr;
	FPropProductDefinition FallbackDef;
	if (!ProductDef && DefaultDataAsset)
	{
		FallbackDef = FPropProductDefinition::FromDataAsset(DefaultDataAsset, ProductID, StaticMeshID);
		ProductDef = &FallbackDef;
	}
	const double WorldSeconds = Manager
		? Manager->WorldElapsedSeconds
		: (GetWorld() ? static_cast<double>(GetWorld()->GetTimeSeconds()) : 0.0);

	for (int32 i = 0; i < Total; ++i)
	{
		if (IsEmptyInstance(i))
		{
			continue;
		}
		const int32 BoundISID = FindISID(i);
		const FPropInstanceUniqueData* UniqueData = (Manager && BoundISID > 0) ? Manager->FindUniqueData(BoundISID) : nullptr;
		const FPropContentDefinition* ContentDef = (Manager && UniqueData && UniqueData->EventData.ContentID > 0)
			? Manager->FindContentDefinition(UniqueData->EventData.ContentID)
			: nullptr;
		UpdateInstanceCustomDataDirect(i, ProductDef, ContentDef, UniqueData, WorldSeconds, false);
	}
	MarkRenderStateDirty();
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
	APropManager* Manager = APropManager::Get(this);
	for (int32 ISMI = 0; ISMI < NumInstances; ++ISMI)
	{
		if (EmptyInstances.Contains(ISMI))
		{
			continue;
		}

		int32 EntryISID = 0;
		if (const int32* PrevISID = PreviousISMIToISID.Find(ISMI))
		{
			EntryISID = *PrevISID;
		}
		else if (Manager && IISMComponentID > 0)
		{
			EntryISID = Manager->GetISIDFromISMI(IISMComponentID, ISMI);
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
// Legacy & Convenience API (Assembled Ephemerally Zero-Copy from APropManager)
// -----------------------------------------------------------------------------
int32 UInteractableISMComponent::AddInteractableInstance(const FTransform& InstanceTransform, const FInteractablePropData& InitialData)
{
	const int32 NewIndex = AddInstance(InstanceTransform, true);
	if (NewIndex != INDEX_NONE)
	{
		FPropPackedRuntimeData Packed;
		Packed.RemainingPortions = InitialData.RemainingPortions;
		Packed.Temperature = InitialData.CurrentTemperature;
		Packed.bIsGrabbable = InitialData.bIsGrabbable;
		if (PackedInstanceData.Num() <= NewIndex)
		{
			PackedInstanceData.SetNum(NewIndex + 1);
		}
		PackedInstanceData[NewIndex] = Packed;

		if (InitialData.ISID > 0)
		{
			BindISIDToISMI(InitialData.ISID, NewIndex);
		}
		UpdateInstanceCustomData(NewIndex, true);
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
	if (!IsValidInstance(InstanceIndex) || IsEmptyInstance(InstanceIndex))
	{
		return false;
	}

	APropManager* Manager = APropManager::Get(this);
	const int32 BoundISID = FindISID(InstanceIndex);

	if (Manager && BoundISID > 0)
	{
		if (const FISIDRuntimeIndex* Record = Manager->FindISIDData(BoundISID))
		{
			const FPropProductDefinition* ProductDef = Manager->FindProductDefinition(Record->ProductID);
			const FPropInstanceUniqueData* UniqueData = Manager->FindUniqueData(BoundISID);
			const FPropContentDefinition* ContentDef = (UniqueData && UniqueData->EventData.ContentID > 0)
				? Manager->FindContentDefinition(UniqueData->EventData.ContentID)
				: nullptr;

			OutData.SyncFromISIDRecord(*Record, ProductDef, ContentDef, UniqueData, Manager->WorldElapsedSeconds);
			OutData.InstanceId = InstanceIndex;
			OutData.IISMComponentID = IISMComponentID;
			OutData.ProductID = ProductID;
			return true;
		}
	}

	if (DefaultDataAsset)
	{
		OutData = CreatePropDataFromAsset(DefaultDataAsset, InstanceIndex);
	}
	else
	{
		OutData = FInteractablePropData();
		OutData.InstanceId = InstanceIndex;
		OutData.ProductID = ProductID;
		OutData.IISMComponentID = IISMComponentID;
		OutData.StaticMesh = GetStaticMesh();
	}

	OutData.ISID = BoundISID;
	if (PackedInstanceData.IsValidIndex(InstanceIndex))
	{
		const FPropPackedRuntimeData& Packed = PackedInstanceData[InstanceIndex];
		OutData.RemainingPortions = Packed.RemainingPortions;
		OutData.CurrentTemperature = Packed.Temperature;
		OutData.bIsGrabbable = Packed.bIsGrabbable;
		if (OutData.NumOfPortions > 0)
		{
			OutData.QuantityLevel = static_cast<float>(OutData.RemainingPortions) / static_cast<float>(OutData.NumOfPortions);
		}
		if (Manager)
		{
			OutData.AgeMinutes = FMath::Max(0.0f, static_cast<float>(Manager->GetCurrentWorldMinutes() - Packed.CreationMinute));
		}
	}

	return true;
}

bool UInteractableISMComponent::SetPropDataForInstance(int32 InstanceIndex, const FInteractablePropData& NewData)
{
	if (!IsValidInstance(InstanceIndex))
	{
		return false;
	}

	if (PackedInstanceData.Num() <= InstanceIndex)
	{
		PackedInstanceData.SetNum(GetInstanceCount());
	}
	PackedInstanceData[InstanceIndex].RemainingPortions = NewData.RemainingPortions;
	PackedInstanceData[InstanceIndex].Temperature = NewData.CurrentTemperature;
	PackedInstanceData[InstanceIndex].bIsGrabbable = NewData.bIsGrabbable;

	APropManager* Manager = APropManager::Get(this);
	const int32 BoundISID = FindISID(InstanceIndex);
	if (Manager && BoundISID > 0)
	{
		if (FISIDRuntimeIndex* Record = Manager->FindISIDData(BoundISID))
		{
			Record->PackedRuntimeData.RemainingPortions = NewData.RemainingPortions;
			Record->PackedRuntimeData.Temperature = NewData.CurrentTemperature;
			Record->PackedRuntimeData.bIsGrabbable = NewData.bIsGrabbable;
			Record->RuntimeData.RemainingPortions = NewData.RemainingPortions;
			Record->RuntimeData.Temperature = NewData.CurrentTemperature;
			if (FPropInstanceUniqueData* Unique = Manager->FindUniqueData(BoundISID))
			{
				Unique->ContinuousData.Temperature = NewData.CurrentTemperature;
				Unique->EventData.bIsGrabbable = NewData.bIsGrabbable;
				const int32 TotalPortions = FMath::Max(1, NewData.NumOfPortions);
				Unique->EventData.ContentLevel = FMath::Clamp(static_cast<float>(NewData.RemainingPortions) / static_cast<float>(TotalPortions), 0.0f, 1.0f);
			}
		}
	}

	UpdateInstanceCustomData(InstanceIndex, true);
	return true;
}

bool UInteractableISMComponent::ConsumeInstancePortions(int32 InstanceIndex, int32 SipsToConsume, AActor* ConsumerActor, int32& OutSipsConsumed)
{
	OutSipsConsumed = 0;
	if (!IsValidInstance(InstanceIndex) || IsEmptyInstance(InstanceIndex))
	{
		return false;
	}

	APropManager* Manager = APropManager::Get(this);
	const int32 BoundISID = FindISID(InstanceIndex);

	if (Manager && BoundISID > 0)
	{
		if (const FPropInstanceUniqueData* UniqueData = Manager->FindUniqueData(BoundISID))
		{
			// Sealed cap prevents drinking/consuming from an ISM bottle until opened
			if (UniqueData->EventData.CapSealed > 0.0f)
			{
				return false;
			}
		}
	}

	FInteractablePropData Data;
	if (!GetPropDataForInstance(InstanceIndex, Data) || Data.IsEmpty())
	{
		return false;
	}

	OutSipsConsumed = Data.ConsumePortions(SipsToConsume);
	if (OutSipsConsumed <= 0) return false;

	if (PackedInstanceData.IsValidIndex(InstanceIndex))
	{
		PackedInstanceData[InstanceIndex].RemainingPortions = Data.RemainingPortions;
	}

	if (Manager && BoundISID > 0)
	{
		if (const FPropInstanceUniqueData* ExistingUnique = Manager->FindUniqueData(BoundISID))
		{
			FPropEventSyncDelta Delta;
			Delta.ISID = BoundISID;
			Delta.SetFlag(EPropEventDirtyMask::ContentLevel);
			Delta.ContentLevel = FMath::Max(0.0f, Data.QuantityLevel);
			if (Delta.ContentLevel <= KINDA_SMALL_NUMBER)
			{
				Delta.ContentLevel = 0.0f;
				Delta.SetFlag(EPropEventDirtyMask::ContentID);
				Delta.ContentID = 0;
			}
			Manager->SubmitInstanceEventUpdate(BoundISID, Delta);
		}
		else
		{
			UpdateInstanceCustomData(InstanceIndex, true);
		}
	}
	else
	{
		UpdateInstanceCustomData(InstanceIndex, true);
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
	if (!IsValidInstance(InstanceIndex) || DeltaSeconds <= 0.0f)
	{
		return;
	}

	APropManager* Manager = APropManager::Get(this);
	const int32 BoundISID = FindISID(InstanceIndex);

	if (Manager && BoundISID > 0)
	{
		if (FPropInstanceUniqueData* Unique = Manager->FindUniqueData(BoundISID))
		{
			const FPropProductDefinition* ProductDef = Manager->FindProductDefinition(ProductID);
			const FPropContentDefinition* ContentDef = (Unique->EventData.ContentID > 0)
				? Manager->FindContentDefinition(Unique->EventData.ContentID)
				: nullptr;

			const float RateMult = (AmbientTemperature >= Unique->ContinuousData.Temperature)
				? (ProductDef ? ProductDef->HeatingRateMultiplier : 1.0f)
				: (ProductDef ? ProductDef->CoolingRateMultiplier : 1.0f);
			const float SpecificHeat = (ContentDef && Unique->EventData.ContentLevel > KINDA_SMALL_NUMBER)
				? FMath::Max(0.1f, ContentDef->SpecificHeatCapacity)
				: 1.0f;
			const float Conductivity = ProductDef ? ProductDef->ThermalConductivity : 0.05f;
			const float Mass = ProductDef ? FMath::Max(0.01f, ProductDef->ThermalMass) : 1.0f;
			const float K = (Conductivity * RateMult) / (Mass * SpecificHeat);

			Unique->ContinuousData.Temperature = AmbientTemperature + (Unique->ContinuousData.Temperature - AmbientTemperature) * FMath::Exp(-K * DeltaSeconds);
			Unique->ContinuousData.LastUpdatedTime = Manager->WorldElapsedSeconds;

			if (PackedInstanceData.IsValidIndex(InstanceIndex))
			{
				PackedInstanceData[InstanceIndex].Temperature = Unique->ContinuousData.Temperature;
			}
			UpdateInstanceCustomDataDirect(InstanceIndex, ProductDef, ContentDef, Unique, Manager->WorldElapsedSeconds, true);
			return;
		}
	}

	if (PackedInstanceData.IsValidIndex(InstanceIndex))
	{
		const float Alpha = FMath::Clamp(DeltaSeconds * 0.015f, 0.0f, 1.0f);
		PackedInstanceData[InstanceIndex].Temperature = FMath::Lerp(PackedInstanceData[InstanceIndex].Temperature, AmbientTemperature, Alpha);
		UpdateInstanceCustomData(InstanceIndex, true);
	}
}

void UInteractableISMComponent::SetInstanceSimulationState(int32 InstanceIndex, EPhysicsSimulationState NewState)
{
	APropManager* Manager = APropManager::Get(this);
	const int32 BoundISID = FindISID(InstanceIndex);
	if (Manager && BoundISID > 0)
	{
		if (FISIDRuntimeIndex* Record = Manager->FindISIDData(BoundISID))
		{
			Record->State = ConvertPhysicsStateToPropState(NewState);
		}
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

	FVector RawImpulse = FVector::ZeroVector;
	const FPropProductDefinition* Definition = Manager ? Manager->FindProductDefinition(ProductID) : nullptr;
	const bool bBottle = Definition && Definition->CustomActiveActorClass
		&& Definition->CustomActiveActorClass->IsChildOf(AColaPropActor::StaticClass());
	if (OtherComp && OtherComp->IsSimulatingPhysics())
	{
		// A bottle uses actual momentum. A heavy moving prop must remain stronger
		// than an equally fast light object when waking its resting ISM.
		RawImpulse = bBottle
			? (!NormalImpulse.IsNearlyZero() ? NormalImpulse : OtherComp->GetComponentVelocity() * OtherComp->GetMass() * .65f)
			: OtherComp->GetComponentVelocity() * .65f;
	}
	else if (OtherActor)
	{
		RawImpulse = OtherActor->GetVelocity() * 0.55f;
	}

	if (RawImpulse.IsNearlyZero() && !NormalImpulse.IsNearlyZero())
	{
		RawImpulse = NormalImpulse * (bBottle ? 1.f : .05f);
	}

	// Clamp physics kick separately so bottles topple naturally while preserving unclamped RawImpulse for impact damage
	FVector SafeImpulse = RawImpulse;
	SafeImpulse.Z = FMath::Clamp(SafeImpulse.Z, -40.0f, 35.0f);
	SafeImpulse = SafeImpulse.GetClampedToMaxSize(220.0f);

	// Bottles need the same mass-aware momentum on both peers. Other props retain their legacy kick.
	if (Manager && CompOwner && CompOwner->HasAuthority())
	{
		return Manager->SendISMHitByICIDToPropManager(
			IISMComponentID,
			InISMI,
			Hit.ImpactPoint,
			Hit.ImpactNormal,
			Hit.Normal,
			RawImpulse,
			OtherActor);
	}

	return SetInstanceUnsettledByISMI(InISMI, bBottle ? RawImpulse : SafeImpulse, Hit.ImpactPoint);
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
	if (PackedInstanceData.IsValidIndex(InstanceIndex))
	{
		PackedInstanceData[InstanceIndex].bIsGrabbable = bNewGrabbable;
	}
	APropManager* Manager = APropManager::Get(this);
	const int32 BoundISID = FindISID(InstanceIndex);
	if (Manager && BoundISID > 0)
	{
		if (FPropInstanceUniqueData* Unique = Manager->FindUniqueData(BoundISID))
		{
			Unique->EventData.bIsGrabbable = bNewGrabbable;
		}
		if (FISIDRuntimeIndex* Record = Manager->FindISIDData(BoundISID))
		{
			Record->PackedRuntimeData.bIsGrabbable = bNewGrabbable;
		}
	}
}

void UInteractableISMComponent::SyncDatabaseWithInstanceCount()
{
	const int32 NumInstances = GetInstanceCount();
	if (PackedInstanceData.Num() < NumInstances)
	{
		const int32 PrevPackedNum = PackedInstanceData.Num();
		PackedInstanceData.SetNum(NumInstances);
		for (int32 i = PrevPackedNum; i < NumInstances; ++i)
		{
			if (DefaultDataAsset)
			{
				PackedInstanceData[i].RemainingPortions = DefaultDataAsset->NumOfPortions;
				PackedInstanceData[i].Temperature = DefaultDataAsset->ConsumableTemperature;
				PackedInstanceData[i].bIsGrabbable = true;
			}
		}
	}
	else if (PackedInstanceData.Num() > NumInstances)
	{
		PackedInstanceData.SetNum(NumInstances);
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
