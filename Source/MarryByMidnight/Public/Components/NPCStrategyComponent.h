#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCEnumsAndTypes.h"
#include "NPCStrategyComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStrategyDiscovered, const FNPCStrategy&, NewStrategy);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStrategyEvaluated, FName, StrategyId, bool, bWasSuccessful);

/**
 * Stores action sequences, known strategies, discovered loopholes, and social learning memory.
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UNPCStrategyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCStrategyComponent();

protected:
	virtual void BeginPlay() override;

public:
	/** Map of known strategies indexed by StrategyId */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Strategies")
	TMap<FName, FNPCStrategy> KnownStrategies;

	/** Events */
	UPROPERTY(BlueprintAssignable, Category = "Strategies|Events")
	FOnStrategyDiscovered OnStrategyDiscovered;

	UPROPERTY(BlueprintAssignable, Category = "Strategies|Events")
	FOnStrategyEvaluated OnStrategyEvaluated;

	/** Records outcome after attempting a strategy; adjusts success rate and confidence */
	UFUNCTION(BlueprintCallable, Category = "Strategies")
	void RecordStrategyOutcome(FName StrategyId, bool bSuccess, float RewardGained, float RiskEncountered);

	/** Copies a strategy observed from another NPC (Social Learning) */
	UFUNCTION(BlueprintCallable, Category = "Strategies")
	void LearnObservedStrategy(const FNPCStrategy& InStrategy);

	/** Adds a newly discovered strategy (from experimentation or forward planning) */
	UFUNCTION(BlueprintCallable, Category = "Strategies")
	void RegisterDiscoveredStrategy(const FNPCStrategy& NewStrategy);

	/** Finds highest confidence strategy for a given goal (e.g. SatisfyHunger) */
	UFUNCTION(BlueprintCallable, Category = "Strategies")
	bool GetBestStrategyForGoal(FName Goal, FNPCStrategy& OutStrategy) const;

	/** Returns true if NPC has any known strategy for goal */
	UFUNCTION(BlueprintPure, Category = "Strategies")
	bool HasStrategyForGoal(FName Goal) const;

private:
	void SeedBaselineStrategies();
};
