#include "Props/InteractablePropDatabase.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractableProp.h"

// -----------------------------------------------------------------------------
// FPropInteractionProfile
// -----------------------------------------------------------------------------
int32 FPropInteractionProfile::ToBitmask() const
{
	int32 Mask = EPropInteractionCapFlags::None;
	if (bCanGrab)       Mask |= EPropInteractionCapFlags::Grab;
	if (bCanUse)        Mask |= EPropInteractionCapFlags::Use;
	if (bCanConsume)    Mask |= EPropInteractionCapFlags::Consume;
	if (bCanOpen)       Mask |= EPropInteractionCapFlags::Open;
	if (bCanClose)      Mask |= EPropInteractionCapFlags::Close;
	if (bCanToggle)     Mask |= EPropInteractionCapFlags::Toggle;
	if (bCanActivate)   Mask |= EPropInteractionCapFlags::Activate;
	if (bCanBreak)      Mask |= EPropInteractionCapFlags::Break;
	if (bCanRepair)     Mask |= EPropInteractionCapFlags::Repair;
	if (bCanDamage)     Mask |= EPropInteractionCapFlags::Damage;
	if (bCanInspect)    Mask |= EPropInteractionCapFlags::Inspect;
	if (bCanCombine)    Mask |= EPropInteractionCapFlags::Combine;
	if (bCanSplit)      Mask |= EPropInteractionCapFlags::Split;
	if (bCanThrow)      Mask |= EPropInteractionCapFlags::Throw;
	if (bCanPickUp)     Mask |= EPropInteractionCapFlags::PickUp;
	if (bCanDrop)       Mask |= EPropInteractionCapFlags::Drop;
	if (bCanMove)       Mask |= EPropInteractionCapFlags::Move;
	if (bCanRotate)     Mask |= EPropInteractionCapFlags::Rotate;
	if (bCanActivate)   Mask |= EPropInteractionCapFlags::Deactivate;
	return Mask;
}

void FPropInteractionProfile::FromBitmask(int32 InBitmask)
{
	bCanGrab     = (InBitmask & EPropInteractionCapFlags::Grab) != 0;
	bCanUse      = (InBitmask & EPropInteractionCapFlags::Use) != 0;
	bCanConsume  = (InBitmask & EPropInteractionCapFlags::Consume) != 0;
	bCanOpen     = (InBitmask & EPropInteractionCapFlags::Open) != 0;
	bCanClose    = (InBitmask & EPropInteractionCapFlags::Close) != 0;
	bCanToggle   = (InBitmask & EPropInteractionCapFlags::Toggle) != 0;
	bCanActivate = (InBitmask & EPropInteractionCapFlags::Activate) != 0;
	bCanBreak    = (InBitmask & EPropInteractionCapFlags::Break) != 0;
	bCanRepair   = (InBitmask & EPropInteractionCapFlags::Repair) != 0;
	bCanDamage   = (InBitmask & EPropInteractionCapFlags::Damage) != 0;
	bCanInspect  = (InBitmask & EPropInteractionCapFlags::Inspect) != 0;
	bCanCombine  = (InBitmask & EPropInteractionCapFlags::Combine) != 0;
	bCanSplit    = (InBitmask & EPropInteractionCapFlags::Split) != 0;
	bCanThrow    = (InBitmask & EPropInteractionCapFlags::Throw) != 0;
	bCanPickUp   = (InBitmask & EPropInteractionCapFlags::PickUp) != 0;
	bCanDrop     = (InBitmask & EPropInteractionCapFlags::Drop) != 0;
	bCanMove     = (InBitmask & EPropInteractionCapFlags::Move) != 0;
	bCanRotate   = (InBitmask & EPropInteractionCapFlags::Rotate) != 0;
}

bool FPropInteractionProfile::SupportsInteraction(EPropInteractionType InteractionType) const
{
	switch (InteractionType)
	{
	case EPropInteractionType::Grab:       return bCanGrab;
	case EPropInteractionType::Release:    return bCanGrab || bCanDrop;
	case EPropInteractionType::Drop:       return bCanDrop || bCanGrab;
	case EPropInteractionType::Throw:      return bCanThrow;
	case EPropInteractionType::PickUp:     return bCanPickUp || bCanGrab;
	case EPropInteractionType::Use:        return bCanUse;
	case EPropInteractionType::Consume:    return bCanConsume;
	case EPropInteractionType::Open:       return bCanOpen;
	case EPropInteractionType::Close:      return bCanClose;
	case EPropInteractionType::Toggle:     return bCanToggle || (bCanOpen && bCanClose);
	case EPropInteractionType::Activate:   return bCanActivate;
	case EPropInteractionType::Deactivate: return bCanActivate;
	case EPropInteractionType::Break:      return bCanBreak;
	case EPropInteractionType::Repair:     return bCanRepair;
	case EPropInteractionType::Damage:     return bCanDamage;
	case EPropInteractionType::Inspect:    return bCanInspect;
	case EPropInteractionType::Combine:    return bCanCombine;
	case EPropInteractionType::Split:      return bCanSplit;
	case EPropInteractionType::Move:       return bCanMove;
	case EPropInteractionType::Rotate:     return bCanRotate;
	default:                               return false;
	}
}

