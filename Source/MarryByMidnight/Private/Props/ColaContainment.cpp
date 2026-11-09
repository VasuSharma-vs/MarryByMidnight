#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Props/ColaPricingLibrary.h"

bool AColaPropActor::CanDemoteToISM() const
{
	return Super::CanDemoteToISM() && !(GetContentLevel() > UE_SMALL_NUMBER && GetCapSealed() < 1.0f)
		&& InteractionPressure <= UE_KINDA_SMALL_NUMBER && AcceptedFailureType == EContainmentFailureType::None;
}
void AColaPropActor::ResetTransientPoolState()
{
	Super::ResetTransientPoolState();
	InteractionPressure = FoamPressure = 0;
	AcceptedFailureType = EContainmentFailureType::None;
	ProcessedImpactIDs.Empty(); LastImpactTime = -1;
	DynamicMaterialInstance = nullptr;
	if (CapMeshComponent) { CapMeshComponent->DestroyComponent(); CapMeshComponent = nullptr; }
	if (RocketComp) RocketComp->ResetRocketSimulation();
}
void AColaPropActor::OnApplyEventData(const FPropEventUpdateData& Data, uint8 DirtyMask)
{
	Super::OnApplyEventData(Data, DirtyMask);
	ColaLevel = GetContentLevel();
	UpdateMorphTargets(); UpdateDynamicMaterial(false);
}
bool AColaPropActor::SetBottleContentType(int32 NewContentID)
{
	if (!HasAuthority() || !UColaPricingLibrary::CanReuseBottle(GetStrength(), LocalUniqueData.EventData.bClean, GetContentLevel()) || !PropManagerReference) return false;
	const FPropProductDefinition* Def = ResolveProductDefinition();
	const FPropContentDefinition* Content = PropManagerReference->FindContentDefinition(NewContentID);
	if (!Def || !Content || (!Def->SupportedContentIDs.IsEmpty() && !Def->SupportedContentIDs.Contains(NewContentID))) return false;
	LocalUniqueData.EventData.ContentID = NewContentID;
	bHasValidUniqueData = true;
	OnApplySharedDefinitions(Def, Content);
	EmitPropEventUpdate(EPropEventDirtyMask::ContentID);
	return true;
}
float AColaPropActor::FillBottleAtRate(float Rate, float DeltaSeconds)
{
	if (!HasAuthority() || !FMath::IsFinite(Rate) || !FMath::IsFinite(DeltaSeconds) || Rate <= 0 || DeltaSeconds <= 0 || GetCapSealed() > 0 || GetContentID() <= 0 || AcceptedFailureType != EContainmentFailureType::None) return 0;
	const FPropProductDefinition* Def = ResolveProductDefinition();
	if (!Def) return 0;
	const bool bDeforms = Def->BottleMaterialType == EBottleMaterialType::Plastic || Def->BottleMaterialType == EBottleMaterialType::HeavyMetal;
	const float MaxLevel = Def->MaxOverfillLevel * (bDeforms ? GetStrengthRatio() : 1.0f);
	const float Flow = Def->FillSlowdownCurve ? FMath::Max(0.0f, Def->FillSlowdownCurve->GetFloatValue(GetContentLevel())) : 1.0f;
	const float Accepted = FMath::Min(Rate * DeltaSeconds * Flow, FMath::Max(0.0f, MaxLevel - GetContentLevel()));
	if (!FMath::IsFinite(Accepted) || Accepted <= 0) return 0;
	const bool bStartsContentLifecycle = IsEmpty();
	LocalUniqueData.EventData.ContentLevel = GetContentLevel() + Accepted;
	if (bStartsContentLifecycle) LocalUniqueData.EventData.ContentCreationTime = PropManagerReference ? PropManagerReference->WorldElapsedSeconds : GetWorld()->GetTimeSeconds();
	ColaLevel = LocalUniqueData.EventData.ContentLevel;
	EmitPropEventUpdate(static_cast<uint8>(EPropEventDirtyMask::ContentLevel), bStartsContentLifecycle ? EPropExtendedEventMask::ContentCreationTime : 0);
	return Accepted * GetNumPortions();
}
void AColaPropActor::RemoveBottleContent()
{
	if (!HasAuthority()) return;
	LocalUniqueData.EventData.ContentLevel = 0; LocalUniqueData.EventData.ContentID = 0;
	ColaLevel = 0;
	EmitPropEventUpdate(EPropEventDirtyMask::ContentLevel | EPropEventDirtyMask::ContentID);
}
void AColaPropActor::ApplyBottleImpact(const FVector& Impulse, int32 EventID)
{
	if (!HasAuthority() || Impulse.ContainsNaN() || (EventID != 0 && ProcessedImpactIDs.Contains(EventID))) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (LastImpactTime >= 0 && Now - LastImpactTime < 0.05) return;
	LastImpactTime = Now;
	if (EventID) { if (ProcessedImpactIDs.Num() >= 128) ProcessedImpactIDs.Empty(); ProcessedImpactIDs.Add(EventID); }
	const FPropProductDefinition* Def = ResolveProductDefinition();
	if (!Def || Def->BottleMaterialType == EBottleMaterialType::Diamond) return;
	const float Severity = FMath::Max(0.0f, Impulse.Size() - DropImpactThreshold) / FMath::Max(1.0f, DropImpactThreshold);
	if (Severity <= 0) return;
	LocalUniqueData.EventData.Strength = FMath::Clamp(GetStrength() - Severity * Def->StrengthDamageRate, 0.0f, 1.0f);
	EmitPropEventUpdate(EPropEventDirtyMask::Strength);
	if (GetStrength() <= 0 && (Def->BottleMaterialType == EBottleMaterialType::Glass || Def->BottleMaterialType == EBottleMaterialType::Concrete))
	{
		FContainmentFailureRequest Failure; Failure.FailureType = EContainmentFailureType::BottleBreakage;
		Failure.FailureLocation = GetActorLocation(); Failure.FailureDirection = Impulse.GetSafeNormal();
		SubmitFailureRequest(Failure);
	}
}
void AColaPropActor::ApplyInitialImpactFromPromotion(FVector Location, const FVector& Impulse, AActor* InstigatorActor, int32 EventID)
{
	// The manager applies the bounded physical kick separately; use original impulse only for damage.
	ApplyBottleImpact(Impulse, EventID);
}
bool AColaPropActor::SubmitFailureRequest(const FContainmentFailureRequest& Request)
{
	if (!HasAuthority() || Request.FailureType == EContainmentFailureType::None || (AcceptedFailureType != EContainmentFailureType::None && static_cast<uint8>(AcceptedFailureType) <= static_cast<uint8>(Request.FailureType))) return false;
	AcceptedFailureType = Request.FailureType;
	const FPropProductDefinition* Def = ResolveProductDefinition();
	if (!Def) return false;
	const int32 TargetPID = Request.FailureType == EContainmentFailureType::BottleBreakage ? Def->BrokenProductID : Request.FailureType == EContainmentFailureType::ThermalFailure ? Def->MeltedProductID : Def->BurstBrokenProductID;
	if (Request.bReleaseContent) RemoveBottleContent();
	if (Request.bReleasePressure) { InteractionPressure = FoamPressure = 0; }
	if (TargetPID > 0 && TargetPID != ProductID && PropManagerReference)
		return PropManagerReference->ChangePropProductWithMode(ISID, TargetPID, EPropProductResetMode::PreserveUniqueState);
	ForceNetUpdate();
	return true;
}

FColaPriceQuote AColaPropActor::GetSaleQuote() const
{
	const FPropContentDefinition* Content = ResolveContentDefinition();
	return Content ? UColaPricingLibrary::QuoteCola(GetContentLevel(), GetNumPortions(), Content->BasePortionPrice, GetCurrentTemperature(), Content->IdealServingTemperature, Content->TemperaturePenaltyRange, IsExpired()) : FColaPriceQuote();
}
