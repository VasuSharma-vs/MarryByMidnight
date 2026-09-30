#include "Components/NPCPersonalityComponent.h"
#include "Components/NPCMentalStateComponent.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Engine/World.h"

UNPCPersonalityComponent::UNPCPersonalityComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	ApplyArchetypeDefaults(Archetype);
}

void UNPCPersonalityComponent::BeginPlay()
{
	Super::BeginPlay();
	ApplyArchetypeDefaults(Archetype);

	UWorld* World = GetWorld();
	if (World && GetOwner() && GetOwner()->HasAuthority() && IncomeRatePerMinute > 0.0f)
	{
		// Accrue passive income every 5.0 seconds
		World->GetTimerManager().SetTimer(IncomeTimerHandle, this, &UNPCPersonalityComponent::AccruePassiveIncome, 5.0f, true);
	}
}

void UNPCPersonalityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(IncomeTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UNPCPersonalityComponent::AccruePassiveIncome()
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	// (IncomeRatePerMinute / 60) * 5 seconds
	const float Accrued = (IncomeRatePerMinute / 60.0f) * 5.0f;
	WalletCash += Accrued;
}

void UNPCPersonalityComponent::AddCash(float Amount)
{
	if (Amount > 0.0f)
	{
		WalletCash += Amount;
	}
}

bool UNPCPersonalityComponent::SpendCash(float Amount)
{
	if (WalletCash >= Amount)
	{
		WalletCash -= Amount;
		return true;
	}
	return false;
}

void UNPCPersonalityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UNPCPersonalityComponent, Archetype);
	DOREPLIFETIME(UNPCPersonalityComponent, WalletCash);
}

#if WITH_EDITOR
void UNPCPersonalityComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNPCPersonalityComponent, Archetype))
	{
		ApplyArchetypeDefaults(Archetype);
	}
}
#endif

