#include "Props/InteractableProp.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractablePropManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Characters/NPCCharacter.h"
#include "Components/NPCPersonalityComponent.h"
#include "Components/NPCNeedsComponent.h"
#include "Components/NPCMentalStateComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

AInteractableProp::AInteractableProp()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;
	bReplicates = true;
	SetReplicateMovement(true);

	PropMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PropMesh"));
	SetRootComponent(PropMesh);
	PropMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));

	DebugText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DebugText"));
	DebugText->SetupAttachment(PropMesh);
	DebugText->SetRelativeLocation(FVector(0.0f, 0.0f, 35.0f));
	DebugText->SetHorizontalAlignment(EHTA_Center);
	DebugText->SetWorldSize(14.0f);
	DebugText->SetTextRenderColor(FColor::Cyan);

	PhysicsSimComponent = CreateDefaultSubobject<UStaticMeshPhysicsSimulationComponent>(TEXT("PhysicsSimComponent"));
	PhysicsSimComponent->TargetMesh = PropMesh;
	PhysicsSimComponent->InitialState = EPhysicsSimulationState::AtRest;

	// Default temperature values
	bEnableTemperature = false;
	ConsumableTemperature = 4.0f;
	CurrentTemperature = 4.0f;
	TemperatureTolerance = 5.0f;
	TemperatureDopamineBonus = 10.0f;
	BodyTemperatureEffect = -0.5f;
	ThermalExchangeRate = 0.015f;
	AmbientRoomTemperature = 21.0f;

	// Default expiry values
	bHasExpiry = true;
	ExpiryLifetimeMinutes = 60.0f;
	AgeMinutes = 0.0f;
	SicknessLevel = 35.0f;
	ExpiredDopaminePenalty = 25.0f;

	// Default taste profile (Cold sweet beverage)
	TasteProfile.Sweet = 0.85f;
	TasteProfile.Healthy = 0.3f;
	TasteProfile.Luxury = 0.2f;

	InitializeDefaultDrinkStimuli();
}

void AInteractableProp::InitializeDefaultDrinkStimuli()
{
	StimuliPerPortion.Empty();
	// Default drink values per sip/portion:
	// Power +10, Hunger -5 (less hungry), Thirst -25 (quenched), Dopamine +5
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Power, 10.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Hunger, -5.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Thirst, -25.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Dopamine, 5.0f));
}

void AInteractableProp::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (PropDataAsset)
	{
		ApplyDataAsset(PropDataAsset);
	}
	else
	{
		UpdateDebugBillboard();
	}
}

void AInteractableProp::ApplyDataAsset(const UInteractablePropDataAsset* InDataAsset)
{
	if (!InDataAsset) return;

	// 1. Static Mesh Component
	if (PropMesh && InDataAsset->StaticMesh)
	{
		PropMesh->SetStaticMesh(InDataAsset->StaticMesh);
	}

	// 2. Identity & Classification
	ItemDisplayName = InDataAsset->ItemDisplayName;
	ConsumableType = InDataAsset->ConsumableType;
	Price = InDataAsset->Price;
	EnergyLevel = InDataAsset->EnergyLevel;

	// 3. Portions & Quantity
	MaxPortions = InDataAsset->MaxPortions;
	RemainingPortions = MaxPortions;
	QuantityLevel = 1.0f;

	// 4. Temperature Configuration
	bEnableTemperature = InDataAsset->bEnableTemperature;
	ConsumableTemperature = InDataAsset->ConsumableTemperature;
	CurrentTemperature = InDataAsset->ConsumableTemperature;
	TemperatureTolerance = InDataAsset->TemperatureTolerance;
	TemperatureDopamineBonus = InDataAsset->TemperatureDopamineBonus;
	BodyTemperatureEffect = InDataAsset->BodyTemperatureEffect;
	ThermalExchangeRate = InDataAsset->ThermalExchangeRate;
	Coldness = (ConsumableTemperature < 15.0f) ? 1.0f : 0.0f;

	// 5. Expiry Configuration
	bHasExpiry = InDataAsset->bHasExpiry;
	ExpiryLifetimeMinutes = InDataAsset->ExpiryLifetimeMinutes;
	AgeMinutes = 0.0f;
	SicknessLevel = InDataAsset->SicknessLevel;
	ExpiredDopaminePenalty = InDataAsset->ExpiredDopaminePenalty;
	ExpiryFreshness = 1.0f;

	// 6. Stimuli & Taste Profile
	StimuliPerPortion = InDataAsset->StimuliPerPortion;
	TasteProfile = InDataAsset->BuildTasteVector(false);

	UpdateDebugBillboard();
}

