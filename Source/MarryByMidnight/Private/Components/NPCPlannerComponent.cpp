#include "Components/NPCPlannerComponent.h"
#include "Components/NPCStrategyComponent.h"
#include "Components/NPCAffordanceComponent.h"
#include "Interfaces/WorldAffordanceInterface.h"
#include "GameFramework/Actor.h"

UNPCPlannerComponent::UNPCPlannerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UNPCPlannerComponent::BuildPlanForGoal(FName Goal, const TArray<AActor*>& PerceivedObjects)
{
	ActivePlan.Empty();
	CurrentStepIndex = 0;
	bIsExecutingPlan = false;

	if (PerceivedObjects.Num() == 0) return false;

	if (Goal == FName("SatisfyHunger"))
	{
		AActor* FoodActor = nullptr;
		FAffordanceOption EatOption;

		AActor* TrashActor = nullptr;
		FAffordanceOption SearchOption;

		AActor* MachineActor = nullptr;
		FAffordanceOption CookOption;

		AActor* RawMeatActor = nullptr;
		FAffordanceOption RawTakeOption;

		// 1. Scan available objects for affordances
		for (AActor* Obj : PerceivedObjects)
		{
			if (!Obj) continue;

			TArray<FAffordanceOption> Options;
			if (Obj->GetClass()->ImplementsInterface(UWorldAffordanceInterface::StaticClass()))
			{
				IWorldAffordanceInterface::Execute_QueryAffordanceOptions(Obj, GetOwner(), Options);
			}
			else
			{
				UNPCAffordanceComponent* AffordComp = Obj->FindComponentByClass<UNPCAffordanceComponent>();
				if (AffordComp)
				{
					for (const FAffordanceDefinition& Def : AffordComp->ProvidedAffordances)
					{
						FAffordanceOption Opt;
						Opt.OptionId = Def.ActionName;
						Opt.ActionType = Def.ActionType;
						Opt.ExecutionTime = Def.ExecutionDuration;
						Opt.bIsAllowed = true;
						Options.Add(Opt);
					}
				}
			}

			for (const FAffordanceOption& Opt : Options)
			{
				if (!Opt.bIsAllowed) continue;

				if (Opt.ActionType == EAffordanceAction::Eat && !FoodActor)
				{
					FoodActor = Obj;
					EatOption = Opt;
				}
				else if (Opt.ActionType == EAffordanceAction::Search && !TrashActor)
				{
					TrashActor = Obj;
					SearchOption = Opt;
				}
				else if (Opt.ActionType == EAffordanceAction::Operate || Opt.ActionType == EAffordanceAction::Insert)
				{
					MachineActor = Obj;
					CookOption = Opt;
				}
				else if (Opt.ActionType == EAffordanceAction::Take)
				{
					RawMeatActor = Obj;
					RawTakeOption = Opt;
				}
			}
		}

		// Priority Plan A: Ready-to-eat food found directly
		if (FoodActor)
		{
			FExecutablePlanStep StepApproach;
			StepApproach.Option.ActionType = EAffordanceAction::Approach;
			StepApproach.Option.DisplayLabel = FText::FromString(TEXT("Approach Food"));
			StepApproach.TargetActor = FoodActor;
			ActivePlan.Add(StepApproach);

			FExecutablePlanStep StepTake;
			StepTake.Option.ActionType = EAffordanceAction::Take;
			StepTake.Option.DisplayLabel = FText::FromString(TEXT("Take Food"));
			StepTake.TargetActor = FoodActor;
			ActivePlan.Add(StepTake);

			FExecutablePlanStep StepEat;
			StepEat.Option = EatOption;
			StepEat.TargetActor = FoodActor;
			ActivePlan.Add(StepEat);
		}
		// Priority Plan B: Emergent Machine Exploit (Raw Ingredient -> Machine -> Eat)
		else if (MachineActor && RawMeatActor)
		{
			FExecutablePlanStep Step1;
			Step1.Option = RawTakeOption;
			Step1.TargetActor = RawMeatActor;
			ActivePlan.Add(Step1);

			FExecutablePlanStep Step2;
			Step2.Option = CookOption;
			Step2.TargetActor = MachineActor;
			ActivePlan.Add(Step2);

			FExecutablePlanStep Step3;
			Step3.Option.ActionType = EAffordanceAction::Eat;
			Step3.Option.DisplayLabel = FText::FromString(TEXT("Eat Cooked Product"));
			Step3.TargetActor = MachineActor;
			ActivePlan.Add(Step3);
		}
		// Priority Plan C: Search Trash / Scavenge
		else if (TrashActor)
		{
			FExecutablePlanStep StepApproach;
			StepApproach.Option.ActionType = EAffordanceAction::Approach;
			StepApproach.TargetActor = TrashActor;
			ActivePlan.Add(StepApproach);

			FExecutablePlanStep StepSearch;
			StepSearch.Option = SearchOption;
			StepSearch.TargetActor = TrashActor;
			ActivePlan.Add(StepSearch);

			FExecutablePlanStep StepEat;
			StepEat.Option.ActionType = EAffordanceAction::Eat;
			StepEat.TargetActor = TrashActor;
			ActivePlan.Add(StepEat);
		}
	}
	else if (Goal == FName("SatisfyThirst"))
	{
		for (AActor* Obj : PerceivedObjects)
		{
			if (!Obj) continue;
			TArray<FAffordanceOption> Options;
			if (Obj->GetClass()->ImplementsInterface(UWorldAffordanceInterface::StaticClass()))
			{
				IWorldAffordanceInterface::Execute_QueryAffordanceOptions(Obj, GetOwner(), Options);
				for (const FAffordanceOption& Opt : Options)
				{
					if (Opt.ActionType == EAffordanceAction::Drink && Opt.bIsAllowed)
					{
						FExecutablePlanStep StepApproach;
						StepApproach.Option.ActionType = EAffordanceAction::Approach;
						StepApproach.TargetActor = Obj;
						ActivePlan.Add(StepApproach);

						FExecutablePlanStep StepDrink;
						StepDrink.Option = Opt;
						StepDrink.TargetActor = Obj;
						ActivePlan.Add(StepDrink);
						break;
					}
				}
			}
			if (ActivePlan.Num() > 0) break;
		}
	}
	else if (Goal == FName("RelieveBladder"))
	{
		for (AActor* Obj : PerceivedObjects)
		{
			if (!Obj) continue;
			TArray<FAffordanceOption> Options;
			if (Obj->GetClass()->ImplementsInterface(UWorldAffordanceInterface::StaticClass()))
			{
				IWorldAffordanceInterface::Execute_QueryAffordanceOptions(Obj, GetOwner(), Options);
				for (const FAffordanceOption& Opt : Options)
				{
					if (Opt.ActionType == EAffordanceAction::UseToilet && Opt.bIsAllowed)
					{
						FExecutablePlanStep StepApproach;
						StepApproach.Option.ActionType = EAffordanceAction::Approach;
						StepApproach.TargetActor = Obj;
						ActivePlan.Add(StepApproach);

						FExecutablePlanStep StepToilet;
						StepToilet.Option = Opt;
						StepToilet.TargetActor = Obj;
						ActivePlan.Add(StepToilet);
						break;
					}
				}
			}
			if (ActivePlan.Num() > 0) break;
		}
	}

	if (ActivePlan.Num() > 0)
	{
		bIsExecutingPlan = true;
		CurrentStepIndex = 0;
		OnPlanStepStarted.Broadcast(0, ActivePlan[0].Option);
		return true;
	}

	return false;
}

