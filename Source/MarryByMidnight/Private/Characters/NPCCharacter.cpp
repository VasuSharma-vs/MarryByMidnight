#include "Characters/NPCCharacter.h"
#include "Components/NPCStateComponent.h"
#include "Components/NPCNeedsComponent.h"
#include "Components/NPCMentalStateComponent.h"
#include "Components/NPCPersonalityComponent.h"
#include "Components/NPCWorldModelComponent.h"
#include "Components/NPCAffordanceComponent.h"
#include "Components/NPCStrategyComponent.h"
#include "Components/NPCDecisionComponent.h"
#include "Components/NPCPlannerComponent.h"
#include "Subsystems/NPCKnowledgeSubsystem.h"
#include "Environment/NPCSimulationTestZone.h"
#include "Environment/NPCOperableObject.h"
#include "AI/NPCAIController.h"

#include "Components/WidgetComponent.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/NPCRagdollComponent.h"
#include "Components/TextRenderComponent.h"
#include "Net/UnrealNetwork.h"

ANPCCharacter::ANPCCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Network Replication & AI Controller
	bReplicates = true;
	SetReplicateMovement(true);
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = ANPCAIController::StaticClass();

	// Movement Defaults
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 360.0f, 0.0f);
	GetCharacterMovement()->MaxWalkSpeed = 300.0f;
	bUseControllerRotationYaw = false;

	// Network Movement Smoothing & Floor Check to prevent sinking / jitter in multiplayer
	GetCharacterMovement()->SetIsReplicated(true);
	GetCharacterMovement()->bAlwaysCheckFloor = true;
	GetCharacterMovement()->NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;
	SetNetUpdateFrequency(66.0f);
	SetMinNetUpdateFrequency(33.0f);
	NetPriority = 2.0f;

	// Instantiate all 9 simulation & cognitive components + Ragdoll physics component
	StateComponent = CreateDefaultSubobject<UNPCStateComponent>(TEXT("NPCStateComponent"));
	NeedsComponent = CreateDefaultSubobject<UNPCNeedsComponent>(TEXT("NPCNeedsComponent"));
	MentalStateComponent = CreateDefaultSubobject<UNPCMentalStateComponent>(TEXT("NPCMentalStateComponent"));
	PersonalityComponent = CreateDefaultSubobject<UNPCPersonalityComponent>(TEXT("NPCPersonalityComponent"));
	WorldModelComponent = CreateDefaultSubobject<UNPCWorldModelComponent>(TEXT("NPCWorldModelComponent"));
	AffordanceComponent = CreateDefaultSubobject<UNPCAffordanceComponent>(TEXT("NPCAffordanceComponent"));
	StrategyComponent = CreateDefaultSubobject<UNPCStrategyComponent>(TEXT("NPCStrategyComponent"));
	DecisionComponent = CreateDefaultSubobject<UNPCDecisionComponent>(TEXT("NPCDecisionComponent"));
	PlannerComponent = CreateDefaultSubobject<UNPCPlannerComponent>(TEXT("NPCPlannerComponent"));
	RagdollComponent = CreateDefaultSubobject<UNPCRagdollComponent>(TEXT("NPCRagdollComponent"));

	// Setup overhead debug UI widget component in World Space so it scales with distance
	DebugWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("DebugWidgetComponent"));
	DebugWidgetComponent->SetupAttachment(RootComponent);
	DebugWidgetComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 115.0f));
	DebugWidgetComponent->SetWidgetSpace(EWidgetSpace::World);
	DebugWidgetComponent->SetDrawSize(FVector2D(320.0f, 220.0f));
	DebugWidgetComponent->SetRelativeScale3D(FVector(0.2f, 0.2f, 0.2f));
	DebugWidgetComponent->SetTwoSided(true);
	DebugWidgetComponent->SetVisibility(false);

	// Setup overhead 3D world-space debug text component (scales with perspective distance)
	DebugTextComponent = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DebugTextComponent"));
	DebugTextComponent->SetupAttachment(RootComponent);
	DebugTextComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 115.0f));
	DebugTextComponent->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	DebugTextComponent->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextBottom);
	DebugTextComponent->SetWorldSize(11.0f);
	DebugTextComponent->SetTextRenderColor(FColor::Cyan);
	DebugTextComponent->SetVisibility(false);
}

void ANPCCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (DebugTextComponent)
	{
		DebugTextComponent->SetVisibility(bShowDebugUI);
	}

	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		MeshInitialRelativeLocation = SkelMesh->GetRelativeLocation();
		MeshInitialRelativeRotation = SkelMesh->GetRelativeRotation();

		if (RagdollComponent)
		{
			RagdollComponent->SetTargetSkeletalMesh(SkelMesh);
		}
	}

	if (StateComponent)
	{
		StateComponent->OnNPCPassedOut.AddDynamic(this, &ANPCCharacter::StartPhysicsSimulation);
	}

	UWorld* World = GetWorld();
	if (World)
	{
		// Register with global cultural knowledge subsystem
		UNPCKnowledgeSubsystem* Subsystem = World->GetSubsystem<UNPCKnowledgeSubsystem>();
		if (Subsystem)
		{
			Subsystem->RegisterNPC(this);
		}
	}
}

void ANPCCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (StateComponent)
	{
		StateComponent->OnNPCPassedOut.RemoveDynamic(this, &ANPCCharacter::StartPhysicsSimulation);
	}

	UWorld* World = GetWorld();
	if (World)
	{
		UNPCKnowledgeSubsystem* Subsystem = World->GetSubsystem<UNPCKnowledgeSubsystem>();
		if (Subsystem)
		{
			Subsystem->UnregisterNPC(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

bool ANPCCharacter::CanInteract_Implementation(AActor* InstigatorActor)
{
	if (!StateComponent) return true;
	return !StateComponent->IsInCriticalCondition() && StateComponent->GetDizzinessTier() != EDizzinessTier::PassedOut;
}

void ANPCCharacter::QueryAffordanceOptions_Implementation(AActor* InstigatorActor, TArray<FAffordanceOption>& OutOptions)
{
	// 1. Talk / Socialize
	FAffordanceOption TalkOpt;
	TalkOpt.OptionId = FName("Talk");
	TalkOpt.DisplayLabel = FText::FromString(TEXT("Talk / Chat"));
	TalkOpt.ActionType = EAffordanceAction::Talk;
	TalkOpt.bIsAllowed = true;
	TalkOpt.ExecutionTime = 1.5f;
	OutOptions.Add(TalkOpt);

	// 2. Bribe
	FAffordanceOption BribeOpt;
	BribeOpt.OptionId = FName("Bribe");
	BribeOpt.DisplayLabel = FText::FromString(TEXT("Offer Bribe ($50)"));
	BribeOpt.ActionType = EAffordanceAction::Talk;
	BribeOpt.bIsAllowed = (PersonalityComponent != nullptr && PersonalityComponent->Archetype != ENPCArchetype::FoodInspector);
	BribeOpt.ExecutionTime = 1.0f;
	OutOptions.Add(BribeOpt);

	// 3. Pickpocket / Steal
	FAffordanceOption StealOpt;
	StealOpt.OptionId = FName("Pickpocket");
	StealOpt.DisplayLabel = FText::FromString(TEXT("Pickpocket"));
	StealOpt.ActionType = EAffordanceAction::Take;
	StealOpt.bIsAllowed = true;
	StealOpt.Risk = 0.6f;
	StealOpt.ExecutionTime = 2.0f;
	OutOptions.Add(StealOpt);
}

bool ANPCCharacter::ExecuteAffordanceOption_Implementation(FName OptionId, AActor* InstigatorActor)
{
	if (OptionId == FName("Talk"))
	{
		if (MentalStateComponent)
		{
			MentalStateComponent->GrantDopamine(10.0f);
			MentalStateComponent->RelieveStress(5.0f);
		}
		return true;
	}
	else if (OptionId == FName("Bribe"))
	{
		if (PersonalityComponent)
		{
			PersonalityComponent->WalletCash += 50.0f;
		}
		if (MentalStateComponent)
		{
			MentalStateComponent->GrantDopamine(20.0f);
			MentalStateComponent->RelieveStress(20.0f);
		}
		return true;
	}
	else if (OptionId == FName("Pickpocket"))
	{
		if (PersonalityComponent && PersonalityComponent->WalletCash > 0.0f)
		{
			PersonalityComponent->WalletCash = FMath::Max(0.0f, PersonalityComponent->WalletCash - 30.0f);
		}
		if (MentalStateComponent)
		{
			MentalStateComponent->AddStress(35.0f); // Becomes suspicious / stressed
		}
		return true;
	}

	return false;
}

ENPCMoodTier ANPCCharacter::GetCurrentMoodTier() const
{
	return MentalStateComponent ? MentalStateComponent->GetMoodTier() : ENPCMoodTier::Content;
}

ENPCArchetype ANPCCharacter::GetArchetype() const
{
	return PersonalityComponent ? PersonalityComponent->Archetype : ENPCArchetype::NormalPublic;
}

void ANPCCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bShowDebugUI)
	{
		DrawDebugOverheadStats();
	}
	else
	{
		if (DebugTextComponent && DebugTextComponent->IsVisible())
		{
			DebugTextComponent->SetVisibility(false);
		}
		if (DebugWidgetComponent && DebugWidgetComponent->IsVisible())
		{
			DebugWidgetComponent->SetVisibility(false);
		}
	}
}

void ANPCCharacter::DrawDebugOverheadStats()
{
	UWorld* World = GetWorld();
	if (!World) return;

	// Lazy creation if this is an older placed actor instance where constructor didn't run
	if (!DebugTextComponent)
	{
		DebugTextComponent = NewObject<UTextRenderComponent>(this, TEXT("DynamicDebugTextComp"));
		if (DebugTextComponent)
		{
			DebugTextComponent->RegisterComponent();
			DebugTextComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			DebugTextComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 115.0f));
			DebugTextComponent->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
			DebugTextComponent->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextBottom);
			DebugTextComponent->SetWorldSize(11.0f);
		}
	}

	if (DebugTextComponent)
	{
		if (!DebugTextComponent->IsVisible())
		{
			DebugTextComponent->SetVisibility(true);
		}

		const FString StatsString = GetDebugStatsFormattedString();
		DebugTextComponent->SetText(FText::FromString(StatsString));

		// Color-code based on mood or critical condition
		FColor DisplayColor = FColor::Cyan;
		if (StateComponent && StateComponent->IsInCriticalCondition())
		{
			DisplayColor = FColor::Red;
		}
		else if (MentalStateComponent)
		{
			switch (MentalStateComponent->CurrentMoodTier)
			{
			case ENPCMoodTier::Ecstatic:
			case ENPCMoodTier::Happy:
				DisplayColor = FColor::Green;
				break;
			case ENPCMoodTier::Content:
			case ENPCMoodTier::Neutral:
				DisplayColor = FColor::Cyan;
				break;
			case ENPCMoodTier::Discontent:
				DisplayColor = FColor::Yellow;
				break;
			case ENPCMoodTier::Frustrated:
				DisplayColor = FColor::Orange;
				break;
			case ENPCMoodTier::Enraged:
				DisplayColor = FColor::Red;
				break;
			default:
				break;
			}
		}

		DebugTextComponent->SetTextRenderColor(DisplayColor);

		// Billboard: face player camera in true 3D world space
		if (APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(World, 0))
		{
			const FVector CamLoc = CamMgr->GetCameraLocation();
			const FVector TextLoc = DebugTextComponent->GetComponentLocation();
			FRotator FaceRot = (CamLoc - TextLoc).Rotation();
			FaceRot.Pitch = 0.0f; // Keep text upright
			FaceRot.Roll = 0.0f;
			DebugTextComponent->SetWorldRotation(FaceRot);
		}
	}
}

