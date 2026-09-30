#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCEnumsAndTypes.h"
#include "NPCNeedsComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNeedUrgencyTriggered, FName, NeedName, float, CurrentUrgency);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnToiletEmergencyFailed, bool, bIsBladder);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFoodComaEntered);

/**
 * Manages biological homeostasis and drives: Hunger, Thirst, Sleepiness, and Bladder/Bowel excretion.
 * Fully networked and replicated for multiplayer.
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UNPCNeedsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCNeedsComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	/** Core needs state (Replicated to clients) */
	UPROPERTY(ReplicatedUsing = OnRep_Needs, EditAnywhere, BlueprintReadWrite, Category = "Needs")
	FNPCNeeds Needs;

	/** Hunger accumulation per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needs|Tuning")
	float HungerRate = 0.05f;

	/** Thirst accumulation per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needs|Tuning")
	float ThirstRate = 0.08f;

	/** Sleepiness accumulation per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needs|Tuning")
	float SleepinessRate = 0.03f;

	/** Hunger accumulation multiplier while sleeping anywhere (floor or resting in volume). Increases hunger faster while asleep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needs|Tuning", meta = (DisplayName = "Sleeping Hunger Multiplier"))
	float SleepingHungerMultiplier = 3.0f;

	/** Additional hunger incurred directly when resting/sleeping in a bed per unit of rest */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needs|Tuning", meta = (DisplayName = "Rest Hunger Cost Ratio"))
	float RestHungerCostRatio = 0.35f;

	/** Multiplier for bladder filling per unit of drink consumed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needs|Tuning")
	float DrinkToBladderRatio = 0.6f;

	/** Multiplier for bowel filling per unit of food consumed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needs|Tuning")
	float FoodToBowelRatio = 0.4f;

	/** Urgency threshold before an NPC actively seeks relief [0-100] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needs|Tuning")
	float UrgentThreshold = 80.0f;

	/** Events */
	UPROPERTY(BlueprintAssignable, Category = "Needs|Events")
	FOnNeedUrgencyTriggered OnNeedUrgencyTriggered;

	UPROPERTY(BlueprintAssignable, Category = "Needs|Events")
	FOnToiletEmergencyFailed OnToiletEmergencyFailed;

	UPROPERTY(BlueprintAssignable, Category = "Needs|Events")
	FOnFoodComaEntered OnFoodComaEntered;

	/** Consumes food, satisfies hunger, advances digestion, and may trigger food coma */
	UFUNCTION(BlueprintCallable, Category = "Needs|Actions")
	void ConsumeFood(float NutritionalValue, bool bCanInduceFoodComa = true);

	/** Consumes beverage, satisfies thirst, and fills bladder */
	UFUNCTION(BlueprintCallable, Category = "Needs|Actions")
	void ConsumeDrink(float HydrationAmount, bool bIsAlcohol = false);

	/** Relieves bladder in toilet */
	UFUNCTION(BlueprintCallable, Category = "Needs|Actions")
	void RelieveBladder();

	/** Relieves bowel in toilet */
	UFUNCTION(BlueprintCallable, Category = "Needs|Actions")
	void RelieveBowel();

	/** Clears sleepiness after resting */
	UFUNCTION(BlueprintCallable, Category = "Needs|Actions")
	void Rest(float RestAmount);

	/** Returns true if any vital need is above urgent threshold */
	UFUNCTION(BlueprintPure, Category = "Needs|Getters")
	bool HasUrgentNeed() const;

	/** Gets highest priority need name */
	UFUNCTION(BlueprintPure, Category = "Needs|Getters")
	FName GetDominantNeed() const;

protected:
	UFUNCTION()
	void OnRep_Needs();

private:
	FTimerHandle NeedsUpdateTimerHandle; // 2 Hz (0.5s)

	bool bHungerUrgentFired = false;
	bool bThirstUrgentFired = false;
	bool bBladderUrgentFired = false;
	bool bBowelUrgentFired = false;
	bool bSleepinessUrgentFired = false;

	void UpdateNeeds();
};
