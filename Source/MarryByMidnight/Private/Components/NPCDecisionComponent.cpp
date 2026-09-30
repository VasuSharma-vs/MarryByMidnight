#include "Components/NPCDecisionComponent.h"
#include "Components/NPCNeedsComponent.h"
#include "Components/NPCStateComponent.h"
#include "Components/NPCMentalStateComponent.h"
#include "Components/NPCPersonalityComponent.h"
#include "Components/NPCStrategyComponent.h"
#include "Environment/NPCSimulationTestZone.h"
#include "Environment/NPCOperableObject.h"
#include "Characters/NPCCharacter.h"

#include "TimerManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UNPCDecisionComponent::UNPCDecisionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UNPCDecisionComponent::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	// Run decisions on server every 2 seconds
	if (World && GetOwner() && GetOwner()->HasAuthority())
	{
		World->GetTimerManager().SetTimer(DecisionTimerHandle, this, &UNPCDecisionComponent::PeriodicDecisionTick, 2.0f, true);
	}
}

void UNPCDecisionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(DecisionTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void UNPCDecisionComponent::PeriodicDecisionTick()
{
	EvaluateGoals();
}

#include "Components/NPCWorldModelComponent.h"

FName UNPCDecisionComponent::EvaluateGoals()
{
	FString DiagnosticLog;
	return PerformSelfAssessment(DiagnosticLog);
}

FName UNPCDecisionComponent::PerformSelfAssessment(FString& OutDiagnosticReasoning)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority()) return ActiveGoal;

	UNPCNeedsComponent* NeedsComp = OwnerActor->FindComponentByClass<UNPCNeedsComponent>();
	UNPCMentalStateComponent* MentalComp = OwnerActor->FindComponentByClass<UNPCMentalStateComponent>();
	UNPCStateComponent* StateComp = OwnerActor->FindComponentByClass<UNPCStateComponent>();
	UNPCPersonalityComponent* PersComp = OwnerActor->FindComponentByClass<UNPCPersonalityComponent>();
	UNPCStrategyComponent* StratComp = OwnerActor->FindComponentByClass<UNPCStrategyComponent>();
	UNPCWorldModelComponent* WorldModel = OwnerActor->FindComponentByClass<UNPCWorldModelComponent>();

	// --- 1. GATHER PHYSIOLOGICAL & MENTAL TELEMETRY ---
	const float PowerVal = StateComp ? StateComp->PhysicalState.Power : 100.0f;
	const float WaterVal = StateComp ? StateComp->PhysicalState.Water : 100.0f;
	const float AmbientTemp = StateComp ? StateComp->PhysicalState.AmbientTemperature : 24.0f;
	const float SicknessVal = StateComp ? StateComp->PhysicalState.Sickness : 0.0f;

	const float HungerVal = NeedsComp ? NeedsComp->Needs.Hunger : 0.0f;
	const float ThirstVal = NeedsComp ? NeedsComp->Needs.Thirst : 0.0f;
	const float SleepinessVal = NeedsComp ? NeedsComp->Needs.Sleepiness : 0.0f;
	const float BladderVal = NeedsComp ? NeedsComp->Needs.BladderUrgency : 0.0f;
	const float BowelVal = NeedsComp ? NeedsComp->Needs.BowelUrgency : 0.0f;

	const float StressVal = MentalComp ? MentalComp->MentalState.Stress : 0.0f;
	const float AngerVal = MentalComp ? FMath::Clamp(MentalComp->MentalState.Arousal - MentalComp->MentalState.Valence, 0.0f, 100.0f) : 0.0f;
	const bool bIsEnraged = MentalComp && MentalComp->IsEnraged();

	const float GreedVal = PersComp ? PersComp->Personality.Greed : 0.5f;
	const float CuriosityVal = PersComp ? PersComp->Personality.Curiosity : 0.5f;
	const float FearVal = PersComp ? PersComp->Personality.FearOfPunishment : 0.5f;
	const float CashVal = PersComp ? PersComp->WalletCash : 50.0f;

	// --- 2. CALCULATE CONTINUOUS DYNAMIC UTILITY DRIVES ---
	TMap<FName, float> DriveScores;

	// Drive A: Bathroom Urgency (Exponential ramp as bladder/bowel fills)
	const float MaxBathroom = FMath::Max(BladderVal, BowelVal);
	const float BathroomScore = FMath::Pow(FMath::Clamp(MaxBathroom / 100.0f, 0.0f, 1.0f), 2.2f) * 135.0f;
	DriveScores.Add(FName("RelieveBladder"), BathroomScore);

	// Drive B: Thirst / Hydration (Sub-quadratic curve accelerated by heat and physical water loss)
	float HeatModifier = 1.0f;
	if (AmbientTemp > 28.0f)
	{
		HeatModifier += (AmbientTemp - 28.0f) * 0.03f;
	}
	const float ThirstScore = FMath::Pow(FMath::Clamp(ThirstVal / 100.0f, 0.0f, 1.0f), 1.3f) * 100.0f * HeatModifier;
	DriveScores.Add(FName("SatisfyThirst"), ThirstScore);

	// Drive C: Hunger / Sustenance (Modulated by gluttony archetype and cash)
	float AppetiteModifier = 1.0f;
	if (PersComp && PersComp->Archetype == ENPCArchetype::Fatty)
	{
		AppetiteModifier = 1.35f;
	}
	const float HungerScore = FMath::Pow(FMath::Clamp(HungerVal / 100.0f, 0.0f, 1.0f), 1.2f) * 95.0f * AppetiteModifier;
	DriveScores.Add(FName("SatisfyHunger"), HungerScore);

	// Drive D: Power / Rest & Recovery (Exhaustion curve combining physical power deficit & sleepiness)
	const float PowerDeficit = FMath::Clamp(100.0f - PowerVal, 0.0f, 100.0f);
	const float CombinedExhaustion = (PowerDeficit * 0.70f) + (SleepinessVal * 0.30f);
	const float SleepScore = FMath::Pow(FMath::Clamp(CombinedExhaustion / 100.0f, 0.0f, 1.0f), 1.4f) * 105.0f;
	DriveScores.Add(FName("RestSleep"), SleepScore);

	// Drive E: Enraged / Violent Retaliation
	float RageScore = 0.0f;
	if (bIsEnraged)
	{
		RageScore = 150.0f;
	}
	else if (AngerVal > 40.0f)
	{
		RageScore = (AngerVal / 100.0f) * 85.0f;
	}
	DriveScores.Add(FName("VandalizeRage"), RageScore);

	// Drive F: Opportunistic Theft (For greedy NPCs who are low on money)
	float StealScore = 0.0f;
	if (GreedVal >= 0.6f && CashVal < 25.0f)
	{
		const float SurvivalUrge = FMath::Max(HungerScore, ThirstScore) * 0.4f;
		StealScore = (GreedVal * (1.0f - FearVal) * 55.0f) + SurvivalUrge;
	}
	DriveScores.Add(FName("StealSupplies"), StealScore);

	// Drive G: Exploration & Roaming (When survival needs are low, curiosity drives exploration)
	const float ExploreScore = 20.0f + (CuriosityVal * 30.0f);
	DriveScores.Add(FName("ExploreAffordances"), ExploreScore);

	// --- 3. REINFORCEMENT LEARNING & MEMORY MODULATION (StrategyComponent & WorldModel) ---
	if (StratComp)
	{
		for (auto& Pair : DriveScores)
		{
			const FName GoalKey = Pair.Key;
			float& Score = Pair.Value;

			FNPCStrategy BestStrat;
			if (StratComp->GetBestStrategyForGoal(GoalKey, BestStrat))
			{
				// Confidence factor (-0.2 to +0.2)
				const float ConfFactor = (BestStrat.Confidence - 0.5f) * 0.4f;

				// Past empirical success ratio
				const float TotalAttempts = BestStrat.SuccessCount + BestStrat.FailureCount;
				float EmpiricalSuccessRate = 0.5f;
				if (TotalAttempts > 0.0f)
				{
					EmpiricalSuccessRate = static_cast<float>(BestStrat.SuccessCount) / TotalAttempts;
				}
				const float ExperienceFactor = (EmpiricalSuccessRate - 0.5f) * 0.3f;

				// Risk aversion weighted by fear
				const float RiskDeterrence = BestStrat.EstimatedRisk * FearVal * 0.35f;

				const float Multiplier = FMath::Clamp(1.0f + ConfFactor + ExperienceFactor - RiskDeterrence, 0.4f, 1.6f);
				Score *= Multiplier;
			}
		}
	}

	// --- 4. IDENTIFY TOP GOAL & BUILD INTROSPECTIVE DIAGNOSTIC ---
	FName TopGoal = FName("Idle");
	float TopScore = 0.0f;

	TArray<TPair<FName, float>> SortedDrives;
	for (const auto& Pair : DriveScores)
	{
		SortedDrives.Add(Pair);
	}
	SortedDrives.Sort([](const TPair<FName, float>& A, const TPair<FName, float>& B)
	{
		return A.Value > B.Value;
	});

	if (SortedDrives.Num() > 0 && SortedDrives[0].Value > 15.0f)
	{
		TopGoal = SortedDrives[0].Key;
		TopScore = SortedDrives[0].Value;
	}

	ActiveGoalScore = TopScore;

	// Construct human-readable diagnostic explanation of the decision
	OutDiagnosticReasoning = FString::Printf(
		TEXT("[Self-Check] Power: %.1f | Thirst: %.1f | Hunger: %.1f | Bladder: %.1f | Stress: %.1f\n")
		TEXT("[Urgency Ranking] 1: %s (%.1f) > 2: %s (%.1f) > 3: %s (%.1f)\n")
		TEXT("[Decision] Addressing: %s first!"),
		PowerVal, ThirstVal, HungerVal, BladderVal, StressVal,
		SortedDrives.Num() > 0 ? *SortedDrives[0].Key.ToString() : TEXT("None"), SortedDrives.Num() > 0 ? SortedDrives[0].Value : 0.0f,
		SortedDrives.Num() > 1 ? *SortedDrives[1].Key.ToString() : TEXT("None"), SortedDrives.Num() > 1 ? SortedDrives[1].Value : 0.0f,
		SortedDrives.Num() > 2 ? *SortedDrives[2].Key.ToString() : TEXT("None"), SortedDrives.Num() > 2 ? SortedDrives[2].Value : 0.0f,
		*TopGoal.ToString()
	);

	LastSelfAssessmentSummary = FString::Printf(
		TEXT("%s (%.0f) [P:%.0f T:%.0f H:%.0f]"),
		*TopGoal.ToString(), TopScore, PowerVal, ThirstVal, HungerVal
	);

	if (TopGoal != ActiveGoal)
	{
		ActiveGoal = TopGoal;
		OnNPCGoalSelected.Broadcast(ActiveGoal);
	}

	return ActiveGoal;
}

