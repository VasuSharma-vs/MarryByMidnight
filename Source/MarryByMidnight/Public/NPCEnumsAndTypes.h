#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"
#include "NPCEnumsAndTypes.generated.h"

class AActor;



/**
 * High-level archetype defining societal role, financial profile, and default behaviors.
 */
UENUM(BlueprintType)
enum class ENPCArchetype : uint8
{
	NormalPublic    UMETA(DisplayName = "Normal Public"),
	Business        UMETA(DisplayName = "Businessman / Woman"),
	Fatty           UMETA(DisplayName = "Fatty (Glutton)"),
	FoodInspector   UMETA(DisplayName = "Food Inspector"),
	Police          UMETA(DisplayName = "Police Officer"),
	UndercoverCop   UMETA(DisplayName = "Undercover Police"),
	Robber          UMETA(DisplayName = "Robber / Thief"),
	Gangster        UMETA(DisplayName = "Gangster")
};

/**
 * Discrete mood tiers derived from continuous Valence (Happy <-> Sad) and Arousal (Calm <-> Energy).
 */
UENUM(BlueprintType)
enum class ENPCMoodTier : uint8
{
	Ecstatic        UMETA(DisplayName = "Ecstatic (Excellent)"),
	Happy           UMETA(DisplayName = "Happy"),
	Content         UMETA(DisplayName = "Content (Good)"),
	Neutral         UMETA(DisplayName = "Neutral (Normal)"),
	Discontent      UMETA(DisplayName = "Discontent (Discomfort)"),
	Frustrated      UMETA(DisplayName = "Frustrated (Not Happy)"),
	Enraged         UMETA(DisplayName = "Enraged (Hostile / High Energy)")
};

/**
 * Physical impairment tier based on derived Dizziness.
 */
UENUM(BlueprintType)
enum class EDizzinessTier : uint8
{
	Normal          UMETA(DisplayName = "Normal (0-25)"),
	Mild            UMETA(DisplayName = "Mild (25-50)"),
	Impaired        UMETA(DisplayName = "Impaired / Mess-Making (50-75)"),
	Severe          UMETA(DisplayName = "Severe Impairment (75-99)"),
	PassedOut       UMETA(DisplayName = "Passed Out (100)")
};

/**
 * Primitive atomic actions exposed by interactive world objects (Affordances).
 */
UENUM(BlueprintType)
enum class EAffordanceAction : uint8
{
	None,
	Approach,
	Take,
	Drop,
	Open,
	Close,
	Insert,
	Operate,
	Wait,
	Eat,
	Drink,
	Search,
	Hide,
	UseToilet,
	Sleep,
	Talk,
	Brawl,
	Sit,
	Inspect,
	Buy
};

/**
 * Physical type classification for consumable prop objects.
 */
UENUM(BlueprintType)
enum class EConsumablePropType : uint8
{
	Drink           UMETA(DisplayName = "Drink (Soda, Water, Juice)"),
	Food            UMETA(DisplayName = "Food (Sandwich, Burger, Snack)"),
	Alcohol         UMETA(DisplayName = "Alcohol (Beer, Liquor)"),
	Medicine        UMETA(DisplayName = "Medicine / Stimulant"),
	Custom          UMETA(DisplayName = "Custom Consumable")
};


/**
 * All simulation stats that can be inspected, tested, or modified over time.
 */
UENUM(BlueprintType)
enum class ENPCSimulationStat : uint8
{
	Power            UMETA(DisplayName = "Power / Energy"),
	Thirst           UMETA(DisplayName = "Thirst"),
	Hunger           UMETA(DisplayName = "Hunger"),
	Sleepiness       UMETA(DisplayName = "Sleepiness"),
	Bladder          UMETA(DisplayName = "Bladder"),
	Bowel            UMETA(DisplayName = "Bowel"),
	BodyTemperature  UMETA(DisplayName = "Body Temperature"),
	Sickness         UMETA(DisplayName = "Sickness"),
	Dopamine         UMETA(DisplayName = "Dopamine"),
	Stress           UMETA(DisplayName = "Stress"),
	AlcoholLevel     UMETA(DisplayName = "Alcohol Level"),
	DrugLevel        UMETA(DisplayName = "Drug Level"),
	WalletCash       UMETA(DisplayName = "Wallet Cash")
};