void AInteractableProp::BeginPlay()
{
	Super::BeginPlay();

	if (PropDataAsset)
	{
		ApplyDataAsset(PropDataAsset);
	}

	// Register with central Prop Manager for distributed round-robin temperature ticks
	if (HasAuthority() && bEnableTemperature)
	{
		if (AInteractablePropManager* Manager = AInteractablePropManager::Get(this))
		{
			Manager->RegisterProp(this);
		}
	}

	UpdateDebugBillboard();
}

void AInteractableProp::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AInteractablePropManager* Manager = AInteractablePropManager::Get(this))
	{
		Manager->UnregisterProp(this);
	}

	Super::EndPlay(EndPlayReason);
}

void AInteractableProp::UpdateTemperature(float AmbientTemp, float DeltaSeconds)
{
	if (!bEnableTemperature) return;

	if (ThermalExchangeRate > 0.0f && FMath::Abs(CurrentTemperature - AmbientTemp) > 0.05f)
	{
		const float TempDelta = (AmbientTemp - CurrentTemperature) * ThermalExchangeRate * DeltaSeconds;
		CurrentTemperature += TempDelta;
	}

	if (bHasExpiry)
	{
		AgeMinutes += (DeltaSeconds / 60.0f);
		if (ExpiryLifetimeMinutes > 0.0f)
		{
			ExpiryFreshness = FMath::Clamp(1.0f - (AgeMinutes / ExpiryLifetimeMinutes), 0.0f, 1.0f);
		}
	}

	if (CurrentTemperature < AmbientTemp)
	{
		Coldness = FMath::Clamp((AmbientTemp - CurrentTemperature) / 17.0f, 0.0f, 1.0f);
	}
	else
	{
		Coldness = 0.0f;
	}

	UpdateDebugBillboard();
}

void AInteractableProp::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// If bEnableTemperature is handled by central Prop Manager, we avoid per-actor thermal tick overhead
	if (!bEnableTemperature && HasAuthority())
	{
		// Non-temperature items only age if they have expiration
		if (bHasExpiry)
		{
			AgeMinutes += (DeltaSeconds / 60.0f);
			if (ExpiryLifetimeMinutes > 0.0f)
			{
				ExpiryFreshness = FMath::Clamp(1.0f - (AgeMinutes / ExpiryLifetimeMinutes), 0.0f, 1.0f);
			}
		}
	}

	UpdateDebugBillboard();
}

void AInteractableProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AInteractableProp, bIsGrabbable);
	DOREPLIFETIME(AInteractableProp, QuantityLevel);
	DOREPLIFETIME(AInteractableProp, RemainingPortions);
	DOREPLIFETIME(AInteractableProp, Coldness);
	DOREPLIFETIME(AInteractableProp, ExpiryFreshness);
	DOREPLIFETIME(AInteractableProp, Price);
	DOREPLIFETIME(AInteractableProp, bEnableTemperature);
	DOREPLIFETIME(AInteractableProp, ConsumableTemperature);
	DOREPLIFETIME(AInteractableProp, CurrentTemperature);
	DOREPLIFETIME(AInteractableProp, bHasExpiry);
	DOREPLIFETIME(AInteractableProp, AgeMinutes);
}

void AInteractableProp::OnRep_IsGrabbable()
{
	UpdateDebugBillboard();
}

void AInteractableProp::SetGrabbable(bool bNewGrabbable)
{
	bIsGrabbable = bNewGrabbable;
	UpdateDebugBillboard();
}

void AInteractableProp::RetriggerUnsettled(float NewDelay)
{
	if (PhysicsSimComponent)
	{
		PhysicsSimComponent->RetriggerUnsettledTimer(NewDelay);
	}
}

