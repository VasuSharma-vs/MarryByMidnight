#if WITH_EDITOR
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/RocketProjectileComponent.h"
#include "HAL/IConsoleManager.h"

namespace
{
// Explicit editor migration only. The caller backs up, recompiles and saves the asset.
// A component template must use its own class's CDO as archetype; RF_ClassDefaultObject
// incorrectly selects ActorComponent's CDO and crashes cooked async loading.
FAutoConsoleCommand RepairBottleRocketTemplate(
	TEXT("Cola.RepairBottleRocketTemplate"),
	TEXT("Repair the legacy bottle rocket SCS template flags in memory; does not save assets."),
	FConsoleCommandDelegate::CreateLambda([]
	{
		auto* Blueprint=LoadObject<UBlueprint>(nullptr,TEXT("/Game/Props/Colas/BP_Cola.BP_Cola"));
		if (!Blueprint || !Blueprint->SimpleConstructionScript) return;
		for (auto* Node:Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			auto* Template=Node ? Cast<URocketProjectileComponent>(Node->ComponentTemplate) : nullptr;
			if (!Template || !Template->HasAnyFlags(RF_ClassDefaultObject)) continue;
			Template->Modify();
			Template->ClearFlags(RF_ClassDefaultObject);
			const FString Name=Node->GetVariableName().ToString()+TEXT("_GEN_VARIABLE");
			if (!Template->Rename(*Name,Template->GetOuter(),REN_DontCreateRedirectors | REN_NonTransactional))
				UE_LOG(LogTemp,Error,TEXT("Bottle rocket template rename failed"));
			Blueprint->MarkPackageDirty();
			UE_LOG(LogTemp,Display,TEXT("BOTTLE_ROCKET_TEMPLATE_REPAIRED path=%s archetype=%s valid=%d"),
				*Template->GetPathName(),*GetPathNameSafe(Template->GetArchetype()),Template->GetArchetype()->IsA(Template->GetClass()));
		}
	}));
}
#endif
