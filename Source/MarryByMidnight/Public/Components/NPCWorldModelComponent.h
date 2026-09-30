#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCEnumsAndTypes.h"
#include "NPCWorldModelComponent.generated.h"

class ANPCOperableObject;
class ANPCConsumableProp;


DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnNewObjectDiscovered, FName, ObjectId, FGameplayTag, RequiredInput, FVector, Location);

/**
 * Represents the NPC's subjective, imperfect knowledge of the world:
 * What machines do, what inputs they accept, and where objects are found.
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UNPCWorldModelComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCWorldModelComponent();

	/** Personal memory of discovered objects and machines */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knowledge")
	TMap<FName, FDiscoveredObjectInfo> DiscoveredObjects;

	/** Event fired when the NPC personally discovers a new object or machine function */
	UPROPERTY(BlueprintAssignable, Category = "Knowledge|Events")
	FOnNewObjectDiscovered OnNewObjectDiscovered;

	/** Records an interaction discovery (e.g. stove takes meat) and notifies the global cultural subsystem */
	UFUNCTION(BlueprintCallable, Category = "Knowledge")
	void RecordDiscovery(FName ObjectId, FGameplayTag InputTag, FGameplayTag OutputTag, FVector Location);

	/** Returns true if this NPC knows about the object */
	UFUNCTION(BlueprintPure, Category = "Knowledge")
	bool IsObjectKnown(FName ObjectId) const;

	/** Returns known locations where this object/ingredient was observed */
	UFUNCTION(BlueprintPure, Category = "Knowledge")
	bool GetKnownLocations(FName ObjectId, TArray<FVector>& OutLocations) const;

	/** Personal memory of discovered places and their stat effects */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knowledge|Places")
	TMap<FName, FDiscoveredPlaceInfo> DiscoveredPlaces;

	/** Records memory of a place and its stat effect */
	UFUNCTION(BlueprintCallable, Category = "Knowledge|Places")
	void RecordPlaceEffect(FName PlaceId, ENPCSimulationStat Stat, float ChangeRate, FVector Location);

	/** Finds best known location that restores/improves the requested stat */
	UFUNCTION(BlueprintPure, Category = "Knowledge|Places")
	bool FindBestKnownPlaceForStat(ENPCSimulationStat Stat, bool bSeekingIncrease, FVector& OutLocation) const;

	// ---------------------------------------------------------
	// Operable Machine & Consumable Prop Memory
	// ---------------------------------------------------------
	/** Personal memory of discovered operable machines (what they require and what they offer) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knowledge|Machines")
	TMap<FName, FDiscoveredMachineMemory> RememberedMachines;

	/** Personal memory of discovered consumable props (location, price, portions, freshness, stimuli) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knowledge|Props")
	TMap<FName, FDiscoveredPropMemory> RememberedProps;

	/** Records or updates memory of an operable machine (location, requirements, offerings) */
	UFUNCTION(BlueprintCallable, Category = "Knowledge|Machines")
	void RememberOperableMachine(ANPCOperableObject* Machine);

	/** Records or updates memory of a consumable prop */
	UFUNCTION(BlueprintCallable, Category = "Knowledge|Props")
	void RememberConsumableProp(ANPCConsumableProp* Prop);

	/** Finds best known source for a specific stat (Hunger, Thirst, Power, Stress) in memory */
	UFUNCTION(BlueprintCallable, Category = "Knowledge")
	bool FindBestKnownSourceForStat(
		ENPCSimulationStat Stat,
		bool bSeekingIncrease,
		float AvailableCash,
		FVector& OutLocation,
		AActor*& OutTargetActor,
		FName& OutOfferingId) const;

	/** Finds an opportunistic scavenge prop (half full, getting low/expiring, cheap/free) */
	UFUNCTION(BlueprintCallable, Category = "Knowledge")
	bool FindOpportunisticScavengeProp(
		float AvailableCash,
		FVector& OutLocation,
		ANPCConsumableProp*& OutProp) const;
};