// -----------------------------------------------------------------------------
// FPropPackedRuntimeData
// -----------------------------------------------------------------------------
uint64 FPropPackedRuntimeData::PackToUint64() const
{
	const uint64 CreationBits = static_cast<uint64>(FMath::Clamp(CreationMinute, 0, 65535)) & 0xFFFFULL;
	const uint64 UpdatedBits  = static_cast<uint64>(FMath::Clamp(LastUpdatedMinute, 0, 65535)) & 0xFFFFULL;
	const uint64 PortionBits  = static_cast<uint64>(FMath::Clamp(RemainingPortions, 0, 255)) & 0xFFULL;

	// Quantize temperature in 0.1C steps relative to -100.0C into 12 bits (0..4095 -> -100.0C..+309.5C)
	const int32 QuantizedTemp = FMath::Clamp(FMath::RoundToInt((Temperature + 100.0f) * 10.0f), 0, 4095);
	const uint64 TempBits     = static_cast<uint64>(QuantizedTemp) & 0xFFFULL;

	const uint64 GrabBit      = bIsGrabbable ? 1ULL : 0ULL;
	const uint64 SchemaBits   = static_cast<uint64>(SchemaFlags) & 0xFFULL;

	uint64 Result = 0ULL;
	Result |= (CreationBits << 0);
	Result |= (UpdatedBits  << 16);
	Result |= (PortionBits  << 32);
	Result |= (TempBits     << 40);
	Result |= (GrabBit      << 52);
	Result |= (SchemaBits   << 56);
	return Result;
}

void FPropPackedRuntimeData::UnpackFromUint64(uint64 PackedBits)
{
	CreationMinute    = static_cast<int32>((PackedBits >> 0) & 0xFFFFULL);
	LastUpdatedMinute = static_cast<int32>((PackedBits >> 16) & 0xFFFFULL);
	RemainingPortions = static_cast<int32>((PackedBits >> 32) & 0xFFULL);

	const int32 QuantizedTemp = static_cast<int32>((PackedBits >> 40) & 0xFFFULL);
	Temperature = (static_cast<float>(QuantizedTemp) * 0.1f) - 100.0f;

	bIsGrabbable = ((PackedBits >> 52) & 0x1ULL) != 0ULL;
	SchemaFlags  = static_cast<int32>((PackedBits >> 56) & 0xFFULL);
}

void FPropPackedRuntimeData::SerializeExplicit(FArchive& Ar)
{
	uint64 Packed = Ar.IsSaving() ? PackToUint64() : 0ULL;
	Ar << Packed;
	if (Ar.IsLoading())
	{
		UnpackFromUint64(Packed);
	}
}

