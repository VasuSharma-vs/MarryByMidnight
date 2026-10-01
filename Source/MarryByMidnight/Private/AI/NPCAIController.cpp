#include "AI/NPCAIController.h"
#include "Characters/NPCCharacter.h"
#include "Components/NPCStateComponent.h"
#include "Components/NPCNeedsComponent.h"
#include "Components/NPCMentalStateComponent.h"
#include "Components/NPCDecisionComponent.h"
#include "Components/NPCPersonalityComponent.h"
#include "Components/NPCStrategyComponent.h"
#include "Components/NPCAffordanceComponent.h"
#include "Components/NPCWorldModelComponent.h"
#include "Environment/NPCSimulationTestZone.h"
#include "Environment/NPCOperableObject.h"
#include "Props/NPCConsumableProp.h"
#include "Interfaces/WorldAffordanceInterface.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Sight.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

ANPCAIController::ANPCAIController()
{
	bWantsPlayerState = false;
	bNetLoadOnClient = false;

	// Setup AI Perception with Sight
	AIPerceptionComp = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerceptionComp"));
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));

	SightConfig->SightRadius = 1500.0f;
	SightConfig->LoseSightRadius = 1800.0f;
	SightConfig->PeripheralVisionAngleDegrees = 90.0f;
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

	AIPerceptionComp->ConfigureSense(*SightConfig);
	AIPerceptionComp->SetDominantSense(SightConfig->GetSenseImplementation());
}

void ANPCAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Ensure AI logic only runs on the authoritative server
	if (!HasAuthority()) return;

	PossessedNPC = Cast<ANPCCharacter>(InPawn);
	if (PossessedNPC.IsValid())
	{
		// Listen to decision engine goal changes
		if (PossessedNPC->DecisionComponent)
		{
			PossessedNPC->DecisionComponent->OnNPCGoalSelected.AddDynamic(this, &ANPCAIController::HandleGoalSelected);
		}

		// Listen to rage state
		if (PossessedNPC->MentalStateComponent)
		{
			PossessedNPC->MentalStateComponent->OnNPCEnraged.AddDynamic(this, &ANPCAIController::HandleNPCEnraged);
		}

		// Listen to bladder emergency
		if (PossessedNPC->NeedsComponent)
		{
			PossessedNPC->NeedsComponent->OnToiletEmergencyFailed.AddDynamic(this, &ANPCAIController::HandleToiletAccident);
		}

		// Listen to perception updates
		AIPerceptionComp->OnTargetPerceptionUpdated.AddDynamic(this, &ANPCAIController::HandlePerceptionUpdated);

		// Listen to physics simulation / collapse / wake up changes
		PossessedNPC->OnPhysicsStateChanged.AddDynamic(this, &ANPCAIController::HandlePhysicsStateChanged);

		// Start 10-second Power and Bed feasibility audit timer
		LastAuditLocation = InPawn->GetActorLocation();
		UWorld* World = GetWorld();
		if (World)
		{
			World->GetTimerManager().SetTimer(PowerAuditTimerHandle, this, &ANPCAIController::AuditPowerAndTaskFeasibility, PowerAuditInterval, true);
		}

		// Execute initial goal
		if (PossessedNPC->DecisionComponent)
		{
			ExecuteGoal(PossessedNPC->DecisionComponent->GetActiveGoal());
		}
	}
}

void ANPCAIController::OnUnPossess()
{
	if (PossessedNPC.IsValid())
	{
		PossessedNPC->OnPhysicsStateChanged.RemoveDynamic(this, &ANPCAIController::HandlePhysicsStateChanged);
		if (PossessedNPC->DecisionComponent)
		{
			PossessedNPC->DecisionComponent->OnNPCGoalSelected.RemoveDynamic(this, &ANPCAIController::HandleGoalSelected);
		}
		if (PossessedNPC->MentalStateComponent)
		{
			PossessedNPC->MentalStateComponent->OnNPCEnraged.RemoveDynamic(this, &ANPCAIController::HandleNPCEnraged);
		}
		if (PossessedNPC->NeedsComponent)
		{
			PossessedNPC->NeedsComponent->OnToiletEmergencyFailed.RemoveDynamic(this, &ANPCAIController::HandleToiletAccident);
		}
	}

	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(RoamTimerHandle);
		World->GetTimerManager().ClearTimer(PowerAuditTimerHandle);
	}

	PossessedNPC = nullptr;
	Super::OnUnPossess();
}

void ANPCAIController::ExecuteGoal(FName Goal)
{
	if (!HasAuthority()) return;
	if (PossessedNPC.IsValid() && PossessedNPC->IsSimulatingPhysics()) return;

	CurrentGoalName = Goal;
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn) return;

	if (Goal == FName("SatisfyHunger"))
	{
		FindFood();
		return;
	}
	else if (Goal == FName("SatisfyThirst"))
	{
		FindDrink();
		return;
	}
	else if (Goal == FName("RelieveBladder"))

	{
		AActor* Target = FindNearestInteractableForAction(EAffordanceAction::UseToilet);
		if (Target)
		{
			CurrentTargetActor = Target;
			const EPathFollowingRequestResult::Type MoveRes = MoveToActor(Target, 100.0f);
			if (MoveRes == EPathFollowingRequestResult::RequestSuccessful) return;
			if (MoveRes == EPathFollowingRequestResult::AlreadyAtGoal)
			{
				CompleteInteractionAtTarget();
				return;
			}
		}

		if (PossessedNPC.IsValid() && PossessedNPC->WorldModelComponent)
		{
			FVector RememberedPlace;
			if (PossessedNPC->WorldModelComponent->FindBestKnownPlaceForStat(ENPCSimulationStat::Bladder, false, RememberedPlace))
			{
				const EPathFollowingRequestResult::Type MoveRes = MoveToLocation(RememberedPlace, 80.0f);
				if (MoveRes == EPathFollowingRequestResult::RequestSuccessful) return;
				if (MoveRes == EPathFollowingRequestResult::AlreadyAtGoal)
				{
					CompleteInteractionAtTarget();
					return;
				}
			}
		}
	}
	else if (Goal == FName("RestSleep"))
	{
		// If already inside an active zone with favorable stimulus utility, stop and charge!
		if (PossessedNPC.IsValid() && PossessedNPC->DecisionComponent)
		{
			for (const auto& ZonePtr : PossessedNPC->GetActiveSimulationZones())
			{
				if (ZonePtr.IsValid())
				{
					FString Reason;
					if (PossessedNPC->DecisionComponent->DecideActionFromZoneStimuli(ZonePtr.Get(), Reason) == FName("StayAndAbsorb"))
					{
						EnterZoneCharging(ZonePtr.Get());
						return;
					}
				}
			}
		}

		AActor* Target = FindNearestInteractableForAction(EAffordanceAction::Sleep);
		if (Target)
		{
			CurrentTargetActor = Target;
			const EPathFollowingRequestResult::Type MoveRes = MoveToActor(Target, 100.0f);
			if (MoveRes == EPathFollowingRequestResult::RequestSuccessful) return;
			if (MoveRes == EPathFollowingRequestResult::AlreadyAtGoal)
			{
				CompleteInteractionAtTarget();
				return;
			}
		}

		// Fallback: search for power resource (bed, recharge station, etc.) via parameterized FindPower()
		FindPower();
		return;
	}
	else if (Goal == FName("WarmUp"))
	{
		FindWarmPlace();
		return;
	}
	else if (Goal == FName("CoolDown"))
	{
		FindChillPlace();
		return;
	}
	else if (Goal == FName("RelieveStress"))
	{
		FindStressRelief();
		return;
	}
	else if (Goal == FName("VandalizeRage"))
	{
		AActor* Target = FindNearestInteractableForAction(EAffordanceAction::Approach);
		if (Target)
		{
			CurrentTargetActor = Target;
			const EPathFollowingRequestResult::Type MoveRes = MoveToActor(Target, 100.0f);
			if (MoveRes == EPathFollowingRequestResult::RequestSuccessful) return;
			if (MoveRes == EPathFollowingRequestResult::AlreadyAtGoal)
			{
				CompleteInteractionAtTarget();
				return;
			}
		}
	}

	// Default fallback: wander/roam in area
	ExecuteRoam();
}

