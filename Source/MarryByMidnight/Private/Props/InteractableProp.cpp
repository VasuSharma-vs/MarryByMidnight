#include "Props/InteractableProp.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractablePropManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Components/TextRenderComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Characters/NPCCharacter.h"
#include "Components/NPCPersonalityComponent.h"
#include "Components/NPCNeedsComponent.h"
#include "Components/NPCMentalStateComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Components/ASMComponent.h"

AActivePropActor::AActivePropActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickInterval = 0.05f;
	bReplicates = false;
	bAlwaysRelevant = false;
	SetReplicateMovement(false);
	bHasNotifiedBeforeReturnToRest = true;

	PropMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PropMesh"));
	SetRootComponent(PropMesh);
	PropMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	PropMesh->SetGenerateOverlapEvents(false);
	MeshComponent = PropMesh;

	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalMeshComponent"));
	SkeletalMeshComponent->SetupAttachment(PropMesh);
	SkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkeletalMeshComponent->SetGenerateOverlapEvents(false);
	SkeletalMeshComponent->SetVisibility(false);

	DebugText = nullptr;

	PhysicsSimComponent = CreateDefaultSubobject<UStaticMeshPhysicsSimulationComponent>(TEXT("PhysicsSimComponent"));
	PhysicsSimComponent->TargetMesh = PropMesh;
	PhysicsSimComponent->InitialState = EPhysicsSimulationState::AtRest;

	// Default temperature values
	bEnableTemperatureUpdate = false;
	ConsumableTemperature = 4.0f;
	CurrentTemperature = 4.0f;
	TemperatureDopamineBonus = 0.5f;
	ThermalExchangeRate = 0.015f;
	AmbientRoomTemperature = 21.0f;

	// Default expiry values
	bHasExpiry = true;
	ExpiryLifetimeMinutes = 60.0f;
	AgeMinutes = 0.0f;
	SicknessLevel = 35.0f;
	ExpiredDopaminePenalty = 25.0f;

	// Default taste profile (Cold sweet beverage)
	TasteProfile.Sweet = 0.85f;
	TasteProfile.Healthy = 0.3f;
	TasteProfile.Luxury = 0.2f;

	InitializeDefaultDrinkStimuli();
}

void AActivePropActor::InitializeDefaultDrinkStimuli()
{
	StimuliPerPortion.Empty();
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Power, 10.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Hunger, -5.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Thirst, -25.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Dopamine, 5.0f));
}

void AActivePropActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	MeshComponent = PropMesh;

	if (PropDataAsset)
	{
		ApplyDataAsset(PropDataAsset);
	}
	else if (ProductID > 0)
	{
		if (APropManager* Manager = APropManager::Get(this))
		{
			if (const FPropProductDefinition* Def = Manager->ProductDefinitions.Find(ProductID))
			{
				ApplyProductDefinition(*Def);
			}
		}
	}
	else
	{
		UpdateDebugBillboard();
	}
}

void AActivePropActor::SetPropDataAsset(UInteractablePropDataAsset* InDataAsset)
{
	PropDataAsset = InDataAsset;
	if (InDataAsset)
	{
		ApplyDataAsset(InDataAsset);
	}
}

void AActivePropActor::ApplyDataAsset(const UInteractablePropDataAsset* InDataAsset)
{
	const UInteractablePropDataAsset* AssetToApply = InDataAsset ? InDataAsset : PropDataAsset.Get();
	if (!AssetToApply)
	{
		return;
	}

	PropDataAsset = const_cast<UInteractablePropDataAsset*>(AssetToApply);

	// Select Skeletal Mesh if present, otherwise fall back to Static Mesh
	if (AssetToApply->SkeletalMesh)
	{
		if (SkeletalMeshComponent)
		{
			if (SkeletalMeshComponent->GetSkeletalMeshAsset() != AssetToApply->SkeletalMesh)
			{
				SkeletalMeshComponent->SetSkinnedAssetAndUpdate(AssetToApply->SkeletalMesh);
			}
			const int32 NumSkelSlots = AssetToApply->SkeletalMesh->GetMaterials().Num();
			for (int32 MatIdx = 0; MatIdx < NumSkelSlots; ++MatIdx)
			{
				UMaterialInterface* TargetMat = nullptr;
				if (AssetToApply->MaterialOverrides.IsValidIndex(MatIdx) && IsValid(AssetToApply->MaterialOverrides[MatIdx]))
				{
					TargetMat = AssetToApply->MaterialOverrides[MatIdx];
				}
				else if (AssetToApply->SkeletalMesh->GetMaterials().IsValidIndex(MatIdx))
				{
					TargetMat = AssetToApply->SkeletalMesh->GetMaterials()[MatIdx].MaterialInterface.Get();
				}

				if (TargetMat && SkeletalMeshComponent->GetMaterial(MatIdx) != TargetMat)
				{
					SkeletalMeshComponent->SetMaterial(MatIdx, TargetMat);
				}
			}

			const int32 ExistingSkelMats = SkeletalMeshComponent->GetNumMaterials();
			for (int32 ExcessIdx = NumSkelSlots; ExcessIdx < ExistingSkelMats; ++ExcessIdx)
			{
				SkeletalMeshComponent->SetMaterial(ExcessIdx, nullptr);
			}
			SkeletalMeshComponent->SetVisibility(true);
		}

		if (PropMesh)
		{
			if (AssetToApply->StaticMesh && PropMesh->GetStaticMesh() != AssetToApply->StaticMesh)
			{
				PropMesh->SetStaticMesh(AssetToApply->StaticMesh);
			}
			if (AssetToApply->StaticMesh)
			{
				const int32 NumStaticSlots = AssetToApply->StaticMesh->GetStaticMaterials().Num();
				for (int32 MatIdx = 0; MatIdx < NumStaticSlots; ++MatIdx)
				{
					UMaterialInterface* TargetMat = nullptr;
					if (AssetToApply->MaterialOverrides.IsValidIndex(MatIdx) && IsValid(AssetToApply->MaterialOverrides[MatIdx]))
					{
						TargetMat = AssetToApply->MaterialOverrides[MatIdx];
					}
					else
					{
						TargetMat = AssetToApply->StaticMesh->GetMaterial(MatIdx);
					}

					if (TargetMat && PropMesh->GetMaterial(MatIdx) != TargetMat)
					{
						PropMesh->SetMaterial(MatIdx, TargetMat);
					}
				}

				const int32 ExistingStaticMats = PropMesh->GetNumMaterials();
				for (int32 ExcessIdx = NumStaticSlots; ExcessIdx < ExistingStaticMats; ++ExcessIdx)
				{
					PropMesh->SetMaterial(ExcessIdx, nullptr);
				}
			}
			PropMesh->SetVisibility(false);
		}
	}
	else
	{
		if (SkeletalMeshComponent)
		{
			SkeletalMeshComponent->SetVisibility(false);
			SkeletalMeshComponent->SetSkinnedAssetAndUpdate(nullptr);
		}

		if (PropMesh)
		{
			if (AssetToApply->StaticMesh && PropMesh->GetStaticMesh() != AssetToApply->StaticMesh)
			{
				PropMesh->SetStaticMesh(AssetToApply->StaticMesh);
			}
			if (AssetToApply->StaticMesh)
			{
				const int32 NumStaticSlots = AssetToApply->StaticMesh->GetStaticMaterials().Num();
				for (int32 MatIdx = 0; MatIdx < NumStaticSlots; ++MatIdx)
				{
					UMaterialInterface* TargetMat = nullptr;
					if (AssetToApply->MaterialOverrides.IsValidIndex(MatIdx) && IsValid(AssetToApply->MaterialOverrides[MatIdx]))
					{
						TargetMat = AssetToApply->MaterialOverrides[MatIdx];
					}
					else
					{
						TargetMat = AssetToApply->StaticMesh->GetMaterial(MatIdx);
					}

					if (TargetMat && PropMesh->GetMaterial(MatIdx) != TargetMat)
					{
						PropMesh->SetMaterial(MatIdx, TargetMat);
					}
				}

				const int32 ExistingStaticMats = PropMesh->GetNumMaterials();
				for (int32 ExcessIdx = NumStaticSlots; ExcessIdx < ExistingStaticMats; ++ExcessIdx)
				{
					PropMesh->SetMaterial(ExcessIdx, nullptr);
				}
			}
			PropMesh->SetVisibility(true);
		}
	}

	ItemDisplayName = AssetToApply->ItemDisplayName;
	ConsumableType = AssetToApply->ConsumableType;
	Price = AssetToApply->Price;

	NumOfPortions = AssetToApply->NumOfPortions;
	RemainingPortions = NumOfPortions;
	QuantityLevel = (NumOfPortions > 0) ? 1.0f : 0.0f;

	bEnableTemperatureUpdate = AssetToApply->bEnableTemperatureUpdate;
	ConsumableTemperature = AssetToApply->ConsumableTemperature;
	CurrentTemperature = AssetToApply->ConsumableTemperature;
	TemperatureDopamineBonus = AssetToApply->TemperatureDopamineBonus;
	ThermalExchangeRate = AssetToApply->ThermalExchangeRate;
	Coldness = (ConsumableTemperature < 15.0f) ? 1.0f : 0.0f;

	bHasExpiry = AssetToApply->bHasExpiry;
	ExpiryLifetimeMinutes = AssetToApply->ExpiryLifetimeMinutes;
	AgeMinutes = 0.0f;
	SicknessLevel = AssetToApply->SicknessLevel;
	ExpiredDopaminePenalty = AssetToApply->ExpiredDopaminePenalty;
	ExpiryStimulusProfile = AssetToApply->ExpiryStimulusProfile;
	ExpiryFreshness = 1.0f;

	StimuliPerPortion = AssetToApply->StimuliPerPortion;
	TasteProfile = AssetToApply->BuildTasteVector(false);

	bIsGrabbable = AssetToApply->bCanGrab;

	if (PhysicsSimComponent)
	{
		PhysicsSimComponent->TargetMesh = PropMesh;
		if (AssetToApply->LinearVelocityTolerance > 0.0f)
		{
			PhysicsSimComponent->VelocityTolerance = AssetToApply->LinearVelocityTolerance;
		}
		if (AssetToApply->AngularVelocityTolerance > 0.0f)
		{
			PhysicsSimComponent->AngularVelocityTolerance = AssetToApply->AngularVelocityTolerance;
		}
		if (AssetToApply->UnsettledDelay > 0.0f)
		{
			PhysicsSimComponent->DefaultUnsettledDelay = AssetToApply->UnsettledDelay;
		}
	}

	PackedRuntimeData.RemainingPortions = RemainingPortions;
	PackedRuntimeData.Temperature = CurrentTemperature;
	PackedRuntimeData.bIsGrabbable = bIsGrabbable;

	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		if (APropManager* Mgr = PropManagerReference ? PropManagerReference.Get() : APropManager::Get(this))
		{
			int32 FoundPID = Mgr->FindProductIDByDataAsset(AssetToApply);
			if (FoundPID <= 0)
			{
				FoundPID = Mgr->RegisterProductFromDataAsset(AssetToApply);
			}
			if (FoundPID > 0)
			{
				ProductID = FoundPID;
				if (ISID > 0)
				{
					if (FISIDRuntimeIndex* Rec = Mgr->FindISIDData(ISID))
					{
						Rec->ProductID = FoundPID;
					}
				}
			}
		}
	}

	UpdateMaterialCustomPrimitiveData();
	UpdateDebugBillboard();
	BP_OnDataAssetApplied(AssetToApply);
}