// -------------------------------------------------------------
// Player Interaction Pathway
// -------------------------------------------------------------
bool AInteractableProp::InteractWithPlayer_Implementation(APawn* PlayerPawn)
{
	if (!PlayerPawn) return false;

	if (bIsGrabbable)
	{
		USceneComponent* AttachComp = PlayerPawn->GetRootComponent();
		return OnPlayerGrab(AttachComp, NAME_None);
	}
	else if (!IsEmpty())
	{
		return ConsumePortion(PlayerPawn);
	}

	return false;
}

bool AInteractableProp::OnPlayerGrab_Implementation(USceneComponent* AttachToParent, FName SocketName)
{
	if (!AttachToParent) return false;

	if (PhysicsSimComponent)
	{
		PhysicsSimComponent->SetStateHeld();
	}

	AttachToComponent(AttachToParent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
	return true;
}

bool AInteractableProp::OnPlayerRelease_Implementation(FVector LaunchVelocity)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	if (LaunchVelocity.SizeSquared() > 100.0f)
	{
		if (PhysicsSimComponent)
		{
			PhysicsSimComponent->SetStateInMotion();
		}
		if (PropMesh)
		{
			PropMesh->AddImpulse(LaunchVelocity, NAME_None, true);
		}
	}
	else
	{
		if (PhysicsSimComponent)
		{
			// Enter unsettled with default 3.0s delay, then settles
			PhysicsSimComponent->SetStateUnsettled(-1.0f);
		}
	}

	return true;
}

// -------------------------------------------------------------
// NPC Interaction Pathway
// -------------------------------------------------------------
bool AInteractableProp::InteractWithNPC_Implementation(ANPCCharacter* NPCCharacter, FName ActionName)
{
	if (!NPCCharacter) return false;

	if (ActionName == FName("Grab") || ActionName == FName("PickUp"))
	{
		USceneComponent* AttachComp = NPCCharacter->GetMesh();
		return OnPlayerGrab(AttachComp, FName("hand_rSocket"));
	}

	return ConsumePortion(NPCCharacter);
}

// -------------------------------------------------------------
// Consumable Logic
// -------------------------------------------------------------
void AInteractableProp::OnConsumedByPlayer_Implementation(APawn* PlayerPawn, float BodyTempDelta, float ItemTemp, bool bWasExpired)
{
	UE_LOG(LogTemp, Log, TEXT("[%s] Consumed by Player [%s]: BodyTempDelta=%.2f, ItemTemp=%.1fC, Expired=%s"),
		*ItemDisplayName.ToString(),
		PlayerPawn ? *PlayerPawn->GetName() : TEXT("None"),
		BodyTempDelta,
		ItemTemp,
		bWasExpired ? TEXT("TRUE") : TEXT("FALSE"));
}