AActor* ANPCAIController::FindNearestInteractableForAction(EAffordanceAction DesiredAction, float SearchRadius)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn) return nullptr;

	const FVector PawnLoc = ControlledPawn->GetActorLocation();
	TArray<AActor*> OverlappingActors;
	UGameplayStatics::GetAllActorsWithInterface(GetWorld(), UWorldAffordanceInterface::StaticClass(), OverlappingActors);

	AActor* Closest = nullptr;
	float BestDistSq = SearchRadius * SearchRadius;

	for (AActor* Act : OverlappingActors)
	{
		if (!Act || Act == ControlledPawn) continue;

		TArray<FAffordanceOption> Options;
		IWorldAffordanceInterface::Execute_QueryAffordanceOptions(Act, ControlledPawn, Options);

		for (const FAffordanceOption& Opt : Options)
		{
			if (Opt.ActionType == DesiredAction && Opt.bIsAllowed)
			{
				const float DistSq = FVector::DistSquared(PawnLoc, Act->GetActorLocation());
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					Closest = Act;
				}
				break;
			}
		}
	}

	// Also check components
	if (!Closest)
	{
		TArray<AActor*> AllActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AActor::StaticClass(), AllActors);
		for (AActor* Act : AllActors)
		{
			if (!Act || Act == ControlledPawn) continue;

			UNPCAffordanceComponent* AffordComp = Act->FindComponentByClass<UNPCAffordanceComponent>();
			if (AffordComp && AffordComp->HasAffordance(DesiredAction))
			{
				const float DistSq = FVector::DistSquared(PawnLoc, Act->GetActorLocation());
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					Closest = Act;
				}
			}
		}
	}

	return Closest;
}

void ANPCAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);

	if (!HasAuthority()) return;

	if (Result.IsSuccess())
	{
		CompleteInteractionAtTarget();
	}
	else
	{
		if (bIsConductingReconnaissance)
		{
			// Candidate was unreachable, skip to next in reconnaissance queue
			QueryNextReconnaissanceCandidate();
			return;
		}

		// If path failed, wait a bit and roam
		UWorld* World = GetWorld();
		if (World)
		{
			World->GetTimerManager().SetTimer(RoamTimerHandle, this, &ANPCAIController::ExecuteRoam, 2.0f, false);
		}
	}
}

