#include "Props/InteractablePropManager.h"

bool APropManager::ReleaseBottleLiquid(int32 ISID)
{
	if (!HasAuthority() || GetNetMode() == NM_Client) return false;
	const FISIDRuntimeIndex* Record = FindISIDData(ISID);
	const FPropInstanceUniqueData* Unique = FindUniqueData(ISID);
	const FPropProductDefinition* Product = Record ? FindProductDefinition(Record->ProductID) : nullptr;
	if (!Record || !Unique || !Product || !FMath::IsFinite(Unique->EventData.ContentLevel) || Unique->EventData.ContentLevel < 0) return false;
	if (Unique->EventData.ContentLevel == 0) return true;
	if (!FindContentDefinition(Unique->EventData.ContentID) || Product->ConsumableProfile.NumOfPortions <= 0) return false;
	FReleasedLiquid Liquid;
	Liquid.ReleaseID = FGuid::NewGuid(); Liquid.SourceISID = ISID; Liquid.SourceProductID = Record->ProductID;
	Liquid.ContentID = Unique->EventData.ContentID;
	Liquid.Portions = double(Unique->EventData.ContentLevel) * Product->ConsumableProfile.NumOfPortions;
	Liquid.Temperature = Unique->ContinuousData.Temperature;
	Liquid.ContentCreationTime = Unique->EventData.ContentCreationTime; Liquid.ReleaseTime = WorldElapsedSeconds;
	Liquid.Transform = GetTransform(ISID);
	if (!ValidateReleasedLiquids({Liquid}, WorldElapsedSeconds)) return false;
	FPropEventSyncDelta Empty; Empty.ISID = ISID; Empty.ContentLevel = 0; Empty.ContentID = 0;
	Empty.DirtyMask = static_cast<uint8>(EPropEventDirtyMask::ContentLevel | EPropEventDirtyMask::ContentID);
	// Make both sides of the transfer visible to callbacks only after the canonical commit.
	ReleasedLiquids.Add(Liquid);
	if (!SubmitAtomicInstanceEventUpdates({Empty})) { ReleasedLiquids.Pop(); return false; }
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
