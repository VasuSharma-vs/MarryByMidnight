#include "Props/InteractableProp.h"
#include "Props/InteractablePropManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/RocketProjectileComponent.h"

const FPropProductDefinition* AActivePropActor::ResolveProductDefinition() const
{
	if (PropManagerReference) if (const FPropProductDefinition* Def = PropManagerReference->FindProductDefinition(ProductID)) return Def;
	return bHasCachedFallbackProductDef ? &CachedFallbackProductDef : nullptr;
}
const FPropContentDefinition* AActivePropActor::ResolveContentDefinition() const
{
	return PropManagerReference ? PropManagerReference->FindContentDefinition(GetContentID()) : nullptr;
}

float AActivePropActor::GetInitialContentLevel_Implementation(float DefinitionDefault) const
{
	return DefinitionDefault;
}

void AActivePropActor::OnRep_LocalUniqueData()
{
	if (!bHasValidUniqueData) return;
	OnApplyEventData(LocalUniqueData.EventData);
	OnApplyContinuousData(LocalUniqueData.ContinuousData);
}
void AActivePropActor::OnApplySharedDefinitions(const FPropProductDefinition* ProductDef, const FPropContentDefinition* ContentDef)
{
	if (ProductDef) { NumOfPortions = ProductDef->ConsumableProfile.NumOfPortions; }
	if (ContentDef && SkeletalMeshComponent && ContentDef->LiquidMaterial) SkeletalMeshComponent->SetMaterial(1, ContentDef->LiquidMaterial);
}
void AActivePropActor::OnApplyEventData(const FPropEventUpdateData& InEventData, uint8 DirtyMask)
{
	if (&InEventData != &LocalUniqueData.EventData && bHasValidUniqueData && static_cast<int32>(InEventData.EventSequence - LocalUniqueData.EventData.EventSequence) < 0) return;
	LocalUniqueData.EventData = InEventData;
	bHasValidUniqueData = true;
	RemainingPortions = GetRemainingPortions();
	QuantityLevel = GetContentLevel();
	bIsGrabbable = InEventData.bIsGrabbable;
	UpdateMaterialCustomPrimitiveData();
}
void AActivePropActor::OnApplyContinuousData(const FPropContinuousUpdateData& InData, float NewStrength, bool bStrengthChanged)
{
	if (InData.LastUpdatedTime < LastDiscreteEventTime || InData.LastUpdatedTime < LocalUniqueData.ContinuousData.LastUpdatedTime) return;
	LocalUniqueData.ContinuousData = InData;
	if (bStrengthChanged) LocalUniqueData.EventData.Strength = FMath::Clamp(NewStrength, 0.0f, 1.0f);
	CurrentTemperature = InData.Temperature;
	AgeMinutes = GetAgeMinutes();
	ExpiryFreshness = GetExpiryFreshness();
	UpdateMaterialCustomPrimitiveData();
}
void AActivePropActor::EmitPropEventUpdate(uint8 DirtyMask, uint8 ExtendedDirtyMask)
{
	if (!HasAuthority() || !bHasValidUniqueData) return;
	if (PropManagerReference && ISID > 0)
	{
		FPropEventSyncDelta Delta;
		Delta.ISID = ISID; Delta.DirtyMask = DirtyMask; Delta.ExtendedDirtyMask = ExtendedDirtyMask;
		const FPropEventUpdateData& E = LocalUniqueData.EventData;
		Delta.ContentLevel = E.ContentLevel; Delta.ContentID = E.ContentID;
		Delta.Strength = E.Strength; Delta.CapSealed = E.CapSealed;
		Delta.CapType = E.CapType; Delta.bIsGrabbable = E.bIsGrabbable;
		Delta.CreationTime = E.CreationTime; Delta.NewProductID = ProductID;
		Delta.ContentCreationTime = E.ContentCreationTime; Delta.bClean = E.bClean;
		Delta.AttachedCapISID = E.AttachedCapISID; Delta.AttachedBottleISID = E.AttachedBottleISID;
		PropManagerReference->SubmitInstanceEventUpdate(ISID, Delta);
	}
	else LocalUniqueData.EventData.EventSequence++;
	ForceNetUpdate();
}
bool AActivePropActor::CanDemoteToISM() const
{
	if (bIsHeld || bIsInMotion || bIsInterpolatingToRest || HasActiveThermalSources()) return false;
	if (const URocketProjectileComponent* Rocket = FindComponentByClass<URocketProjectileComponent>())
		if (Rocket->GetSimulationState() != ERocketSimulationState::Idle && Rocket->GetSimulationState() != ERocketSimulationState::AtRest) return false;
	return true;
}
bool AActivePropActor::HasActiveThermalSources() const { return false; }
void AActivePropActor::ApplyInitialImpactFromPromotion(FVector Location, const FVector& Impulse, AActor* InstigatorActor, int32 EventID)
{
	if (!HasAuthority() || Impulse.ContainsNaN()) return;
	const FPropProductDefinition* Def = ResolveProductDefinition();
	if (!Def || !Def->InteractionProfile.bCanDamage || !bHasValidUniqueData) return;
	const float Damage = FMath::Max(0.0f, Def->StrengthDamageRate) * Impulse.Size() / 2500.0f;
	LocalUniqueData.EventData.Strength = FMath::Clamp(GetStrength() - Damage, 0.0f, 1.0f);
	EmitPropEventUpdate(EPropEventDirtyMask::Strength);
}
void AActivePropActor::OnBeforePoolDeactivate() { ResetTransientPoolState(); }
void AActivePropActor::ResetTransientPoolState()
{
	++PoolGeneration;
	bPendingServerPromoteAck = false;
	LinearVelocity = AngularVelocity = FVector::ZeroVector;
	if (SkeletalMeshComponent) SkeletalMeshComponent->ClearMorphTargets();
}
bool AActivePropActor::ChangeProductWithMode(int32 NewProductID, EPropProductResetMode Mode)
{
	return HasAuthority() && PropManagerReference && PropManagerReference->ChangePropProductWithMode(ISID, NewProductID, Mode);
}