bool AInteractableProp::ConsumePortion(AActor* ConsumerActor, float PortionRatio)
{
	if (IsEmpty()) return false;
	if (!ConsumerActor) return false;

	if (!HasAuthority())
	{
		Server_ConsumePortion(ConsumerActor, PortionRatio);
		return true;
	}

	const float Ratio = FMath::Clamp(PortionRatio, 0.1f, 1.0f);
	const bool bItemExpired = IsExpired() || IsSpoiled();

	ANPCCharacter* NPC = Cast<ANPCCharacter>(ConsumerActor);
	if (NPC)
	{
		// 1. Check expired consumption
		if (bItemExpired)
		{
			// Expired item: add sickness level, subtract dopamine (depression), add stress
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::Sickness, SicknessLevel * Ratio);
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::Dopamine, -ExpiredDopaminePenalty * Ratio);
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::Stress, 25.0f * Ratio);

			// Taste changes to expired profile
			if (PropDataAsset)
			{
				TasteProfile = PropDataAsset->BuildTasteVector(true);
			}

			UE_LOG(LogTemp, Warning, TEXT("[%s] Consumed EXPIRED prop [%s]! Sickness +%.1f, Dopamine -%.1f (depressing), Stress +25.0"),
				*NPC->GetName(), *ItemDisplayName.ToString(), SicknessLevel * Ratio, ExpiredDopaminePenalty * Ratio);
		}
		else
		{
			// 2. Temperature Closeness Check: If current temperature is close to ideal ConsumableTemperature, grant dopamine bonus!
			const float TempDiff = FMath::Abs(CurrentTemperature - ConsumableTemperature);
			if (TempDiff <= TemperatureTolerance)
			{
				const float Closeness = 1.0f - (TempDiff / FMath::Max(0.1f, TemperatureTolerance));
				const float Bonus = TemperatureDopamineBonus * Closeness * Ratio;
				NPC->ApplySimulationStatDelta(ENPCSimulationStat::Dopamine, Bonus);
				UE_LOG(LogTemp, Log, TEXT("[%s] Consumed [%s] at optimal temp (%.1fC vs ideal %.1fC)! Dopamine bonus: +%.1f"),
					*NPC->GetName(), *ItemDisplayName.ToString(), CurrentTemperature, ConsumableTemperature, Bonus);
			}

			// 3. Primary Stimuli Modifiers (Index stats are chooseable)
			for (const auto& Mod : StimuliPerPortion)
			{
				NPC->ApplySimulationStatDelta(Mod.Stat, Mod.ChangeRate * Ratio);
			}

			// 4. Energy level boosting Power
			if (EnergyLevel > 0.0f)
			{
				NPC->ApplySimulationStatDelta(ENPCSimulationStat::Power, EnergyLevel * Ratio);
			}

			// 5. Taste profile evaluation against NPC personality
			if (NPC->PersonalityComponent)
			{
				const float Compatibility = NPC->PersonalityComponent->EvaluateTaste(TasteProfile);
				if (Compatibility >= 0.65f)
				{
					NPC->ApplySimulationStatDelta(ENPCSimulationStat::Dopamine, 10.0f * Compatibility * Ratio);
				}
				else if (Compatibility <= 0.30f)
				{
					NPC->ApplySimulationStatDelta(ENPCSimulationStat::Stress, 8.0f * (1.0f - Compatibility) * Ratio);
				}
			}
		}

		// 6. Body temperature effect on NPC
		if (BodyTemperatureEffect != 0.0f)
		{
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::BodyTemperature, BodyTemperatureEffect * Ratio);
		}
	}
	else if (APawn* PlayerPawn = Cast<APawn>(ConsumerActor))
	{
		// Consumed by Player: fire event so Player Blueprints / Character can handle body temperature, sickness, etc.
		OnConsumedByPlayer(PlayerPawn, BodyTemperatureEffect * Ratio, CurrentTemperature, bItemExpired);
	}

	// 7. Decrement portion count
	RemainingPortions = FMath::Max(0, RemainingPortions - 1);
	QuantityLevel = MaxPortions > 0 ? static_cast<float>(RemainingPortions) / static_cast<float>(MaxPortions) : 0.0f;

	Multicast_OnPortionConsumed(ConsumerActor, RemainingPortions);
	UpdateDebugBillboard();

	if (RemainingPortions <= 0 && bDestroyWhenEmpty)
	{
		SetLifeSpan(1.0f);
	}

	return true;
}

void AInteractableProp::Server_ConsumePortion_Implementation(AActor* ConsumerActor, float PortionRatio)
{
	ConsumePortion(ConsumerActor, PortionRatio);
}

bool AInteractableProp::Server_ConsumePortion_Validate(AActor* ConsumerActor, float PortionRatio)
{
	return true;
}

void AInteractableProp::Multicast_OnPortionConsumed_Implementation(AActor* ConsumerActor, int32 PortionsLeft)
{
	UpdateDebugBillboard();
}

void AInteractableProp::OnRep_QuantityLevel()
{
	UpdateDebugBillboard();
}

void AInteractableProp::OnRep_RemainingPortions()
{
	UpdateDebugBillboard();
}

// -------------------------------------------------------------
// IWorldAffordanceInterface
// -------------------------------------------------------------
bool AInteractableProp::CanInteract_Implementation(AActor* InstigatorActor)
{
	if (!InstigatorActor) return false;

	const bool bIsPlayer = InstigatorActor->IsA<APawn>() && !InstigatorActor->IsA<ANPCCharacter>();
	if (bIsPlayer)
	{
		return bIsGrabbable || !IsEmpty();
	}

	return !IsEmpty();
}

