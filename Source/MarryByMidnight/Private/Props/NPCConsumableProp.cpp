#include "Props/NPCConsumableProp.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Characters/NPCCharacter.h"
#include "Components/NPCPersonalityComponent.h"
#include "Components/NPCNeedsComponent.h"
#include "Components/NPCMentalStateComponent.h"
#include "Net/UnrealNetwork.h"

ANPCConsumableProp::ANPCConsumableProp()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f; // Update every 0.5s for efficiency
	bReplicates = true;
	SetReplicateMovement(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PropMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PropMesh"));
	PropMesh->SetupAttachment(SceneRoot);
	PropMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));

	DebugText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DebugText"));
	DebugText->SetupAttachment(SceneRoot);
	DebugText->SetRelativeLocation(FVector(0.0f, 0.0f, 35.0f));
	DebugText->SetHorizontalAlignment(EHTA_Center);
	DebugText->SetWorldSize(14.0f);
	DebugText->SetTextRenderColor(FColor::Cyan);

	// Default taste profile (Cold sweet beverage)
	TasteProfile.Sweet = 0.85f;
	TasteProfile.Healthy = 0.3f;
	TasteProfile.Luxury = 0.2f;

	InitializeDefaultDrinkStimuli();
}

void ANPCConsumableProp::InitializeDefaultDrinkStimuli()
{
	StimuliPerPortion.Empty();
	// Default drink values per sip/portion as requested:
	// Power +10, Hunger -5 (less hungry), Thirst -25 (quenched), Dopamine +5
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Power, 10.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Hunger, -5.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Thirst, -25.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Dopamine, 5.0f));
	StimuliPerPortion.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Bladder, 15.0f)); // Drinking fills bladder
}

void ANPCConsumableProp::BeginPlay()
{
	Super::BeginPlay();
	UpdateDebugBillboard();
}

void ANPCConsumableProp::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Gradual freshness decay
	if (ExpiryRatePerMinute > 0.0f && ExpiryFreshness > 0.0f)
	{
		ExpiryFreshness = FMath::Clamp(ExpiryFreshness - (ExpiryRatePerMinute / 60.0f) * DeltaSeconds, 0.0f, 1.0f);
	}

	// Cold items slowly warm up to room temperature over ~10 minutes
	if (Coldness > 0.0f)
	{
		Coldness = FMath::Clamp(Coldness - (0.1f / 60.0f) * DeltaSeconds, 0.0f, 1.0f);
	}

	UpdateDebugBillboard();
}