/**
 * Configurable rate modifier for a simulation stat.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FNPCSimulationStatModifier
{
	GENERATED_BODY()

	/** The simulation stat to modify */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	ENPCSimulationStat Stat = ENPCSimulationStat::Power;

	/** Rate of change per second (e.g. +15.0 increases stat by 15/sec, -2.0 costs/decreases stat by 2/sec) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	float ChangeRate = 0.2f;

	FNPCSimulationStatModifier() = default;
	FNPCSimulationStatModifier(ENPCSimulationStat InStat, float InChangeRate)
		: Stat(InStat), ChangeRate(InChangeRate) {}
};

/**
 * Inherent biological valence for simulation stats:
 *  +1.0f: Increasing is inherently GOOD, Decreasing is BAD (Power, WalletCash, Dopamine)
 *  -1.0f: Increasing is inherently BAD, Decreasing is GOOD (Thirst, Hunger, Sleepiness, Bladder, Bowel, Sickness, Stress)
 *   0.0f: Baseline / neutral / context dependent
 */
inline float GetSimulationStatDesirabilitySign(ENPCSimulationStat Stat)
{
	switch (Stat)
	{
	case ENPCSimulationStat::Power:
	case ENPCSimulationStat::WalletCash:
	case ENPCSimulationStat::Dopamine:
		return 1.0f; // Increasing is Good (+), Decreasing is Bad (-)

	case ENPCSimulationStat::Thirst:
	case ENPCSimulationStat::Hunger:
	case ENPCSimulationStat::Sleepiness:
	case ENPCSimulationStat::Bladder:
	case ENPCSimulationStat::Bowel:
	case ENPCSimulationStat::Sickness:
	case ENPCSimulationStat::Stress:
		return -1.0f; // Increasing is Bad (-), Decreasing is Good (+)

	case ENPCSimulationStat::BodyTemperature:
	case ENPCSimulationStat::AlcoholLevel:
	case ENPCSimulationStat::DrugLevel:
	default:
		return 0.0f;
	}
}

/**
 * Result of evaluating an individual stimulus against current biological deficits and personality.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FNPCStimulusEvaluation
{
	GENERATED_BODY()

	/** The simulation stat evaluated */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stimulus")
	ENPCSimulationStat Stat = ENPCSimulationStat::Power;

	/** Rate of change from the stimulus per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stimulus")
	float ChangeRate = 0.0f;

	/** Whether this stimulus effect is fundamentally good (>0) or bad (<0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stimulus")
	bool bIsGood = true;

	/** Subjective urgency or need for this stat (0.0 = satisfied/no need, 1.0+ = urgently needed) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stimulus")
	float NeedUrgency = 0.0f;

	/** Weighted utility score (positive = net benefit, negative = net cost/harm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stimulus")
	float WeightedUtilityScore = 0.0f;
};

/**
 * Comprehensive evaluation of all detected stimuli in an environment volume or zone.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FNPCZoneStimuliEvaluation
{
	GENERATED_BODY()

	/** Total net utility score (Benefit minus Cost) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stimulus")
	float NetUtilityScore = 0.0f;

	/** Total positive benefit score */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stimulus")
	float TotalBenefit = 0.0f;

	/** Total negative cost/harm score */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stimulus")
	float TotalCost = 0.0f;

	/** Recommended action based on detected stimuli: StayAndAbsorb, LeaveSatisfied, FleeHazard, LeaveBroke, None */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stimulus")
	FName RecommendedAction = FName("None");

	/** Diagnostic explanation of why the NPC made this choice */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stimulus")
	FString DiagnosticReasoning;

	/** Individual breakdowns for each stimulus detected */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stimulus")
	TArray<FNPCStimulusEvaluation> Breakdown;
};