void ANPCAIController::CompleteInteractionAtTarget()
{
	if (!HasAuthority()) return;
	if (!PossessedNPC.IsValid()) return;
	if (PossessedNPC->IsSimulatingPhysics()) return;

	// 0. Physical Reconnaissance Query: inspect candidate, record memory, and check if it satisfies query
	if (bIsConductingReconnaissance && CurrentTargetActor.IsValid())
	{
		AActor* Target = CurrentTargetActor.Get();
		const float CurrentCash = PossessedNPC->PersonalityComponent ? PossessedNPC->PersonalityComponent->GetCash() : 0.0f;

		// Operable Machine Candidate
		if (ANPCOperableObject* Operable = Cast<ANPCOperableObject>(Target))
		{
			if (PossessedNPC->WorldModelComponent)
			{
				PossessedNPC->WorldModelComponent->RememberOperableMachine(Operable);
			}

			for (const auto& Offering : Operable->AvailableOfferings)
			{
				if (Operable->CanNPCAffordOffering(PossessedNPC.Get(), Offering))
				{
					for (const auto& Mod : Offering.OfferedStimuli)
					{
						const bool bRateMatches = PendingSearchQuery.bSeekingIncrease ? (Mod.ChangeRate >= PendingSearchQuery.MinDesirableRate) : (Mod.ChangeRate <= -PendingSearchQuery.MinDesirableRate);
						if (Mod.Stat == PendingSearchQuery.TargetStat && bRateMatches)
						{
							bIsConductingReconnaissance = false;
							ReconnaissanceCandidates.Empty();
							PendingOfferingId = Offering.OfferingId;

							UE_LOG(LogTemp, Log, TEXT("[%s Reconnaissance SUCCESS] Found matching machine [%s], offering [%s]! Operating."),
								*PossessedNPC->GetName(), *Operable->GetName(), *Offering.OfferingId.ToString());

							PossessedNPC->InteractWithOperableObject(Operable, PendingOfferingId);
							CurrentTargetActor = nullptr;
							PendingOfferingId = NAME_None;
							CurrentGoalName = FName("Idle");

							UWorld* World = GetWorld();
							if (World && PossessedNPC->DecisionComponent)
							{
								World->GetTimerManager().SetTimer(RoamTimerHandle, this, &ANPCAIController::ReevaluateGoalAfterInteraction, 1.5f, false);
							}
							return;
						}
					}
				}
			}
		}
		// Consumable Prop Candidate
		else if (ANPCConsumableProp* Prop = Cast<ANPCConsumableProp>(Target))
		{
			if (PossessedNPC->WorldModelComponent)
			{
				PossessedNPC->WorldModelComponent->RememberConsumableProp(Prop);
			}

			if (!Prop->IsEmpty() && Prop->Price <= CurrentCash)
			{
				bool bMatchesStat = false;
				for (const auto& Mod : Prop->StimuliPerPortion)
				{
					const bool bRateMatches = PendingSearchQuery.bSeekingIncrease ? (Mod.ChangeRate >= PendingSearchQuery.MinDesirableRate) : (Mod.ChangeRate <= -PendingSearchQuery.MinDesirableRate);
					if (Mod.Stat == PendingSearchQuery.TargetStat && bRateMatches)
					{
						bMatchesStat = true;
						break;
					}
				}
				if (!bMatchesStat)
				{
					if (PendingSearchQuery.TargetStat == ENPCSimulationStat::Hunger && Prop->ConsumableType == EConsumablePropType::Food) bMatchesStat = true;
					if (PendingSearchQuery.TargetStat == ENPCSimulationStat::Thirst && (Prop->ConsumableType == EConsumablePropType::Drink || Prop->ConsumableType == EConsumablePropType::Alcohol)) bMatchesStat = true;
				}

				if (bMatchesStat)
				{
					bIsConductingReconnaissance = false;
					ReconnaissanceCandidates.Empty();

					UE_LOG(LogTemp, Log, TEXT("[%s Reconnaissance SUCCESS] Found matching prop [%s]! Consuming portion."),
						*PossessedNPC->GetName(), *Prop->GetName());

					Prop->ConsumePortion(PossessedNPC.Get());
					CurrentTargetActor = nullptr;
					PendingOfferingId = NAME_None;
					CurrentGoalName = FName("Idle");

					UWorld* World = GetWorld();
					if (World && PossessedNPC->DecisionComponent)
					{
						World->GetTimerManager().SetTimer(RoamTimerHandle, this, &ANPCAIController::ReevaluateGoalAfterInteraction, 1.0f, false);
					}
					return;
				}
			}
		}
		// Generic Affordance Candidate
		else if (Target->Implements<UWorldAffordanceInterface>())
		{
			TArray<FAffordanceOption> Options;
			IWorldAffordanceInterface::Execute_QueryAffordanceOptions(Target, PossessedNPC.Get(), Options);
			if (PossessedNPC->WorldModelComponent)
			{
				for (const auto& Opt : Options)
				{
					PossessedNPC->WorldModelComponent->RecordDiscovery(Target->GetFName(), Opt.RequiredTag, Opt.ProducedTag, Target->GetActorLocation());
				}
			}

			for (const auto& Opt : Options)
			{
				if (!Opt.bIsAllowed || Opt.MoneyCost > CurrentCash) continue;

				for (const auto& Mod : Opt.OfferedStimuli)
				{
					const bool bRateMatches = PendingSearchQuery.bSeekingIncrease ? (Mod.ChangeRate >= PendingSearchQuery.MinDesirableRate) : (Mod.ChangeRate <= -PendingSearchQuery.MinDesirableRate);
					if (Mod.Stat == PendingSearchQuery.TargetStat && bRateMatches)
					{
						bIsConductingReconnaissance = false;
						ReconnaissanceCandidates.Empty();

						UE_LOG(LogTemp, Log, TEXT("[%s Reconnaissance SUCCESS] Found matching affordance [%s] on [%s]! Interacting."),
							*PossessedNPC->GetName(), *Opt.OptionId.ToString(), *Target->GetName());

						IWorldAffordanceInterface::Execute_ExecuteAffordanceOption(Target, Opt.OptionId, PossessedNPC.Get());
						CurrentTargetActor = nullptr;
						CurrentGoalName = FName("Idle");

						UWorld* World = GetWorld();
						if (World && PossessedNPC->DecisionComponent)
						{
							World->GetTimerManager().SetTimer(RoamTimerHandle, this, &ANPCAIController::ReevaluateGoalAfterInteraction, 1.5f, false);
						}
						return;
					}
				}
			}
		}

		// Candidate did NOT satisfy our query -> move to next candidate
		CurrentTargetActor = nullptr;
		QueryNextReconnaissanceCandidate();
		return;
	}

	// 1. Check if target is a physical consumable prop (drink can, food item)
	if (CurrentTargetActor.IsValid())
	{
		if (ANPCConsumableProp* Prop = Cast<ANPCConsumableProp>(CurrentTargetActor.Get()))
		{
			Prop->ConsumePortion(PossessedNPC.Get());
			CurrentTargetActor = nullptr;
			PendingOfferingId = NAME_None;
			CurrentGoalName = FName("Idle");

			UWorld* World = GetWorld();
			if (World && PossessedNPC->DecisionComponent)
			{
				World->GetTimerManager().SetTimer(RoamTimerHandle, this, &ANPCAIController::ReevaluateGoalAfterInteraction, 1.0f, false);
			}
			return;
		}
		// 2. Check if target is an interactive operable machine (vending machine, massage sofa, computer terminal)
		else if (ANPCOperableObject* Machine = Cast<ANPCOperableObject>(CurrentTargetActor.Get()))
		{
			const bool bWasComputerTerminal = Machine->bIsComputerTerminal && Machine->bHasInternetConnection;

			PossessedNPC->InteractWithOperableObject(Machine, PendingOfferingId);
			CurrentTargetActor = nullptr;
			PendingOfferingId = NAME_None;

			if (bWasComputerTerminal)
			{
				UE_LOG(LogTemp, Log, TEXT("[%s] Synced knowledge via computer terminal! Re-evaluating search query with newly downloaded global data."),
					*PossessedNPC->GetName());

				// Retry the search query with computer fallback disabled so we do not loop infinitely at the terminal
				FNPCResourceSearchQuery UpdatedQuery = PendingSearchQuery;
				UpdatedQuery.bAllowComputerSearchFallback = false;
				FindResource(UpdatedQuery);
				return;
			}

			CurrentGoalName = FName("Idle");

			UWorld* World = GetWorld();
			if (World && PossessedNPC->DecisionComponent)
			{
				World->GetTimerManager().SetTimer(RoamTimerHandle, this, &ANPCAIController::ReevaluateGoalAfterInteraction, 1.5f, false);
			}
			return;
		}
	}

	if (CurrentGoalName == FName("SatisfyHunger"))

	{
		if (PossessedNPC->NeedsComponent)
		{
			PossessedNPC->NeedsComponent->ConsumeFood(50.0f);
		}
		if (PossessedNPC->MentalStateComponent)
		{
			PossessedNPC->MentalStateComponent->GrantDopamine(25.0f);
			PossessedNPC->MentalStateComponent->RelieveStress(15.0f);
		}
		CurrentTargetActor = nullptr;
	}
	else if (CurrentGoalName == FName("SatisfyThirst"))
	{
		if (PossessedNPC->NeedsComponent)
		{
			PossessedNPC->NeedsComponent->ConsumeDrink(40.0f);
		}
		if (PossessedNPC->MentalStateComponent)
		{
			PossessedNPC->MentalStateComponent->GrantDopamine(15.0f);
		}
		CurrentTargetActor = nullptr;
	}
	else if (CurrentGoalName == FName("RelieveBladder"))
	{
		if (PossessedNPC->NeedsComponent)
		{
			PossessedNPC->NeedsComponent->RelieveBladder();
			PossessedNPC->NeedsComponent->RelieveBowel();
		}
		if (PossessedNPC->MentalStateComponent)
		{
			PossessedNPC->MentalStateComponent->RelieveStress(25.0f);
		}
		CurrentTargetActor = nullptr;
	}
	else if (CurrentGoalName == FName("RestSleep"))
	{
		// If inside an active zone with favorable stimulus utility, enter persistent stationary stay/recharge!
		if (PossessedNPC.IsValid() && PossessedNPC->DecisionComponent)
		{
			for (const auto& ZonePtr : PossessedNPC->GetActiveSimulationZones())
			{
				if (ZonePtr.IsValid())
				{
					FString Reason;
					if (PossessedNPC->DecisionComponent->DecideActionFromZoneStimuli(ZonePtr.Get(), Reason) == FName("StayAndAbsorb"))
					{
						EnterZoneCharging(ZonePtr.Get());
						return;
					}
				}
			}
		}

		// Only restore power if NPC actually reached a valid Bed affordance or known power recharge place!
		bool bValidRestLocation = false;
		if (CurrentTargetActor.IsValid())
		{
			UNPCAffordanceComponent* AffordComp = CurrentTargetActor->FindComponentByClass<UNPCAffordanceComponent>();
			if (AffordComp && AffordComp->HasAffordance(EAffordanceAction::Sleep))
			{
				bValidRestLocation = true;
			}
		}
		else if (PossessedNPC->WorldModelComponent)
		{
			FVector PowerPlace;
			if (PossessedNPC->WorldModelComponent->FindBestKnownPlaceForStat(ENPCSimulationStat::Power, true, PowerPlace))
			{
				if (FVector::DistSquared(PossessedNPC->GetActorLocation(), PowerPlace) <= FMath::Square(300.0f))
				{
					bValidRestLocation = true;
				}
			}
		}

		if (bValidRestLocation && PossessedNPC->NeedsComponent)
		{
			PossessedNPC->NeedsComponent->Rest(60.0f);
		}
		CurrentTargetActor = nullptr;
		CurrentGoalName = FName("Idle");
	}
	else if (CurrentGoalName == FName("VandalizeRage"))
	{
		if (PossessedNPC->MentalStateComponent)
		{
			// Calms down slightly after smashing/messing
			PossessedNPC->MentalStateComponent->RelieveStress(20.0f);
		}
		CurrentTargetActor = nullptr;
	}

	// Re-evaluate next goal after brief delay
	UWorld* World = GetWorld();
	if (World && PossessedNPC->DecisionComponent)
	{
		World->GetTimerManager().SetTimer(RoamTimerHandle, this, &ANPCAIController::ReevaluateGoalAfterInteraction, 1.5f, false);
	}
}