FString ANPCCharacter::GetDebugStatsFormattedString() const
{
	FString ArchetypeName = TEXT("Unknown");
	float Money = 0.0f;
	if (PersonalityComponent)
	{
		ArchetypeName = UEnum::GetDisplayValueAsText(PersonalityComponent->Archetype).ToString();
		Money = PersonalityComponent->WalletCash;
	}

	float Hunger = 0.0f;
	float Thirst = 0.0f;
	if (NeedsComponent)
	{
		Hunger = NeedsComponent->Needs.Hunger;
		Thirst = NeedsComponent->Needs.Thirst;
	}

	float Power = 0.0f;
	float BodyTemp = 37.0f;
	float RoomTemp = 24.0f;
	float Sickness = 0.0f;
	if (StateComponent)
	{
		Power = StateComponent->PhysicalState.Power;
		BodyTemp = StateComponent->PhysicalState.BodyTemperature;
		RoomTemp = StateComponent->PhysicalState.AmbientTemperature;
		Sickness = StateComponent->PhysicalState.Sickness;
	}

	FString MoodName = TEXT("Content");
	float Valence = 0.0f;
	if (MentalStateComponent)
	{
		MoodName = UEnum::GetDisplayValueAsText(MentalStateComponent->CurrentMoodTier).ToString();
		Valence = MentalStateComponent->MentalState.Valence;
	}

	FString BaseStr = FString::Printf(
		TEXT("[%s]\nHunger: %.1f/100 | Thirst: %.1f/100\nPower: %.1f/100 | Body: %.1f C (Room: %.1f C)\nSickness: %.1f%% | Mood: %s (V: %+.2f)\nCash: $%.2f"),
		*ArchetypeName,
		Hunger, Thirst,
		Power, BodyTemp, RoomTemp,
		Sickness, *MoodName, Valence,
		Money
	);

	if (RagdollComponent && RagdollComponent->IsRagdollActive())
	{
		const FString StateName = UEnum::GetDisplayValueAsText(RagdollComponent->GetRagdollState()).ToString();
		BaseStr += FString::Printf(TEXT("\n[RAGDOLL: %s]"), *StateName);
	}
	else if (bIsSimulatingPhysics)
	{
		BaseStr += TEXT("\n[COLLAPSED (SIMULATING PHYSICS)]");
	}

	if (DecisionComponent)
	{
		BaseStr += FString::Printf(
			TEXT("\n[Drive: %s (Score: %.0f)]"),
			*DecisionComponent->GetActiveGoal().ToString(),
			DecisionComponent->ActiveGoalScore
		);
		if (!DecisionComponent->LastSelfAssessmentSummary.IsEmpty())
		{
			BaseStr += FString::Printf(TEXT("\n[Self-Check: %s]"), *DecisionComponent->LastSelfAssessmentSummary);
		}
	}

	if (AController* Ctrl = GetController())
	{
		if (ANPCAIController* AICtrl = Cast<ANPCAIController>(Ctrl))
		{
			BaseStr += FString::Printf(
				TEXT("\n[Energy Audit] Bed Known: %s | ToBed: %.1f | TaskCost: %.1f"),
				AICtrl->bLastAuditKnowsBed ? TEXT("YES") : TEXT("NO"),
				AICtrl->LastPowerToBed,
				AICtrl->LastTaskPowerCost
			);

			if (AICtrl->IsChargingInZone())
			{
				BaseStr += TEXT("\n[⚡ RESTING & CHARGING IN VOLUME]");
			}
			else if (IsInPowerRechargeZone())
			{
				BaseStr += TEXT("\n[⚡ INSIDE POWER VOLUME]");
			}
		}
	}

	if (DecisionComponent && DecisionComponent->LastZoneEvaluation.RecommendedAction != FName("None"))
	{
		BaseStr += FString::Printf(
			TEXT("\n[Stimulus Value: Net %+.1f (%s)]"),
			DecisionComponent->LastZoneEvaluation.NetUtilityScore,
			*DecisionComponent->LastZoneEvaluation.RecommendedAction.ToString()
		);
	}

	return BaseStr;
}