/**
 * Memory record of a discovered place, its world location, and its stat effect on simulation.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FDiscoveredPlaceInfo
{
	GENERATED_BODY()

	/** Unique name or identifier of the place / zone */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	FName PlaceId = NAME_None;

	/** The simulation stat affected (e.g. Power, Thirst, Hunger, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	ENPCSimulationStat AffectedStat = ENPCSimulationStat::Power;

	/** Rate of change per second at this location (positive = restores/increases, negative = drains) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	float ChangeRate = 0.2f;

	/** World location of the place */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	FVector Location = FVector::ZeroVector;

	/** Number of times visited or experienced */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	int32 VisitCount = 1;
};

/**
 * Flavor and dietary profile vector used to compute food compatibility.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FNPCTasteVector
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taste")
	float Spicy = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taste")
	float Sweet = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taste")
	float Salty = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taste")
	float Sour = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taste")
	float Bitter = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taste")
	float Meat = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taste")
	float Vegetarian = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taste")
	float Healthy = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Taste")
	float Luxury = 0.5f;

	/** Calculates compatibility score [0.0 - 1.0] against another taste vector (e.g. food recipe) */
	float CalculateCompatibility(const FNPCTasteVector& Other) const
	{
		float DiffSum = 0.0f;
		DiffSum += FMath::Abs(Spicy - Other.Spicy);
		DiffSum += FMath::Abs(Sweet - Other.Sweet);
		DiffSum += FMath::Abs(Salty - Other.Salty);
		DiffSum += FMath::Abs(Sour - Other.Sour);
		DiffSum += FMath::Abs(Bitter - Other.Bitter);
		DiffSum += FMath::Abs(Meat - Other.Meat);
		DiffSum += FMath::Abs(Vegetarian - Other.Vegetarian);
		DiffSum += FMath::Abs(Healthy - Other.Healthy);
		DiffSum += FMath::Abs(Luxury - Other.Luxury);

		// Normalized over 9 attributes
		float AvgDiff = DiffSum / 9.0f;
		return FMath::Clamp(1.0f - AvgDiff, 0.0f, 1.0f);
	}
};