void ANPCAIController::ReevaluateGoalAfterInteraction()
{
	if (PossessedNPC.IsValid() && PossessedNPC->DecisionComponent && !PossessedNPC->IsSimulatingPhysics())
	{
		ExecuteGoal(PossessedNPC->DecisionComponent->EvaluateGoals());
	}
}

void ANPCAIController::ExecuteRoam()
{
	if (!HasAuthority()) return;
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn) return;
	if (PossessedNPC.IsValid() && PossessedNPC->IsSimulatingPhysics()) return;

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (NavSys)
	{
		FNavLocation RandomNavLoc;
		if (NavSys->GetRandomReachablePointInRadius(ControlledPawn->GetActorLocation(), 1200.0f, RandomNavLoc))
		{
			const EPathFollowingRequestResult::Type MoveRes = MoveToLocation(RandomNavLoc.Location, 50.0f);
			if (MoveRes == EPathFollowingRequestResult::RequestSuccessful)
			{
				return;
			}
		}
	}

	// If NavSys is null, unreachable, or MoveTo failed: schedule retry in 1.5s so AI doesn't get stuck
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().SetTimer(RoamTimerHandle, this, &ANPCAIController::ExecuteRoam, 1.5f, false);
	}
}

void ANPCAIController::AuditPowerAndTaskFeasibility()
{
	if (!HasAuthority()) return;
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || !PossessedNPC.IsValid()) return;

	// If currently simulating physics (collapsed), check if power recovered enough to stand back up
	if (PossessedNPC->IsSimulatingPhysics())
	{
		if (PossessedNPC->StateComponent && PossessedNPC->StateComponent->PhysicalState.Power >= 35.0f)
		{
			PossessedNPC->StopPhysicsSimulation();
			// StopPhysicsSimulation broadcasts OnPhysicsStateChanged(false), which triggers ResumeAIAfterWakeup
		}
		return;
	}

	// 1. Audit active resting/charging inside simulation volume based on continuous stimulus utility valuation
	if (bIsChargingInZone)
	{
		if (!ActiveChargingZone.IsValid())
		{
			ExitZoneCharging(TEXT("Simulation volume is no longer valid"));
			return;
		}

		if (PossessedNPC->DecisionComponent)
		{
			FString DecisionReasoning;
			const FName Action = PossessedNPC->DecisionComponent->DecideActionFromZoneStimuli(ActiveChargingZone.Get(), DecisionReasoning);

			if (Action == FName("StayAndAbsorb"))
			{
				// Benefits of staying still outweigh costs! Check for critical emergency
				if (PossessedNPC->NeedsComponent && PossessedNPC->NeedsComponent->HasUrgentNeed())
				{
					ExitZoneCharging(TEXT("Urgent bodily emergency interrupted volume stay"));
					return;
				}

				// Content to remain stationary and recharge: do not let watchdog trigger roam!
				return;
			}
			else
			{
				// Utility has shifted: LeaveSatisfied (power full), LeaveBroke (out of money), or FleeHazard:
				ExitZoneCharging(DecisionReasoning);
				return;
			}
		}
	}

	const FVector CurrentLocation = ControlledPawn->GetActorLocation();
	const float DistMovedSq = FVector::DistSquared(CurrentLocation, LastAuditLocation);
	const bool bIsMoving = ControlledPawn->GetVelocity().SizeSquared() > 100.0f;
	const bool bHasMoved = DistMovedSq > FMath::Square(MovementThresholdForAudit);

	if (!bHasMoved && !bIsMoving)
	{
		// Watchdog: If NPC has been completely stationary for 10 seconds without active interaction, physics, or zone charging, kickstart roam!
		if (!PossessedNPC->IsSimulatingPhysics() && !CurrentTargetActor.IsValid() && !bIsChargingInZone)
		{
			ExecuteRoam();
		}
		return;
	}

	LastAuditLocation = CurrentLocation;

	FVector BedLocation = FVector::ZeroVector;
	AssessPowerAndTaskFeasibility(
		LastPowerToBed,
		LastTaskPowerCost,
		LastTotalPowerRequired,
		BedLocation,
		bLastAuditKnowsBed
	);

	const float CurrentPower = PossessedNPC->StateComponent ? PossessedNPC->StateComponent->PhysicalState.Power : 100.0f;

	if (!bLastAuditKnowsBed)
	{
		// NPC does NOT know where a bed or a way to increase power is!
		// If power is low or cannot sustain task, stop movement and start simulate physics!
		if (CurrentPower <= 20.0f || (LastTaskPowerCost > 0.0f && CurrentPower <= LastTaskPowerCost))
		{
			StopMovement();
			PossessedNPC->StartPhysicsSimulation();
			return;
		}
	}
	else
	{
		// NPC DOES know where a bed or power source is!
		// 1. Can NPC reach the bed directly?
		if (CurrentPower <= LastPowerToBed)
		{
			// Only collapse into physics simulation if power is critically low (<= 15.0f).
			// Otherwise, head straight towards the bed while power remains!
			if (CurrentPower <= 15.0f)
			{
				StopMovement();
				PossessedNPC->StartPhysicsSimulation();
				return;
			}
			else
			{
				ExecuteGoal(FName("RestSleep"));
				return;
			}
		}

		// 2. Can NPC perform the task AND reach the bed afterwards?
		if (LastTaskPowerCost > 0.0f && CurrentPower < LastTotalPowerRequired)
		{
			// Not enough power to perform task + travel to bed!
			// Abort task and head straight to bed immediately while power remains!
			ExecuteGoal(FName("RestSleep"));
		}
	}
}