void AActivePropActor::ApplyProductDefinition(const FPropProductDefinition& InProductDef)
{
	ProductID = InProductDef.ProductID;
	if (InProductDef.SourceDataAsset)
	{
		PropDataAsset = InProductDef.SourceDataAsset;
	}

	const UInteractablePropDataAsset* SourceAsset = InProductDef.SourceDataAsset ? InProductDef.SourceDataAsset.Get() : PropDataAsset.Get();
	const TArray<TObjectPtr<UMaterialInterface>>& SourceOverrides = (SourceAsset && SourceAsset->MaterialOverrides.Num() > 0)
		? SourceAsset->MaterialOverrides
		: InProductDef.MaterialOverrides;

	USkeletalMesh* TargetSkelMesh = InProductDef.SkeletalMesh ? InProductDef.SkeletalMesh.Get() : (SourceAsset ? SourceAsset->SkeletalMesh.Get() : nullptr);
	UStaticMesh* TargetStaticMesh = InProductDef.StaticMesh ? InProductDef.StaticMesh.Get() : (SourceAsset ? SourceAsset->StaticMesh.Get() : nullptr);

	// Select Skeletal Mesh if present, otherwise fall back to Static Mesh
	if (TargetSkelMesh)
	{
		if (SkeletalMeshComponent)
		{
			if (SkeletalMeshComponent->GetSkeletalMeshAsset() != TargetSkelMesh)
			{
				SkeletalMeshComponent->SetSkinnedAssetAndUpdate(TargetSkelMesh);
			}
			const int32 NumSkelSlots = TargetSkelMesh->GetMaterials().Num();
			for (int32 MatIdx = 0; MatIdx < NumSkelSlots; ++MatIdx)
			{
				UMaterialInterface* TargetMat = nullptr;
				if (SourceOverrides.IsValidIndex(MatIdx) && IsValid(SourceOverrides[MatIdx]))
				{
					TargetMat = SourceOverrides[MatIdx];
				}
				else if (TargetSkelMesh->GetMaterials().IsValidIndex(MatIdx))
				{
					TargetMat = TargetSkelMesh->GetMaterials()[MatIdx].MaterialInterface.Get();
				}

				if (TargetMat && SkeletalMeshComponent->GetMaterial(MatIdx) != TargetMat)
				{
					SkeletalMeshComponent->SetMaterial(MatIdx, TargetMat);
				}
			}

			const int32 ExistingSkelMats = SkeletalMeshComponent->GetNumMaterials();
			for (int32 ExcessIdx = NumSkelSlots; ExcessIdx < ExistingSkelMats; ++ExcessIdx)
			{
				SkeletalMeshComponent->SetMaterial(ExcessIdx, nullptr);
			}
			SkeletalMeshComponent->SetVisibility(true);
		}

		if (PropMesh)
		{
			if (TargetStaticMesh && PropMesh->GetStaticMesh() != TargetStaticMesh)
			{
				PropMesh->SetStaticMesh(TargetStaticMesh);
			}
			if (TargetStaticMesh)
			{
				const int32 NumStaticSlots = TargetStaticMesh->GetStaticMaterials().Num();
				for (int32 MatIdx = 0; MatIdx < NumStaticSlots; ++MatIdx)
				{
					UMaterialInterface* TargetMat = nullptr;
					if (SourceOverrides.IsValidIndex(MatIdx) && IsValid(SourceOverrides[MatIdx]))
					{
						TargetMat = SourceOverrides[MatIdx];
					}
					else
					{
						TargetMat = TargetStaticMesh->GetMaterial(MatIdx);
					}

					if (TargetMat && PropMesh->GetMaterial(MatIdx) != TargetMat)
					{
						PropMesh->SetMaterial(MatIdx, TargetMat);
					}
				}

				const int32 ExistingStaticMats = PropMesh->GetNumMaterials();
				for (int32 ExcessIdx = NumStaticSlots; ExcessIdx < ExistingStaticMats; ++ExcessIdx)
				{
					PropMesh->SetMaterial(ExcessIdx, nullptr);
				}
			}
			PropMesh->SetVisibility(false);
		}
	}
	else
	{
		if (SkeletalMeshComponent)
		{
			SkeletalMeshComponent->SetVisibility(false);
			SkeletalMeshComponent->SetSkinnedAssetAndUpdate(nullptr);
		}

		if (PropMesh)
		{
			if (TargetStaticMesh && PropMesh->GetStaticMesh() != TargetStaticMesh)
			{
				PropMesh->SetStaticMesh(TargetStaticMesh);
			}
			if (TargetStaticMesh)
			{
				const int32 NumStaticSlots = TargetStaticMesh->GetStaticMaterials().Num();
				for (int32 MatIdx = 0; MatIdx < NumStaticSlots; ++MatIdx)
				{
					UMaterialInterface* TargetMat = nullptr;
					if (SourceOverrides.IsValidIndex(MatIdx) && IsValid(SourceOverrides[MatIdx]))
					{
						TargetMat = SourceOverrides[MatIdx];
					}
					else
					{
						TargetMat = TargetStaticMesh->GetMaterial(MatIdx);
					}

					if (TargetMat && PropMesh->GetMaterial(MatIdx) != TargetMat)
					{
						PropMesh->SetMaterial(MatIdx, TargetMat);
					}
				}

				const int32 ExistingStaticMats = PropMesh->GetNumMaterials();
				for (int32 ExcessIdx = NumStaticSlots; ExcessIdx < ExistingStaticMats; ++ExcessIdx)
				{
					PropMesh->SetMaterial(ExcessIdx, nullptr);
				}
			}
			PropMesh->SetVisibility(true);
		}
	}

	ItemDisplayName = InProductDef.DisplayName;
	ConsumableType = InProductDef.ConsumableProfile.ConsumableType;
	Price = InProductDef.ConsumableProfile.Price;
	NumOfPortions = InProductDef.ConsumableProfile.NumOfPortions;
	RemainingPortions = InProductDef.ConsumableProfile.NumOfPortions;
	QuantityLevel = (NumOfPortions > 0) ? static_cast<float>(RemainingPortions) / static_cast<float>(NumOfPortions) : 0.0f;
	bDestroyWhenEmpty = InProductDef.ConsumableProfile.bDestroyWhenEmpty;

	bEnableTemperatureUpdate = InProductDef.TemperatureProfile.bEnableTemperatureUpdate;
	ConsumableTemperature = InProductDef.TemperatureProfile.ConsumableTemperature;
	CurrentTemperature = InProductDef.TemperatureProfile.ConsumableTemperature;
	TemperatureDopamineBonus = InProductDef.TemperatureProfile.TemperatureDopamineBonus;
	ThermalExchangeRate = InProductDef.TemperatureProfile.ThermalExchangeRate;

	bHasExpiry = InProductDef.ExpiryProfile.bHasExpiry;
	ExpiryLifetimeMinutes = InProductDef.ExpiryProfile.ExpiryLifetimeMinutes;
	SicknessLevel = InProductDef.ExpiryProfile.SicknessLevel;
	ExpiredDopaminePenalty = InProductDef.ExpiryProfile.ExpiredDopaminePenalty;
	ExpiryStimulusProfile = InProductDef.ExpiryProfile.ExpiryStimulusProfile;

	StimuliPerPortion = InProductDef.StimulusProfile;
	TasteProfile = InProductDef.TasteProfile;

	if (PhysicsSimComponent)
	{
		if (InProductDef.PhysicsProfile.LinearVelocityTolerance > 0.0f)
		{
			PhysicsSimComponent->VelocityTolerance = InProductDef.PhysicsProfile.LinearVelocityTolerance;
		}
		if (InProductDef.PhysicsProfile.AngularVelocityTolerance > 0.0f)
		{
			PhysicsSimComponent->AngularVelocityTolerance = InProductDef.PhysicsProfile.AngularVelocityTolerance;
		}
		if (InProductDef.PhysicsProfile.UnsettledDelay > 0.0f)
		{
			PhysicsSimComponent->DefaultUnsettledDelay = InProductDef.PhysicsProfile.UnsettledDelay;
		}
		if (!InProductDef.PhysicsProfile.DefaultCollisionProfile.IsNone())
		{
			PhysicsSimComponent->DefaultCollisionProfile = InProductDef.PhysicsProfile.DefaultCollisionProfile;
		}
	}

	UpdateMaterialCustomPrimitiveData();
}

void AActivePropActor::UpdateMaterialCustomPrimitiveData()
{
	const float PortionRatio = (NumOfPortions > 0) ? FMath::Clamp(static_cast<float>(RemainingPortions) / static_cast<float>(NumOfPortions), 0.0f, 1.0f) : 0.0f;
	const float FrostFactor = FMath::Clamp((AmbientRoomTemperature - CurrentTemperature) / FMath::Max(1.0f, AmbientRoomTemperature), 0.0f, 1.0f);
	const float ExpiryAge = (bHasExpiry && ExpiryLifetimeMinutes > 0.0f) ? FMath::Clamp(AgeMinutes / ExpiryLifetimeMinutes, 0.0f, 1.0f) : 0.0f;

	if (PropMesh)
	{
		PropMesh->SetCustomPrimitiveDataFloat(0, PortionRatio);
		PropMesh->SetCustomPrimitiveDataFloat(1, FrostFactor);
		PropMesh->SetCustomPrimitiveDataFloat(2, ExpiryAge);
	}
	if (SkeletalMeshComponent)
	{
		SkeletalMeshComponent->SetCustomPrimitiveDataFloat(0, PortionRatio);
		SkeletalMeshComponent->SetCustomPrimitiveDataFloat(1, FrostFactor);
		SkeletalMeshComponent->SetCustomPrimitiveDataFloat(2, ExpiryAge);
	}
}

