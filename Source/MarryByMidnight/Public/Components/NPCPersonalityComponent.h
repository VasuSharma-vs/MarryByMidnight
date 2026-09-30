#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCEnumsAndTypes.h"
#include "NPCPersonalityComponent.generated.h"

/**
 * Stores static personality traits, taste preferences, archetype defaults, and tipping logic.
 * Replicated for multiplayer networking.
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UNPCPersonalityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCPersonalityComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	FTimerHandle IncomeTimerHandle;
	void AccruePassiveIncome();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	/** Societal archetype (Changing this in Editor Details panel auto-updates personality values) */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "Identity")
	ENPCArchetype Archetype = ENPCArchetype::NormalPublic;

	/** Static personality traits */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Personality")
	FNPCPersonality Personality;

	/** Available pocket cash (Replicated) */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "Economy")
	float WalletCash = 100.0f;

	/** Passive money generation per minute */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	float IncomeRatePerMinute = 10.0f;

	/** Adds money to NPC wallet */
	UFUNCTION(BlueprintCallable, Category = "Personality|Economy")
	void AddCash(float Amount);

	/** Spends money from NPC wallet if sufficient balance is available */
	UFUNCTION(BlueprintCallable, Category = "Personality|Economy")
	bool SpendCash(float Amount);

	/** Initializes archetype traits to balanced starting profiles */
	UFUNCTION(BlueprintCallable, Category = "Personality")
	void ApplyArchetypeDefaults(ENPCArchetype InArchetype);

	/** Calculates taste compatibility against a food recipe vector [0.0 - 1.0] */
	UFUNCTION(BlueprintPure, Category = "Personality|Taste")
	float EvaluateTaste(const FNPCTasteVector& FoodTaste) const;

	/** Calculates expected wait patience in seconds adjusted by personality and current affect */
	UFUNCTION(BlueprintCallable, Category = "Personality|Patience")
	float CalculateMaxWaitDuration(float BasePatienceSeconds) const;

	/** Calculates tip amount paid after meal evaluation */
	UFUNCTION(BlueprintCallable, Category = "Personality|Economy")
	float CalculateTip(float BillTotal, float OverallExperienceScore) const;
};