void ANPCCharacter::SetDebugUIEnabled(bool bEnable)
{
	bShowDebugUI = bEnable;
	if (DebugTextComponent)
	{
		DebugTextComponent->SetVisibility(bEnable);
	}
	if (DebugWidgetComponent)
	{
		DebugWidgetComponent->SetVisibility(bEnable);
	}
}

void ANPCCharacter::ToggleDebugUI()
{
	SetDebugUIEnabled(!bShowDebugUI);
}

void ANPCCharacter::SetAllNPCDebugUIEnabled(const UObject* WorldContextObject, bool bEnable)
{
	if (!WorldContextObject) return;
	UWorld* World = WorldContextObject->GetWorld();
	if (!World) return;

	TArray<AActor*> FoundNPCs;
	UGameplayStatics::GetAllActorsOfClass(World, ANPCCharacter::StaticClass(), FoundNPCs);
	for (AActor* Act : FoundNPCs)
	{
		if (ANPCCharacter* NPC = Cast<ANPCCharacter>(Act))
		{
			NPC->SetDebugUIEnabled(bEnable);
		}
	}
}

void ANPCCharacter::ToggleAllNPCDebugUI(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return;
	UWorld* World = WorldContextObject->GetWorld();
	if (!World) return;

	TArray<AActor*> FoundNPCs;
	UGameplayStatics::GetAllActorsOfClass(World, ANPCCharacter::StaticClass(), FoundNPCs);
	if (FoundNPCs.Num() > 0)
	{
		ANPCCharacter* FirstNPC = Cast<ANPCCharacter>(FoundNPCs[0]);
		const bool bNewState = FirstNPC ? !FirstNPC->bShowDebugUI : true;
		for (AActor* Act : FoundNPCs)
		{
			if (ANPCCharacter* NPC = Cast<ANPCCharacter>(Act))
			{
				NPC->SetDebugUIEnabled(bNewState);
			}
		}
	}
}