// -----------------------------------------------------------------------------
// FPropProductDefinition
// -----------------------------------------------------------------------------
FPropProductDefinition FPropProductDefinition::FromDataAsset(const UInteractablePropDataAsset* InAsset, int32 InProductID, int32 InStaticMeshID)
{
	FPropProductDefinition Def;
	Def.ProductID = InProductID;
	Def.StaticMeshID = InStaticMeshID;

	if (InAsset)
	{
		Def.SourceDataAsset = const_cast<UInteractablePropDataAsset*>(InAsset);
		Def.CustomActiveActorClass = InAsset->CustomActiveActorClass;
		Def.DisplayName = InAsset->ItemDisplayName;
		Def.StaticMesh = InAsset->StaticMesh;
		Def.MaterialOverrides = InAsset->MaterialOverrides;

		Def.ConsumableProfile.ConsumableType = InAsset->ConsumableType;
		Def.ConsumableProfile.NumOfPortions = InAsset->NumOfPortions;
		Def.ConsumableProfile.Price = InAsset->Price;
		Def.ConsumableProfile.bDestroyWhenEmpty = true;

		Def.TemperatureProfile.bEnableTemperatureUpdate = InAsset->bEnableTemperatureUpdate;
		Def.TemperatureProfile.ConsumableTemperature = InAsset->ConsumableTemperature;
		Def.TemperatureProfile.TemperatureDopamineBonus = InAsset->TemperatureDopamineBonus;
		Def.TemperatureProfile.ThermalExchangeRate = InAsset->ThermalExchangeRate;

		Def.ExpiryProfile.bHasExpiry = InAsset->bHasExpiry;
		Def.ExpiryProfile.ExpiryLifetimeMinutes = InAsset->ExpiryLifetimeMinutes;
		Def.ExpiryProfile.SicknessLevel = InAsset->SicknessLevel;
		Def.ExpiryProfile.ExpiredDopaminePenalty = InAsset->ExpiredDopaminePenalty;
		Def.ExpiryProfile.ExpiryStimulusProfile = InAsset->ExpiryStimulusProfile;

		Def.InteractionProfile.bCanGrab = InAsset->bCanGrab;
		Def.InteractionProfile.bCanUse = InAsset->bCanUse;
		Def.InteractionProfile.bCanConsume = InAsset->bCanConsume;
		Def.InteractionProfile.bCanThrow = InAsset->bCanThrow;
		Def.InteractionProfile.bCanInspect = InAsset->bCanInspect;

		Def.PhysicsProfile.LinearVelocityTolerance = InAsset->LinearVelocityTolerance;
		Def.PhysicsProfile.AngularVelocityTolerance = InAsset->AngularVelocityTolerance;
		Def.PhysicsProfile.UnsettledDelay = InAsset->UnsettledDelay;

		Def.StimulusProfile = InAsset->StimuliPerPortion;
		Def.TasteProfile = InAsset->BuildTasteVector(false);
		Def.ExpiredTasteProfile = InAsset->BuildTasteVector(true);

		Def.RuntimeDefaults.RemainingPortions = InAsset->NumOfPortions;
		Def.RuntimeDefaults.Temperature = InAsset->ConsumableTemperature;
		Def.RuntimeDefaults.bIsGrabbable = InAsset->bCanGrab;
	}

	return Def;
}

// -----------------------------------------------------------------------------
// FInteractablePropData
// -----------------------------------------------------------------------------
FInteractablePropData::FInteractablePropData()
{
	ConsumableTemperature = 21.0f;
	CurrentTemperature = 21.0f;
	bEnableTemperatureUpdate = false;
	TemperatureDopamineBonus = 0.5f;
	ThermalExchangeRate = 0.015f;

	NumOfPortions = 4;
	RemainingPortions = 4;
	QuantityLevel = 1.0f;
	Price = 2.0f;

	bIsGrabbable = true;
	SimulationState = EPhysicsSimulationState::AtRest;
	VelocityTolerance = 10.0f;
	AngularVelocityTolerance = 15.0f;

	bHasExpiry = true;
	ExpiryLifetimeMinutes = 60.0f;
	AgeMinutes = 0.0f;
	SicknessLevel = 35.0f;
	ExpiredDopaminePenalty = 25.0f;

	// Default stimuli
	StimuliPerPortion.Empty();
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Power, 10.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Hunger, -5.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Thirst, -25.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Dopamine, 5.0f));

	// Default taste profile
	TasteProfile.Sweet = 0.85f;
	TasteProfile.Healthy = 0.3f;
	TasteProfile.Luxury = 0.2f;
}

float FInteractablePropData::CalculateTemperatureDopamine(float CurrentTemp, float IdealTemp, float MaxDopamineBonus, float ConsumerTolerance)
{
	const float RawDiff = FMath::Abs(CurrentTemp - IdealTemp);
	const float ToleranceFactor = FMath::Max(0.1f, ConsumerTolerance * 2.0f);
	const float EffectiveDiff = RawDiff / ToleranceFactor;

	float PenaltyDrop = 0.0f;
	if (EffectiveDiff <= 10.0f)
	{
		// e.g. Ideal=2C, Max=0.5: at 4C (d=2) -> drop=0.05 (dopamine=0.45); at 12C (d=10) -> drop=0.40 (dopamine=0.10)
		PenaltyDrop = (0.001875f * EffectiveDiff * EffectiveDiff) + (0.02125f * EffectiveDiff);
	}
	else
	{
		// e.g. Ideal=2C, Max=0.5: at 20C (d=18, excess=8) -> drop=1.70 (dopamine=-1.20)
		const float Excess = EffectiveDiff - 10.0f;
		PenaltyDrop = 0.40f + (0.02f * Excess * Excess) + (0.0025f * Excess);
	}

	const float BonusScale = (MaxDopamineBonus > 0.0f) ? (MaxDopamineBonus / 0.5f) : 1.0f;
	return MaxDopamineBonus - (PenaltyDrop * BonusScale);
}