bool UNPCDecisionComponent::ShouldExploreNovelStrategy(FName ForGoal) const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor) return false;

	UNPCPersonalityComponent* PersComp = OwnerActor->FindComponentByClass<UNPCPersonalityComponent>();
	UNPCStrategyComponent* StratComp = OwnerActor->FindComponentByClass<UNPCStrategyComponent>();

	// If no known strategy exists, must explore/plan
	if (StratComp && !StratComp->HasStrategyForGoal(ForGoal))
	{
		return true;
	}

	// If starving/hungry but broke, must explore alternatives (loopholes, stealing, trash)
	if (ForGoal == FName("SatisfyHunger") && PersComp && PersComp->WalletCash < 10.0f)
	{
		return true;
	}

	// High curiosity and low fear of punishment encourages experimentation
	if (PersComp && PersComp->Personality.Curiosity >= 0.75f && PersComp->Personality.FearOfPunishment <= 0.35f)
	{
		return true;
	}

	return false;
}

float UNPCDecisionComponent::GetStatDesirabilitySign(ENPCSimulationStat Stat) const
{
	return GetSimulationStatDesirabilitySign(Stat);
}

FNPCStimulusEvaluation UNPCDecisionComponent::EvaluateSingleStimulus(ENPCSimulationStat Stat, float ChangeRate) const
{
	FNPCStimulusEvaluation Eval;
	Eval.Stat = Stat;
	Eval.ChangeRate = ChangeRate;

	const float DesirabilitySign = GetSimulationStatDesirabilitySign(Stat);
	// Raw valence: is the rate of change fundamentally good (>0) or bad (<0)?
	const float RawValence = ChangeRate * DesirabilitySign;
	Eval.bIsGood = (RawValence > 0.0f);

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor) return Eval;

	UNPCStateComponent* StateComp = OwnerActor->FindComponentByClass<UNPCStateComponent>();
	UNPCNeedsComponent* NeedsComp = OwnerActor->FindComponentByClass<UNPCNeedsComponent>();
	UNPCMentalStateComponent* MentalComp = OwnerActor->FindComponentByClass<UNPCMentalStateComponent>();
	UNPCPersonalityComponent* PersComp = OwnerActor->FindComponentByClass<UNPCPersonalityComponent>();

	float NeedUrgency = 0.0f;
	float WeightedScore = 0.0f;

	switch (Stat)
	{
	case ENPCSimulationStat::Power:
	{
		const float PowerVal = StateComp ? StateComp->PhysicalState.Power : 100.0f;
		const float Deficit = FMath::Clamp(100.0f - PowerVal, 0.0f, 100.0f);
		NeedUrgency = FMath::Pow(Deficit / 100.0f, 1.4f);

		if (ChangeRate > 0.0f)
		{
			// Increasing power is GOOD, value scales with how exhausted the NPC is
			WeightedScore = ChangeRate * (NeedUrgency * 7.5f);
		}
		else
		{
			// Decreasing power is BAD
			WeightedScore = ChangeRate * (1.0f + NeedUrgency * 5.0f);
		}
		break;
	}
	case ENPCSimulationStat::WalletCash:
	{
		const float CashVal = PersComp ? PersComp->WalletCash : 0.0f;
		const float PriceSens = PersComp ? PersComp->Personality.PriceSensitivity : 0.5f;
		const float Greed = PersComp ? PersComp->Personality.Greed : 0.5f;

		if (ChangeRate < 0.0f)
		{
			// Decreasing money is BAD (cost/fee), amplified by price sensitivity and low funds
			float CostMultiplier = 3.5f * (1.0f + PriceSens);
			if (CashVal <= 0.0f)
			{
				CostMultiplier *= 6.0f; // Extreme penalty if broke!
			}
			else if (CashVal < 20.0f)
			{
				CostMultiplier *= 2.0f;
			}
			WeightedScore = ChangeRate * CostMultiplier; // negative
			NeedUrgency = (CashVal <= 0.0f) ? 1.0f : FMath::Clamp((50.0f - CashVal) / 50.0f, 0.0f, 1.0f);
		}
		else
		{
			// Increasing money is GOOD (income/reward)
			WeightedScore = ChangeRate * (1.0f + Greed) * 3.0f;
			NeedUrgency = FMath::Clamp((100.0f - CashVal) / 100.0f, 0.0f, 1.0f);
		}
		break;
	}
	case ENPCSimulationStat::Thirst:
	{
		const float ThirstVal = NeedsComp ? NeedsComp->Needs.Thirst : 0.0f;
		NeedUrgency = FMath::Pow(ThirstVal / 100.0f, 1.3f);

		if (ChangeRate < 0.0f)
		{
			// Decreasing thirst is GOOD (drinking/hydrating)
			WeightedScore = (-ChangeRate) * (NeedUrgency * 7.0f);
		}
		else
		{
			// Increasing thirst is BAD (dehydration)
			WeightedScore = (-ChangeRate) * (1.0f + NeedUrgency * 4.0f);
		}
		break;
	}
	case ENPCSimulationStat::Hunger:
	{
		const float HungerVal = NeedsComp ? NeedsComp->Needs.Hunger : 0.0f;
		NeedUrgency = FMath::Pow(HungerVal / 100.0f, 1.2f);

		if (ChangeRate < 0.0f)
		{
			// Decreasing hunger is GOOD (eating)
			WeightedScore = (-ChangeRate) * (NeedUrgency * 6.5f);
		}
		else
		{
			// Increasing hunger is BAD
			WeightedScore = (-ChangeRate) * (1.0f + NeedUrgency * 4.0f);
		}
		break;
	}
	case ENPCSimulationStat::Sleepiness:
	{
		const float SleepVal = NeedsComp ? NeedsComp->Needs.Sleepiness : 0.0f;
		NeedUrgency = FMath::Pow(SleepVal / 100.0f, 1.4f);

		if (ChangeRate < 0.0f)
		{
			// Decreasing sleepiness is GOOD
			WeightedScore = (-ChangeRate) * (NeedUrgency * 6.0f);
		}
		else
		{
			// Increasing sleepiness is BAD
			WeightedScore = (-ChangeRate) * (1.0f + NeedUrgency * 4.0f);
		}
		break;
	}
	case ENPCSimulationStat::Bladder:
	case ENPCSimulationStat::Bowel:
	{
		const float Urgency = NeedsComp ? FMath::Max(NeedsComp->Needs.BladderUrgency, NeedsComp->Needs.BowelUrgency) : 0.0f;
		NeedUrgency = FMath::Pow(Urgency / 100.0f, 2.0f);

		if (ChangeRate < 0.0f)
		{
			// Decreasing is GOOD (relieving urgency)
			WeightedScore = (-ChangeRate) * (NeedUrgency * 8.0f);
		}
		else
		{
			// Increasing is BAD
			WeightedScore = (-ChangeRate) * (1.0f + NeedUrgency * 5.0f);
		}
		break;
	}
	case ENPCSimulationStat::Sickness:
	{
		const float SicknessVal = StateComp ? StateComp->PhysicalState.Sickness : 0.0f;
		NeedUrgency = SicknessVal / 100.0f;

		if (ChangeRate > 0.0f)
		{
			// Increasing sickness is actively HAZARDOUS (toxic/disease)
			WeightedScore = -ChangeRate * 12.0f;
		}
		else
		{
			// Decreasing sickness is GOOD (medicine/healing)
			WeightedScore = (-ChangeRate) * (NeedUrgency * 10.0f);
		}
		break;
	}
	case ENPCSimulationStat::Stress:
	{
		const float StressVal = MentalComp ? MentalComp->MentalState.Stress : 0.0f;
		NeedUrgency = StressVal / 100.0f;

		if (ChangeRate < 0.0f)
		{
			// Decreasing stress is GOOD (relaxation)
			WeightedScore = (-ChangeRate) * (NeedUrgency * 5.0f);
		}
		else
		{
			// Increasing stress is BAD (agitation)
			WeightedScore = -ChangeRate * (1.0f + NeedUrgency * 3.0f);
		}
		break;
	}
	case ENPCSimulationStat::Dopamine:
	{
		if (ChangeRate > 0.0f)
		{
			WeightedScore = ChangeRate * 4.0f;
		}
		else
		{
			WeightedScore = ChangeRate * 3.0f;
		}
		NeedUrgency = 0.5f;
		break;
	}
	default:
		WeightedScore = RawValence * 2.0f;
		NeedUrgency = 0.5f;
		break;
	}

	Eval.NeedUrgency = NeedUrgency;
	Eval.WeightedUtilityScore = WeightedScore;
	return Eval;
}

