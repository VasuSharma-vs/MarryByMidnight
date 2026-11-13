#include "Props/InteractablePropManager.h"
#include "Props/ColaPricingLibrary.h"
#include "Props/InteractableProp.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Curves/CurveFloat.h"

namespace
{
	bool IsExpiredSupply(const FPropInstanceUniqueData& Unique, const FPropContentDefinition& Content, double Now)
	{
		return Content.ExpiryProfile.bHasExpiry && Now - Unique.EventData.ContentCreationTime >= Content.ExpiryProfile.ExpiryLifetimeMinutes * 60.0;
	}
}

FGuid APropManager::BeginLiquidTransfer(int32 SourceID, int32 TargetID)
{
	if (!HasAuthority() || SourceID == TargetID) return FGuid();
	const FISIDRuntimeIndex* SourceRecord = FindISIDData(SourceID);
	const FISIDRuntimeIndex* TargetRecord = FindISIDData(TargetID);
	const FPropInstanceUniqueData* Source = FindUniqueData(SourceID);
	const FPropInstanceUniqueData* Target = FindUniqueData(TargetID);
	const FPropProductDefinition* SourceDef = SourceRecord ? FindProductDefinition(SourceRecord->ProductID) : nullptr;
	const FPropProductDefinition* TargetDef = TargetRecord ? FindProductDefinition(TargetRecord->ProductID) : nullptr;
	const FPropContentDefinition* Content = Source ? FindContentDefinition(Source->EventData.ContentID) : nullptr;
	if (!SourceDef || !TargetDef || !Source || !Target || !Content || SourceDef->ConsumableProfile.NumOfPortions <= 0 || TargetDef->ConsumableProfile.NumOfPortions <= 0
		|| Source->EventData.ContentLevel <= 0 || Target->EventData.CapSealed > 0 || Target->EventData.AttachedCapISID>0 || !UColaPricingLibrary::CanReuseBottle(Target->EventData.Strength, Target->EventData.bClean, Target->EventData.ContentLevel)
		|| IsExpiredSupply(*Source, *Content, WorldElapsedSeconds) || (!TargetDef->SupportedContentIDs.IsEmpty() && !TargetDef->SupportedContentIDs.Contains(Content->ContentID))) return FGuid();
	for (const auto& Pair : FillSessions) if (Pair.Value.TargetISID == TargetID) return FGuid();
	if (ShopCheckout)
	{
		FShopStockEntry Entry;
		if ((ShopCheckout->GetStockEntry(SourceID, Entry) && (!ShopCheckout->IsAvailableShopStock(SourceID)))
			|| (ShopCheckout->GetStockEntry(TargetID, Entry) && (!ShopCheckout->IsAvailableShopStock(TargetID)))) return FGuid();
	}
	const FGuid ID = FGuid::NewGuid();
	FFillSession Session; Session.SourceISID = SourceID; Session.TargetISID = TargetID; Session.ContentID = Content->ContentID;
	FillSessions.Add(ID, Session);
	return ID;
}

