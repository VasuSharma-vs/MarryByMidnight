#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NPCEnumsAndTypes.h"
#include "NPCKnowledgeSubsystem.generated.h"

class UNPCWorldModelComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPermanentKnowledgeUnlocked, FName, ObjectId);

/**
 * World subsystem tracking NPC cultural discovery.
 * When > 50% of NPCs discover an object's function or recipe, it gets permanently
 * saved to disk, and future game sessions inherit this knowledge and its top 5 locations.
 */
UCLASS()
class MARRYBYMIDNIGHT_API UNPCKnowledgeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Global map of cultural knowledge */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Knowledge")
	TMap<FName, FGlobalObjectKnowledge> GlobalKnowledgeMap;

	/** Fired when > 50% discovery threshold is reached and knowledge is permanently saved */
	UPROPERTY(BlueprintAssignable, Category = "NPC Knowledge|Events")
	FOnPermanentKnowledgeUnlocked OnPermanentKnowledgeUnlocked;

	/** Registers an active NPC in the world */
	UFUNCTION(BlueprintCallable, Category = "NPC Knowledge")
	void RegisterNPC(AActor* NPCActor);

	/** Unregisters an NPC upon death or despawn */
	UFUNCTION(BlueprintCallable, Category = "NPC Knowledge")
	void UnregisterNPC(AActor* NPCActor);

	/** Records an object discovery by an individual NPC */
	UFUNCTION(BlueprintCallable, Category = "NPC Knowledge")
	void RecordDiscovery(FName ObjectId, FGameplayTag RequiredTag, FGameplayTag ProducedTag, FVector Location, AActor* DiscovererNPC);

	/** Global map of permanent places and their effects (saved to disk) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Knowledge")
	TMap<FName, FDiscoveredPlaceInfo> PermanentPlacesMap;

	/** Global network map of machines uploaded by NPCs via computer/laptop */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Knowledge")
	TMap<FName, FDiscoveredMachineMemory> GlobalMachinesMap;

	/** Global network map of props uploaded by NPCs via computer/laptop */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Knowledge")
	TMap<FName, FDiscoveredPropMemory> GlobalPropsMap;

	/** Uploads an NPC's personal discovered machines and props to the global internet database (called when using computer) */
	UFUNCTION(BlueprintCallable, Category = "NPC Knowledge")
	void UploadFromNPC(UNPCWorldModelComponent* SourceModel, AActor* NPCActor);

	/** Downloads all known global network machines and props into the NPC's personal memory (called when using computer) */
	UFUNCTION(BlueprintCallable, Category = "NPC Knowledge")
	void DownloadToNPC(UNPCWorldModelComponent* TargetModel);

	/** Records a place discovery by an individual NPC */
	UFUNCTION(BlueprintCallable, Category = "NPC Knowledge")
	void RecordPlaceDiscovery(FName PlaceId, ENPCSimulationStat Stat, float ChangeRate, FVector Location, AActor* DiscovererNPC, bool bForceImmediatePermanentSave = false);

	/** Injects permanently known objects, places, and top locations into a newly spawned NPC */
	UFUNCTION(BlueprintCallable, Category = "NPC Knowledge")
	void PopulateKnownCulturalData(UNPCWorldModelComponent* TargetModel);


	/** Saves permanent knowledge to disk slot */
	UFUNCTION(BlueprintCallable, Category = "NPC Knowledge")
	void SaveKnowledgeToDisk();

	/** Loads permanent knowledge from disk slot */
	UFUNCTION(BlueprintCallable, Category = "NPC Knowledge")
	void LoadKnowledgeFromDisk();

private:
	TSet<TWeakObjectPtr<AActor>> RegisteredNPCs;
	TMap<FName, TSet<TWeakObjectPtr<AActor>>> DiscoveredByNPCs;
	TMap<FName, TArray<FVector>> DiscoveredLocationsMap;

	const FString SaveSlotName = TEXT("NPC_CulturalKnowledge");

	void PromoteToPermanentStorage(FName ObjectId);
	void ComputeTop5Locations(FName ObjectId, TArray<FVector>& OutTop5);
};