void UNPCPersonalityComponent::ApplyArchetypeDefaults(ENPCArchetype InArchetype)
{
	Archetype = InArchetype;

	switch (InArchetype)
	{
	case ENPCArchetype::Business:
		Personality.Patience = 0.20f;
		Personality.PriceSensitivity = 0.05f;
		Personality.LuxuryPreference = 0.95f;
		Personality.RiskTolerance = 0.15f;
		Personality.Curiosity = 0.30f;
		Personality.ExplorationDrive = 0.20f;
		Personality.Greed = 0.75f;
		Personality.FearOfPunishment = 0.90f;
		Personality.AlcoholTolerance = 0.60f;
		Personality.DizzinessTolerance = 0.50f;
		Personality.TemperatureTolerance = 0.30f;
		Personality.AddictionSusceptibility = 0.20f;
		Personality.CleanlinessPreference = 0.90f;
		Personality.SocialPreference = 0.70f;
		WalletCash = 1000.0f;
		IncomeRatePerMinute = 50.0f;

		// Gourmet & Luxury taste
		Personality.TastePreferences.Luxury = 1.0f;
		Personality.TastePreferences.Healthy = 0.7f;
		Personality.TastePreferences.Meat = 0.8f;
		Personality.TastePreferences.Spicy = 0.3f;
		Personality.TastePreferences.Sweet = 0.4f;
		Personality.TastePreferences.Salty = 0.4f;
		Personality.TastePreferences.Sour = 0.3f;
		Personality.TastePreferences.Bitter = 0.5f;
		Personality.TastePreferences.Vegetarian = 0.3f;
		break;

	case ENPCArchetype::Fatty:
		Personality.Patience = 0.85f; // Extremely patient waiting for large meals
		Personality.PriceSensitivity = 0.85f; // Wants cheap food / buffets
		Personality.LuxuryPreference = 0.15f;
		Personality.RiskTolerance = 0.40f;
		Personality.Curiosity = 0.50f;
		Personality.ExplorationDrive = 0.35f;
		Personality.Greed = 0.30f;
		Personality.FearOfPunishment = 0.50f;
		Personality.AlcoholTolerance = 0.85f;
		Personality.DizzinessTolerance = 0.75f;
		Personality.TemperatureTolerance = 0.40f;
		Personality.AddictionSusceptibility = 0.65f;
		Personality.CleanlinessPreference = 0.15f; // Messy eater, soils restrooms
		Personality.SocialPreference = 0.40f;
		WalletCash = 60.0f;
		IncomeRatePerMinute = 5.0f;

		// Heavy, sweet, salty comfort food
		Personality.TastePreferences.Meat = 1.0f;
		Personality.TastePreferences.Sweet = 0.9f;
		Personality.TastePreferences.Salty = 0.9f;
		Personality.TastePreferences.Spicy = 0.6f;
		Personality.TastePreferences.Sour = 0.4f;
		Personality.TastePreferences.Bitter = 0.2f;
		Personality.TastePreferences.Vegetarian = 0.1f;
		Personality.TastePreferences.Healthy = 0.1f;
		Personality.TastePreferences.Luxury = 0.2f;
		break;

	case ENPCArchetype::FoodInspector:
		Personality.Patience = 0.45f;
		Personality.PriceSensitivity = 0.20f;
		Personality.LuxuryPreference = 0.80f;
		Personality.RiskTolerance = 0.10f;
		Personality.Curiosity = 0.85f; // Inspects every room
		Personality.ExplorationDrive = 0.75f;
		Personality.Greed = 0.30f;
		Personality.FearOfPunishment = 0.98f;
		Personality.AlcoholTolerance = 0.30f;
		Personality.DizzinessTolerance = 0.30f;
		Personality.TemperatureTolerance = 0.50f;
		Personality.AddictionSusceptibility = 0.10f;
		Personality.CleanlinessPreference = 0.99f; // Demands perfection!
		Personality.SocialPreference = 0.30f;
		WalletCash = 350.0f;
		IncomeRatePerMinute = 15.0f;

		Personality.TastePreferences.Healthy = 0.95f;
		Personality.TastePreferences.Luxury = 0.80f;
		Personality.TastePreferences.Meat = 0.50f;
		Personality.TastePreferences.Vegetarian = 0.70f;
		Personality.TastePreferences.Spicy = 0.40f;
		Personality.TastePreferences.Sweet = 0.40f;
		Personality.TastePreferences.Salty = 0.40f;
		Personality.TastePreferences.Sour = 0.40f;
		Personality.TastePreferences.Bitter = 0.40f;
		break;

	case ENPCArchetype::Police:
		Personality.Patience = 0.25f; // On duty, quick grab & go
		Personality.PriceSensitivity = 0.50f;
		Personality.LuxuryPreference = 0.35f;
		Personality.RiskTolerance = 0.30f;
		Personality.Curiosity = 0.70f; // Scans patrons and suspects
		Personality.ExplorationDrive = 0.40f;
		Personality.Greed = 0.20f;
		Personality.FearOfPunishment = 0.95f;
		Personality.AlcoholTolerance = 0.40f;
		Personality.DizzinessTolerance = 0.40f;
		Personality.TemperatureTolerance = 0.60f;
		Personality.AddictionSusceptibility = 0.15f;
		Personality.CleanlinessPreference = 0.65f;
		Personality.SocialPreference = 0.40f;
		WalletCash = 120.0f;
		IncomeRatePerMinute = 10.0f;

		Personality.TastePreferences.Meat = 0.80f;
		Personality.TastePreferences.Salty = 0.70f;
		Personality.TastePreferences.Bitter = 0.60f; // Coffee
		Personality.TastePreferences.Healthy = 0.50f;
		Personality.TastePreferences.Spicy = 0.50f;
		Personality.TastePreferences.Sweet = 0.40f;
		Personality.TastePreferences.Sour = 0.30f;
		Personality.TastePreferences.Vegetarian = 0.30f;
		Personality.TastePreferences.Luxury = 0.30f;
		break;

	case ENPCArchetype::UndercoverCop:
		Personality.Patience = 0.75f; // Takes long time eating to observe
		Personality.PriceSensitivity = 0.40f;
		Personality.LuxuryPreference = 0.50f;
		Personality.RiskTolerance = 0.80f; // Infiltrates employee rooms
		Personality.Curiosity = 0.95f; // Searches for dead bodies & drugs
		Personality.ExplorationDrive = 0.90f;
		Personality.Greed = 0.15f;
		Personality.FearOfPunishment = 0.90f;
		Personality.AlcoholTolerance = 0.25f; // Avoids alcohol, sips coffee
		Personality.DizzinessTolerance = 0.40f;
		Personality.TemperatureTolerance = 0.60f;
		Personality.AddictionSusceptibility = 0.10f;
		Personality.CleanlinessPreference = 0.70f;
		Personality.SocialPreference = 0.60f;
		WalletCash = 180.0f;
		IncomeRatePerMinute = 12.0f;

		Personality.TastePreferences.Bitter = 0.85f; // Black coffee
		Personality.TastePreferences.Healthy = 0.70f;
		Personality.TastePreferences.Meat = 0.60f;
		Personality.TastePreferences.Salty = 0.50f;
		Personality.TastePreferences.Vegetarian = 0.50f;
		Personality.TastePreferences.Spicy = 0.40f;
		Personality.TastePreferences.Luxury = 0.40f;
		Personality.TastePreferences.Sweet = 0.30f;
		Personality.TastePreferences.Sour = 0.40f;
		break;

	case ENPCArchetype::Robber:
		Personality.Patience = 0.35f;
		Personality.PriceSensitivity = 0.95f; // Broke, sneaks free food
		Personality.LuxuryPreference = 0.30f;
		Personality.RiskTolerance = 0.90f; // High risk thief
		Personality.Curiosity = 0.85f; // Searches for unlocked doors / safe
		Personality.ExplorationDrive = 0.85f;
		Personality.Greed = 0.95f; // Driven by money
		Personality.FearOfPunishment = 0.25f;
		Personality.AlcoholTolerance = 0.50f;
		Personality.DizzinessTolerance = 0.45f;
		Personality.TemperatureTolerance = 0.50f;
		Personality.AddictionSusceptibility = 0.55f;
		Personality.CleanlinessPreference = 0.25f;
		Personality.SocialPreference = 0.20f;
		WalletCash = 25.0f; // Broke
		IncomeRatePerMinute = 0.0f;

		Personality.TastePreferences.Meat = 0.70f;
		Personality.TastePreferences.Sweet = 0.60f;
		Personality.TastePreferences.Salty = 0.70f;
		Personality.TastePreferences.Spicy = 0.50f;
		Personality.TastePreferences.Luxury = 0.30f;
		Personality.TastePreferences.Sour = 0.30f;
		Personality.TastePreferences.Bitter = 0.20f;
		Personality.TastePreferences.Vegetarian = 0.20f;
		Personality.TastePreferences.Healthy = 0.20f;
		break;

	case ENPCArchetype::Gangster:
		Personality.Patience = 0.25f; // Low patience, quick to anger
		Personality.PriceSensitivity = 0.30f;
		Personality.LuxuryPreference = 0.70f; // Rents private rooms
		Personality.RiskTolerance = 0.95f; // Deals drugs, brawls
		Personality.Curiosity = 0.50f;
		Personality.ExplorationDrive = 0.40f;
		Personality.Greed = 0.85f;
		Personality.FearOfPunishment = 0.10f; // Zero fear of police
		Personality.AlcoholTolerance = 0.95f; // Hard drinker
		Personality.DizzinessTolerance = 0.85f;
		Personality.TemperatureTolerance = 0.70f;
		Personality.AddictionSusceptibility = 0.70f;
		Personality.CleanlinessPreference = 0.35f;
		Personality.SocialPreference = 0.60f;
		WalletCash = 600.0f;
		IncomeRatePerMinute = 30.0f;

		// Heavy meat, spicy, hard alcohol
		Personality.TastePreferences.Meat = 0.95f;
		Personality.TastePreferences.Spicy = 0.85f;
		Personality.TastePreferences.Salty = 0.80f;
		Personality.TastePreferences.Bitter = 0.80f; // Hard liquor
		Personality.TastePreferences.Luxury = 0.75f;
		Personality.TastePreferences.Sweet = 0.30f;
		Personality.TastePreferences.Sour = 0.30f;
		Personality.TastePreferences.Vegetarian = 0.10f;
		Personality.TastePreferences.Healthy = 0.10f;
		break;

	case ENPCArchetype::NormalPublic:
	default:
		Personality.Patience = 0.50f;
		Personality.PriceSensitivity = 0.50f;
		Personality.LuxuryPreference = 0.45f;
		Personality.RiskTolerance = 0.25f;
		Personality.Curiosity = 0.50f;
		Personality.ExplorationDrive = 0.40f;
		Personality.Greed = 0.40f;
		Personality.FearOfPunishment = 0.80f;
		Personality.AlcoholTolerance = 0.50f;
		Personality.DizzinessTolerance = 0.50f;
		Personality.TemperatureTolerance = 0.50f;
		Personality.AddictionSusceptibility = 0.30f;
		Personality.CleanlinessPreference = 0.60f;
		Personality.SocialPreference = 0.55f;
		WalletCash = 120.0f;
		IncomeRatePerMinute = 8.0f;

		Personality.TastePreferences.Meat = 0.60f;
		Personality.TastePreferences.Sweet = 0.50f;
		Personality.TastePreferences.Salty = 0.50f;
		Personality.TastePreferences.Spicy = 0.40f;
		Personality.TastePreferences.Healthy = 0.50f;
		Personality.TastePreferences.Luxury = 0.40f;
		Personality.TastePreferences.Vegetarian = 0.40f;
		Personality.TastePreferences.Sour = 0.30f;
		Personality.TastePreferences.Bitter = 0.30f;
		break;
	}
}