bool ANPCAIController::AssessPowerAndTaskFeasibility(
	float& OutPowerToReachBed,
	float& OutTaskPowerCost,
	float& OutTotalPowerRequired,
	FVector& OutBedLocation,
	bool& bOutKnowsBedOrRecharge)
{
	OutPowerToReachBed = 0.0f;
	OutTaskPowerCost = 0.0f;
	OutTotalPowerRequired = 0.0f;
	OutBedLocation = FVector::ZeroVector;
	bOutKnowsBedOrRecharge = false;

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || !PossessedNPC.IsValid()) return false;

	const FVector CurrentLoc = ControlledPawn->GetActorLocation();

	// 1. Search for Bed / Sleep affordance actor in the level
	AActor* BedActor = FindNearestInteractableForAction(EAffordanceAction::Sleep, 10000.0f);
	if (BedActor)
	{
		OutBedLocation = BedActor->GetActorLocation();
		bOutKnowsBedOrRecharge = true;
	}
	else if (PossessedNPC->WorldModelComponent)
	{
		// 2. Check personal memory / saved places for a location that restores Power (DeltaRate > 0)
		FVector RememberedPowerLoc;
		if (PossessedNPC->WorldModelComponent->FindBestKnownPlaceForStat(ENPCSimulationStat::Power, true, RememberedPowerLoc))
		{
			OutBedLocation = RememberedPowerLoc;
			bOutKnowsBedOrRecharge = true;
		}
	}

	// Calculate power required to reach bed (0.002 power per distance unit: ~0.6 power/s at 300 speed)
	if (bOutKnowsBedOrRecharge)
	{
		const float DistToBed = FVector::Dist(CurrentLoc, OutBedLocation);
		OutPowerToReachBed = DistToBed * 0.002f;
	}
	else
	{
		OutPowerToReachBed = 9999.0f;
	}

	// 3. Check if NPC needs to perform any task and calculate power required
	if (CurrentGoalName != FName("Idle") && CurrentGoalName != FName("RestSleep"))
	{
		// Dynamically query StrategyComponent action chain for estimated effort
		if (PossessedNPC->StrategyComponent)
		{
			FNPCStrategy ActiveStrat;
			if (PossessedNPC->StrategyComponent->GetBestStrategyForGoal(CurrentGoalName, ActiveStrat))
			{
				OutTaskPowerCost = FMath::Max(2.0f, ActiveStrat.ActionChain.Num() * 2.0f);
			}
		}

		if (OutTaskPowerCost <= 0.0f)
		{
			if (CurrentGoalName == FName("SatisfyHunger")) OutTaskPowerCost = 5.0f;
			else if (CurrentGoalName == FName("SatisfyThirst")) OutTaskPowerCost = 3.0f;
			else if (CurrentGoalName == FName("RelieveBladder")) OutTaskPowerCost = 2.0f;
			else if (CurrentGoalName == FName("VandalizeRage")) OutTaskPowerCost = 15.0f;
			else OutTaskPowerCost = 4.0f;
		}

		// Travel power cost to reach the task
		FVector TaskLoc = CurrentLoc;
		if (CurrentTargetActor.IsValid())
		{
			TaskLoc = CurrentTargetActor->GetActorLocation();
			const float DistToTask = FVector::Dist(CurrentLoc, TaskLoc);
			OutTaskPowerCost += DistToTask * 0.002f;
		}

		// Travel power cost from the task to the bed/power recharge location
		float DistFromTaskToBed = 0.0f;
		if (bOutKnowsBedOrRecharge)
		{
			DistFromTaskToBed = FVector::Dist(TaskLoc, OutBedLocation);
		}
		OutTotalPowerRequired = OutTaskPowerCost + (DistFromTaskToBed * 0.002f);
	}
	else
	{
		OutTotalPowerRequired = OutPowerToReachBed;
	}

	return bOutKnowsBedOrRecharge;
}

void ANPCAIController::HandleGoalSelected(FName NewGoal)
{
	ExecuteGoal(NewGoal);
}

void ANPCAIController::HandleNPCEnraged()
{
	ExecuteGoal(FName("VandalizeRage"));
}

void ANPCAIController::HandleToiletAccident(bool bIsBladder)
{
	if (PossessedNPC.IsValid() && PossessedNPC->MentalStateComponent)
	{
		PossessedNPC->MentalStateComponent->AddStress(50.0f); // Extreme embarrassment
	}
}