float AActivePropActor::GetContentLevel() const { return bHasValidUniqueData ? LocalUniqueData.EventData.ContentLevel : QuantityLevel; }
int32 AActivePropActor::GetRemainingPortions() const { return FMath::FloorToInt(GetContentLevel() * GetNumPortions()); }
float AActivePropActor::GetStrength() const { return bHasValidUniqueData ? LocalUniqueData.EventData.Strength : 1.0f; }
float AActivePropActor::GetStrengthRatio() const { return FMath::Clamp(GetStrength(), 0.0f, 1.0f); }
float AActivePropActor::GetCapSealed() const { return bHasValidUniqueData ? LocalUniqueData.EventData.CapSealed : 0.0f; }
uint8 AActivePropActor::GetCapType() const { return LocalUniqueData.EventData.CapType; }
int32 AActivePropActor::GetContentID() const { return bHasValidUniqueData ? LocalUniqueData.EventData.ContentID : 0; }
double AActivePropActor::GetCreationTime() const { return LocalUniqueData.EventData.CreationTime; }
double AActivePropActor::GetContentCreationTime() const { return LocalUniqueData.EventData.ContentCreationTime; }
float AActivePropActor::GetCurrentTemperature() const { return bHasValidUniqueData ? LocalUniqueData.ContinuousData.Temperature : CurrentTemperature; }
double AActivePropActor::GetLastUpdatedTime() const { return LocalUniqueData.ContinuousData.LastUpdatedTime; }
float AActivePropActor::GetAgeMinutes() const { return bHasValidUniqueData && PropManagerReference ? static_cast<float>(FMath::Max(0.0, PropManagerReference->WorldElapsedSeconds - GetContentCreationTime()) / 60.0) : AgeMinutes; }
float AActivePropActor::GetExpiryAge() const { return GetHasExpiry() && GetExpiryLifetimeMinutes() > 0 ? FMath::Clamp(GetAgeMinutes() / GetExpiryLifetimeMinutes(), 0.0f, 1.0f) : 0.0f; }
float AActivePropActor::GetExpiryFreshness() const { return 1.0f - GetExpiryAge(); }
bool AActivePropActor::GetIsGrabbableFromUniqueData() const { return bHasValidUniqueData ? LocalUniqueData.EventData.bIsGrabbable : bIsGrabbable; }
bool AActivePropActor::IsEmpty() const { return GetContentLevel() <= UE_SMALL_NUMBER; }
bool AActivePropActor::IsExpired() const { return GetHasExpiry() && GetExpiryAge() >= 1.0f; }
bool AActivePropActor::IsSpoiled() const { return GetHasExpiry() && GetExpiryAge() >= 0.75f; }
bool AActivePropActor::IsAtIdealTemperature(float Tolerance) const { return FMath::Abs(GetCurrentTemperature() - GetConsumableTemperature()) <= Tolerance; }
float AActivePropActor::GetCurrentWorldMinutes() const { return PropManagerReference ? static_cast<float>(PropManagerReference->WorldElapsedSeconds / 60.0) : GetWorld() ? GetWorld()->GetTimeSeconds() / 60.0f : 0; }

