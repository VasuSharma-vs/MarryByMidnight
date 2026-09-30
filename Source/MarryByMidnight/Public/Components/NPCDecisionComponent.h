#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCEnumsAndTypes.h"
#include "NPCDecisionComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNPCGoalSelected, FName, NewGoal);

class ANPCSimulationTestZone;
class ANPCOperableObject;


/**
 * Utility AI decision engine: scores biological drives, risk, and personality
 * to select the active goal and decides whether to reuse a known strategy or experiment.
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UNPCDecisionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCDecisionComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/** Currently active selected goal */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Decision")
	FName ActiveGoal = FName("None");

	/** Evaluated utility score of the currently selected goal */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Decision")
	float ActiveGoalScore = 0.0f;

	/** Full human-readable diagnostic summary of the last self-assessment */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Decision")
	FString LastSelfAssessmentSummary;

	/** Event fired when high-level goal changes */
	UPROPERTY(BlueprintAssignable, Category = "Decision|Events")
	FOnNPCGoalSelected OnNPCGoalSelected;

	/** Evaluates all utility scores and selects the highest priority goal */
	UFUNCTION(BlueprintCallable, Category = "Decision")
	FName EvaluateGoals();

	/** Introspective self-check: audits all internal drives, reinforcement learning strategies, and memory to decide what to handle first */
	UFUNCTION(BlueprintCallable, Category = "Decision")
	FName PerformSelfAssessment(FString& OutDiagnosticReasoning);

	/** Returns true if NPC should bypass normal buying and attempt emergent exploration/theft */
	UFUNCTION(BlueprintCallable, Category = "Decision")
	bool ShouldExploreNovelStrategy(FName ForGoal) const;

	/** Returns current active goal */
	UFUNCTION(BlueprintPure, Category = "Decision")
	FName GetActiveGoal() const { return ActiveGoal; }

	/** Returns short diagnostic summary for overhead UI */
	UFUNCTION(BlueprintPure, Category = "Decision")
	FString GetSelfAssessmentSummary() const { return LastSelfAssessmentSummary; }

	/** Returns the inherent desirability sign for a stat (+1.0 = increasing is good, -1.0 = increasing is bad) */
	UFUNCTION(BlueprintPure, Category = "Decision|Stimulus")
	float GetStatDesirabilitySign(ENPCSimulationStat Stat) const;

	/** Evaluates a single stimulus based on current biological deficits and personality */
	UFUNCTION(BlueprintCallable, Category = "Decision|Stimulus")
	FNPCStimulusEvaluation EvaluateSingleStimulus(ENPCSimulationStat Stat, float ChangeRate) const;

	/** Evaluates an array of stimuli modifiers and computes net utility, total benefit, cost, and recommended action */
	UFUNCTION(BlueprintCallable, Category = "Decision|Stimulus")
	FNPCZoneStimuliEvaluation EvaluateStimuliArray(const TArray<FNPCSimulationStatModifier>& Stimuli) const;

	/** Evaluates the stimuli of an environmental zone (ANPCSimulationTestZone) */
	UFUNCTION(BlueprintCallable, Category = "Decision|Stimulus")
	FNPCZoneStimuliEvaluation EvaluateZoneStimuli(const ANPCSimulationTestZone* Zone) const;

	/** Makes an autonomous action decision for a zone based on detected stimuli values */
	UFUNCTION(BlueprintCallable, Category = "Decision|Stimulus")
	FName DecideActionFromZoneStimuli(const ANPCSimulationTestZone* Zone, FString& OutReasoning);

	/** Stores the last zone stimulus evaluation result for debugging and UI display */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Decision|Stimulus")
	FNPCZoneStimuliEvaluation LastZoneEvaluation;

	/** Evaluates an operable machine offering (incorporating money cost vs utility) */
	UFUNCTION(BlueprintCallable, Category = "Decision|Operable")
	float EvaluateOperableOffering(const FNPCOperableOffering& Offering, FString& OutReasoning) const;

	/** Evaluates all options from an operable object and returns the best offering ID, or NAME_None if none provide positive net utility */
	UFUNCTION(BlueprintCallable, Category = "Decision|Operable")
	FName DecideBestOperableOffering(const ANPCOperableObject* OperableObject, FString& OutReasoning) const;

private:

	FTimerHandle DecisionTimerHandle; // Runs every 2.0s
	void PeriodicDecisionTick();
};