float UNPCPersonalityComponent::EvaluateTaste(const FNPCTasteVector& FoodTaste) const
{
	return Personality.TastePreferences.CalculateCompatibility(FoodTaste);
}

float UNPCPersonalityComponent::CalculateMaxWaitDuration(float BasePatienceSeconds) const
{
	float ValenceModifier = 1.0f;
	float ArousalModifier = 1.0f;

	AActor* OwnerActor = GetOwner();
	if (OwnerActor)
	{
		UNPCMentalStateComponent* MentalComp = OwnerActor->FindComponentByClass<UNPCMentalStateComponent>();
		if (MentalComp)
		{
			if (MentalComp->MentalState.Valence >= 75.0f)
			{
				ValenceModifier = 1.5f;
			}
			else if (MentalComp->MentalState.Valence <= 35.0f)
			{
				ValenceModifier = 0.6f;
			}

			if (MentalComp->MentalState.Arousal >= 70.0f)
			{
				ArousalModifier = 0.55f;
			}
			else if (MentalComp->MentalState.Arousal <= 30.0f)
			{
				ArousalModifier = 1.3f;
			}
		}
	}

	const float TraitMultiplier = 0.5f + Personality.Patience;
	return FMath::Max(10.0f, BasePatienceSeconds * TraitMultiplier * ValenceModifier * ArousalModifier);
}

float UNPCPersonalityComponent::CalculateTip(float BillTotal, float OverallExperienceScore) const
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor)
	{
		UNPCMentalStateComponent* MentalComp = OwnerActor->FindComponentByClass<UNPCMentalStateComponent>();
		if (MentalComp && MentalComp->MentalState.Valence <= 45.0f)
		{
			return 0.0f;
		}
	}

	if (Archetype == ENPCArchetype::Fatty || Archetype == ENPCArchetype::Police || Archetype == ENPCArchetype::Gangster || Archetype == ENPCArchetype::Robber)
	{
		return 0.0f;
	}

	if (Archetype == ENPCArchetype::Business)
	{
		if (OverallExperienceScore >= 75.0f)
		{
			const float TipRate = 1.0f + ((OverallExperienceScore - 75.0f) / 25.0f);
			return BillTotal * TipRate;
		}
		return 0.0f;
	}

	if (OverallExperienceScore >= 50.0f)
	{
		const float NormalizedScore = (OverallExperienceScore - 50.0f) / 50.0f;
		const float TipPercent = 0.05f + (0.15f * NormalizedScore);
		return BillTotal * TipPercent;
	}

	return 0.0f;
}
