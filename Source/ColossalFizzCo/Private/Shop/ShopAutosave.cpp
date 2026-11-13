#include "Props/InteractablePropManager.h"

void APropManager::PerformAutosave()
{
	if (!HasAuthority() || !bEnableAutosave) return;
	bLastAutosaveSucceeded = SavePropDatabase(AutosaveSlot, 0);
	if (!bLastAutosaveSucceeded) UE_LOG(LogTemp, Error, TEXT("Shop autosave failed for slot %s"), *AutosaveSlot);
}
