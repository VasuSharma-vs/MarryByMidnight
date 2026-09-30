#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCEnumsAndTypes.h"
#include "NPCPlannerComponent.generated.h"

USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FExecutablePlanStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planner")
	FAffordanceOption Option;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planner")
	TWeakObjectPtr<AActor> TargetActor = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlanCompleted, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlanStepStarted, int32, StepIndex, const FAffordanceOption&, StepOption);

/**
 * Forward-search action planner: constructs multi-step action sequences
 * from available world affordances to solve goals and discover emergent loopholes.
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UNPCPlannerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCPlannerComponent();

	/** Currently active executing plan */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planner")
	TArray<FExecutablePlanStep> ActivePlan;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planner")
	int32 CurrentStepIndex = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planner")
	bool bIsExecutingPlan = false;

	/** Events */
	UPROPERTY(BlueprintAssignable, Category = "Planner|Events")
	FOnPlanCompleted OnPlanCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Planner|Events")
	FOnPlanStepStarted OnPlanStepStarted;

	/** Generates a novel action plan by chaining perceived world affordances toward a goal */
	UFUNCTION(BlueprintCallable, Category = "Planner")
	bool BuildPlanForGoal(FName Goal, const TArray<AActor*>& PerceivedObjects);

	/** Executes the current step of the plan */
	UFUNCTION(BlueprintCallable, Category = "Planner")
	void AdvancePlan();

	/** Aborts plan upon failure, interruption, or danger */
	UFUNCTION(BlueprintCallable, Category = "Planner")
	void AbortPlan(const FString& Reason);
};