/**
 * Biological and physical state variables.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FNPCPhysicalState
{
	GENERATED_BODY()

	/** Available physical/mechanical energy [0.0 - 100.0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physical")
	float Power = 100.0f;

	/** Rate of change per second for Power (positive = recovering, negative = draining) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physical")
	float PowerChangeRate = 0.0f;

	/** Hydration level [0.0 - 100.0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physical")
	float Water = 100.0f;

	/** Core body temperature in Celsius (Standard: 37.0°C) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physical")
	float BodyTemperature = 37.0f;

	/** Perceived local ambient temperature in Celsius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physical")
	float AmbientTemperature = 24.0f;

	/** Pathological illness / poisoning [0.0 - 100.0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physical")
	float Sickness = 0.0f;

	/** Derived physiological disorientation [0.0 - 100.0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physical")
	float Dizziness = 0.0f;

	/** Blood alcohol concentration [0.0 - 100.0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Substances")
	float AlcoholLevel = 0.0f;

	/** Narcotic / drug concentration [0.0 - 100.0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Substances")
	float DrugLevel = 0.0f;
};

/**
 * Homeostatic drives and biological urgencies.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FNPCNeeds
{
	GENERATED_BODY()

	/** Hunger level [0.0 - 100.0], where 0 = Full, 100 = Starving */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needs")
	float Hunger = 20.0f;

	/** Thirst drive [0.0 - 100.0], where 0 = Hydrated, 100 = Parched */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needs")
	float Thirst = 20.0f;

	/** Drive for sleep / resting [0.0 - 100.0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needs")
	float Sleepiness = 10.0f;

	/** Fluid waste urgency [0.0 - 100.0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needs")
	float BladderUrgency = 15.0f;

	/** Solid waste urgency [0.0 - 100.0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needs")
	float BowelUrgency = 10.0f;

	/** Environmental and physical comfort [0.0 - 100.0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needs")
	float Comfort = 80.0f;
};

/**
 * Psychological state following Russell's Circumplex Model of Affect.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FNPCMentalState
{
	GENERATED_BODY()

	/** Immediate reward / neurochemical stimulation [0.0 - 100.0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mental")
	float Dopamine = 50.0f;

	/** Post-stimulation crash severity [0.0 - 100.0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mental")
	float DopamineCrash = 0.0f;

	/** Psychological pressure / agitation [0.0 - 100.0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mental")
	float Stress = 10.0f;

	/** Affect Axis 1: Valence (0.0 = Miserable / Sad, 100.0 = Ecstatic / Happy) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affect")
	float Valence = 75.0f;

	/** Affect Axis 2: Arousal (0.0 = Calm / Lethargic, 100.0 = High Energy / Agitated) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affect")
	float Arousal = 40.0f;
};

/**
 * Static behavioral and psychological personality traits.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FNPCPersonality
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Personality")
	float Patience = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Personality")
	float PriceSensitivity = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Personality")
	float LuxuryPreference = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Personality")
	float RiskTolerance = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Personality")
	float Curiosity = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Personality")
	float ExplorationDrive = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Personality")
	float Greed = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Personality")
	float FearOfPunishment = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tolerances")
	float AlcoholTolerance = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tolerances")
	float DizzinessTolerance = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tolerances")
	float TemperatureTolerance = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tolerances")
	float AddictionSusceptibility = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preferences")
	float CleanlinessPreference = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preferences")
	float SocialPreference = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preferences")
	FNPCTasteVector TastePreferences;
};

/**
 * Definition of an atomic affordance exposed by world objects.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FAffordanceDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance")
	EAffordanceAction ActionType = EAffordanceAction::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance")
	FName ActionName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance")
	FGameplayTagContainer Preconditions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance")
	FGameplayTagContainer Postconditions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance")
	float ExecutionDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance")
	float DetectionRisk = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance")
	float BaseReward = 10.0f;
};

/**
 * Requirement specifying a required item tag and quantity (e.g. 2x Arcade Tokens, 1x Coffee Mug).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FNPCItemRequirement
{
	GENERATED_BODY()

	/** Specific item tag required (e.g. Item.Currency.Token, Item.Ingredient.CoffeeBean, Item.Tool.Wrench) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirement")
	FGameplayTag ItemTag;

	/** Number of units / quantity required */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirement", meta = (ClampMin = "1"))
	int32 Quantity = 1;

	/** Human-readable description (e.g. "2x Arcade Tokens") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirement")
	FText RequirementDescription = FText::GetEmpty();

	FNPCItemRequirement() = default;
	FNPCItemRequirement(FGameplayTag InTag, int32 InQty, FText InDesc = FText::GetEmpty())
		: ItemTag(InTag), Quantity(InQty), RequirementDescription(InDesc) {}
};

/**
 * Dynamic option returned when an NPC queries an object: "Can I interact with you? Give me options."
 * Includes safety validation (e.g. ladder can be climbed or carried, but cannot block essential player paths).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FAffordanceOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance")
	FName OptionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance")
	FText DisplayLabel = FText::GetEmpty();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance")
	EAffordanceAction ActionType = EAffordanceAction::None;

	/** Whether this option is currently allowed (e.g. false if ladder is actively supporting the player on roof) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance")
	bool bIsAllowed = true;

	/** Reason why interaction is prohibited */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance")
	FText DisallowedReason = FText::GetEmpty();

	/** Specific input item or ingredient tag required (e.g. Tag "Item.Ingredient.Meat") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance")
	FGameplayTag RequiredTag;

	/** Multiple item requirements with different quantities (e.g. 2 Tokens, 1 Empty Bottle) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance|Requirements")
	TArray<FNPCItemRequirement> RequiredItems;

	/** Produced output item or result tag */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance")
	FGameplayTag ProducedTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance")
	float ExecutionTime = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance")
	float Risk = 0.0f;

	/** Monetary cost required to use this option (e.g. $2.00 for cold drink) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance|Requirements")
	float MoneyCost = 0.0f;

	/** Stimuli modifiers granted to the instigator (instant delta or rate/sec) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance|Offerings")
	TArray<FNPCSimulationStatModifier> OfferedStimuli;

	/** Whether offered stimuli apply continuously over time (e.g. massage chair per second) vs instantly upon completion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance|Offerings")
	bool bIsOverTime = false;

	/** Class of prop/item dispensed into world or hands upon successful interaction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affordance|Offerings")
	TSubclassOf<AActor> DispensedItemClass = nullptr;
};

/**
 * Configurable offering on an interactive machine or operable object (e.g. Vending Machine, Massage Sofa).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FNPCOperableOffering
{
	GENERATED_BODY()

	/** Unique identifier for this offering */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offering")
	FName OfferingId = NAME_None;

	/** Human-readable title displayed on machine / in-game debug */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offering")
	FText DisplayTitle = FText::GetEmpty();

	/** Atomic action type for this offering (Operate, Drink, Eat, Sleep, Sit, Buy, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offering")
	EAffordanceAction ActionType = EAffordanceAction::Operate;

	/** Cash price required to use this offering (e.g. $2.00) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offering")
	float MoneyCost = 0.0f;

	/** Optional single gameplay tag required */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offering")
	FGameplayTag RequiredTag;

	/** Multiple item requirements with different quantities (e.g. 2 Tokens, 1 Empty Bottle, 3 Coffee Beans) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offering")
	TArray<FNPCItemRequirement> RequiredItems;

	/** Time in seconds needed to complete interaction or duration of continuous service */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offering")
	float ExecutionDuration = 1.0f;

	/** If true, stimuli are applied continuously per second (e.g. massage chair); if false, applied once on completion (e.g. vending machine) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offering")
	bool bIsContinuousOverTime = false;

	/** Array of stimuli changes applied to the NPC */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offering")
	TArray<FNPCSimulationStatModifier> OfferedStimuli;

	/** Optional consumable prop actor spawned/dispensed (e.g. BP_ColdDrinkCan) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offering")
	TSubclassOf<AActor> DispensedPropClass = nullptr;

	/** Converts this offering to a FAffordanceOption for universal affordance queries */
	FAffordanceOption ToAffordanceOption(bool bAllowed = true, const FText& Reason = FText::GetEmpty()) const
	{
		FAffordanceOption Option;
		Option.OptionId = OfferingId;
		Option.DisplayLabel = DisplayTitle;
		Option.ActionType = ActionType;
		Option.bIsAllowed = bAllowed;
		Option.DisallowedReason = Reason;
		Option.RequiredTag = RequiredTag;
		Option.RequiredItems = RequiredItems;
		Option.ExecutionTime = ExecutionDuration;
		Option.MoneyCost = MoneyCost;
		Option.OfferedStimuli = OfferedStimuli;
		Option.bIsOverTime = bIsContinuousOverTime;
		Option.DispensedItemClass = DispensedPropClass;
		return Option;
	}
};

