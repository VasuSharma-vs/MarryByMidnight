#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "NPCEnumsAndTypes.h"
#include "NPCKnowledgeSaveGame.generated.h"

/**
 * Permanent disk save storage for globally unlocked NPC cultural knowledge.
 */
UCLASS()
class MARRYBYMIDNIGHT_API UNPCKnowledgeSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "SaveData")
	TMap<FName, FGlobalObjectKnowledge> PermanentKnowledge;

	/** Permanent disk storage for discovered places and their stat effects */
	UPROPERTY(VisibleAnywhere, Category = "SaveData")
	TMap<FName, FDiscoveredPlaceInfo> PermanentPlaces;

	/** Permanent disk storage for global machine knowledge uploaded via computers */
	UPROPERTY(VisibleAnywhere, Category = "SaveData")
	TMap<FName, FDiscoveredMachineMemory> PermanentMachines;

	/** Permanent disk storage for global prop knowledge uploaded via computers */
	UPROPERTY(VisibleAnywhere, Category = "SaveData")
	TMap<FName, FDiscoveredPropMemory> PermanentProps;
};