void ANPCCharacter::ApplySimulationStatDelta(ENPCSimulationStat Stat, float DeltaAmount)
{
	switch (Stat)
	{
	case ENPCSimulationStat::Power:
		if (StateComponent) StateComponent->ModifyPower(DeltaAmount);
		break;
	case ENPCSimulationStat::Thirst:
		if (NeedsComponent) NeedsComponent->Needs.Thirst = FMath::Clamp(NeedsComponent->Needs.Thirst + DeltaAmount, 0.0f, 100.0f);
		break;
	case ENPCSimulationStat::Hunger:
		if (NeedsComponent) NeedsComponent->Needs.Hunger = FMath::Clamp(NeedsComponent->Needs.Hunger + DeltaAmount, 0.0f, 100.0f);
		break;
	case ENPCSimulationStat::Sleepiness:
		if (NeedsComponent) NeedsComponent->Needs.Sleepiness = FMath::Clamp(NeedsComponent->Needs.Sleepiness + DeltaAmount, 0.0f, 100.0f);
		break;
	case ENPCSimulationStat::Bladder:
		if (NeedsComponent) NeedsComponent->Needs.BladderUrgency = FMath::Clamp(NeedsComponent->Needs.BladderUrgency + DeltaAmount, 0.0f, 100.0f);
		break;
	case ENPCSimulationStat::Bowel:
		if (NeedsComponent) NeedsComponent->Needs.BowelUrgency = FMath::Clamp(NeedsComponent->Needs.BowelUrgency + DeltaAmount, 0.0f, 100.0f);
		break;
	case ENPCSimulationStat::BodyTemperature:
		if (StateComponent) StateComponent->PhysicalState.BodyTemperature = FMath::Clamp(StateComponent->PhysicalState.BodyTemperature + DeltaAmount, 20.0f, 50.0f);
		break;
	case ENPCSimulationStat::Sickness:
		if (StateComponent)
		{
			if (DeltaAmount > 0.0f) StateComponent->InflictSickness(DeltaAmount);
			else StateComponent->HealSickness(-DeltaAmount);
		}
		break;
	case ENPCSimulationStat::Dopamine:
		if (MentalStateComponent)
		{
			if (DeltaAmount > 0.0f) MentalStateComponent->GrantDopamine(DeltaAmount);
			else MentalStateComponent->MentalState.Dopamine = FMath::Max(0.0f, MentalStateComponent->MentalState.Dopamine + DeltaAmount);
		}
		break;
	case ENPCSimulationStat::Stress:
		if (MentalStateComponent)
		{
			if (DeltaAmount > 0.0f) MentalStateComponent->AddStress(DeltaAmount);
			else MentalStateComponent->RelieveStress(-DeltaAmount);
		}
		break;
	case ENPCSimulationStat::AlcoholLevel:
		if (StateComponent) StateComponent->IngestAlcohol(DeltaAmount);
		break;
	case ENPCSimulationStat::DrugLevel:
		if (StateComponent) StateComponent->IngestDrug(DeltaAmount);
		break;
	case ENPCSimulationStat::WalletCash:
		if (PersonalityComponent)
		{
			if (DeltaAmount > 0.0f) PersonalityComponent->AddCash(DeltaAmount);
			else PersonalityComponent->SpendCash(-DeltaAmount);
		}
		break;
	default:
		break;
	}
}