bool AActivePropActor::ChangeProduct(int32 NewProductID, bool bResetRuntimeData)
{
	if (NewProductID <= 0 || NewProductID == ProductID)
	{
		return false;
	}

	APropManager* Mgr = PropManagerReference ? PropManagerReference.Get() : APropManager::Get(this);
	if (Mgr)
	{
		if (!Mgr->ChangePropProduct(ISID, NewProductID, bResetRuntimeData))
		{
			return false;
		}
	}
	else
	{
		ProductID = NewProductID;
	}

	return true;
}

bool AActivePropActor::ChangeProductFromDataAsset(UInteractablePropDataAsset* NewDataAsset, bool bResetRuntimeData)
{
	if (!NewDataAsset)
	{
		return false;
	}

	APropManager* Mgr = PropManagerReference ? PropManagerReference.Get() : APropManager::Get(this);
	if (Mgr)
	{
		int32 TargetPID = Mgr->FindProductIDByDataAsset(NewDataAsset);
		if (TargetPID <= 0)
		{
			TargetPID = Mgr->RegisterProductFromDataAsset(NewDataAsset);
		}
		if (TargetPID > 0)
		{
			return ChangeProduct(TargetPID, bResetRuntimeData);
		}
	}
	else
	{
		ApplyDataAsset(NewDataAsset);
		return true;
	}

	return false;
}

void AActivePropActor::InitializeFromISIDRecord(const FISIDRuntimeIndex& Record, const FPropProductDefinition* ProductDef, APropManager* InManager)
{
	PropManagerReference = InManager;
	MeshComponent = PropMesh;

	if (ProductDef)
	{
		ApplyProductDefinition(*ProductDef);
	}
	else if (PropDataAsset && PropMesh && !PropMesh->GetStaticMesh())
	{
		ApplyDataAsset(PropDataAsset);
	}

	// Copy ISID, PID, State, Runtime Data, and Version (Section 7.2 Step 6)
	ISID = Record.ISID;
	if (Record.ProductID > 0)
	{
		ProductID = Record.ProductID;
	}
	State = Record.State;
	Version = Record.Version;
	PackedRuntimeData = Record.PackedRuntimeData;
	RuntimeData = Record.RuntimeData;

	InteractionState = Record.RuntimeData.InteractionState;
	InteractionOwner = Record.RuntimeData.InteractionOwner.Get();
	bIsHeld = (Record.State == EPropState::Held);

	RemainingPortions = Record.PackedRuntimeData.RemainingPortions;
	if (ProductDef && ProductDef->ConsumableProfile.NumOfPortions <= 0)
	{
		RemainingPortions = 0;
	}
	else if (NumOfPortions <= 0)
	{
		RemainingPortions = 0;
	}
	CurrentTemperature = Record.PackedRuntimeData.Temperature;
	bIsGrabbable = Record.PackedRuntimeData.bIsGrabbable;
	QuantityLevel = (NumOfPortions > 0) ? static_cast<float>(RemainingPortions) / static_cast<float>(NumOfPortions) : 0.0f;
	AgeMinutes = FMath::Max(0.0f, static_cast<float>(Record.PackedRuntimeData.LastUpdatedMinute - Record.PackedRuntimeData.CreationMinute));
	if (bHasExpiry && ExpiryLifetimeMinutes > 0.0f)
	{
		ExpiryFreshness = FMath::Clamp(1.0f - (AgeMinutes / ExpiryLifetimeMinutes), 0.0f, 1.0f);
	}

	UpdateDebugBillboard();
}

void AActivePropActor::WriteToISIDRecord(FISIDRuntimeIndex& InOutRecord) const
{
	// Read ASM transform, runtime data, state, velocity, and interaction info (Section 7.3 Step 13 & 14)
	InOutRecord.ActiveTransform = GetActorTransform();
	InOutRecord.State = State;
	InOutRecord.Version = FMath::Max(InOutRecord.Version, Version);

	if (ProductID > 0)
	{
		InOutRecord.ProductID = ProductID;
	}

	InOutRecord.PackedRuntimeData = PackedRuntimeData;
	InOutRecord.PackedRuntimeData.RemainingPortions = RemainingPortions;
	InOutRecord.PackedRuntimeData.Temperature = CurrentTemperature;
	InOutRecord.PackedRuntimeData.bIsGrabbable = bIsGrabbable;

	InOutRecord.RuntimeData = RuntimeData;
	InOutRecord.RuntimeData.RemainingPortions = RemainingPortions;
	InOutRecord.RuntimeData.Temperature = CurrentTemperature;
	InOutRecord.RuntimeData.InteractionState = InteractionState;
	InOutRecord.RuntimeData.InteractionOwner = InteractionOwner;
}

void AActivePropActor::ActivateASMRepresentation(const FTransform& GameplayTransform, EPropState InitialPropState, bool bEnableReplication)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	const EPropState PrevPropState = State;
	bIsActive = true;
	bIsInterpolatingToRest = false;
	OnRestInterpComplete = nullptr;
	RestInterpAlpha = 0.0f;
	bHasNotifiedBeforeReturnToRest = false;
	bIsReplicated = bEnableReplication;
	State = InitialPropState;
	bIsHeld = (InitialPropState == EPropState::Held);
	bIsInMotion = (InitialPropState == EPropState::InMotion);

	FTransform SafeGameplayTransform = GameplayTransform;
	if (SafeGameplayTransform.GetScale3D().GetMax() < 0.005f)
	{
		SafeGameplayTransform.SetScale3D(FVector::OneVector);
	}

	// 1. Place actor at the ISM GameplayTransform while collision and physics are still disabled
	// so no OnComponentHit or physics contact can fire before OnBecomeActive.
	SetActorTransform(SafeGameplayTransform, false, nullptr, ETeleportType::TeleportPhysics);

	// Enable replication on Server before firing OnBecomeActive so that any replicated Blueprint properties
	// modified during OnBecomeActive are properly tracked by the network driver and replicated to clients.
	if (HasAuthority())
	{
		bReplicates = bEnableReplication;
		bAlwaysRelevant = bEnableReplication;
		SetReplicates(bEnableReplication);
		SetReplicateMovement(bEnableReplication);
	}
	else
	{
		bReplicates = false;
		bAlwaysRelevant = false;
	}

	// 2. Make visible and fire OnBecomeActive / OnActivatedFromISM FIRST (all data & transform are applied,
	// before collision, physics simulation, timers, or Tick run).
	SetActorHiddenInGame(false);
	if (SkeletalMeshComponent && SkeletalMeshComponent->GetSkeletalMeshAsset())
	{
		SkeletalMeshComponent->SetHiddenInGame(false, true);
		SkeletalMeshComponent->SetVisibility(true, true);
		SkeletalMeshComponent->MarkRenderStateDirty();

		if (PropMesh)
		{
			PropMesh->SetHiddenInGame(false, false);
			PropMesh->SetVisibility(false, false);
			PropMesh->BodyInstance.SetMaxDepenetrationVelocity(80.0f);
			PropMesh->SetLinearDamping(0.2f);
			PropMesh->SetAngularDamping(0.3f);
			PropMesh->MarkRenderStateDirty();
		}
	}
	else if (PropMesh)
	{
		if (SkeletalMeshComponent)
		{
			SkeletalMeshComponent->SetVisibility(false, true);
		}
		PropMesh->SetHiddenInGame(false, true);
		PropMesh->SetVisibility(true, true);
		PropMesh->BodyInstance.SetMaxDepenetrationVelocity(80.0f);
		PropMesh->SetLinearDamping(0.2f);
		PropMesh->SetAngularDamping(0.3f);
		PropMesh->MarkRenderStateDirty();
	}
	UpdateMaterialCustomPrimitiveData();

	const FTransform CurrentActiveTransform = GetActorTransform();
	BP_OnBecomeActive(ISID, ProductID, CurrentActiveTransform, InitialPropState);
	BP_OnActivatedFromISM(ISID, ProductID, InitialPropState);
	OnPropBecomeActive.Broadcast(this, ISID, ProductID, CurrentActiveTransform);

	// 3. Now enable collision, replication, tick, and physics simulation state
	if (!GetActorEnableCollision())
	{
		SetActorEnableCollision(true);
	}
	if (PropMesh && PropMesh->GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics)
	{
		PropMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	SetActorTickEnabled(true);
	if (HasAuthority())
	{
		bReplicates = bEnableReplication;
		bAlwaysRelevant = bEnableReplication;
		SetReplicateMovement(bEnableReplication);

		if (const UWorld* World = GetWorld())
		{
			if (!World->IsNetMode(NM_Client))
			{
				ForceNetUpdate();
			}
		}
	}

	if (PhysicsSimComponent)
	{
		PhysicsSimComponent->SetComponentTickEnabled(true);
		PhysicsSimComponent->OnPhysicsSimulationStateChanged.AddUniqueDynamic(this, &AActivePropActor::HandlePhysicsSimStateChanged);
		PhysicsSimComponent->SetPhysicsState(ConvertPropStateToPhysicsState(InitialPropState));
	}

	if (InitialPropState == EPropState::Held)
	{
		bIsHeld = true;
		bIsGrabbable = false;
		PackedRuntimeData.bIsGrabbable = false;
		if (PropMesh)
		{
			PropMesh->SetSimulatePhysics(false);
			PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
			PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
		}
	}

	if (InitialPropState != PrevPropState)
	{
		BP_OnPropStateChanged(InitialPropState, PrevPropState);
		OnActivePropStateChanged.Broadcast(this, InitialPropState, PrevPropState);
	}
}