/**
 * Individual NPC's memory of an operable machine (location, requirements, and offerings).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FDiscoveredMachineMemory
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	FName MachineId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	FText DisplayName = FText::GetEmpty();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	TWeakObjectPtr<AActor> MachineActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	TArray<FNPCOperableOffering> KnownOfferings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	float LastSeenTime = 0.0f;
};

/**
 * Individual NPC's memory of a consumable prop (location, freshness, quantity, price, and stimuli).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FDiscoveredPropMemory
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	FName PropId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	FText ItemName = FText::GetEmpty();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	EConsumablePropType ConsumableType = EConsumablePropType::Drink;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	TWeakObjectPtr<AActor> PropActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	float Price = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	float QuantityLevel = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	int32 RemainingPortions = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	float ExpiryFreshness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	TArray<FNPCSimulationStatModifier> KnownStimuli;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	float LastSeenTime = 0.0f;
};

/**
 * Parameterized resource search query: controls what to find (Food, Drink, Warm place, Chill place, Power, Stress relief)
 * and how to find it (distance thresholds, affordability, internet computer search, and active reconnaissance).
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FNPCResourceSearchQuery
{
	GENERATED_BODY()

	/** Target simulation stat to satisfy (Hunger, Thirst, BodyTemperature, Power, Stress, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource Search")
	ENPCSimulationStat TargetStat = ENPCSimulationStat::Hunger;

	/** Whether we want this stat to increase (e.g. Power, Dopamine, BodyTemp for warm place) or decrease (e.g. Hunger, Thirst, Stress, BodyTemp for chill place) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource Search")
	bool bSeekingIncrease = false;

	/** Minimum acceptable magnitude of the effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource Search")
	float MinDesirableRate = 0.5f;

	/** Maximum travel distance before considering an option "too far" (default: 3500 cm = 35 meters) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource Search")
	float MaxAcceptableDistance = 3500.0f;

	/** Whether opportunistic scavenging of half-full/getting-low props is permitted */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource Search")
	bool bAllowOpportunisticScavenge = true;

	/** If memory has no match or is too far/unaffordable, can visit nearest computer to search online */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource Search")
	bool bAllowComputerSearchFallback = true;

	/** If computer search yields nothing or no computer is available, inspect nearby machines/objects one by one */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource Search")
	bool bAllowPhysicalReconnaissanceFallback = true;

	static FNPCResourceSearchQuery ForFood()
	{
		FNPCResourceSearchQuery Q;
		Q.TargetStat = ENPCSimulationStat::Hunger;
		Q.bSeekingIncrease = false;
		return Q;
	}

	static FNPCResourceSearchQuery ForDrink()
	{
		FNPCResourceSearchQuery Q;
		Q.TargetStat = ENPCSimulationStat::Thirst;
		Q.bSeekingIncrease = false;
		return Q;
	}

	static FNPCResourceSearchQuery ForWarmPlace()
	{
		FNPCResourceSearchQuery Q;
		Q.TargetStat = ENPCSimulationStat::BodyTemperature;
		Q.bSeekingIncrease = true;
		Q.bAllowOpportunisticScavenge = false;
		return Q;
	}

	static FNPCResourceSearchQuery ForChillPlace()
	{
		FNPCResourceSearchQuery Q;
		Q.TargetStat = ENPCSimulationStat::BodyTemperature;
		Q.bSeekingIncrease = false;
		Q.bAllowOpportunisticScavenge = true; // Chilled drinks can also cool body down!
		return Q;
	}

	static FNPCResourceSearchQuery ForPower()
	{
		FNPCResourceSearchQuery Q;
		Q.TargetStat = ENPCSimulationStat::Power;
		Q.bSeekingIncrease = true;
		Q.bAllowOpportunisticScavenge = false;
		return Q;
	}

	static FNPCResourceSearchQuery ForStressRelief()
	{
		FNPCResourceSearchQuery Q;
		Q.TargetStat = ENPCSimulationStat::Stress;
		Q.bSeekingIncrease = false;
		return Q;
	}
};