void ANPCCharacter::ApplySimulationStatRate(ENPCSimulationStat Stat, float RatePerSecond, float DeltaSeconds)
{
	ApplySimulationStatDelta(Stat, RatePerSecond * DeltaSeconds);
}

void ANPCCharacter::NotifyEnteredSimulationZone(ANPCSimulationTestZone* Zone)
{
	if (!Zone) return;
	ActiveSimulationZones.AddUnique(Zone);

	if (ANPCAIController* AIC = Cast<ANPCAIController>(GetController()))
	{
		AIC->NotifyEnteredSimulationZone(Zone);
	}
}

void ANPCCharacter::NotifyExitedSimulationZone(ANPCSimulationTestZone* Zone)
{
	if (!Zone) return;
	ActiveSimulationZones.Remove(Zone);

	if (ANPCAIController* AIC = Cast<ANPCAIController>(GetController()))
	{
		AIC->NotifyExitedSimulationZone(Zone);
	}
}

bool ANPCCharacter::IsInPowerRechargeZone() const
{
	for (const auto& ZonePtr : ActiveSimulationZones)
	{
		if (ZonePtr.IsValid() && ZonePtr->HasPositiveStimulusFor(ENPCSimulationStat::Power))
		{
			return true;
		}
	}
	return false;
}

TArray<ANPCSimulationTestZone*> ANPCCharacter::GetActiveSimulationZonesBP() const
{
	TArray<ANPCSimulationTestZone*> Result;
	for (const auto& ZonePtr : ActiveSimulationZones)
	{
		if (ZonePtr.IsValid())
		{
			Result.Add(ZonePtr.Get());
		}
	}
	return Result;
}

bool ANPCCharacter::InteractWithOperableObject(ANPCOperableObject* OperableObject, FName OfferingId)
{
	if (!OperableObject) return false;

	if (!HasAuthority())
	{
		Server_InteractWithOperableObject(OperableObject, OfferingId);
		return true;
	}

	FString FailReason;
	if (OperableObject->StartOperatingOffering(this, OfferingId, FailReason))
	{
		CurrentOperatingObject = OperableObject;
		ReplicatedOperatingObject = OperableObject;
		return true;
	}
	return false;
}

void ANPCCharacter::StopInteractingWithOperableObject()
{
	if (!HasAuthority())
	{
		Server_StopInteractingWithOperableObject();
		return;
	}

	if (CurrentOperatingObject.IsValid())
	{
		CurrentOperatingObject->StopOperatingOffering(this);
		CurrentOperatingObject = nullptr;
		ReplicatedOperatingObject = nullptr;
	}
}

void ANPCCharacter::Server_InteractWithOperableObject_Implementation(ANPCOperableObject* OperableObject, FName OfferingId)
{
	InteractWithOperableObject(OperableObject, OfferingId);
}

bool ANPCCharacter::Server_InteractWithOperableObject_Validate(ANPCOperableObject* OperableObject, FName OfferingId)
{
	return OperableObject != nullptr;
}

void ANPCCharacter::Server_StopInteractingWithOperableObject_Implementation()
{
	StopInteractingWithOperableObject();
}

bool ANPCCharacter::Server_StopInteractingWithOperableObject_Validate()
{
	return true;
}

void ANPCCharacter::AddInventoryItem(FGameplayTag ItemTag, int32 Quantity)
{
	if (!ItemTag.IsValid() || Quantity <= 0) return;
	int32& Count = ItemInventory.FindOrAdd(ItemTag);
	Count += Quantity;

	if (HasAuthority())
	{
		ReplicatedInventory.Empty();
		for (const auto& Pair : ItemInventory)
		{
			ReplicatedInventory.Add(FNPCItemRequirement(Pair.Key, Pair.Value));
		}
	}
}