void AActivePropActor::NotifyBeforeReturnToRestPosition()
{
	if (!bIsActive || bHasNotifiedBeforeReturnToRest)
	{
		return;
	}

	bHasNotifiedBeforeReturnToRest = true;
	const FTransform CurrentGameplayTransform = GetActorTransform();
	const int32 CurrentActiveISID = ISID;

	// Fire pre-return events while actor is still active and at its real world gameplay transform
	BP_OnBeforeReturnToRestPosition(CurrentActiveISID, ProductID, CurrentGameplayTransform);
	OnBeforeReturnToRestPosition.Broadcast(this, CurrentActiveISID, ProductID, CurrentGameplayTransform);

	if (State != EPropState::AtRest)
	{
		const EPropState PrevPropState = State;
		State = EPropState::AtRest;
		BP_OnPropStateChanged(EPropState::AtRest, PrevPropState);
		OnActivePropStateChanged.Broadcast(this, EPropState::AtRest, PrevPropState);
	}

	BP_OnReturnedToISM(CurrentActiveISID);
}

void AActivePropActor::DeactivateToPoolState(const FTransform& ResetTransform)
{
	// Run pre-return logic BEFORE stripping collision/physics and BEFORE moving to ResetTransform
	if (bIsActive)
	{
		NotifyBeforeReturnToRestPosition();
	}

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	bIsActive = false;
	bIsInterpolatingToRest = false;
	OnRestInterpComplete = nullptr;
	RestInterpAlpha = 0.0f;
	SetActorTickInterval(0.05f);
	bHasNotifiedBeforeReturnToRest = true;
	bIsHeld = false;
	bIsInMotion = false;
	bIsReplicated = false;
	bAlwaysRelevant = false;
	if (HasAuthority())
	{
		SetReplicates(false);
	}
	State = EPropState::AtRest;
	InteractionState = EPropInteractionLockState::Free;
	InteractionOwner = nullptr;
	LinearVelocity = FVector::ZeroVector;
	AngularVelocity = FVector::ZeroVector;

	if (PhysicsSimComponent)
	{
		PhysicsSimComponent->SetComponentTickEnabled(false);
		if (PropManagerReference)
		{
			PropManagerReference->UnregisterSettlingComponent(PhysicsSimComponent);
		}
		PhysicsSimComponent->bIsInMotion = false;
		PhysicsSimComponent->CurrentState = EPhysicsSimulationState::AtRest;
	}

	if (PropMesh)
	{
		if (PropMesh->IsSimulatingPhysics())
		{
			PropMesh->SetSimulatePhysics(false);
		}
		if (PropMesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		PropMesh->SetVisibility(false, true);
	}

	if (SkeletalMeshComponent)
	{
		SkeletalMeshComponent->SetVisibility(false, true);
	}

	if (GetActorEnableCollision())
	{
		SetActorEnableCollision(false);
	}
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);
	SetReplicateMovement(false);

	// Keep unit scale on pooled actors (visibility and collision are already disabled) so clients never inherit 0.0001 scale
	FTransform SafePoolTransform = ResetTransform;
	SafePoolTransform.SetScale3D(FVector::OneVector);

	if (!GetActorLocation().Equals(SafePoolTransform.GetLocation(), 50.0f) || GetActorScale3D().GetMax() < 0.005f)
	{
		SetActorTransform(SafePoolTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (const UWorld* World = GetWorld())
	{
		if (!World->IsNetMode(NM_Client) && HasAuthority())
		{
			ForceNetUpdate();
		}
	}
}

void AActivePropActor::ApplyAuthoritativePhysicsSync(const FActivePropPhysicsSync& SyncData)
{
	if (!PropMesh || bIsInterpolatingToRest) return;

	// Keep authoritative state in sync
	if (State != SyncData.State)
	{
		const EPropState PrevState = State;
		State = SyncData.State;
		if (PhysicsSimComponent)
		{
			PhysicsSimComponent->SetPhysicsState(ConvertPropStateToPhysicsState(SyncData.State));
		}
		BP_OnPropStateChanged(State, PrevState);
		OnActivePropStateChanged.Broadcast(this, State, PrevState);
	}

	if (State == EPropState::Held || SyncData.State == EPropState::Held)
	{
		bIsHeld = true;
		bIsGrabbable = false;
		PackedRuntimeData.bIsGrabbable = false;
		if (PropMesh)
		{
			if (PropMesh->IsSimulatingPhysics())
			{
				PropMesh->SetSimulatePhysics(false);
			}
			PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
			PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
		}
		return;
	}

	const FVector TargetLoc = SyncData.Location;
	const FRotator TargetRot = SyncData.Rotation;
	const FVector TargetLinVel = SyncData.LinearVelocity;
	const FVector TargetAngVel = SyncData.AngularVelocity;

	const FVector CurrentLoc = PropMesh->GetComponentLocation();
	const FVector PosError = TargetLoc - CurrentLoc;
	const float DistSq = PosError.SizeSquared();

	// If physics is not yet active on client (e.g. was pooled or resting), activate and snap once
	if (!PropMesh->IsSimulatingPhysics())
	{
		PropMesh->SetWorldLocationAndRotation(TargetLoc, TargetRot, false, nullptr, ETeleportType::TeleportPhysics);
		PropMesh->SetSimulatePhysics(true);
		PropMesh->SetPhysicsLinearVelocity(TargetLinVel);
		PropMesh->SetPhysicsAngularVelocityInDegrees(TargetAngVel);
		return;
	}

	// 1. Extreme desync (e.g. > 150 cm / 1.5 meters) -> Hard teleport to prevent wandering divergence
	if (DistSq > FMath::Square(150.0f))
	{
		PropMesh->SetWorldLocationAndRotation(TargetLoc, TargetRot, false, nullptr, ETeleportType::TeleportPhysics);
		PropMesh->SetPhysicsLinearVelocity(TargetLinVel);
		PropMesh->SetPhysicsAngularVelocityInDegrees(TargetAngVel);
		return;
	}

	// 2. Deadzone (error < 4 cm) -> Client physics is already spot-on; only lightly blend velocity
	if (DistSq < FMath::Square(4.0f))
	{
		const FVector CurrentVel = PropMesh->GetComponentVelocity();
		PropMesh->SetPhysicsLinearVelocity(FMath::VInterpTo(CurrentVel, TargetLinVel, 0.05f, 6.0f));
		return;
	}

	// 3. Velocity Nudge (5 cm - 150 cm) -> Spring-damper corrective velocity without teleporting Chaos
	const float CorrectionFactor = 6.0f; // Spring pull rate
	const FVector CorrectiveLinVel = (PosError * CorrectionFactor).GetClampedToMaxSize(250.0f);
	const FVector NewLinearVelocity = TargetLinVel + CorrectiveLinVel;
	PropMesh->SetPhysicsLinearVelocity(NewLinearVelocity);

	// Angular correction: find delta rotation quaternion
	const FQuat CurrentQuat = PropMesh->GetComponentQuat();
	const FQuat TargetQuat = TargetRot.Quaternion();
	const FQuat DeltaQuat = TargetQuat * CurrentQuat.Inverse();
	FVector Axis;
	float AngleRad;
	DeltaQuat.ToAxisAndAngle(Axis, AngleRad);
	const float AngleDeg = FMath::RadiansToDegrees(FMath::UnwindRadians(AngleRad));
	const FVector CorrectiveAngVel = (Axis * (AngleDeg * 4.0f)).GetClampedToMaxSize(360.0f);
	PropMesh->SetPhysicsAngularVelocityInDegrees(TargetAngVel + CorrectiveAngVel);
}

void AActivePropActor::StartInterpolateToRestTransform(const FTransform& InTargetTransform, float Duration, TFunction<void()> OnComplete)
{
	bIsInterpolatingToRest = true;
	StartRestTransform = GetActorTransform();
	TargetRestTransform = InTargetTransform;
	RestInterpDuration = FMath::Max(0.01f, Duration);
	RestInterpAlpha = 0.0f;
	OnRestInterpComplete = MoveTemp(OnComplete);

	// Disable physics and collision so Chaos physics does not fight the smooth kinematic glide
	if (PropMesh)
	{
		if (PropMesh->IsSimulatingPhysics())
		{
			PropMesh->SetSimulatePhysics(false);
		}
		if (PropMesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
	SetActorEnableCollision(false);

	// Tick every frame during interpolation for a smooth 60/120fps ease-out
	SetActorTickInterval(0.0f);
	SetActorTickEnabled(true);
}

int32 AActivePropActor::ConvertPlacedPropToISMIfAtRest(bool bForceConvert)
{
	if (!bForceConvert && State != EPropState::AtRest)
	{
		return 0;
	}

	APropManager* Manager = PropManagerReference ? PropManagerReference.Get() : APropManager::Get(this);
	if (!Manager)
	{
		return 0;
	}
	PropManagerReference = Manager;

	if (!Manager->IsProductRegistered(ProductID))
	{
		const int32 RegisteredPID = RegisterProductWithManager();
		if (RegisteredPID <= 0 && ProductID <= 0)
		{
			return 0;
		}
	}

	return Manager->ConvertPlacedActorToISM(this, bForceConvert);
}

int32 AActivePropActor::RegisterProductWithManager()
{
	APropManager* Manager = PropManagerReference ? PropManagerReference.Get() : APropManager::Get(this);
	if (!Manager)
	{
		return 0;
	}
	PropManagerReference = Manager;

	return Manager->RegisterProductFromActor(this);
}

bool AActivePropActor::IsProductRegistered() const
{
	const APropManager* Manager = PropManagerReference ? PropManagerReference.Get() : APropManager::Get(this);
	return Manager ? Manager->IsProductRegistered(ProductID) : false;
}

void AActivePropActor::QueueSelfForISMConversion()
{
	APropManager* Manager = PropManagerReference ? PropManagerReference.Get() : APropManager::Get(this);
	if (!Manager)
	{
		return;
	}
	PropManagerReference = Manager;

	// If a PropDataAsset is assigned, ensure ProductID is synchronized with the DataAsset
	if (PropDataAsset)
	{
		int32 AssetPID = Manager->FindProductIDByDataAsset(PropDataAsset);
		if (AssetPID <= 0)
		{
			AssetPID = Manager->RegisterProductFromDataAsset(PropDataAsset);
		}
		if (AssetPID > 0)
		{
			ProductID = AssetPID;
		}
	}
	else if (!Manager->IsProductRegistered(ProductID))
	{
		RegisterProductWithManager();
	}

	if (ASMID <= 0)
	{
		ASMID = Manager->AllocateASMID();
		Manager->RegisterProp(this);
	}

	Manager->QueuePlacedASMForConversion(ASMID);
}

void AActivePropActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	MeshComponent = PropMesh;

	if (PhysicsSimComponent)
	{
		PhysicsSimComponent->TargetMesh = PropMesh;
		PhysicsSimComponent->OnPhysicsSimulationStateChanged.AddUniqueDynamic(this, &AActivePropActor::HandlePhysicsSimStateChanged);
	}

	UpdateDebugBillboard();
}

void AActivePropActor::BeginPlay()
{
	Super::BeginPlay();

	MeshComponent = PropMesh;

	if (PhysicsSimComponent)
	{
		PhysicsSimComponent->TargetMesh = PropMesh;
		PhysicsSimComponent->OnPhysicsSimulationStateChanged.AddUniqueDynamic(this, &AActivePropActor::HandlePhysicsSimStateChanged);
	}

	if (PropDataAsset)
	{
		ApplyDataAsset(PropDataAsset);
	}

	if (APropManager* Manager = APropManager::Get(this))
	{
		PropManagerReference = Manager;
		Manager->EnsureLevelPropsInitialized();
		Manager->RegisterProp(this);
	}

	// If this actor was placed in the level at rest, queue it for smooth batched conversion to ISM (10-20 per frame)
	if (bAutoConvertToISMIfAtRest && State == EPropState::AtRest && ISID <= 0 && GetOwner() != PropManagerReference)
	{
		QueueSelfForISMConversion();
	}

	if (!HasAuthority())
	{
		if (bAutoConvertToISMIfAtRest && State == EPropState::AtRest && ISID <= 0)
		{
			// Placed actor at rest on client: Server will authoritatively convert to ISM and sync.
			// Deactivate and park to pool immediately on client so it doesn't duplicate the ISM instance.
			const FTransform ParkTransform = (ASMID > 0 && PropManagerReference)
				? PropManagerReference->GetResetTransformForSlot(ASMID)
				: FTransform(FQuat::Identity, FVector(0.0f, 0.0f, -100000.0f));
			DeactivateToPoolState(ParkTransform);
			if (APropManager* Manager = APropManager::Get(this))
			{
				Manager->RegisterProp(this);
				if (Manager->ASMComponent)
				{
					Manager->ASMComponent->ASMActors.AddUnique(this);
					if (ASMID > 0)
					{
						Manager->ASMComponent->EmptyASMIDs.AddUnique(ASMID);
					}
				}
				if (ASMID > 0)
				{
					Manager->EmptyASMIDs.AddUnique(ASMID);
				}
			}
		}
		else if (bIsActive && ISID > 0)
		{
			OnRep_IsActive();
		}
		else if (!bIsActive)
		{
			SetActorHiddenInGame(true);
			SetActorEnableCollision(false);
			SetActorTickEnabled(false);
			if (PropMesh)
			{
				PropMesh->SetSimulatePhysics(false);
				PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				PropMesh->SetVisibility(false, true);
			}
		}
	}

	UpdateDebugBillboard();
}

void AActivePropActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PhysicsSimComponent)
	{
		PhysicsSimComponent->OnPhysicsSimulationStateChanged.RemoveDynamic(this, &AActivePropActor::HandlePhysicsSimStateChanged);
	}

	if (APropManager* Manager = APropManager::Get(this))
	{
		Manager->UnregisterProp(this);
	}

	Super::EndPlay(EndPlayReason);
}

void AActivePropActor::HandlePhysicsSimStateChanged(EPhysicsSimulationState NewSimState, EPhysicsSimulationState PrevSimState)
{
	if (bIsInMotion && NewSimState != EPhysicsSimulationState::InMotion && NewSimState != EPhysicsSimulationState::Held)
	{
		return;
	}

	const EPropState PrevPropState = State;
	const EPropState TargetPropState = ConvertPhysicsStateToPropState(NewSimState);
	bIsHeld = (TargetPropState == EPropState::Held);

	if (TargetPropState == EPropState::Held)
	{
		bIsInMotion = false;
		bIsGrabbable = false;
		PackedRuntimeData.bIsGrabbable = false;
		if (PropMesh)
		{
			PropMesh->SetSimulatePhysics(false);
			PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
			PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
		}
	}
	else if (TargetPropState == EPropState::InMotion)
	{
		bIsInMotion = true;
	}
	else if (TargetPropState == EPropState::Settling || TargetPropState == EPropState::AtRest)
	{
		bIsInMotion = false;
	}

	// When an active ASM settles back to AtRest on the server, ConvertASMToISM -> NotifyBeforeReturnToRestPosition
	// will fire BP_OnBeforeReturnToRestPosition and BP_OnPropStateChanged(AtRest, PrevPropState) before moving to ResetTransform.
	if (HasAuthority() && ISID > 0 && bIsActive && NewSimState == EPhysicsSimulationState::AtRest && PrevSimState != EPhysicsSimulationState::AtRest)
	{
		if (bIsHeld || bIsInMotion)
		{
			return;
		}
		if (!PropManagerReference)
		{
			PropManagerReference = APropManager::Get(this);
		}
		if (PropManagerReference)
		{
			if (const FISIDRuntimeIndex* Record = PropManagerReference->FindISIDData(ISID))
			{
				if (Record->ActiveActor && (Record->ActiveActor->bIsHeld || Record->ActiveActor->bIsInMotion))
				{
					return;
				}
			}
			PropManagerReference->ConvertASMToISM(static_cast<uint32>(ISID), EPropState::AtRest, true, true);
			return;
		}
	}

	State = TargetPropState;
	if (State != PrevPropState)
	{
		BP_OnPropStateChanged(State, PrevPropState);
		OnActivePropStateChanged.Broadcast(this, State, PrevPropState);
	}

	if (HasAuthority() && ISID > 0 && bIsActive)
	{
		if (!PropManagerReference)
		{
			PropManagerReference = APropManager::Get(this);
		}
		if (PropManagerReference)
		{
			PropManagerReference->SetPropState(ISID, State);
		}
	}
}

void AActivePropActor::UpdateTemperature(float AmbientTemp, float DeltaSeconds)
{
	if (!bEnableTemperatureUpdate) return;

	if (ThermalExchangeRate > 0.0f && FMath::Abs(CurrentTemperature - AmbientTemp) > 0.05f)
	{
		const float TempDelta = (AmbientTemp - CurrentTemperature) * ThermalExchangeRate * DeltaSeconds;
		CurrentTemperature += TempDelta;
	}

	if (bHasExpiry)
	{
		AgeMinutes += (DeltaSeconds / 60.0f);
		if (ExpiryLifetimeMinutes > 0.0f)
		{
			ExpiryFreshness = FMath::Clamp(1.0f - (AgeMinutes / ExpiryLifetimeMinutes), 0.0f, 1.0f);
		}
	}

	if (CurrentTemperature < AmbientTemp)
	{
		Coldness = FMath::Clamp((AmbientTemp - CurrentTemperature) / 17.0f, 0.0f, 1.0f);
	}
	else
	{
		Coldness = 0.0f;
	}

	PackedRuntimeData.Temperature = CurrentTemperature;
	RuntimeData.Temperature = CurrentTemperature;
}

void AActivePropActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bIsActive) return;

	if (bIsInterpolatingToRest)
	{
		RestInterpAlpha += (DeltaSeconds / RestInterpDuration);
		if (RestInterpAlpha >= 1.0f)
		{
			RestInterpAlpha = 1.0f;
			bIsInterpolatingToRest = false;
			SetActorTransform(TargetRestTransform, false, nullptr, ETeleportType::TeleportPhysics);

			TFunction<void()> CompletionCallback = MoveTemp(OnRestInterpComplete);
			OnRestInterpComplete = nullptr;
			if (CompletionCallback)
			{
				CompletionCallback();
			}
			return;
		}

		// Smooth ease-out interpolation for location, rotation (quaternion slerp via FTransform::Blend), and scale
		const float EasedAlpha = FMath::InterpEaseOut(0.0f, 1.0f, RestInterpAlpha, 2.0f);
		FTransform BlendedTransform;
		BlendedTransform.Blend(StartRestTransform, TargetRestTransform, EasedAlpha);
		SetActorTransform(BlendedTransform, false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}

	if (PropMesh && PropMesh->IsSimulatingPhysics())
	{
		LinearVelocity = PropMesh->GetComponentVelocity();
		AngularVelocity = PropMesh->GetPhysicsAngularVelocityInDegrees();
	}

	if (!bEnableTemperatureUpdate && HasAuthority())
	{
		if (bHasExpiry)
		{
			AgeMinutes += (DeltaSeconds / 60.0f);
			if (ExpiryLifetimeMinutes > 0.0f)
			{
				ExpiryFreshness = FMath::Clamp(1.0f - (AgeMinutes / ExpiryLifetimeMinutes), 0.0f, 1.0f);
			}
		}
	}
}

void AActivePropActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AActivePropActor, ASMID);
	DOREPLIFETIME(AActivePropActor, ISID);
	DOREPLIFETIME(AActivePropActor, ProductID);
	DOREPLIFETIME(AActivePropActor, State);
	DOREPLIFETIME(AActivePropActor, Version);
	DOREPLIFETIME(AActivePropActor, InteractionState);
	DOREPLIFETIME(AActivePropActor, InteractionOwner);
	DOREPLIFETIME(AActivePropActor, LinearVelocity);
	DOREPLIFETIME(AActivePropActor, AngularVelocity);
	DOREPLIFETIME(AActivePropActor, bIsActive);
	DOREPLIFETIME(AActivePropActor, bIsHeld);
	DOREPLIFETIME(AActivePropActor, bIsInMotion);
}

void AActivePropActor::OnRep_ASMID()
{
	if (!PropManagerReference)
	{
		PropManagerReference = APropManager::Get(this);
	}
	if (PropManagerReference && ASMID > 0)
	{
		PropManagerReference->RegisterProp(this);
	}
}

void AActivePropActor::OnRep_ISID()
{
	if (!PropManagerReference)
	{
		PropManagerReference = APropManager::Get(this);
	}
	if (bIsActive && ISID > 0)
	{
		const bool bHasMesh = (PropMesh && PropMesh->GetStaticMesh() != nullptr) || (SkeletalMeshComponent && SkeletalMeshComponent->GetSkeletalMeshAsset() != nullptr);
		// If this ASM was already locally promoted on the Client for this exact ISID, do not re-run activation or snap its physics!
		if (!bHasNotifiedBeforeReturnToRest && !IsHidden() && GetActorEnableCollision() && GetActorLocation().Z > -50000.0f && bHasMesh)
		{
			if (PropManagerReference && PropManagerReference->GetActiveActorFromISID(ISID) == this)
			{
				return;
			}
		}
		OnRep_IsActive();
	}
	else if (!bIsActive || ISID <= 0)
	{
		OnRep_IsActive();
	}
}

