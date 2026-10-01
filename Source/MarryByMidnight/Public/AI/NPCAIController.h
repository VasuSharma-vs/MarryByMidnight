#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "NPCEnumsAndTypes.h"
#include "NPCAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class ANPCCharacter;
class ANPCSimulationTestZone;
class ANPCOperableObject;
class ANPCConsumableProp;


/**
 * Native C++ AI Controller for NPCs in MarryByMidnight.
 * Direct bridge connecting the simulation brain (Needs, Affect, Goals) to physical 3D navigation and interactions.
 */
UCLASS()
class MARRYBYMIDNIGHT_API ANPCAIController : public AAIController
{
	GENERATED_BODY()

public:
	ANPCAIController();

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

public:
	/** AI Perception component with configured sight sense */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Perception")
	TObjectPtr<UAIPerceptionComponent> AIPerceptionComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Perception")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	/** Currently targeted actor for interaction */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Navigation")
	TWeakObjectPtr<AActor> CurrentTargetActor;

	/** Current high-level active goal */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Goal")
	FName CurrentGoalName = FName("Idle");

	/** Manually triggers goal execution */
	UFUNCTION(BlueprintCallable, Category = "AI")
	void ExecuteGoal(FName Goal);

	/** Finds the nearest actor exposing a specific affordance action */
	UFUNCTION(BlueprintCallable, Category = "AI")
	AActor* FindNearestInteractableForAction(EAffordanceAction DesiredAction, float SearchRadius = 3000.0f);

	/** Pending offering ID to execute when reaching target operable machine or prop */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Interaction")
	FName PendingOfferingId = NAME_None;

	/** Parameterized resource search: memory check -> internet computer search -> physical machine reconnaissance */
	UFUNCTION(BlueprintCallable, Category = "AI|Resource")
	void FindResource(const FNPCResourceSearchQuery& Query);

	/** Shortcut for food using unified FindResource */
	UFUNCTION(BlueprintCallable, Category = "AI|Resource")
	void FindFood() { FindResource(FNPCResourceSearchQuery::ForFood()); }

	/** Shortcut for drink using unified FindResource */
	UFUNCTION(BlueprintCallable, Category = "AI|Resource")
	void FindDrink() { FindResource(FNPCResourceSearchQuery::ForDrink()); }

	/** Shortcut for warm place (raising body temperature) */
	UFUNCTION(BlueprintCallable, Category = "AI|Resource")
	void FindWarmPlace() { FindResource(FNPCResourceSearchQuery::ForWarmPlace()); }

	/** Shortcut for chill place (cooling down body temperature) */
	UFUNCTION(BlueprintCallable, Category = "AI|Resource")
	void FindChillPlace() { FindResource(FNPCResourceSearchQuery::ForChillPlace()); }

	/** Shortcut for power */
	UFUNCTION(BlueprintCallable, Category = "AI|Resource")
	void FindPower() { FindResource(FNPCResourceSearchQuery::ForPower()); }

	/** Shortcut for stress relief */
	UFUNCTION(BlueprintCallable, Category = "AI|Resource")
	void FindStressRelief() { FindResource(FNPCResourceSearchQuery::ForStressRelief()); }

	/** Finds the nearest computer or laptop with an active internet connection */
	UFUNCTION(BlueprintCallable, Category = "AI|Resource")
	ANPCOperableObject* FindNearestComputerWithInternet(float MaxRadius = 6000.0f) const;

	/** Starts physical reconnaissance: visits and queries nearby operable machines and objects one by one */
	UFUNCTION(BlueprintCallable, Category = "AI|Resource")
	void StartPhysicalObjectQueryReconnaissance(const FNPCResourceSearchQuery& Query);

	/** Queries the next candidate in the physical reconnaissance queue */
	UFUNCTION(BlueprintCallable, Category = "AI|Resource")
	void QueryNextReconnaissanceCandidate();

	/** Explores outward to locate new resource locations when no places are known */
	UFUNCTION(BlueprintCallable, Category = "AI|Resource")
	void StartSearchingForResource(ENPCSimulationStat NeededStat);

	/** Active pending search query */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Resource")
	FNPCResourceSearchQuery PendingSearchQuery;

	/** Queue of objects to query during physical reconnaissance */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> ReconnaissanceCandidates;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Resource")
	int32 CurrentReconnaissanceIndex = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Resource")
	bool bIsConductingReconnaissance = false;


	/** Timer handle for periodic 10-second power and bed feasibility audits */
	FTimerHandle PowerAuditTimerHandle;


	/** Location recorded during last power audit to detect if new movement occurred */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Power")
	FVector LastAuditLocation = FVector::ZeroVector;

	/** Time interval between power audits (default: 10 seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Power")
	float PowerAuditInterval = 10.0f;

	/** Minimum distance moved (in cm) required to trigger the power audit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Power")
	float MovementThresholdForAudit = 50.0f;

	/** Last evaluated power required to reach nearest bed / power recharge station */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Power")
	float LastPowerToBed = 0.0f;

	/** Last evaluated power cost of the pending task */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Power")
	float LastTaskPowerCost = 0.0f;

	/** Last evaluated total power required (Task + Travel to Bed) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Power")
	float LastTotalPowerRequired = 0.0f;

	/** Whether the NPC currently knows where a bed or power source is */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Power")
	bool bLastAuditKnowsBed = false;

	/** Audits current power vs bed and task requirements. Runs every 10 seconds if movement occurred. */
	UFUNCTION(BlueprintCallable, Category = "AI|Power")
	void AuditPowerAndTaskFeasibility();

	/** Assesses power feasibility: calculates required power for bed and pending task */
	UFUNCTION(BlueprintCallable, Category = "AI|Power")
	bool AssessPowerAndTaskFeasibility(
		float& OutPowerToReachBed,
		float& OutTaskPowerCost,
		float& OutTotalPowerRequired,
		FVector& OutBedLocation,
		bool& bOutKnowsBedOrRecharge
	);

