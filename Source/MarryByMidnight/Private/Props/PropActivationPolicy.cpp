#include "Props/InteractablePropManager.h"
#include "Components/PropActivationVolumeComponent.h"

void APropManager::RegisterActivationVolume(UPropActivationVolumeComponent* Volume)
{
	if (HasAuthority() && GetNetMode()!=NM_Client && IsValid(Volume) && Volume->GetWorld()==GetWorld())
		ActivationVolumes.Add(Volume);
}

void APropManager::UnregisterActivationVolume(UPropActivationVolumeComponent* Volume)
{
	ActivationVolumes.Remove(Volume);
}

bool APropManager::IsInstanceKeptActive(int32 ISID) const
{
	if (!HasAuthority() || GetNetMode()==NM_Client || ISID<=0) return false;
	for (const auto& WeakVolume:ActivationVolumes)
		if (const auto* Volume=WeakVolume.Get())
			if (Volume->ContainsActiveInstance(this,ISID)) return true;
	return false;
}
