#include "Props/InteractablePropManager.h"

bool APropManager::ReleaseBottleLiquid(int32 ISID)
{
	if (!HasAuthority() || GetNetMode()==NM_Client) return false;
	const auto* Unique=FindUniqueData(ISID);
	if (!Unique) return false;
	if (Unique->EventData.ContentLevel==0) return true;
	return ReleaseBottleLiquidPortions(ISID,TNumericLimits<double>::Max(),FGuid::NewGuid());
}

bool APropManager::ReleaseBottleLiquidPortions(int32 ISID,double RequestedPortions,FGuid ReleaseEpisode)
{
	if (!HasAuthority() || GetNetMode() == NM_Client || !ReleaseEpisode.IsValid() || !FMath::IsFinite(RequestedPortions) || RequestedPortions<=0) return false;
	const FISIDRuntimeIndex* Record = FindISIDData(ISID);
	const FPropInstanceUniqueData* Unique = FindUniqueData(ISID);
	const FPropProductDefinition* Product = Record ? FindProductDefinition(Record->ProductID) : nullptr;
	if (!Record || !Unique || !Product || !FMath::IsFinite(Unique->EventData.ContentLevel) || Unique->EventData.ContentLevel < 0) return false;
	if (Unique->EventData.ContentLevel == 0) return true;
	if (!FindContentDefinition(Unique->EventData.ContentID) || Product->ConsumableProfile.NumOfPortions <= 0) return false;
	const double Available=double(Unique->EventData.ContentLevel)*Product->ConsumableProfile.NumOfPortions;
	const double Requested=FMath::Min(Available,RequestedPortions);
	const float Remaining=Requested>=Available ? 0.f : FMath::Max(0.f,Unique->EventData.ContentLevel-float(Requested/Product->ConsumableProfile.NumOfPortions));
	const double Released=Available-double(Remaining)*Product->ConsumableProfile.NumOfPortions;
	if (Released<=0) return false;
	const int32 Existing=ReleasedLiquids.IndexOfByPredicate([&](const auto& Item){return Item.ReleaseID==ReleaseEpisode;});
	if (Existing!=INDEX_NONE && (ReleasedLiquids[Existing].SourceISID!=ISID || ReleasedLiquids[Existing].ContentID!=Unique->EventData.ContentID || ReleasedLiquids[Existing].ContentCreationTime!=Unique->EventData.ContentCreationTime)) return false;
	FReleasedLiquid Liquid=Existing!=INDEX_NONE ? ReleasedLiquids[Existing] : FReleasedLiquid();
	Liquid.ReleaseID = ReleaseEpisode; Liquid.SourceISID = ISID; Liquid.SourceProductID = Record->ProductID;
	Liquid.ContentID = Unique->EventData.ContentID;
	Liquid.Portions += Released;
	Liquid.Temperature = Unique->ContinuousData.Temperature;
	Liquid.ContentCreationTime = Unique->EventData.ContentCreationTime; Liquid.ReleaseTime = WorldElapsedSeconds;
	Liquid.Transform = GetTransform(ISID);
	if (!ValidateReleasedLiquids({Liquid}, WorldElapsedSeconds)) return false;
	FPropEventSyncDelta Empty; Empty.ISID = ISID; Empty.ContentLevel = Remaining; Empty.ContentID = Remaining>0 ? Unique->EventData.ContentID : 0;
	Empty.DirtyMask = static_cast<uint8>(EPropEventDirtyMask::ContentLevel);
	if (Remaining<=0) Empty.DirtyMask|=static_cast<uint8>(EPropEventDirtyMask::ContentID);
	// Make both sides of the transfer visible to callbacks only after the canonical commit.
	const FReleasedLiquid Previous=Existing!=INDEX_NONE ? ReleasedLiquids[Existing] : FReleasedLiquid();
	if (Existing!=INDEX_NONE) ReleasedLiquids[Existing]=Liquid; else ReleasedLiquids.Add(Liquid);
	if (!SubmitAtomicInstanceEventUpdates({Empty}))
	{
		if (Existing!=INDEX_NONE) ReleasedLiquids[Existing]=Previous; else ReleasedLiquids.Pop();
		return false;
	}
	OnLiquidReleasesChanged.Broadcast(); ForceNetUpdate();
	return true;
}

void APropManager::OnRep_ReleasedLiquids() { OnLiquidReleasesChanged.Broadcast(); }

bool APropManager::ValidateReleasedLiquids(const TArray<FReleasedLiquid>& Liquids, double WorldTime)
{
	if (!FMath::IsFinite(WorldTime) || WorldTime < 0) return false;
	TSet<FGuid> IDs;
	for (const auto& Liquid : Liquids)
	{
		if (!Liquid.ReleaseID.IsValid() || IDs.Contains(Liquid.ReleaseID) || Liquid.SourceISID <= 0 || Liquid.SourceProductID <= 0 || Liquid.ContentID <= 0
			|| !FMath::IsFinite(Liquid.Portions) || Liquid.Portions <= 0 || !FMath::IsFinite(Liquid.Temperature)
			|| !FMath::IsFinite(Liquid.ContentCreationTime) || Liquid.ContentCreationTime < 0 || Liquid.ContentCreationTime > Liquid.ReleaseTime
			|| !FMath::IsFinite(Liquid.ReleaseTime) || Liquid.ReleaseTime > WorldTime || Liquid.ReleaseTime < 0 || Liquid.Transform.ContainsNaN()) return false;
		IDs.Add(Liquid.ReleaseID);
	}
	return true;
}