void ANPCAIController::HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!HasAuthority()) return;
	if (!Actor || !Stimulus.WasSuccessfullySensed()) return;
	if (!PossessedNPC.IsValid() || PossessedNPC->IsSimulatingPhysics()) return;

	// 1. Operable Machine Perception & Memory Discovery
	if (ANPCOperableObject* Operable = Cast<ANPCOperableObject>(Actor))
	{
		if (PossessedNPC->WorldModelComponent)
		{
			PossessedNPC->WorldModelComponent->RememberOperableMachine(Operable);
		}

		// If NPC is actively seeking a resource or goal, check if this newly spotted machine solves it
		for (const auto& Offering : Operable->AvailableOfferings)
		{
			if (Operable->CanNPCAffordOffering(PossessedNPC.Get(), Offering))
			{
				for (const auto& Mod : Offering.OfferedStimuli)
				{
					const bool bMatchesSearch = (Mod.Stat == PendingSearchQuery.TargetStat && (PendingSearchQuery.bSeekingIncrease ? Mod.ChangeRate > 0.0f : Mod.ChangeRate < 0.0f));
					const bool bMatchesLegacyHunger = (CurrentGoalName == FName("SatisfyHunger") && Mod.Stat == ENPCSimulationStat::Hunger && Mod.ChangeRate < 0.0f);
					const bool bMatchesLegacyThirst = (CurrentGoalName == FName("SatisfyThirst") && Mod.Stat == ENPCSimulationStat::Thirst && Mod.ChangeRate < 0.0f);

					if (bMatchesSearch || bMatchesLegacyHunger || bMatchesLegacyThirst)
					{
						CurrentTargetActor = Operable;
						PendingOfferingId = Offering.OfferingId;
						MoveToActor(Operable, 120.0f);
						return;
					}
				}
			}
		}
	}
	// 2. Consumable Prop Perception & Memory Discovery
	else if (ANPCConsumableProp* Prop = Cast<ANPCConsumableProp>(Actor))
	{
		if (PossessedNPC->WorldModelComponent)
		{
			PossessedNPC->WorldModelComponent->RememberConsumableProp(Prop);
		}

		if (!Prop->IsEmpty())
		{
			const float Cash = PossessedNPC->PersonalityComponent ? PossessedNPC->PersonalityComponent->GetCash() : 0.0f;
			const bool bAffordable = (Prop->Price <= Cash);

			// Opportunistic consumption: if getting low, half-drank, or expiring soon, consume if cost is bearable!
			const bool bGettingLow = (Prop->QuantityLevel <= 0.65f || Prop->RemainingPortions <= 2 || Prop->ExpiryFreshness <= 0.60f);
			bool bHasNeed = false;
			if (PossessedNPC->NeedsComponent)
			{
				if (Prop->ConsumableType == EConsumablePropType::Drink && PossessedNPC->NeedsComponent->Needs.Thirst >= 20.0f) bHasNeed = true;
				if (Prop->ConsumableType == EConsumablePropType::Food && PossessedNPC->NeedsComponent->Needs.Hunger >= 20.0f) bHasNeed = true;
				if (Prop->ConsumableType == EConsumablePropType::Alcohol) bHasNeed = true;
			}

			// Check if matches active search query
			bool bMatchesSearch = false;
			for (const auto& Mod : Prop->StimuliPerPortion)
			{
				if (Mod.Stat == PendingSearchQuery.TargetStat && (PendingSearchQuery.bSeekingIncrease ? Mod.ChangeRate > 0.0f : Mod.ChangeRate < 0.0f))
				{
					bMatchesSearch = true;
					break;
				}
			}

			if (bAffordable && (bGettingLow || bHasNeed || bMatchesSearch))
			{
				CurrentTargetActor = Prop;
				PendingOfferingId = FName("ConsumePortion");
				MoveToActor(Prop, 80.0f);
				return;
			}
		}
	}
	// 3. Generic World Affordance Object Discovery
	else if (Actor->Implements<UWorldAffordanceInterface>())
	{
		TArray<FAffordanceOption> Options;
		IWorldAffordanceInterface::Execute_QueryAffordanceOptions(Actor, PossessedNPC.Get(), Options);
		if (PossessedNPC->WorldModelComponent && Options.Num() > 0)
		{
			for (const auto& Opt : Options)
			{
				PossessedNPC->WorldModelComponent->RecordDiscovery(Actor->GetFName(), Opt.RequiredTag, Opt.ProducedTag, Actor->GetActorLocation());
			}
		}
	}
}


void ANPCAIController::HandlePhysicsStateChanged(bool bIsSimulating)
{
	if (!HasAuthority()) return;

	if (bIsSimulating)
	{
		// Entering ragdoll / collapse: stop pathfinding immediately
		StopMovement();
		UWorld* World = GetWorld();
		if (World)
		{
			World->GetTimerManager().ClearTimer(RoamTimerHandle);
		}
	}
	else
	{
		// Waking up from collapse / physics simulation!
		StopMovement();

		APawn* ControlledPawn = GetPawn();
		if (ControlledPawn)
		{
			LastAuditLocation = ControlledPawn->GetActorLocation();
		}

		CurrentGoalName = FName("None");

		// Delay by 0.25s to allow physics settlement, floor trace, and NavMesh projection to finalize
		UWorld* World = GetWorld();
		if (World)
		{
			World->GetTimerManager().ClearTimer(RoamTimerHandle);
			World->GetTimerManager().SetTimer(RoamTimerHandle, this, &ANPCAIController::ResumeAIAfterWakeup, 0.25f, false);
		}
	}
}

void ANPCAIController::ResumeAIAfterWakeup()
{
	if (!HasAuthority()) return;
	if (!PossessedNPC.IsValid() || PossessedNPC->IsSimulatingPhysics()) return;

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn) return;

	// Ensure movement mode is Walking and physics/forces are clear
	if (UCharacterMovementComponent* MoveComp = PossessedNPC->GetCharacterMovement())
	{
		MoveComp->SetMovementMode(MOVE_Walking);
		MoveComp->Velocity = FVector::ZeroVector;
		MoveComp->ClearAccumulatedForces();
		MoveComp->UpdateComponentVelocity();
	}

	LastAuditLocation = ControlledPawn->GetActorLocation();
	CurrentTargetActor = nullptr;

	// Unpause pathfollowing component if needed
	if (UPathFollowingComponent* PFO = GetPathFollowingComponent())
	{
		if (PFO->GetStatus() == EPathFollowingStatus::Paused)
		{
			PFO->ResumeMove();
		}
	}

	// Dynamic Self-Assessment upon waking up: checks physical body, needs deficits, reinforcement strategies, and memory to decide what to take care of first
	if (PossessedNPC->DecisionComponent)
	{
		FString SelfCheckDiagnostic;
		const FName TopPriorityGoal = PossessedNPC->DecisionComponent->PerformSelfAssessment(SelfCheckDiagnostic);
		UE_LOG(LogTemp, Log, TEXT("[%s Autonomous Wakeup Self-Assessment]\n%s"), *PossessedNPC->GetName(), *SelfCheckDiagnostic);
		ExecuteGoal(TopPriorityGoal);
	}
	else
	{
		ExecuteRoam();
	}
}

void ANPCAIController::NotifyEnteredSimulationZone(ANPCSimulationTestZone* Zone)
{
	if (!Zone || !PossessedNPC.IsValid()) return;
	if (PossessedNPC->IsSimulatingPhysics()) return;

	// Introspectively evaluate all detected stimuli values in this zone
	if (PossessedNPC->DecisionComponent)
	{
		FString DecisionReasoning;
		const FName RecommendedAction = PossessedNPC->DecisionComponent->DecideActionFromZoneStimuli(Zone, DecisionReasoning);
		UE_LOG(LogTemp, Log, TEXT("[%s Detected Zone Stimuli]\n%s"), *PossessedNPC->GetName(), *DecisionReasoning);

		if (RecommendedAction == FName("StayAndAbsorb"))
		{
			// Detected that stimuli benefits outweigh costs: stops and stays!
			EnterZoneCharging(Zone);
		}
		else if (RecommendedAction == FName("FleeHazard"))
		{
			// Detected hazardous/toxic stimuli: flee immediately!
			ExecuteRoam();
		}
	}
}

void ANPCAIController::NotifyExitedSimulationZone(ANPCSimulationTestZone* Zone)
{
	if (ActiveChargingZone.Get() == Zone)
	{
		ExitZoneCharging(TEXT("Exited simulation zone boundary"));
	}
}

void ANPCAIController::EnterZoneCharging(ANPCSimulationTestZone* Zone)
{
	if (!Zone || !PossessedNPC.IsValid()) return;

	bIsChargingInZone = true;
	ActiveChargingZone = Zone;
	CurrentGoalName = FName("ChargingInZone");
	PossessedNPC->bIsRestingInSimulationZone = true;

	// Stop any active movement and clear roam timers
	StopMovement();
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(RoamTimerHandle);
	}

	UE_LOG(LogTemp, Log, TEXT("[%s] Figures out they can stop to charge power in volume (Fee: $%.2f/s). Halting movement."),
		*PossessedNPC->GetName(), Zone->GetMoneyCostRate());
}