FText AActivePropActor::GetDisplayName() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->DisplayName : ItemDisplayName;
}
EConsumablePropType AActivePropActor::GetConsumableType() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->ConsumableProfile.ConsumableType : ConsumableType;
}
float AActivePropActor::GetPrice() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->ConsumableProfile.Price : Price;
}
bool AActivePropActor::GetEnableTemperatureUpdate() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->bEnableContinuousUpdateData : bEnableTemperatureUpdate;
}
float AActivePropActor::GetConsumableTemperature() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->TemperatureProfile.ConsumableTemperature : ConsumableTemperature;
}
float AActivePropActor::GetTemperatureDopamineBonus() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->TemperatureProfile.TemperatureDopamineBonus : TemperatureDopamineBonus;
}
float AActivePropActor::GetThermalExchangeRate() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->TemperatureProfile.ThermalExchangeRate : ThermalExchangeRate;
}
bool AActivePropActor::GetHasExpiry() const
{
	if (const FPropContentDefinition* C = ResolveContentDefinition()) return C->ExpiryProfile.bHasExpiry;
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->ExpiryProfile.bHasExpiry : bHasExpiry;
}
float AActivePropActor::GetExpiryLifetimeMinutes() const
{
	if (const FPropContentDefinition* C = ResolveContentDefinition()) return C->ExpiryProfile.ExpiryLifetimeMinutes;
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->ExpiryProfile.ExpiryLifetimeMinutes : ExpiryLifetimeMinutes;
}
float AActivePropActor::GetSicknessLevel() const
{
	if (const FPropContentDefinition* C = ResolveContentDefinition()) return C->ExpiryProfile.SicknessLevel;
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->ExpiryProfile.SicknessLevel : SicknessLevel;
}
float AActivePropActor::GetExpiredDopaminePenalty() const
{
	if (const FPropContentDefinition* C = ResolveContentDefinition()) return C->ExpiryProfile.ExpiredDopaminePenalty;
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->ExpiryProfile.ExpiredDopaminePenalty : ExpiredDopaminePenalty;
}
const TArray<FNPCSimulationStatModifier>& AActivePropActor::GetExpiryStimulusProfile() const
{
	if (const FPropContentDefinition* C = ResolveContentDefinition()) return C->ExpiryProfile.ExpiryStimulusProfile;
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->ExpiryProfile.ExpiryStimulusProfile : ExpiryStimulusProfile;
}
int32 AActivePropActor::GetNumPortions() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->ConsumableProfile.NumOfPortions : NumOfPortions;
}
const FNPCTasteVector& AActivePropActor::GetTasteProfile() const
{
	if (const FPropContentDefinition* C = ResolveContentDefinition()) return C->TasteProfile;
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->TasteProfile : TasteProfile;
}
const TArray<FNPCSimulationStatModifier>& AActivePropActor::GetStimuliPerPortion() const
{
	if (const FPropContentDefinition* C = ResolveContentDefinition()) return C->StimulusProfile;
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->StimulusProfile : StimuliPerPortion;
}
bool AActivePropActor::GetDestroyWhenEmpty() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->ConsumableProfile.bDestroyWhenEmpty : bDestroyWhenEmpty;
}
float AActivePropActor::GetMaxStrength() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->MaxStrength : 1.0f;
}
int32 AActivePropActor::GetEmptyBottleProductID() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->EmptyBottleProductID : 0;
}
float AActivePropActor::GetMaxOverfillLevel() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->MaxOverfillLevel : 1.7f;
}
float AActivePropActor::GetThermalMass() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->ThermalMass : 1.0f;
}
float AActivePropActor::GetThermalConductivity() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->ThermalConductivity : 0.05f;
}
float AActivePropActor::GetNormalCapacity() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->NormalCapacity : 1.0f;
}
float AActivePropActor::GetBreakageFailureThreshold() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->BreakageFailureThreshold : 0.0f;
}
EBottleMaterialType AActivePropActor::GetBottleMaterialType() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->BottleMaterialType : EBottleMaterialType::Plastic;
}
bool AActivePropActor::GetEnableInstanceUniqueData() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->bEnableInstanceUniqueData : false;
}
bool AActivePropActor::GetEnableContinuousUpdateData() const
{
	const FPropProductDefinition* D = ResolveProductDefinition();
	return D ? D->bEnableContinuousUpdateData : false;
}