bool ANPCCharacter::RemoveInventoryItem(FGameplayTag ItemTag, int32 Quantity)
{
	if (!ItemTag.IsValid() || Quantity <= 0) return false;
	int32* Count = ItemInventory.Find(ItemTag);
	if (!Count || *Count < Quantity) return false;

	*Count -= Quantity;
	if (*Count <= 0)
	{
		ItemInventory.Remove(ItemTag);
	}

	if (HasAuthority())
	{
		ReplicatedInventory.Empty();
		for (const auto& Pair : ItemInventory)
		{
			ReplicatedInventory.Add(FNPCItemRequirement(Pair.Key, Pair.Value));
		}
	}
	return true;
}

int32 ANPCCharacter::GetInventoryItemCount(FGameplayTag ItemTag) const
{
	if (!ItemTag.IsValid()) return 0;
	if (const int32* Count = ItemInventory.Find(ItemTag))
	{
		return *Count;
	}
	return 0;
}

bool ANPCCharacter::HasRequiredItems(const TArray<FNPCItemRequirement>& Requirements) const
{
	for (const auto& Req : Requirements)
	{
		if (Req.ItemTag.IsValid() && Req.Quantity > 0)
		{
			if (GetInventoryItemCount(Req.ItemTag) < Req.Quantity)
			{
				return false;
			}
		}
	}
	return true;
}

void ANPCCharacter::StartPhysicsSimulation()


{
	bIsSimulatingPhysics = true;

	// Always halt AI controller movement when collapsing
	if (AController* Ctrl = GetController())
	{
		Ctrl->StopMovement();
	}

	if (RagdollComponent)
	{
		RagdollComponent->StartRagdoll();
	}
	else
	{
		// 1. Immediately halt and disable locomotion
		if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
		{
			MoveComp->StopMovementImmediately();
			MoveComp->DisableMovement();
		}

		// 2. Disable Capsule Component collision so the physics body falls directly to the floor
		if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}

		// 3. Enable physics simulation on Skeletal Mesh (if skeletal character)
		if (USkeletalMeshComponent* SkelMesh = GetMesh())
		{
			SkelMesh->SetCollisionProfileName(TEXT("Ragdoll"));
			SkelMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			SkelMesh->SetSimulatePhysics(true);
			SkelMesh->WakeAllRigidBodies();
		}
	}

	// 4. Enable physics simulation on any StaticMesh components attached (like PSO_BaseMesh)
	TArray<UStaticMeshComponent*> StaticMeshes;
	GetComponents<UStaticMeshComponent>(StaticMeshes);
	for (UStaticMeshComponent* SM : StaticMeshes)
	{
		if (SM)
		{
			SM->SetCollisionProfileName(TEXT("PhysicsActor"));
			SM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			SM->SetSimulatePhysics(true);
			SM->WakeRigidBody();
		}
	}

	OnPhysicsStateChanged.Broadcast(true);
}

void ANPCCharacter::StopPhysicsSimulation()
{
	bIsSimulatingPhysics = false;

	if (RagdollComponent)
	{
		RagdollComponent->StopRagdoll();
	}
	else
	{
		// 1. Find where the physics mesh landed
		FVector LandLocation = GetActorLocation();
		if (USkeletalMeshComponent* SkelMesh = GetMesh())
		{
			LandLocation = SkelMesh->GetComponentLocation();
			SkelMesh->SetSimulatePhysics(false);
			SkelMesh->SetCollisionProfileName(TEXT("CharacterMesh"));
			SkelMesh->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			SkelMesh->SetRelativeLocationAndRotation(MeshInitialRelativeLocation, MeshInitialRelativeRotation);
		}

		// 2. Move root capsule to ground contact point
		TeleportTo(LandLocation + FVector(0.0f, 0.0f, 60.0f), GetActorRotation());
	}

	// Always guarantee capsule collision is restored to Pawn profile
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Capsule->UpdateComponentToWorld();
	}

	// Always guarantee walking locomotion is restored
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->SetMovementMode(MOVE_Walking);
		MoveComp->Velocity = FVector::ZeroVector;
		MoveComp->ClearAccumulatedForces();
		MoveComp->UpdateComponentVelocity();
	}

	// Restore StaticMesh components
	TArray<UStaticMeshComponent*> StaticMeshes;
	GetComponents<UStaticMeshComponent>(StaticMeshes);
	for (UStaticMeshComponent* SM : StaticMeshes)
	{
		if (SM)
		{
			SM->SetSimulatePhysics(false);
			SM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
	}

	OnPhysicsStateChanged.Broadcast(false);
}

