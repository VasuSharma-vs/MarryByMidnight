#include "Components/NPCStrategyComponent.h"

UNPCStrategyComponent::UNPCStrategyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UNPCStrategyComponent::BeginPlay()
{
	Super::BeginPlay();
	SeedBaselineStrategies();
}

void UNPCStrategyComponent::RecordStrategyOutcome(FName StrategyId, bool bSuccess, float RewardGained, float RiskEncountered)
{
	FNPCStrategy* Strategy = KnownStrategies.Find(StrategyId);
	if (!Strategy) return;

	if (bSuccess)
	{
		Strategy->SuccessCount++;
		Strategy->Confidence = FMath::Clamp(Strategy->Confidence + 0.1f, 0.0f, 1.0f);
		Strategy->EstimatedReward = FMath::Lerp(Strategy->EstimatedReward, RewardGained, 0.2f);
	}
	else
	{
		Strategy->FailureCount++;
		Strategy->Confidence = FMath::Clamp(Strategy->Confidence - 0.2f, 0.0f, 1.0f);
		Strategy->EstimatedRisk = FMath::Clamp(Strategy->EstimatedRisk + 0.15f, 0.0f, 1.0f);
	}

	OnStrategyEvaluated.Broadcast(StrategyId, bSuccess);
}

void UNPCStrategyComponent::LearnObservedStrategy(const FNPCStrategy& InStrategy)
{
	FNPCStrategy* Existing = KnownStrategies.Find(InStrategy.StrategyId);
	if (Existing)
	{
		// Boost confidence slightly upon observing someone else succeed
		Existing->Confidence = FMath::Clamp(Existing->Confidence + 0.15f, 0.0f, 1.0f);
	}
	else
	{
		FNPCStrategy Learned = InStrategy;
		Learned.Confidence = 0.5f; // Initial curiosity confidence
		Learned.SuccessCount = 1;
		Learned.FailureCount = 0;
		KnownStrategies.Add(Learned.StrategyId, Learned);
		OnStrategyDiscovered.Broadcast(Learned);
	}
}

void UNPCStrategyComponent::RegisterDiscoveredStrategy(const FNPCStrategy& NewStrategy)
{
	KnownStrategies.Add(NewStrategy.StrategyId, NewStrategy);
	OnStrategyDiscovered.Broadcast(NewStrategy);
}

bool UNPCStrategyComponent::GetBestStrategyForGoal(FName Goal, FNPCStrategy& OutStrategy) const
{
	const FNPCStrategy* Best = nullptr;
	float BestScore = -1.0f;

	for (const auto& Pair : KnownStrategies)
	{
		const FNPCStrategy& Strat = Pair.Value;
		if (Strat.Goal == Goal)
		{
			// Utility score: High confidence + high reward, penalized by risk
			const float Score = (Strat.Confidence * 0.6f) + ((Strat.EstimatedReward / 100.0f) * 0.4f) - (Strat.EstimatedRisk * 0.3f);
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = &Strat;
			}
		}
	}

	if (Best)
	{
		OutStrategy = *Best;
		return true;
	}

	return false;
}

bool UNPCStrategyComponent::HasStrategyForGoal(FName Goal) const
{
	for (const auto& Pair : KnownStrategies)
	{
		if (Pair.Value.Goal == Goal)
		{
			return true;
		}
	}
	return false;
}

void UNPCStrategyComponent::SeedBaselineStrategies()
{
	// Baseline 1: Standard Buy Food
	FNPCStrategy BuyFood;
	BuyFood.StrategyId = FName("Strategy_BuyFood");
	BuyFood.Goal = FName("SatisfyHunger");
	BuyFood.Confidence = 0.90f;
	BuyFood.EstimatedRisk = 0.0f;
	BuyFood.EstimatedReward = 40.0f;
	BuyFood.bIsExploit = false;

	FNPCStrategyStep Step1;
	Step1.ActionType = EAffordanceAction::Approach;
	Step1.OptionId = FName("Counter");
	BuyFood.ActionChain.Add(Step1);

	FNPCStrategyStep Step2;
	Step2.ActionType = EAffordanceAction::Wait;
	Step2.OptionId = FName("Order");
	BuyFood.ActionChain.Add(Step2);

	FNPCStrategyStep Step3;
	Step3.ActionType = EAffordanceAction::Take;
	Step3.OptionId = FName("Food");
	BuyFood.ActionChain.Add(Step3);

	FNPCStrategyStep Step4;
	Step4.ActionType = EAffordanceAction::Eat;
	Step4.OptionId = FName("Food");
	BuyFood.ActionChain.Add(Step4);

	KnownStrategies.Add(BuyFood.StrategyId, BuyFood);

	// Baseline 2: Drink Water
	FNPCStrategy DrinkWater;
	DrinkWater.StrategyId = FName("Strategy_DrinkWater");
	DrinkWater.Goal = FName("SatisfyThirst");
	DrinkWater.Confidence = 0.95f;
	DrinkWater.EstimatedRisk = 0.0f;
	DrinkWater.EstimatedReward = 30.0f;

	FNPCStrategyStep WStep1;
	WStep1.ActionType = EAffordanceAction::Approach;
	WStep1.OptionId = FName("WaterDispenser");
	DrinkWater.ActionChain.Add(WStep1);

	FNPCStrategyStep WStep2;
	WStep2.ActionType = EAffordanceAction::Drink;
	WStep2.OptionId = FName("Water");
	DrinkWater.ActionChain.Add(WStep2);

	KnownStrategies.Add(DrinkWater.StrategyId, DrinkWater);

	// Baseline 3: Use Restroom
	FNPCStrategy UseRestroom;
	UseRestroom.StrategyId = FName("Strategy_UseRestroom");
	UseRestroom.Goal = FName("RelieveBladder");
	UseRestroom.Confidence = 0.95f;
	UseRestroom.EstimatedRisk = 0.0f;
	UseRestroom.EstimatedReward = 50.0f;

	FNPCStrategyStep RStep1;
	RStep1.ActionType = EAffordanceAction::Approach;
	RStep1.OptionId = FName("Toilet");
	UseRestroom.ActionChain.Add(RStep1);

	FNPCStrategyStep RStep2;
	RStep2.ActionType = EAffordanceAction::UseToilet;
	RStep2.OptionId = FName("Toilet");
	UseRestroom.ActionChain.Add(RStep2);

	KnownStrategies.Add(UseRestroom.StrategyId, UseRestroom);
}
