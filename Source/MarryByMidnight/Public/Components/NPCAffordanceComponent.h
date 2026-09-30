#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCEnumsAndTypes.h"
#include "NPCAffordanceComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAffordanceExecuted, const FAffordanceDefinition&, Affordance, AActor*, InstigatorActor);

/**
 * Exposes atomic affordances (what can be done to or by this object/NPC) for the emergent planning engine.
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UNPCAffordanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCAffordanceComponent();

	/** List of atomic actions offered by this object/actor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordances")
	TArray<FAffordanceDefinition> ProvidedAffordances;

	/** Event triggered when an affordance is successfully executed */
	UPROPERTY(BlueprintAssignable, Category = "Affordances|Events")
	FOnAffordanceExecuted OnAffordanceExecuted;

	/** Adds an affordance at runtime */
	UFUNCTION(BlueprintCallable, Category = "Affordances")
	void RegisterAffordance(const FAffordanceDefinition& InAffordance);

	/** Removes affordances by action name */
	UFUNCTION(BlueprintCallable, Category = "Affordances")
	void UnregisterAffordance(FName ActionName);

	/** Returns true if this object exposes a specific affordance action */
	UFUNCTION(BlueprintPure, Category = "Affordances")
	bool HasAffordance(EAffordanceAction ActionType) const;

	/** Attempts to find affordance definition by action type */
	UFUNCTION(BlueprintPure, Category = "Affordances")
	bool GetAffordance(EAffordanceAction ActionType, FAffordanceDefinition& OutAffordance) const;

	/** Executes the affordance action, applying effects and broadcasting event */
	UFUNCTION(BlueprintCallable, Category = "Affordances")
	bool ExecuteAffordance(EAffordanceAction ActionType, AActor* InstigatorActor);
};
