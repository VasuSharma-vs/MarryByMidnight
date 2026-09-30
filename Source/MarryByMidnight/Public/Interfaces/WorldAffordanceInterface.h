#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "NPCEnumsAndTypes.h"
#include "WorldAffordanceInterface.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UWorldAffordanceInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Universal interface for world objects, machines, and tools to answer:
 * "Can I interact with you? What options do you give me?"
 */
class MARRYBYMIDNIGHT_API IWorldAffordanceInterface
{
	GENERATED_BODY()

public:
	/**
	 * Asks the object: "Can I interact with you right now?"
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Affordance Interface")
	bool CanInteract(AActor* InstigatorActor);

	/**
	 * Asks the object: "What options do you offer me?"
	 * (e.g. Ladder returns: Climb, Carry, Bridge. Stoves return: Cook Meat, etc.)
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Affordance Interface")
	void QueryAffordanceOptions(AActor* InstigatorActor, TArray<FAffordanceOption>& OutOptions);

	/**
	 * Instructs the object to execute a selected option.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Affordance Interface")
	bool ExecuteAffordanceOption(FName OptionId, AActor* InstigatorActor);
};