float APropManager::TransferLiquid(FGuid SessionID, float RequestedPortions)
{
	if (!HasAuthority() || !FMath::IsFinite(RequestedPortions) || RequestedPortions <= 0) return 0;
	const FFillSession* Found = FillSessions.Find(SessionID);
	if (!Found) return 0;
	const FFillSession Session = *Found;
	FISIDRuntimeIndex* SourceRecord = FindISIDData(Session.SourceISID);
	FISIDRuntimeIndex* TargetRecord = FindISIDData(Session.TargetISID);
	const FPropInstanceUniqueData* Source = FindUniqueData(Session.SourceISID);
	const FPropInstanceUniqueData* Target = FindUniqueData(Session.TargetISID);
	const FPropProductDefinition* SourceDef = SourceRecord ? FindProductDefinition(SourceRecord->ProductID) : nullptr;
	const FPropProductDefinition* TargetDef = TargetRecord ? FindProductDefinition(TargetRecord->ProductID) : nullptr;
	const FPropContentDefinition* Content = FindContentDefinition(Session.ContentID);
	if (!Source || !Target || !SourceDef || !TargetDef || !Content || Source->EventData.ContentID != Session.ContentID
		|| (Target->EventData.ContentID && Target->EventData.ContentID != Session.ContentID) || Target->EventData.CapSealed > 0 || Target->EventData.AttachedCapISID>0
		|| Target->EventData.Strength <= 0.70f || !Target->EventData.bClean || IsExpiredSupply(*Source, *Content, WorldElapsedSeconds))
	{
		EndLiquidTransfer(SessionID); return 0;
	}
	if (ShopCheckout)
	{
		FShopStockEntry Entry;
		if ((ShopCheckout->GetStockEntry(Session.SourceISID, Entry) && !ShopCheckout->IsAvailableShopStock(Session.SourceISID)) || (ShopCheckout->GetStockEntry(Session.TargetISID, Entry) && !ShopCheckout->IsAvailableShopStock(Session.TargetISID))) { EndLiquidTransfer(SessionID); return 0; }
	}
	const int32 SourcePortions = SourceDef->ConsumableProfile.NumOfPortions, TargetPortions = TargetDef->ConsumableProfile.NumOfPortions;
	if (SourcePortions <= 0 || TargetPortions <= 0) { EndLiquidTransfer(SessionID); return 0; }
	const bool Deforms = TargetDef->BottleMaterialType == EBottleMaterialType::Plastic || TargetDef->BottleMaterialType == EBottleMaterialType::HeavyMetal;
	const float Capacity = TargetDef->MaxOverfillLevel * (Deforms ? FMath::Clamp(Target->EventData.Strength, 0.0f, 1.0f) : 1.0f);
	const float Flow = TargetDef->FillSlowdownCurve ? TargetDef->FillSlowdownCurve->GetFloatValue(Target->EventData.ContentLevel) : 1.0f;
	if (!FMath::IsFinite(Flow) || Flow <= 0) return 0;
	const double Available = static_cast<double>(Source->EventData.ContentLevel) * SourcePortions;
	const double Room = FMath::Max(0.0, static_cast<double>(Capacity - Target->EventData.ContentLevel) * TargetPortions);
	const double Accepted = FMath::Min3(static_cast<double>(RequestedPortions) * Flow, Available, Room);
	if (Accepted <= 0 || !FMath::IsFinite(Accepted)) return 0;
	const bool StartsContent = Target->EventData.ContentLevel == 0;
	const int32* FilledPID = TargetDef->FilledProductIDByContentID.Find(Session.ContentID);
	if (StartsContent && FilledPID && *FilledPID != TargetRecord->ProductID)
	{
		const FPropProductDefinition* FilledDef = FindProductDefinition(*FilledPID);
		if (!FilledDef || FilledDef->ConsumableProfile.NumOfPortions != TargetPortions || FilledDef->BottleMaterialType != TargetDef->BottleMaterialType || !FMath::IsNearlyEqual(FilledDef->MaxOverfillLevel, TargetDef->MaxOverfillLevel)) return 0;
		if (!ChangePropProductWithMode(Session.TargetISID, *FilledPID, EPropProductResetMode::PreserveUniqueState)) return 0;
	}
	TargetRecord = FindISIDData(Session.TargetISID);
	if (!TargetRecord) return 0;
	if (TargetRecord->Representation == EPropRepresentation::ISM)
	{
		AActivePropActor* Actor = nullptr;
		if (!PromoteInstanceToASM(Session.TargetISID, EPropActivationReason::Interaction, true, Actor)) return 0;
	}
	// Pool growth/promotion can reallocate maps. Resolve canonical pointers again before committing.
	Source = FindUniqueData(Session.SourceISID); Target = FindUniqueData(Session.TargetISID);
	if (!Source || !Target) return 0;
	FPropEventSyncDelta SourceDelta, TargetDelta;
	SourceDelta.ISID = Session.SourceISID; SourceDelta.SetFlag(EPropEventDirtyMask::ContentLevel);
	SourceDelta.ContentLevel = Accepted >= Available ? 0.0f : FMath::Max(0.0f, Source->EventData.ContentLevel - static_cast<float>(Accepted / SourcePortions));
	TargetDelta.ISID = Session.TargetISID; TargetDelta.SetFlag(EPropEventDirtyMask::ContentLevel); TargetDelta.SetFlag(EPropEventDirtyMask::ContentID);
	TargetDelta.ContentLevel = FMath::Min(Capacity, Target->EventData.ContentLevel + static_cast<float>(Accepted / TargetPortions));
	TargetDelta.ContentID = Session.ContentID;
	if (StartsContent) { TargetDelta.ExtendedDirtyMask = EPropExtendedEventMask::ContentCreationTime; TargetDelta.ContentCreationTime = WorldElapsedSeconds; }
	return SubmitAtomicInstanceEventUpdates({SourceDelta, TargetDelta}) ? static_cast<float>(Accepted) : 0;
}

void APropManager::EndLiquidTransfer(FGuid SessionID) { if (HasAuthority()) FillSessions.Remove(SessionID); }
void APropManager::CancelLiquidTransfers() { if (HasAuthority()) FillSessions.Empty(); }