bool ANPCConsumableProp::ConsumePortion(AActor* ConsumerActor, float PortionRatio)
{
	if (!ConsumerActor || IsEmpty())
	{
		return false;
	}

	if (!HasAuthority())
	{
		Server_ConsumePortion(ConsumerActor, PortionRatio);
		return true;
	}

	ANPCCharacter* NPC = Cast<ANPCCharacter>(ConsumerActor);
	if (!NPC)
	{
		return false;
	}

	const float Ratio = FMath::Clamp(PortionRatio, 0.1f, 1.0f);

	// 1. Apply configured stimuli deltas
	for (const auto& Mod : StimuliPerPortion)
	{
		float DeltaAmount = Mod.ChangeRate * Ratio;
		if (Mod.Stat == ENPCSimulationStat::Power && EnergyLevel > 0.0f)
		{
			DeltaAmount = EnergyLevel * Ratio;
		}
		NPC->ApplySimulationStatDelta(Mod.Stat, DeltaAmount);
	}

	// 2. Check Expiry / Spoiled Penalty
	if (IsSpoiled())
	{
		// Consuming spoiled food/drink causes food poisoning
		NPC->ApplySimulationStatDelta(ENPCSimulationStat::Sickness, 25.0f * Ratio);
		NPC->ApplySimulationStatDelta(ENPCSimulationStat::Stress, 15.0f * Ratio);
		NPC->ApplySimulationStatDelta(ENPCSimulationStat::Dopamine, -20.0f * Ratio);
	}
	else
	{
		// 3. Coldness satisfaction bonus for chilled drinks
		if (Coldness >= 0.5f && ConsumableType == EConsumablePropType::Drink)
		{
			const float ChilledBonus = 6.0f * Coldness * Ratio;
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::Dopamine, ChilledBonus);
			// Slightly reduce body temperature
			NPC->ApplySimulationStatDelta(ENPCSimulationStat::BodyTemperature, -0.3f * Coldness * Ratio);
		}

		// 4. Taste compatibility check against NPC personality
		if (NPC->PersonalityComponent)
		{
			const float Compatibility = NPC->PersonalityComponent->EvaluateTaste(TasteProfile);
			if (Compatibility >= 0.65f)
			{
				// Loved the taste!
				NPC->ApplySimulationStatDelta(ENPCSimulationStat::Dopamine, 10.0f * Compatibility * Ratio);
			}
			else if (Compatibility <= 0.30f)
			{
				// Disliked the taste!
				NPC->ApplySimulationStatDelta(ENPCSimulationStat::Stress, 8.0f * (1.0f - Compatibility) * Ratio);
			}
		}
	}

	// 5. Decrement portion count
	RemainingPortions = FMath::Max(0, RemainingPortions - 1);
	QuantityLevel = (float)RemainingPortions / (float)FMath::Max(1, MaxPortions);

	Multicast_OnPortionConsumed(ConsumerActor, RemainingPortions);
	UpdateDebugBillboard();

	// 6. Dispose when empty
	if (IsEmpty() && bDestroyWhenEmpty)
	{
		Destroy();
	}

	return true;
}

void ANPCConsumableProp::Server_ConsumePortion_Implementation(AActor* ConsumerActor, float PortionRatio)
{
	ConsumePortion(ConsumerActor, PortionRatio);
}

bool ANPCConsumableProp::Server_ConsumePortion_Validate(AActor* ConsumerActor, float PortionRatio)
{
	return ConsumerActor != nullptr;
}

void ANPCConsumableProp::Multicast_OnPortionConsumed_Implementation(AActor* ConsumerActor, int32 PortionsLeft)
{
	UpdateDebugBillboard();
}

void ANPCConsumableProp::OnRep_QuantityLevel()
{
	UpdateDebugBillboard();
}

void ANPCConsumableProp::OnRep_RemainingPortions()
{
	UpdateDebugBillboard();
}

void ANPCConsumableProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ANPCConsumableProp, QuantityLevel);
	DOREPLIFETIME(ANPCConsumableProp, RemainingPortions);
	DOREPLIFETIME(ANPCConsumableProp, Coldness);
	DOREPLIFETIME(ANPCConsumableProp, ExpiryFreshness);
	DOREPLIFETIME(ANPCConsumableProp, Price);
}

bool ANPCConsumableProp::CanInteract_Implementation(AActor* InstigatorActor)
{
	return !IsEmpty();
}