float FInteractablePropData::CalculateBodyTemperatureDelta(float CurrentTemp, float NeutralTemp)
{
	// Consuming something hotter than neutral warms the consumer; colder cools the consumer
	return (CurrentTemp - NeutralTemp) * 0.03f;
}

int32 FInteractablePropData::ConsumePortions(int32 PortionsToConsume)
{
	if (RemainingPortions <= 0) return 0;

	const int32 ActualConsumed = FMath::Clamp(PortionsToConsume, 1, RemainingPortions);
	RemainingPortions = FMath::Max(0, RemainingPortions - ActualConsumed);
	QuantityLevel = (NumOfPortions > 0) ? static_cast<float>(RemainingPortions) / static_cast<float>(NumOfPortions) : 0.0f;
	return ActualConsumed;
}

void FInteractablePropData::UpdateTemperature(float AmbientTemperature, float DeltaSeconds)
{
	if (!bEnableTemperatureUpdate) return;

	if (ThermalExchangeRate > 0.0f && FMath::Abs(CurrentTemperature - AmbientTemperature) > 0.05f)
	{
		const float TempDelta = (AmbientTemperature - CurrentTemperature) * ThermalExchangeRate * DeltaSeconds;
		CurrentTemperature += TempDelta;
	}

	if (bHasExpiry)
	{
		AgeMinutes += (DeltaSeconds / 60.0f);
	}
}

void FInteractablePropData::SyncFromISIDRecord(const FISIDRuntimeIndex& Record, const FPropProductDefinition* ProductDef)
{
	ISID = Record.ISID;
	ProductID = Record.ProductID;
	IISMComponentID = Record.IISMComponentID;
	InstanceId = Record.ISMI;

	if (ProductDef)
	{
		ItemDisplayName = ProductDef->DisplayName;
		ConsumableType = ProductDef->ConsumableProfile.ConsumableType;
		StaticMesh = ProductDef->StaticMesh;
		DataAsset = ProductDef->SourceDataAsset;

		bEnableTemperatureUpdate = ProductDef->TemperatureProfile.bEnableTemperatureUpdate;
		ConsumableTemperature = ProductDef->TemperatureProfile.ConsumableTemperature;
		TemperatureDopamineBonus = ProductDef->TemperatureProfile.TemperatureDopamineBonus;
		ThermalExchangeRate = ProductDef->TemperatureProfile.ThermalExchangeRate;

		Price = ProductDef->ConsumableProfile.Price;
		NumOfPortions = ProductDef->ConsumableProfile.NumOfPortions;

		VelocityTolerance = ProductDef->PhysicsProfile.LinearVelocityTolerance;
		AngularVelocityTolerance = ProductDef->PhysicsProfile.AngularVelocityTolerance;

		bHasExpiry = ProductDef->ExpiryProfile.bHasExpiry;
		ExpiryLifetimeMinutes = ProductDef->ExpiryProfile.ExpiryLifetimeMinutes;
		SicknessLevel = ProductDef->ExpiryProfile.SicknessLevel;
		ExpiredDopaminePenalty = ProductDef->ExpiryProfile.ExpiredDopaminePenalty;
		ExpiryStimulusProfile = ProductDef->ExpiryProfile.ExpiryStimulusProfile;

		StimuliPerPortion = ProductDef->StimulusProfile;
		TasteProfile = ProductDef->TasteProfile;
	}

	RemainingPortions = Record.PackedRuntimeData.RemainingPortions;
	QuantityLevel = (NumOfPortions > 0) ? static_cast<float>(RemainingPortions) / static_cast<float>(NumOfPortions) : 0.0f;
	CurrentTemperature = Record.PackedRuntimeData.Temperature;
	bIsGrabbable = Record.PackedRuntimeData.bIsGrabbable;
	SimulationState = ConvertPropStateToPhysicsState(Record.State);
	AgeMinutes = FMath::Max(0.0f, static_cast<float>(Record.PackedRuntimeData.LastUpdatedMinute - Record.PackedRuntimeData.CreationMinute));
}

// -----------------------------------------------------------------------------
// FPropRuntimeFieldDelta
// -----------------------------------------------------------------------------
bool FPropRuntimeFieldDelta::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	Ar << ISID;
	Ar << DirtyFlags;

	if (HasFlag(EPropRuntimeDirtyMask::Temperature))
	{
		Ar << Temperature;
	}
	if (HasFlag(EPropRuntimeDirtyMask::Portions))
	{
		Ar << RemainingPortions;
	}
	if (HasFlag(EPropRuntimeDirtyMask::State))
	{
		uint8 StateByte = static_cast<uint8>(State);
		Ar << StateByte;
		State = static_cast<EPropState>(StateByte);
	}
	Ar << LastUpdatedMinute;

	bOutSuccess = true;
	return true;
}