void ANPCAIController::ExitZoneCharging(const FString& Reason)
{
	bIsChargingInZone = false;
	ActiveChargingZone = nullptr;

	if (PossessedNPC.IsValid())
	{
		PossessedNPC->bIsRestingInSimulationZone = false;
	}

	UE_LOG(LogTemp, Log, TEXT("[%s] Exited charging state: %s. Resuming autonomous decision making."),
		PossessedNPC.IsValid() ? *PossessedNPC->GetName() : TEXT("NPC"), *Reason);

	// Evaluate what needs to be taken care of next
	if (PossessedNPC.IsValid() && PossessedNPC->DecisionComponent)
	{
		ExecuteGoal(PossessedNPC->DecisionComponent->EvaluateGoals());
	}
	else
	{
		ExecuteRoam();
	}
}

void ANPCAIController::FindResource(const FNPCResourceSearchQuery& Query)
{
	if (!HasAuthority() || !PossessedNPC.IsValid()) return;
	if (PossessedNPC->IsSimulatingPhysics()) return;

	PendingSearchQuery = Query;
	bIsConductingReconnaissance = false;
	ReconnaissanceCandidates.Empty();
	CurrentReconnaissanceIndex = 0;

	CurrentGoalName = FName(*FString::Printf(TEXT("FindResource_%s"), *UEnum::GetValueAsString(Query.TargetStat)));

	const float CurrentCash = PossessedNPC->PersonalityComponent ? PossessedNPC->PersonalityComponent->GetCash() : 0.0f;
	const FVector PawnLoc = PossessedNPC->GetActorLocation();

	// -------------------------------------------------------------
	// STEP 1: Personal Memory & Opportunistic Scavenging
	// -------------------------------------------------------------
	if (PossessedNPC->WorldModelComponent)
	{
		// 1A. Opportunistic Scavenging (half-full, near expiry, getting low, bearable cost)
		if (Query.bAllowOpportunisticScavenge)
		{
			FVector ScavengeLoc;
			ANPCConsumableProp* ScavengeProp = nullptr;
			if (PossessedNPC->WorldModelComponent->FindOpportunisticScavengeProp(CurrentCash, ScavengeLoc, ScavengeProp))
			{
				if (ScavengeProp && !ScavengeProp->IsEmpty())
				{
					bool bSatisfiesQuery = false;
					for (const auto& Mod : ScavengeProp->StimuliPerPortion)
					{
						const bool bRateMatches = Query.bSeekingIncrease ? (Mod.ChangeRate >= Query.MinDesirableRate) : (Mod.ChangeRate <= -Query.MinDesirableRate);
						if (Mod.Stat == Query.TargetStat && bRateMatches)
						{
							bSatisfiesQuery = true;
							break;
						}
					}
					if (!bSatisfiesQuery)
					{
						if (Query.TargetStat == ENPCSimulationStat::Hunger && ScavengeProp->ConsumableType == EConsumablePropType::Food) bSatisfiesQuery = true;
						if (Query.TargetStat == ENPCSimulationStat::Thirst && (ScavengeProp->ConsumableType == EConsumablePropType::Drink || ScavengeProp->ConsumableType == EConsumablePropType::Alcohol)) bSatisfiesQuery = true;
					}

					if (bSatisfiesQuery && FVector::Dist(PawnLoc, ScavengeLoc) <= Query.MaxAcceptableDistance)
					{
						UE_LOG(LogTemp, Log, TEXT("[%s] Step 1: Found opportunistic scavenge prop [%s] at distance %.0f cm."),
							*PossessedNPC->GetName(), *ScavengeProp->GetName(), FVector::Dist(PawnLoc, ScavengeLoc));

						CurrentTargetActor = ScavengeProp;
						PendingOfferingId = FName("ConsumePortion");
						const EPathFollowingRequestResult::Type MoveRes = MoveToActor(ScavengeProp, 80.0f);
						if (MoveRes == EPathFollowingRequestResult::RequestSuccessful) return;
						if (MoveRes == EPathFollowingRequestResult::AlreadyAtGoal)
						{
							CompleteInteractionAtTarget();
							return;
						}
					}
				}
			}
		}

		// 1B. Best known machine, prop, or place in memory
		FVector BestLoc;
		AActor* TargetActor = nullptr;
		FName OfferingId = NAME_None;
		if (PossessedNPC->WorldModelComponent->FindBestKnownSourceForStat(
			Query.TargetStat, Query.bSeekingIncrease, CurrentCash, BestLoc, TargetActor, OfferingId))
		{
			const float Dist = FVector::Dist(PawnLoc, BestLoc);
			if (Dist <= Query.MaxAcceptableDistance)
			{
				UE_LOG(LogTemp, Log, TEXT("[%s] Step 1: Found known source in memory at distance %.0f cm (Target: %s, Offering: %s)."),
					*PossessedNPC->GetName(), Dist, TargetActor ? *TargetActor->GetName() : TEXT("ZoneLocation"), *OfferingId.ToString());

				if (TargetActor)
				{
					CurrentTargetActor = TargetActor;
					PendingOfferingId = OfferingId;
					const EPathFollowingRequestResult::Type MoveRes = MoveToActor(TargetActor, 100.0f);
					if (MoveRes == EPathFollowingRequestResult::RequestSuccessful) return;
					if (MoveRes == EPathFollowingRequestResult::AlreadyAtGoal)
					{
						CompleteInteractionAtTarget();
						return;
					}
				}
				else
				{
					const EPathFollowingRequestResult::Type MoveRes = MoveToLocation(BestLoc, 80.0f);
					if (MoveRes == EPathFollowingRequestResult::RequestSuccessful) return;
					if (MoveRes == EPathFollowingRequestResult::AlreadyAtGoal)
					{
						CompleteInteractionAtTarget();
						return;
					}
				}
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("[%s] Step 1: Known source in memory is TOO FAR (%.0f cm > %.0f cm max). Proceeding to computer / reconnaissance."),
					*PossessedNPC->GetName(), Dist, Query.MaxAcceptableDistance);
			}
		}
	}

	// -------------------------------------------------------------
	// STEP 2: Internet / Computer Knowledge Search Fallback
	// -------------------------------------------------------------
	if (Query.bAllowComputerSearchFallback)
	{
		ANPCOperableObject* Computer = FindNearestComputerWithInternet();
		if (Computer)
		{
			UE_LOG(LogTemp, Log, TEXT("[%s] Step 2: Heading to nearest computer terminal [%s] to download global network knowledge!"),
				*PossessedNPC->GetName(), *Computer->GetName());

			CurrentTargetActor = Computer;
			PendingOfferingId = Computer->AvailableOfferings.Num() > 0 ? Computer->AvailableOfferings[0].OfferingId : NAME_None;
			const EPathFollowingRequestResult::Type MoveRes = MoveToActor(Computer, 120.0f);
			if (MoveRes == EPathFollowingRequestResult::RequestSuccessful) return;
			if (MoveRes == EPathFollowingRequestResult::AlreadyAtGoal)
			{
				CompleteInteractionAtTarget();
				return;
			}
		}
	}

	// -------------------------------------------------------------
	// STEP 3: Physical Object Query Reconnaissance Fallback
	// -------------------------------------------------------------
	if (Query.bAllowPhysicalReconnaissanceFallback)
	{
		UE_LOG(LogTemp, Log, TEXT("[%s] Step 3: Starting physical reconnaissance of nearby machines and objects one by one."),
			*PossessedNPC->GetName());

		StartPhysicalObjectQueryReconnaissance(Query);
		return;
	}

	// Final Fallback: Roam/explore outward
	StartSearchingForResource(Query.TargetStat);
}