void ANPCCharacter::StartRagdoll()
{
	StartPhysicsSimulation();
}

void ANPCCharacter::StopRagdoll()
{
	StopPhysicsSimulation();
}

bool ANPCCharacter::IsSimulatingPhysics() const
{
	if (RagdollComponent)
	{
		return RagdollComponent->IsRagdollActive();
	}
	return bIsSimulatingPhysics;
}

bool ANPCCharacter::IsRagdollActive() const
{
	return IsSimulatingPhysics();
}

void ANPCCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ANPCCharacter, bIsSimulatingPhysics);
	DOREPLIFETIME(ANPCCharacter, bIsRestingInSimulationZone);
	DOREPLIFETIME(ANPCCharacter, ReplicatedOperatingObject);
	DOREPLIFETIME(ANPCCharacter, ReplicatedInventory);
}

void ANPCCharacter::OnRep_IsRestingInSimulationZone()
{
	DrawDebugOverheadStats();
}

void ANPCCharacter::OnRep_OperatingObject()
{
	CurrentOperatingObject = ReplicatedOperatingObject;
	DrawDebugOverheadStats();
}

void ANPCCharacter::OnRep_ReplicatedInventory()
{
	ItemInventory.Empty();
	for (const auto& Req : ReplicatedInventory)
	{
		ItemInventory.Add(Req.ItemTag, Req.Quantity);
	}
}

void ANPCCharacter::OnRep_IsSimulatingPhysics()
{
	if (bIsSimulatingPhysics)
	{
		if (RagdollComponent)
		{
			RagdollComponent->StartRagdoll();
		}
		else
		{
			// Client-side visual ragdoll
			if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
			{
				MoveComp->StopMovementImmediately();
				MoveComp->DisableMovement();
			}
			if (UCapsuleComponent* Capsule = GetCapsuleComponent())
			{
				Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			if (USkeletalMeshComponent* SkelMesh = GetMesh())
			{
				SkelMesh->SetCollisionProfileName(TEXT("Ragdoll"));
				SkelMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				SkelMesh->SetSimulatePhysics(true);
				SkelMesh->WakeAllRigidBodies();
			}
		}

		TArray<UStaticMeshComponent*> StaticMeshes;
		GetComponents<UStaticMeshComponent>(StaticMeshes);
		for (UStaticMeshComponent* SM : StaticMeshes)
		{
			if (SM)
			{
				SM->SetCollisionProfileName(TEXT("PhysicsActor"));
				SM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				SM->SetSimulatePhysics(true);
				SM->WakeRigidBody();
			}
		}

		OnPhysicsStateChanged.Broadcast(true);
	}
	else
	{
		if (RagdollComponent)
		{
			RagdollComponent->StopRagdoll();
		}
		else
		{
			if (USkeletalMeshComponent* SkelMesh = GetMesh())
			{
				SkelMesh->SetSimulatePhysics(false);
				SkelMesh->SetCollisionProfileName(TEXT("CharacterMesh"));
				SkelMesh->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
				SkelMesh->SetRelativeLocationAndRotation(MeshInitialRelativeLocation, MeshInitialRelativeRotation);
			}
			if (UCapsuleComponent* Capsule = GetCapsuleComponent())
			{
				Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}
			if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
			{
				MoveComp->SetMovementMode(MOVE_Walking);
			}
		}

		TArray<UStaticMeshComponent*> StaticMeshes;
		GetComponents<UStaticMeshComponent>(StaticMeshes);
		for (UStaticMeshComponent* SM : StaticMeshes)
		{
			if (SM)
			{
				SM->SetSimulatePhysics(false);
				SM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}
		}

		OnPhysicsStateChanged.Broadcast(false);
	}
}