void UNPCPlannerComponent::AdvancePlan()
{
	if (!bIsExecutingPlan || ActivePlan.Num() == 0) return;

	// Execute interaction on target if valid
	if (ActivePlan.IsValidIndex(CurrentStepIndex))
	{
		AActor* Target = ActivePlan[CurrentStepIndex].TargetActor.Get();
		if (Target && Target->GetClass()->ImplementsInterface(UWorldAffordanceInterface::StaticClass()))
		{
			IWorldAffordanceInterface::Execute_ExecuteAffordanceOption(
				Target,
				ActivePlan[CurrentStepIndex].Option.OptionId,
				GetOwner()
			);
		}
	}

	CurrentStepIndex++;

	// Plan completed successfully
	if (CurrentStepIndex >= ActivePlan.Num())
	{
		bIsExecutingPlan = false;

		// Convert successful plan into a remembered strategy on UNPCStrategyComponent
		AActor* OwnerActor = GetOwner();
		if (OwnerActor)
		{
			UNPCStrategyComponent* StratComp = OwnerActor->FindComponentByClass<UNPCStrategyComponent>();
			if (StratComp)
			{
				FNPCStrategy DiscoveredStrat;
				DiscoveredStrat.StrategyId = FName(*FString::Printf(TEXT("Plan_%s_%d"), *OwnerActor->GetName(), FMath::RandRange(100, 999)));
				DiscoveredStrat.Goal = FName("SatisfyHunger");
				DiscoveredStrat.Confidence = 0.8f;
				DiscoveredStrat.SuccessCount = 1;
				DiscoveredStrat.FailureCount = 0;
				DiscoveredStrat.bIsExploit = true;

				for (const FExecutablePlanStep& Step : ActivePlan)
				{
					FNPCStrategyStep StratStep;
					StratStep.ActionType = Step.Option.ActionType;
					StratStep.OptionId = Step.Option.OptionId;
					DiscoveredStrat.ActionChain.Add(StratStep);
				}

				StratComp->RegisterDiscoveredStrategy(DiscoveredStrat);
			}
		}

		OnPlanCompleted.Broadcast(true);
	}
	else
	{
		OnPlanStepStarted.Broadcast(CurrentStepIndex, ActivePlan[CurrentStepIndex].Option);
	}
}

void UNPCPlannerComponent::AbortPlan(const FString& Reason)
{
	bIsExecutingPlan = false;
	ActivePlan.Empty();
	CurrentStepIndex = 0;
	OnPlanCompleted.Broadcast(false);
}