void ANPCConsumableProp::QueryAffordanceOptions_Implementation(AActor* InstigatorActor, TArray<FAffordanceOption>& OutOptions)
{
	if (IsEmpty()) return;

	// Primary Consumption Option (Drink vs Eat)
	FAffordanceOption ConsumeOption;
	ConsumeOption.OptionId = FName("ConsumePortion");
	ConsumeOption.bIsAllowed = true;
	ConsumeOption.ExecutionTime = 1.5f;
	ConsumeOption.MoneyCost = 0.0f; // Already owned / in hand
	ConsumeOption.OfferedStimuli = StimuliPerPortion;
	ConsumeOption.bIsOverTime = false;

	if (ConsumableType == EConsumablePropType::Drink || ConsumableType == EConsumablePropType::Alcohol)
	{
		ConsumeOption.ActionType = EAffordanceAction::Drink;
		ConsumeOption.DisplayLabel = FText::Format(
			FText::FromString(TEXT("Drink {0} ({1}/{2} sips left)")),
			ItemDisplayName,
			FText::AsNumber(RemainingPortions),
			FText::AsNumber(MaxPortions)
		);
	}
	else
	{
		ConsumeOption.ActionType = EAffordanceAction::Eat;
		ConsumeOption.DisplayLabel = FText::Format(
			FText::FromString(TEXT("Eat {0} ({1}/{2} bites left)")),
			ItemDisplayName,
			FText::AsNumber(RemainingPortions),
			FText::AsNumber(MaxPortions)
		);
	}
	OutOptions.Add(ConsumeOption);

	// Pick Up Option
	FAffordanceOption TakeOption;
	TakeOption.OptionId = FName("TakeProp");
	TakeOption.DisplayLabel = FText::Format(FText::FromString(TEXT("Take {0}")), ItemDisplayName);
	TakeOption.ActionType = EAffordanceAction::Take;
	TakeOption.bIsAllowed = true;
	TakeOption.ExecutionTime = 0.5f;
	OutOptions.Add(TakeOption);

	// Inspect Option
	FAffordanceOption InspectOption;
	InspectOption.OptionId = FName("InspectProp");
	InspectOption.DisplayLabel = FText::Format(
		FText::FromString(TEXT("Inspect: Cold={0}% Fresh={1}% Energy={2}")),
		FText::AsNumber(FMath::RoundToInt(Coldness * 100.0f)),
		FText::AsNumber(FMath::RoundToInt(ExpiryFreshness * 100.0f)),
		FText::AsNumber(FMath::RoundToInt(EnergyLevel))
	);
	InspectOption.ActionType = EAffordanceAction::Inspect;
	InspectOption.bIsAllowed = true;
	InspectOption.ExecutionTime = 0.2f;
	OutOptions.Add(InspectOption);
}

bool ANPCConsumableProp::ExecuteAffordanceOption_Implementation(FName OptionId, AActor* InstigatorActor)
{
	if (OptionId == FName("ConsumePortion"))
	{
		return ConsumePortion(InstigatorActor);
	}
	else if (OptionId == FName("TakeProp"))
	{
		if (InstigatorActor)
		{
			AttachToActor(InstigatorActor, FAttachmentTransformRules::KeepWorldTransform);
			PropMesh->SetSimulatePhysics(false);
			PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			return true;
		}
	}
	else if (OptionId == FName("InspectProp"))
	{
		return true;
	}

	return false;
}

void ANPCConsumableProp::UpdateDebugBillboard()
{
	if (!DebugText) return;

	if (IsEmpty())
	{
		DebugText->SetText(FText::FromString(TEXT("[EMPTY CAN]")));
		DebugText->SetTextRenderColor(FColor::Silver);
		return;
	}

	const FString FreshnessStr = IsSpoiled() ? TEXT("SPOILED!") : FString::Printf(TEXT("%d%%"), FMath::RoundToInt(ExpiryFreshness * 100.0f));
	const FString ColdStr = FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Coldness * 100.0f));

	const FString Formatted = FString::Printf(
		TEXT("%s\n$%0.2f | Cold: %s | Fresh: %s\nPortions: %d/%d | Energy: %0.0f"),
		*ItemDisplayName.ToString(),
		Price,
		*ColdStr,
		*FreshnessStr,
		RemainingPortions,
		MaxPortions,
		EnergyLevel
	);

	DebugText->SetText(FText::FromString(Formatted));

	if (IsSpoiled())
	{
		DebugText->SetTextRenderColor(FColor::Red);
	}
	else if (Coldness >= 0.7f)
	{
		DebugText->SetTextRenderColor(FColor::Cyan);
	}
	else
	{
		DebugText->SetTextRenderColor(FColor::Green);
	}
}