void AActivePropActor::OnRep_State(EPropState PreviousState)
{
	if (bIsActive && ISID > 0 && (bHasNotifiedBeforeReturnToRest || !GetActorEnableCollision() || IsHidden()))
	{
		OnRep_IsActive();
	}

	if (State == EPropState::Held)
	{
		bIsHeld = true;
		bIsGrabbable = false;
		PackedRuntimeData.bIsGrabbable = false;
		if (PropMesh)
		{
			PropMesh->SetSimulatePhysics(false);
			PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
			PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
		}
	}
	else if (PreviousState == EPropState::Held)
	{
		bIsHeld = false;
	}

	if (bIsActive && PhysicsSimComponent)
	{
		const EPhysicsSimulationState TargetSimState = ConvertPropStateToPhysicsState(State);
		if (PhysicsSimComponent->CurrentState != TargetSimState)
		{
			PhysicsSimComponent->SetPhysicsState(TargetSimState);
		}
	}

	if (State != PreviousState)
	{
		BP_OnPropStateChanged(State, PreviousState);
		OnActivePropStateChanged.Broadcast(this, State, PreviousState);
	}
}

void AActivePropActor::SetPropState(EPropState NewState)
{
	const EPropState PrevState = State;
	if (PrevState == NewState)
	{
		return;
	}

	if (bIsInMotion && NewState != EPropState::InMotion && NewState != EPropState::Held)
	{
		return;
	}

	State = NewState;
	if (NewState == EPropState::Held)
	{
		bIsHeld = true;
		bIsGrabbable = false;
		PackedRuntimeData.bIsGrabbable = false;
		if (PropMesh)
		{
			PropMesh->SetSimulatePhysics(false);
			PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
			PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
		}
	}
	else if (PrevState == EPropState::Held)
	{
		bIsHeld = false;
	}

	if (PhysicsSimComponent)
	{
		const EPhysicsSimulationState TargetSimState = ConvertPropStateToPhysicsState(NewState);
		if (PhysicsSimComponent->CurrentState != TargetSimState)
		{
			PhysicsSimComponent->SetPhysicsState(TargetSimState);
		}
	}

	BP_OnPropStateChanged(NewState, PrevState);
	OnActivePropStateChanged.Broadcast(this, NewState, PrevState);

	if (HasAuthority() && ISID > 0)
	{
		if (!PropManagerReference)
		{
			PropManagerReference = APropManager::Get(this);
		}
		if (PropManagerReference)
		{
			PropManagerReference->SetPropState(ISID, NewState);
		}
		ForceNetUpdate();
	}
}