/**
 * Individual NPC's memory of an object or machine.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FDiscoveredObjectInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge")
	FName ObjectId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge")
	FGameplayTagContainer KnownInputs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge")
	FGameplayTagContainer KnownOutputs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge")
	TArray<FVector> DiscoveredLocations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge")
	int32 TimesInteracted = 0;
};

/**
 * Global cultural knowledge entry tracked across the map.
 * When >50% of NPCs discover it, it is saved permanently to disk.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FGlobalObjectKnowledge
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge")
	FName ObjectId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge")
	FGameplayTag RequiredTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge")
	FGameplayTag ProducedTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge")
	int32 DiscovererCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge")
	bool bIsPermanentlyLearned = false;

	/** Top 5 most frequent locations where this item or object was discovered */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knowledge")
	TArray<FVector> Top5Locations;
};

/**
 * Single step within a planned or remembered strategy chain.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FNPCStrategyStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy")
	EAffordanceAction ActionType = EAffordanceAction::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy")
	FName OptionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy")
	FName TargetTagOrClass = NAME_None;
};

/**
 * A discovered, planned, or socially learned strategy to fulfill a goal.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FNPCStrategy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy")
	FName StrategyId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy")
	FName Goal = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy")
	TArray<FNPCStrategyStep> ActionChain;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy")
	int32 SuccessCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy")
	int32 FailureCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy")
	float Confidence = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy")
	float EstimatedRisk = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy")
	float EstimatedReward = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy")
	bool bIsExploit = false;

	float GetSuccessRate() const
	{
		const int32 Total = SuccessCount + FailureCount;
		return Total > 0 ? (float)SuccessCount / (float)Total : 0.5f;
	}
};