FNPCZoneStimuliEvaluation UNPCDecisionComponent::EvaluateStimuliArray(const TArray<FNPCSimulationStatModifier>& Stimuli) const
{
	FNPCZoneStimuliEvaluation Result;

	for (const FNPCSimulationStatModifier& Mod : Stimuli)
	{
		FNPCStimulusEvaluation SingleEval = EvaluateSingleStimulus(Mod.Stat, Mod.ChangeRate);
		Result.Breakdown.Add(SingleEval);

		if (SingleEval.WeightedUtilityScore > 0.0f)
		{
			Result.TotalBenefit += SingleEval.WeightedUtilityScore;
		}
		else
		{
			Result.TotalCost += (-SingleEval.WeightedUtilityScore);
		}
	}

	Result.NetUtilityScore = Result.TotalBenefit - Result.TotalCost;
	return Result;
}

FNPCZoneStimuliEvaluation UNPCDecisionComponent::EvaluateZoneStimuli(const ANPCSimulationTestZone* Zone) const
{
	if (!Zone) return FNPCZoneStimuliEvaluation();

	if (Zone->StimuliModifiers.Num() > 0)
	{
		return EvaluateStimuliArray(Zone->StimuliModifiers);
	}

	// Legacy fallback
	TArray<FNPCSimulationStatModifier> FallbackStimuli;
	FallbackStimuli.Add(FNPCSimulationStatModifier(Zone->TargetStat, Zone->ChangeRate));
	for (const auto& Mod : Zone->AdditionalModifiers)
	{
		FallbackStimuli.Add(Mod);
	}
	return EvaluateStimuliArray(FallbackStimuli);
}

