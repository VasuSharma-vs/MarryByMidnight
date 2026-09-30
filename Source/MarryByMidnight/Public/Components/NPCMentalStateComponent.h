#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCEnumsAndTypes.h"
#include "NPCMentalStateComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMoodTierChanged, ENPCMoodTier, NewTier, ENPCMoodTier, OldTier);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNPCEnraged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDopamineCrash, float, CrashSeverity);

/**
 * Manages mental state, dopamine rewards, stress, and Russell's 2-Axis Affect Model (Valence & Arousal).
 * Fully networked and replicated for multiplayer.
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UNPCMentalStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCMentalStateComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	/** Core psychological and affect state (Replicated) */
	UPROPERTY(ReplicatedUsing = OnRep_MentalState, EditAnywhere, BlueprintReadWrite, Category = "Mental")
	FNPCMentalState MentalState;

	/** Rate at which dopamine returns to baseline per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mental|Tuning")
	float DopamineDecayRate = 1.0f;

	/** Rate at which stress naturally diffuses when in a calm environment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mental|Tuning")
	float StressRecoveryRate = 0.5f;

	/** Current derived mood tier (Replicated to sync client animations and UI overhead) */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentMoodTier, VisibleAnywhere, BlueprintReadOnly, Category = "Mental|Affect")
	ENPCMoodTier CurrentMoodTier = ENPCMoodTier::Content;

	/** Events */
	UPROPERTY(BlueprintAssignable, Category = "Mental|Events")
	FOnMoodTierChanged OnMoodTierChanged;

	UPROPERTY(BlueprintAssignable, Category = "Mental|Events")
	FOnNPCEnraged OnNPCEnraged;

	UPROPERTY(BlueprintAssignable, Category = "Mental|Events")
	FOnDopamineCrash OnDopamineCrash;

	/** Grants immediate neurochemical pleasure (good food, win, reward) */
	UFUNCTION(BlueprintCallable, Category = "Mental|Actions")
	void GrantDopamine(float Amount);

	/** Adds psychological stress (waiting, high prices, mess, danger) */
	UFUNCTION(BlueprintCallable, Category = "Mental|Actions")
	void AddStress(float Amount);

	/** Reduces psychological stress (good service, rest, luxury) */
	UFUNCTION(BlueprintCallable, Category = "Mental|Actions")
	void RelieveStress(float Amount);

	/** Manually triggers dopamine crash (e.g. substance wearing off) */
	UFUNCTION(BlueprintCallable, Category = "Mental|Actions")
	void TriggerDopamineCrash(float Severity);

	/** Returns current mood tier */
	UFUNCTION(BlueprintPure, Category = "Mental|Getters")
	ENPCMoodTier GetMoodTier() const { return CurrentMoodTier; }

	/** Returns true if in aggressive / enraged high-arousal negative state */
	UFUNCTION(BlueprintPure, Category = "Mental|Getters")
	bool IsEnraged() const { return CurrentMoodTier == ENPCMoodTier::Enraged; }

	/** Derives Valence and Arousal from physical, needs, and mental inputs */
	UFUNCTION(BlueprintCallable, Category = "Mental|Affect")
	void RecalculateAffect();

protected:
	UFUNCTION()
	void OnRep_MentalState();

	UFUNCTION()
	void OnRep_CurrentMoodTier();

private:
	FTimerHandle MentalTickTimerHandle; // 1 Hz (1.0s)

	float PreviousDopamine = 50.0f;
	ENPCMoodTier PreviousTierClient = ENPCMoodTier::Content;

	void MentalTick();
	ENPCMoodTier EvaluateMoodTier(float Valence, float Arousal);
};