void AActivePropActor::OnRep_IsActive()
{
	if (!PropManagerReference)
	{
		PropManagerReference = APropManager::Get(this);
	}

	const bool bEffectiveActive = (bIsActive && ISID > 0);

	if (!bEffectiveActive)
	{
		bIsInterpolatingToRest = false;
		OnRestInterpComplete = nullptr;

		int32 ResolvedOldISID = ISID;
		if (ResolvedOldISID <= 0 && PropManagerReference)
		{
			ResolvedOldISID = PropManagerReference->GetISID(PropMesh);
			if (ResolvedOldISID <= 0)
			{
				for (const TPair<int32, TObjectPtr<AActivePropActor>>& Pair : PropManagerReference->ActiveActorByISID)
				{
					if (Pair.Value == this)
					{
						ResolvedOldISID = Pair.Key;
						break;
					}
				}
			}
		}

		if (!bHasNotifiedBeforeReturnToRest)
		{
			bHasNotifiedBeforeReturnToRest = true;
			const FTransform CurrentGameplayTransform = GetActorTransform();
			BP_OnBeforeReturnToRestPosition(ResolvedOldISID, ProductID, CurrentGameplayTransform);
			OnBeforeReturnToRestPosition.Broadcast(this, ResolvedOldISID, ProductID, CurrentGameplayTransform);
			BP_OnReturnedToISM(ResolvedOldISID);
		}

		if (PropManagerReference && ResolvedOldISID > 0)
		{
			if (FISIDRuntimeIndex* Record = PropManagerReference->FindISIDData(ResolvedOldISID))
			{
				if (GetActorLocation().Z > -50000.0f && GetActorScale3D().GetMax() >= 0.005f)
				{
					Record->ActiveTransform = GetActorTransform();
				}
				if (Record->ActiveActor == this || Record->ASMID == ASMID)
				{
					Record->Representation = EPropRepresentation::ISM;
					Record->State = EPropState::AtRest;
					Record->ActiveActor = nullptr;
					Record->ASMID = 0;
				}
			}
			if (PropManagerReference->GetActiveActorFromISID(ResolvedOldISID) == this)
			{
				PropManagerReference->UnregisterActiveActor(ResolvedOldISID);
			}
			PropManagerReference->UpdateClientISM(ResolvedOldISID);
		}

		if (PropManagerReference && ASMID > 0)
		{
			PropManagerReference->RegisterProp(this);
		}

		bAlwaysRelevant = false;
		SetReplicates(false);
		bReplicates = false;
		SetReplicateMovement(false);

		SetActorHiddenInGame(true);
		if (GetActorEnableCollision())
		{
			SetActorEnableCollision(false);
		}
		SetActorTickEnabled(false);
		if (PropMesh)
		{
			if (PropMesh->IsSimulatingPhysics())
			{
				PropMesh->SetSimulatePhysics(false);
			}
			PropMesh->SetVisibility(false, true);
			if (PropMesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
			{
				PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}

		if (SkeletalMeshComponent)
		{
			SkeletalMeshComponent->SetVisibility(false, true);
		}

		const FTransform SafePoolTransform(FQuat::Identity, FVector(0.0f, 0.0f, -100000.0f), FVector::OneVector);
		SetActorTransform(SafePoolTransform, false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}

	// bEffectiveActive == true (bIsActive && ISID > 0):
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);

	// If this exact ASM was already predicted & activated locally on the Client for this exact ISID,
	// do NOT re-teleport it or reset its physics velocity when the Server's confirmation arrives!
	const bool bHasMesh = (PropMesh && PropMesh->GetStaticMesh() != nullptr) || (SkeletalMeshComponent && SkeletalMeshComponent->GetSkeletalMeshAsset() != nullptr);
	if (!bHasNotifiedBeforeReturnToRest && !IsHidden() && GetActorEnableCollision() && GetActorLocation().Z > -50000.0f && bHasMesh)
	{
		if (PropManagerReference && PropManagerReference->GetActiveActorFromISID(ISID) == this)
		{
			return;
		}
	}

	const bool bIsBecomingActiveNow = (bHasNotifiedBeforeReturnToRest || IsHidden() || GetActorLocation().Z < -50000.0f);

	// 1. Reconcile any different client-predicted ASM that was temporarily bound to this ISID, then register this authoritative ASM
	if (PropManagerReference)
	{
		if (AActivePropActor* OtherASM = PropManagerReference->GetActiveActorFromISID(ISID))
		{
			if (OtherASM != this)
			{
				PropManagerReference->UnregisterActiveActor(ISID);
				OtherASM->ISID = 0;
				OtherASM->bIsActive = false;
				OtherASM->DeactivateToPoolState(PropManagerReference->DefaultResetTransform);
				if (OtherASM->ASMID > 0)
				{
					PropManagerReference->RegisterProp(OtherASM);
				}
			}
		}

		PropManagerReference->RegisterProp(this);
		PropManagerReference->RegisterActiveActor(ISID, this);
		if (FISIDRuntimeIndex* Record = PropManagerReference->FindISIDData(ISID))
		{
			Record->Representation = EPropRepresentation::ASM;
			Record->State = State;
			Record->ActiveActor = this;
			Record->ASMID = ASMID;
			if (Record->ProductID <= 0 && ProductID > 0)
			{
				Record->ProductID = ProductID;
			}
			else if (ProductID <= 0 && Record->ProductID > 0)
			{
				ProductID = Record->ProductID;
			}

			const FPropProductDefinition* Def = PropManagerReference->ProductDefinitions.Find(ProductID > 0 ? ProductID : Record->ProductID);
			InitializeFromISIDRecord(*Record, Def, PropManagerReference);
		}
		PropManagerReference->UpdateClientISM(ISID);
		PropManagerReference->UpdateClientASM(ISID);
	}

	if (PropMesh && !PropMesh->GetStaticMesh())
	{
		if (PropDataAsset)
		{
			ApplyDataAsset(PropDataAsset);
		}
		else if (PropManagerReference && ProductID > 0)
		{
			if (const FPropProductDefinition* Def = PropManagerReference->ProductDefinitions.Find(ProductID))
			{
				ApplyProductDefinition(*Def);
			}
		}
	}

	// Ensure scale is never stuck at 0.0001f from ResetTransform
	if (GetActorScale3D().GetMax() < 0.005f)
	{
		SetActorScale3D(FVector::OneVector);
	}

	// Place at gameplay transform when becoming active from pool
	if (bIsBecomingActiveNow || GetActorLocation().Z < -50000.0f)
	{
		bool bPlacedFromRecord = false;
		if (PropManagerReference)
		{
			if (FISIDRuntimeIndex* Record = PropManagerReference->FindISIDData(ISID))
			{
				if (Record->ActiveTransform.GetLocation().Z > -50000.0f)
				{
					SetActorTransform(Record->ActiveTransform, false, nullptr, ETeleportType::TeleportPhysics);
					bPlacedFromRecord = true;
				}
			}
		}
		if (!bPlacedFromRecord)
		{
			const FRepMovement& RepMove = GetReplicatedMovement();
			if (!RepMove.Location.IsNearlyZero() && RepMove.Location.Z > -50000.0f)
			{
				SetActorLocationAndRotation(RepMove.Location, RepMove.Rotation, false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
	}

	// 2. Make visible and fire OnBecomeActive BEFORE enabling collision, physics simulation, or tick
	SetActorHiddenInGame(false);
	if (SkeletalMeshComponent && SkeletalMeshComponent->GetSkeletalMeshAsset())
	{
		SkeletalMeshComponent->SetHiddenInGame(false, true);
		SkeletalMeshComponent->SetVisibility(true, true);
		if (PropMesh)
		{
			PropMesh->SetHiddenInGame(false, false);
			PropMesh->SetVisibility(false, false);
			PropMesh->BodyInstance.SetMaxDepenetrationVelocity(80.0f);
			PropMesh->SetLinearDamping(0.2f);
			PropMesh->SetAngularDamping(0.3f);
			PropMesh->MarkRenderStateDirty();
		}
	}
	else if (PropMesh)
	{
		if (SkeletalMeshComponent)
		{
			SkeletalMeshComponent->SetVisibility(false, true);
		}
		PropMesh->SetHiddenInGame(false, true);
		PropMesh->SetVisibility(true, true);
		PropMesh->BodyInstance.SetMaxDepenetrationVelocity(80.0f);
		PropMesh->SetLinearDamping(0.2f);
		PropMesh->SetAngularDamping(0.3f);
		PropMesh->MarkRenderStateDirty();
	}
	UpdateMaterialCustomPrimitiveData();

	if (bHasNotifiedBeforeReturnToRest)
	{
		bHasNotifiedBeforeReturnToRest = false;
		const FTransform CurrentActiveTransform = GetActorTransform();
		BP_OnBecomeActive(ISID, ProductID, CurrentActiveTransform, State);
		BP_OnActivatedFromISM(ISID, ProductID, State);
		OnPropBecomeActive.Broadcast(this, ISID, ProductID, CurrentActiveTransform);
	}

	// 3. Enable collision, tick, and physics simulation state now that mesh and transform are placed
	if (!GetActorEnableCollision())
	{
		SetActorEnableCollision(true);
	}
	SetActorTickEnabled(true);
	if (PropMesh)
	{
		if (PropMesh->GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics)
		{
			PropMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
	}

	if (PhysicsSimComponent)
	{
		const EPhysicsSimulationState TargetSimState = ConvertPropStateToPhysicsState(State);
		if (PhysicsSimComponent->CurrentState != TargetSimState || (PropMesh && !PropMesh->IsSimulatingPhysics() && TargetSimState != EPhysicsSimulationState::AtRest && TargetSimState != EPhysicsSimulationState::Held))
		{
			PhysicsSimComponent->SetPhysicsState(TargetSimState);
		}
	}

	if (State == EPropState::Held)
	{
		bIsHeld = true;
		bIsGrabbable = false;
		PackedRuntimeData.bIsGrabbable = false;
		if (PropMesh)
		{
			PropMesh->SetSimulatePhysics(false);
			PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
			PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
		}
	}

	// Apply initial Server velocity once upon activation if simulating physics (without hard-snapping ReplicatedMovement)
	if (bIsBecomingActiveNow && PropMesh && PropMesh->IsSimulatingPhysics() && State != EPropState::Held)
	{
		if (!LinearVelocity.IsNearlyZero())
		{
			PropMesh->SetPhysicsLinearVelocity(LinearVelocity.GetClampedToMaxSize(250.0f));
		}
		if (!AngularVelocity.IsNearlyZero())
		{
			PropMesh->SetPhysicsAngularVelocityInDegrees(AngularVelocity.GetClampedToMaxSize(360.0f));
		}
	}
}

void AActivePropActor::OnRep_IsHeld()
{
	if (bIsHeld)
	{
		bIsInMotion = false;
	}
}

void AActivePropActor::OnRep_IsInMotion()
{
	if (bIsInMotion && PropMesh)
	{
		PropMesh->SetSimulatePhysics(true);
		PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
	}
}

void AActivePropActor::SetInMotion(bool bNewInMotion)
{
	bIsInMotion = bNewInMotion;
	if (bNewInMotion)
	{
		bIsHeld = false;
		State = EPropState::InMotion;
		if (PhysicsSimComponent)
		{
			PhysicsSimComponent->SetInMotion(true);
		}
		if (PropMesh && !PropMesh->IsSimulatingPhysics())
		{
			PropMesh->SetSimulatePhysics(true);
			PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
		}
	}
	else
	{
		if (PhysicsSimComponent)
		{
			PhysicsSimComponent->SetInMotion(false);
		}
		if (State == EPropState::InMotion)
		{
			SetPropToSettling();
		}
	}
}

void AActivePropActor::SetHeld(bool bNewHeld)
{
	bIsHeld = bNewHeld;
	if (bNewHeld)
	{
		bIsInMotion = false;
		State = EPropState::Held;
		bIsGrabbable = false;
		PackedRuntimeData.bIsGrabbable = false;
		if (PhysicsSimComponent)
		{
			PhysicsSimComponent->SetHeld(true);
		}
		if (PropMesh)
		{
			PropMesh->SetSimulatePhysics(false);
			PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
			PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
		}
	}
	else
	{
		bIsGrabbable = true;
		PackedRuntimeData.bIsGrabbable = true;
		InteractionState = EPropInteractionLockState::Free;
		InteractionOwner = nullptr;
		RuntimeData.CurrentInteractor = nullptr;
		RuntimeData.InteractionOwner = nullptr;
		RuntimeData.InteractionState = EPropInteractionLockState::Free;

		if (PhysicsSimComponent)
		{
			PhysicsSimComponent->SetHeld(false);
		}
		if (State == EPropState::Held)
		{
			SetPropToSettling();
		}
	}
}

void AActivePropActor::OnRep_IsGrabbable()
{
}

void AActivePropActor::SetGrabbable(bool bNewGrabbable)
{
	bIsGrabbable = bNewGrabbable;
	PackedRuntimeData.bIsGrabbable = bNewGrabbable;
}

void AActivePropActor::RetriggerUnsettled(float NewDelay)
{
	if (PhysicsSimComponent)
	{
		PhysicsSimComponent->RetriggerUnsettledTimer(NewDelay);
	}
}

void AActivePropActor::SetPropToUnsettled(float UnsettledDelay)
{
	if (bIsHeld || bIsInMotion)
	{
		return;
	}

	SetPropState(EPropState::Unsettled);
	if (PhysicsSimComponent)
	{
		PhysicsSimComponent->SetStateUnsettled(UnsettledDelay);
	}
	else if (PropManagerReference && ISID > 0)
	{
		PropManagerReference->SetInstanceToUnsettled(ISID);
	}
}

void AActivePropActor::SetPropToSettling(float VelocityTolerance)
{
	if (bIsHeld || bIsInMotion)
	{
		return;
	}

	if (PhysicsSimComponent)
	{
		PhysicsSimComponent->SetStateSettling(VelocityTolerance);
	}
	else if (PropManagerReference && ISID > 0)
	{
		PropManagerReference->SetInstanceToSettling(ISID, VelocityTolerance);
	}
}

void AActivePropActor::SetPropToInMotion(FVector LaunchImpulse)
{
	if (bIsHeld)
	{
		return;
	}

	bIsInMotion = true;
	State = EPropState::InMotion;

	if (PhysicsSimComponent)
	{
		PhysicsSimComponent->SetInMotion(true);
	}
	else if (PropManagerReference && ISID > 0)
	{
		PropManagerReference->SetInstanceToInMotion(ISID, LaunchImpulse, GetActorLocation());
	}

	if (!LaunchImpulse.IsNearlyZero() && PropMesh && PropMesh->IsSimulatingPhysics())
	{
		PropMesh->AddImpulse(LaunchImpulse, NAME_None, true);
	}
}

// -------------------------------------------------------------
// Player Interaction Pathway
// -------------------------------------------------------------
bool AActivePropActor::InteractWithPlayer_Implementation(APawn* PlayerPawn)
{
	if (!PlayerPawn) return false;

	if (bIsGrabbable)
	{
		USceneComponent* AttachComp = PlayerPawn->GetRootComponent();
		return OnPlayerGrab(AttachComp, NAME_None);
	}
	else if (!IsEmpty())
	{
		return ConsumePortion(PlayerPawn);
	}

	return false;
}

bool AActivePropActor::OnPlayerGrab_Implementation(USceneComponent* AttachToParent, FName SocketName)
{
	if (!AttachToParent) return false;

	State = EPropState::Held;
	bIsHeld = true;
	bIsInMotion = false;
	bIsGrabbable = false;
	PackedRuntimeData.bIsGrabbable = false;
	InteractionState = EPropInteractionLockState::Held;
	InteractionOwner = AttachToParent->GetOwner();
	RuntimeData.CurrentInteractor = InteractionOwner;
	RuntimeData.InteractionOwner = InteractionOwner;
	RuntimeData.InteractionState = EPropInteractionLockState::Held;
	RuntimeData.AttachSocketName = SocketName;

	if (PropMesh)
	{
		PropMesh->SetSimulatePhysics(false);
		PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
	}

	if (PhysicsSimComponent)
	{
		PhysicsSimComponent->ClearUnsettledTimer();
		PhysicsSimComponent->SetHeld(true);
	}

	AttachToComponent(AttachToParent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
	return true;
}

bool AActivePropActor::OnPlayerRelease_Implementation(FVector LaunchVelocity)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	bIsHeld = false;
	bIsGrabbable = true;
	PackedRuntimeData.bIsGrabbable = true;
	InteractionState = EPropInteractionLockState::Free;
	InteractionOwner = nullptr;
	RuntimeData.CurrentInteractor = nullptr;
	RuntimeData.InteractionOwner = nullptr;
	RuntimeData.InteractionState = EPropInteractionLockState::Free;

	if (LaunchVelocity.SizeSquared() > 100.0f)
	{
		bIsInMotion = false;
		State = EPropState::InMotion;
		if (PhysicsSimComponent)
		{
			PhysicsSimComponent->bIsInMotion = false;
			PhysicsSimComponent->SetStateInMotion();
		}
		if (PropMesh)
		{
			PropMesh->SetSimulatePhysics(true);
			PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
			PropMesh->AddImpulse(LaunchVelocity, NAME_None, true);
		}
	}
	else
	{
		bIsInMotion = false;
		State = EPropState::Unsettled;
		if (PhysicsSimComponent)
		{
			PhysicsSimComponent->SetHeld(false);
			PhysicsSimComponent->SetStateUnsettled(-1.0f);
		}
		if (PropMesh)
		{
			PropMesh->SetSimulatePhysics(true);
			PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
		}
	}

	return true;
}

// -------------------------------------------------------------
// NPC Interaction Pathway
// -------------------------------------------------------------
bool AActivePropActor::InteractWithNPC_Implementation(ANPCCharacter* NPCCharacter, FName ActionName)
{
	if (!NPCCharacter) return false;

	if (ActionName == FName("Grab") || ActionName == FName("PickUp"))
	{
		USceneComponent* AttachComp = NPCCharacter->GetMesh();
		return OnPlayerGrab(AttachComp, FName("hand_rSocket"));
	}

	return ConsumePortion(NPCCharacter);
}

// -------------------------------------------------------------
// Consumable Logic
// -------------------------------------------------------------
void AActivePropActor::OnConsumedByPlayer_Implementation(APawn* PlayerPawn, float BodyTempDelta, float ItemTemp, bool bWasExpired)
{
	UE_LOG(LogTemp, Log, TEXT("[%s] Consumed by Player [%s]: BodyTempDelta=%.2f, ItemTemp=%.1fC, Expired=%s"),
		*ItemDisplayName.ToString(),
		PlayerPawn ? *PlayerPawn->GetName() : TEXT("None"),
		BodyTempDelta,
		ItemTemp,
		bWasExpired ? TEXT("TRUE") : TEXT("FALSE"));
}

bool AActivePropActor::ConsumePortion(AActor* ConsumerActor, float PortionRatio)
{
	if (IsEmpty()) return false;
	if (!ConsumerActor) return false;

	if (!HasAuthority())
	{
		Server_ConsumePortion(ConsumerActor, PortionRatio);
		return true;
	}

	const float Ratio = FMath::Clamp(PortionRatio, 0.1f, 1.0f);
	const bool bItemExpired = IsExpired() || IsSpoiled();
	const float AutoBodyTempDelta = FInteractablePropData::CalculateBodyTemperatureDelta(CurrentTemperature, AmbientRoomTemperature) * Ratio;

	ANPCCharacter* NPC = Cast<ANPCCharacter>(ConsumerActor);
	if (NPC)
	{
		if (bItemExpired)
		{
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::Sickness, SicknessLevel * Ratio);
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::Dopamine, -ExpiredDopaminePenalty * Ratio);
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::Stress, 25.0f * Ratio);

			for (const auto& Mod : ExpiryStimulusProfile)
			{
				NPC->ApplySimulationStatDelta(Mod.Stat, Mod.ChangeRate * Ratio);
			}

			if (PropDataAsset)
			{
				TasteProfile = PropDataAsset->BuildTasteVector(true);
			}
		}
		else
		{
			const float ConsumerTolerance = (NPC->PersonalityComponent) ? NPC->PersonalityComponent->Personality.TemperatureTolerance : 0.5f;
			const float TempDopamine = FInteractablePropData::CalculateTemperatureDopamine(CurrentTemperature, ConsumableTemperature, TemperatureDopamineBonus, ConsumerTolerance) * Ratio;
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::Dopamine, TempDopamine);

			for (const auto& Mod : StimuliPerPortion)
			{
				NPC->ApplySimulationStatDelta(Mod.Stat, Mod.ChangeRate * Ratio);
			}

			if (NPC->PersonalityComponent)
			{
				const float Compatibility = NPC->PersonalityComponent->EvaluateTaste(TasteProfile);
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

		if (FMath::Abs(AutoBodyTempDelta) > KINDA_SMALL_NUMBER)
		{
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::BodyTemperature, AutoBodyTempDelta);
		}
	}
	else if (APawn* PlayerPawn = Cast<APawn>(ConsumerActor))
	{
		OnConsumedByPlayer(PlayerPawn, AutoBodyTempDelta, CurrentTemperature, bItemExpired);
	}

	RemainingPortions = FMath::Max(0, RemainingPortions - 1);
	QuantityLevel = NumOfPortions > 0 ? static_cast<float>(RemainingPortions) / static_cast<float>(NumOfPortions) : 0.0f;
	PackedRuntimeData.RemainingPortions = RemainingPortions;
	RuntimeData.RemainingPortions = RemainingPortions;
	Version++;

	if (ISID > 0)
	{
		if (!PropManagerReference)
		{
			PropManagerReference = APropManager::Get(this);
		}
		if (PropManagerReference)
		{
			PropManagerReference->ConsumeInstancePortion(ISID, ConsumerActor, PortionRatio);
		}
	}

	Multicast_OnPortionConsumed(ConsumerActor, RemainingPortions);
	UpdateDebugBillboard();

	if (RemainingPortions <= 0 && bDestroyWhenEmpty)
	{
		if (ISID > 0 && PropManagerReference)
		{
			PropManagerReference->ReleaseInstanceToEmpty(ISID);
		}
		else
		{
			SetLifeSpan(1.0f);
		}
	}

	return true;
}

void AActivePropActor::Server_ConsumePortion_Implementation(AActor* ConsumerActor, float PortionRatio)
{
	ConsumePortion(ConsumerActor, PortionRatio);
}

bool AActivePropActor::Server_ConsumePortion_Validate(AActor* ConsumerActor, float PortionRatio)
{
	return true;
}

void AActivePropActor::Multicast_OnPortionConsumed_Implementation(AActor* ConsumerActor, int32 PortionsLeft)
{
	UpdateDebugBillboard();
}

void AActivePropActor::OnRep_QuantityLevel()
{
	UpdateDebugBillboard();
}

void AActivePropActor::OnRep_RemainingPortions()
{
	UpdateDebugBillboard();
}

bool AActivePropActor::GetRuntimeDefaults(FPropPackedRuntimeData& OutData) const
{
	if (ISID > 0)
	{
		const APropManager* Manager = PropManagerReference ? PropManagerReference.Get() : APropManager::Get(this);
		if (Manager)
		{
			return Manager->GetPackedRuntimeData(ISID, OutData);
		}
	}
	OutData = PackedRuntimeData;
	return false;
}

float AActivePropActor::GetAuthoritativeTemperature() const
{
	FPropPackedRuntimeData Data;
	if (GetRuntimeDefaults(Data))
	{
		return Data.Temperature;
	}
	return CurrentTemperature;
}

int32 AActivePropActor::GetAuthoritativeRemainingPortions() const
{
	FPropPackedRuntimeData Data;
	if (GetRuntimeDefaults(Data))
	{
		return Data.RemainingPortions;
	}
	return RemainingPortions;
}

// -------------------------------------------------------------
// IWorldAffordanceInterface
// -------------------------------------------------------------
bool AActivePropActor::CanInteract_Implementation(AActor* InstigatorActor)
{
	if (!InstigatorActor || !bIsActive) return false;

	const bool bIsPlayer = InstigatorActor->IsA<APawn>() && !InstigatorActor->IsA<ANPCCharacter>();
	if (bIsPlayer)
	{
		return bIsGrabbable || !IsEmpty();
	}

	return !IsEmpty();
}

void AActivePropActor::QueryAffordanceOptions_Implementation(AActor* InstigatorActor, TArray<FAffordanceOption>& OutOptions)
{
	OutOptions.Empty();
	if (!InstigatorActor || !bIsActive) return;

	const bool bIsPlayer = InstigatorActor->IsA<APawn>() && !InstigatorActor->IsA<ANPCCharacter>();

	if (bIsPlayer)
	{
		if (bIsGrabbable)
		{
			FAffordanceOption GrabOpt;
			GrabOpt.OptionId = FName("Grab");
			GrabOpt.DisplayLabel = FText::FromString(TEXT("Pick Up / Grab"));
			GrabOpt.MoneyCost = 0.0f;
			GrabOpt.ExecutionTime = 0.2f;
			OutOptions.Add(GrabOpt);
		}

		if (!IsEmpty())
		{
			FAffordanceOption UseOpt;
			UseOpt.OptionId = FName("ConsumePortion");
			UseOpt.DisplayLabel = FText::FromString(ConsumableType == EConsumablePropType::Drink ? TEXT("Take a Sip") : TEXT("Take a Bite"));
			UseOpt.MoneyCost = Price;
			UseOpt.OfferedStimuli = StimuliPerPortion;
			UseOpt.ExecutionTime = 1.0f;
			OutOptions.Add(UseOpt);
		}
	}
	else
	{
		if (!IsEmpty())
		{
			FAffordanceOption DrinkOpt;
			DrinkOpt.OptionId = FName("ConsumePortion");
			DrinkOpt.DisplayLabel = FText::FromString(ConsumableType == EConsumablePropType::Drink ? TEXT("Drink") : TEXT("Eat"));
			DrinkOpt.MoneyCost = Price;
			DrinkOpt.OfferedStimuli = StimuliPerPortion;
			DrinkOpt.ExecutionTime = 2.0f;
			OutOptions.Add(DrinkOpt);
		}

		if (bIsGrabbable)
		{
			FAffordanceOption PickOpt;
			PickOpt.OptionId = FName("PickUp");
			PickOpt.DisplayLabel = FText::FromString(TEXT("Pick Up Item"));
			PickOpt.MoneyCost = Price;
			PickOpt.ExecutionTime = 1.0f;
			OutOptions.Add(PickOpt);
		}
	}
}

bool AActivePropActor::ExecuteAffordanceOption_Implementation(FName OptionId, AActor* InstigatorActor)
{
	if (!InstigatorActor || !bIsActive) return false;

	if (OptionId == FName("Grab") || OptionId == FName("PickUp"))
	{
		if (APawn* Pawn = Cast<APawn>(InstigatorActor))
		{
			return OnPlayerGrab(Pawn->GetRootComponent(), NAME_None);
		}
	}
	else if (OptionId == FName("ConsumePortion"))
	{
		return ConsumePortion(InstigatorActor);
	}

	return false;
}

void AActivePropActor::UpdateDebugBillboard()
{
	if (DebugText)
	{
		DebugText->SetText(FText::GetEmpty());
		DebugText->SetVisibility(false, true);
		DebugText->SetHiddenInGame(true);
	}
}