void AInteractableProp::QueryAffordanceOptions_Implementation(AActor* InstigatorActor, TArray<FAffordanceOption>& OutOptions)
{
	OutOptions.Empty();
	if (!InstigatorActor) return;

	const bool bIsPlayer = InstigatorActor->IsA<APawn>() && !InstigatorActor->IsA<ANPCCharacter>();

	if (bIsPlayer)
	{
		if (bIsGrabbable)
		{
			FAffordanceOption GrabOpt;
			GrabOpt.OptionId = FName("Grab");
			GrabOpt.DisplayLabel = FText::FromString(TEXT("Pick Up / Grab"));
			GrabOpt.MoneyCost = 0.0f;
			GrabOpt.ExecutionTime = 0.2f;
			OutOptions.Add(GrabOpt);
		}

		if (!IsEmpty())
		{
			FAffordanceOption UseOpt;
			UseOpt.OptionId = FName("ConsumePortion");
			UseOpt.DisplayLabel = FText::FromString(ConsumableType == EConsumablePropType::Drink ? TEXT("Take a Sip") : TEXT("Take a Bite"));
			UseOpt.MoneyCost = Price;
			UseOpt.OfferedStimuli = StimuliPerPortion;
			UseOpt.ExecutionTime = 1.0f;
			OutOptions.Add(UseOpt);
		}
	}
	else
	{
		if (!IsEmpty())
		{
			FAffordanceOption DrinkOpt;
			DrinkOpt.OptionId = FName("ConsumePortion");
			DrinkOpt.DisplayLabel = FText::FromString(ConsumableType == EConsumablePropType::Drink ? TEXT("Drink") : TEXT("Eat"));
			DrinkOpt.MoneyCost = Price;
			DrinkOpt.OfferedStimuli = StimuliPerPortion;
			DrinkOpt.ExecutionTime = 2.0f;
			OutOptions.Add(DrinkOpt);
		}

		if (bIsGrabbable)
		{
			FAffordanceOption PickOpt;
			PickOpt.OptionId = FName("PickUp");
			PickOpt.DisplayLabel = FText::FromString(TEXT("Pick Up Item"));
			PickOpt.MoneyCost = Price;
			PickOpt.ExecutionTime = 1.0f;
			OutOptions.Add(PickOpt);
		}
	}
}

bool AInteractableProp::ExecuteAffordanceOption_Implementation(FName OptionId, AActor* InstigatorActor)
{
	if (!InstigatorActor) return false;

	if (OptionId == FName("Grab") || OptionId == FName("PickUp"))
	{
		if (APawn* Pawn = Cast<APawn>(InstigatorActor))
		{
			return OnPlayerGrab(Pawn->GetRootComponent(), NAME_None);
		}
	}
	else if (OptionId == FName("ConsumePortion"))
	{
		return ConsumePortion(InstigatorActor);
	}

	return false;
}

void AInteractableProp::UpdateDebugBillboard()
{
	if (!DebugText) return;

	const FString StateName = PhysicsSimComponent ? *UEnum::GetValueAsString(PhysicsSimComponent->CurrentState) : TEXT("None");
	const FString ExpiredText = IsExpired() ? TEXT("EXPIRED!") : FString::Printf(TEXT("Age:%.1fm/%.1fm"), AgeMinutes, ExpiryLifetimeMinutes);
	const FString TempText = FString::Printf(TEXT("Temp:%.1fC (Ideal:%.1fC)"), CurrentTemperature, ConsumableTemperature);
	const FString GrabText = bIsGrabbable ? TEXT("GRABBABLE") : TEXT("LOCKED");

	DebugText->SetText(FText::FromString(FString::Printf(
		TEXT("%s\n[$%.2f | %s]\n[%s]\n[Portions: %d/%d (%.0f%%)]\n[Phys: %s | %s]"),
		*ItemDisplayName.ToString(),
		Price,
		*TempText,
		*ExpiredText,
		RemainingPortions,
		MaxPortions,
		QuantityLevel * 100.0f,
		*StateName,
		*GrabText
	)));

	if (IsExpired())
	{
		DebugText->SetTextRenderColor(FColor::Red);
	}
	else if (IsAtIdealTemperature())
	{
		DebugText->SetTextRenderColor(FColor::Green);
	}
	else if (CurrentTemperature < 15.0f)
	{
		DebugText->SetTextRenderColor(FColor::Cyan);
	}
	else
	{
		DebugText->SetTextRenderColor(FColor::Orange);
	}
}