ANPCOperableObject* ANPCAIController::FindNearestComputerWithInternet(float MaxRadius) const
{
	if (!PossessedNPC.IsValid()) return nullptr;
	const FVector PawnLoc = PossessedNPC->GetActorLocation();

	TArray<AActor*> AllOperables;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ANPCOperableObject::StaticClass(), AllOperables);

	ANPCOperableObject* BestComputer = nullptr;
	float BestDistSq = MaxRadius * MaxRadius;

	for (AActor* Act : AllOperables)
	{
		ANPCOperableObject* Op = Cast<ANPCOperableObject>(Act);
		if (Op && Op->bIsComputerTerminal && Op->bHasInternetConnection)
		{
			const float DistSq = FVector::DistSquared(PawnLoc, Op->GetActorLocation());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestComputer = Op;
			}
		}
	}

	return BestComputer;
}

void ANPCAIController::StartPhysicalObjectQueryReconnaissance(const FNPCResourceSearchQuery& Query)
{
	if (!HasAuthority() || !PossessedNPC.IsValid()) return;
	if (PossessedNPC->IsSimulatingPhysics()) return;

	PendingSearchQuery = Query;
	bIsConductingReconnaissance = true;
	ReconnaissanceCandidates.Empty();
	CurrentReconnaissanceIndex = 0;

	const FVector PawnLoc = PossessedNPC->GetActorLocation();
	const float SearchRadius = Query.MaxAcceptableDistance > 0.0f ? Query.MaxAcceptableDistance : 3500.0f;

	TArray<AActor*> FoundActors;

	// 1. Gather all currently perceived actors via perception sight
	if (AIPerceptionComp)
	{
		TArray<AActor*> SightActors;
		AIPerceptionComp->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), SightActors);
		for (AActor* Act : SightActors)
		{
			if (Act && Act != PossessedNPC.Get())
			{
				FoundActors.AddUnique(Act);
			}
		}
	}

	// 2. Gather operable machines in radius
	TArray<AActor*> Operables;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ANPCOperableObject::StaticClass(), Operables);
	for (AActor* Act : Operables)
	{
		if (Act && FVector::DistSquared(PawnLoc, Act->GetActorLocation()) <= FMath::Square(SearchRadius))
		{
			FoundActors.AddUnique(Act);
		}
	}

	// 3. Gather consumable props in radius
	TArray<AActor*> Props;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ANPCConsumableProp::StaticClass(), Props);
	for (AActor* Act : Props)
	{
		if (Act && FVector::DistSquared(PawnLoc, Act->GetActorLocation()) <= FMath::Square(SearchRadius))
		{
			FoundActors.AddUnique(Act);
		}
	}

	// 4. Gather generic affordance objects in radius
	TArray<AActor*> AffordanceActors;
	UGameplayStatics::GetAllActorsWithInterface(GetWorld(), UWorldAffordanceInterface::StaticClass(), AffordanceActors);
	for (AActor* Act : AffordanceActors)
	{
		if (Act && Act != PossessedNPC.Get() && FVector::DistSquared(PawnLoc, Act->GetActorLocation()) <= FMath::Square(SearchRadius))
		{
			FoundActors.AddUnique(Act);
		}
	}

	// Filter down to valid candidates and sort by distance
	FoundActors.Sort([PawnLoc](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared(PawnLoc, A.GetActorLocation()) < FVector::DistSquared(PawnLoc, B.GetActorLocation());
	});

	for (AActor* Act : FoundActors)
	{
		ReconnaissanceCandidates.Add(Act);
	}

	if (ReconnaissanceCandidates.Num() > 0)
	{
		QueryNextReconnaissanceCandidate();
	}
	else
	{
		bIsConductingReconnaissance = false;
		StartSearchingForResource(Query.TargetStat);
	}
}

void ANPCAIController::QueryNextReconnaissanceCandidate()
{
	if (!HasAuthority() || !PossessedNPC.IsValid()) return;
	if (PossessedNPC->IsSimulatingPhysics()) return;

	while (CurrentReconnaissanceIndex < ReconnaissanceCandidates.Num())
	{
		AActor* Candidate = ReconnaissanceCandidates[CurrentReconnaissanceIndex].Get();
		CurrentReconnaissanceIndex++;

		if (!Candidate || Candidate == PossessedNPC.Get())
		{
			continue;
		}

		// Check if Candidate is already close enough (e.g. within 150 units) to query directly
		const float Dist = FVector::Dist(PossessedNPC->GetActorLocation(), Candidate->GetActorLocation());
		if (Dist <= 150.0f)
		{
			CurrentTargetActor = Candidate;
			CompleteInteractionAtTarget();
			return;
		}
		else
		{
			CurrentTargetActor = Candidate;
			const EPathFollowingRequestResult::Type MoveRes = MoveToActor(Candidate, 100.0f);
			if (MoveRes == EPathFollowingRequestResult::RequestSuccessful)
			{
				return;
			}
			if (MoveRes == EPathFollowingRequestResult::AlreadyAtGoal)
			{
				CompleteInteractionAtTarget();
				return;
			}
		}
	}

	// Reached end of candidate queue
	bIsConductingReconnaissance = false;
	UE_LOG(LogTemp, Log, TEXT("[%s] Completed physical reconnaissance queue. No matches found. Roaming outward."), *PossessedNPC->GetName());
	StartSearchingForResource(PendingSearchQuery.TargetStat);
}

void ANPCAIController::StartSearchingForResource(ENPCSimulationStat NeededStat)
{
	if (!HasAuthority() || !PossessedNPC.IsValid()) return;
	if (PossessedNPC->IsSimulatingPhysics()) return;

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn) return;

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (NavSys)
	{
		FNavLocation RandomNavLoc;
		// Explore outward with a wide radius (1800 units) to scan new rooms/areas with sight perception
		if (NavSys->GetRandomReachablePointInRadius(ControlledPawn->GetActorLocation(), 1800.0f, RandomNavLoc))
		{
			MoveToLocation(RandomNavLoc.Location, 60.0f);
			return;
		}
	}

	// Fallback to local roam if NavSys failed
	ExecuteRoam();
}