protected:
	UFUNCTION()
	void HandleGoalSelected(FName NewGoal);

	UFUNCTION()
	void HandleNPCEnraged();

	UFUNCTION()
	void HandleToiletAccident(bool bIsBladder);

	UFUNCTION()
	void HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	UFUNCTION()
	void HandlePhysicsStateChanged(bool bIsSimulating);

	UFUNCTION()
	void ResumeAIAfterWakeup();

public:
	/** Notified when possessed NPC enters a simulation zone */
	UFUNCTION(BlueprintCallable, Category = "AI|Simulation")
	void NotifyEnteredSimulationZone(ANPCSimulationTestZone* Zone);

	/** Notified when possessed NPC exits a simulation zone */
	UFUNCTION(BlueprintCallable, Category = "AI|Simulation")
	void NotifyExitedSimulationZone(ANPCSimulationTestZone* Zone);

	/** Enters stationary charging/resting inside a simulation zone */
	UFUNCTION(BlueprintCallable, Category = "AI|Simulation")
	void EnterZoneCharging(ANPCSimulationTestZone* Zone);

	/** Exits charging state and resumes autonomous decision making */
	UFUNCTION(BlueprintCallable, Category = "AI|Simulation")
	void ExitZoneCharging(const FString& Reason);

	/** Checks if currently charging/resting stationary inside a zone */
	UFUNCTION(BlueprintPure, Category = "AI|Simulation")
	bool IsChargingInZone() const { return bIsChargingInZone; }

	/** Whether the NPC is actively stationary charging/resting in a zone */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Simulation")
	bool bIsChargingInZone = false;

	/** The active zone where charging is taking place */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Simulation")
	TWeakObjectPtr<ANPCSimulationTestZone> ActiveChargingZone;

private:
	TWeakObjectPtr<ANPCCharacter> PossessedNPC;
	FTimerHandle RoamTimerHandle;

	void ExecuteRoam();
	void CompleteInteractionAtTarget();
	void ReevaluateGoalAfterInteraction();
};