FName UNPCDecisionComponent::DecideActionFromZoneStimuli(const ANPCSimulationTestZone* Zone, FString& OutReasoning)
{
	if (!Zone)
	{
		OutReasoning = TEXT("Null zone");
		return FName("None");
	}

	AActor* OwnerActor = GetOwner();
	ANPCCharacter* OwnerNPC = Cast<ANPCCharacter>(OwnerActor);

	// Evaluate all stimuli mathematically
	FNPCZoneStimuliEvaluation Eval = EvaluateZoneStimuli(Zone);

	// 1. Check Affordability
	const float CostPerSec = Zone->GetMoneyCostRate();
	if (CostPerSec > 0.0f && !Zone->CanNPCAfford(OwnerNPC))
	{
		Eval.RecommendedAction = FName("LeaveBroke");
		OutReasoning = FString::Printf(TEXT("[Stimulus Detection: UNAFFORDABLE] Zone fee is $%.1f/sec, but wallet is empty. Action: LEAVE BROKE."), CostPerSec);
		Eval.DiagnosticReasoning = OutReasoning;
		LastZoneEvaluation = Eval;
		return Eval.RecommendedAction;
	}

	// 2. Check Active Hazardous Condition (e.g. Sickness/Toxicity with no benefits)
	if (Eval.TotalCost >= 25.0f && Eval.TotalBenefit <= 5.0f)
	{
		Eval.RecommendedAction = FName("FleeHazard");
		OutReasoning = FString::Printf(TEXT("[Stimulus Detection: HAZARDOUS (-%.1f)] Harmful stimuli detected with no benefits. Action: FLEE HAZARD!"), Eval.TotalCost);
		Eval.DiagnosticReasoning = OutReasoning;
		LastZoneEvaluation = Eval;
		return Eval.RecommendedAction;
	}

	// 3. Positive Net Utility (Benefits significantly outweigh costs)
	if (Eval.NetUtilityScore >= 8.0f)
	{
		Eval.RecommendedAction = FName("StayAndAbsorb");
		OutReasoning = FString::Printf(TEXT("[Stimulus Detection: BENEFICIAL (Net: +%.1f)] Benefit (+%.1f) outweighs Cost (-%.1f). Action: STAY & ABSORB."),
			Eval.NetUtilityScore, Eval.TotalBenefit, Eval.TotalCost);
		Eval.DiagnosticReasoning = OutReasoning;
		LastZoneEvaluation = Eval;
		return Eval.RecommendedAction;
	}

	// 4. Net Unfavorable or Diminishing Returns (e.g. power already full, so paying money is bad)
	Eval.RecommendedAction = FName("LeaveSatisfied");
	OutReasoning = FString::Printf(TEXT("[Stimulus Detection: SATISFIED / UNFAVORABLE (Net: %+.1f)] Needs met or costs exceed benefits. Action: LEAVE SATISFIED."),
		Eval.NetUtilityScore);
	Eval.DiagnosticReasoning = OutReasoning;
	LastZoneEvaluation = Eval;
	return Eval.RecommendedAction;
}

float UNPCDecisionComponent::EvaluateOperableOffering(const FNPCOperableOffering& Offering, FString& OutReasoning) const
{
	// 1. Evaluate the offered stimuli array against current biological deficits
	FNPCZoneStimuliEvaluation StimEval = EvaluateStimuliArray(Offering.OfferedStimuli);

	// 2. Penalize by money cost weighted by NPC's price sensitivity
	float CostPenalty = 0.0f;
	if (Offering.MoneyCost > 0.0f)
	{
		AActor* OwnerActor = GetOwner();
		UNPCPersonalityComponent* PersComp = OwnerActor ? OwnerActor->FindComponentByClass<UNPCPersonalityComponent>() : nullptr;
		const float Sensitivity = PersComp ? PersComp->Personality.PriceSensitivity : 0.5f;
		const float Cash = PersComp ? PersComp->GetCash() : 0.0f;

		if (Cash < Offering.MoneyCost)
		{
			OutReasoning = FString::Printf(TEXT("Cannot afford offering (Costs $%.2f, wallet has $%.2f)"), Offering.MoneyCost, Cash);
			return -999.0f;
		}

		CostPenalty = Offering.MoneyCost * (1.0f + Sensitivity * 2.5f);
	}

	const float FinalUtility = StimEval.NetUtilityScore - CostPenalty;
	OutReasoning = FString::Printf(TEXT("[%s] StimBenefit: +%.1f | StimCost: -%.1f | PricePenalty: -%.1f => NetScore: %+.1f"),
		*Offering.DisplayTitle.ToString(), StimEval.TotalBenefit, StimEval.TotalCost, CostPenalty, FinalUtility);

	return FinalUtility;
}

FName UNPCDecisionComponent::DecideBestOperableOffering(const ANPCOperableObject* OperableObject, FString& OutReasoning) const
{
	if (!OperableObject || OperableObject->AvailableOfferings.Num() == 0)
	{
		OutReasoning = TEXT("No offerings available on operable object");
		return NAME_None;
	}

	FName BestOfferingId = NAME_None;
	float BestScore = 0.0f; // Must have positive net utility to be worthwhile
	FString BestReasoning;

	for (const auto& Offering : OperableObject->AvailableOfferings)
	{
		FString OptionReason;
		const float Score = EvaluateOperableOffering(Offering, OptionReason);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestOfferingId = Offering.OfferingId;
			BestReasoning = OptionReason;
		}
	}

	OutReasoning = BestOfferingId != NAME_None ? BestReasoning : TEXT("No offering on machine provided sufficient positive utility");
	return BestOfferingId;
}

