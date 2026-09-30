#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCEnumsAndTypes.h"
#include "NPCStateComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPhysicalCriticalStateChanged, bool, bIsCritical, const FString&, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDizzinessTierChanged, EDizzinessTier, NewTier);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNPCPassedOut);

/**
 * Handles physical body simulation: Power, Hydration, Temperature, Sickness, and derived Dizziness.
 * Fully networked and replicated for multiplayer environments.
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UNPCStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCStateComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	/** Core physical state properties (Replicated from server to clients) */
	UPROPERTY(ReplicatedUsing = OnRep_PhysicalState, EditAnywhere, BlueprintReadWrite, Category = "State|Physical")
	FNPCPhysicalState PhysicalState;

	/** Thermal resistance coefficient [0.01 - 0.2]. Lower means body temp changes slower */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State|Tuning")
	float ThermalTransferRate = 0.05f;

	/** Alcohol metabolism rate (points cleared per second) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State|Tuning")
	float AlcoholBurnRate = 0.25f;

	/** Drug metabolism rate (points cleared per second) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State|Tuning")
	float DrugBurnRate = 0.15f;

	/** Current impairment tier */
	UPROPERTY(ReplicatedUsing = OnRep_DizzinessTier, VisibleAnywhere, BlueprintReadOnly, Category = "State|Physical")
	EDizzinessTier CurrentDizzinessTier = EDizzinessTier::Normal;

	/** Events */
	UPROPERTY(BlueprintAssignable, Category = "State|Events")
	FOnPhysicalCriticalStateChanged OnPhysicalCriticalStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "State|Events")
	FOnDizzinessTierChanged OnDizzinessTierChanged;

	UPROPERTY(BlueprintAssignable, Category = "State|Events")
	FOnNPCPassedOut OnNPCPassedOut;

	/** Modifies power by delta, clamped to [0, 100] (Server authoritative) */
	UFUNCTION(BlueprintCallable, Category = "State|Actions")
	void ModifyPower(float DeltaAmount);

	/** Sets active drain or recovery rate for power */
	UFUNCTION(BlueprintCallable, Category = "State|Actions")
	void SetPowerChangeRate(float NewRate);

	/** Consumes hydration */
	UFUNCTION(BlueprintCallable, Category = "State|Actions")
	void ConsumeWater(float Amount);

	/** Adds hydration and applies beverage temperature delta */
	UFUNCTION(BlueprintCallable, Category = "State|Actions")
	void Hydrate(float Amount, float TemperatureDelta = 0.0f);

	/** Adds alcohol to bloodstream */
	UFUNCTION(BlueprintCallable, Category = "State|Actions")
	void IngestAlcohol(float Amount);

	/** Ingests narcotic / drug */
	UFUNCTION(BlueprintCallable, Category = "State|Actions")
	void IngestDrug(float Amount);

	/** Incur sickness (spoiled food, infection) */
	UFUNCTION(BlueprintCallable, Category = "State|Actions")
	void InflictSickness(float Amount);

	/** Heals sickness */
	UFUNCTION(BlueprintCallable, Category = "State|Actions")
	void HealSickness(float Amount);

	/** Updates local perceived ambient temperature */
	UFUNCTION(BlueprintCallable, Category = "State|Actions")
	void SetAmbientTemperature(float NewAmbientTemp);

	/** Returns true if power or water is at 0, or body temp is fatal */
	UFUNCTION(BlueprintPure, Category = "State|Getters")
	bool IsInCriticalCondition() const;

	/** Gets derived dizziness tier */
	UFUNCTION(BlueprintPure, Category = "State|Getters")
	EDizzinessTier GetDizzinessTier() const { return CurrentDizzinessTier; }

protected:
	UFUNCTION()
	void OnRep_PhysicalState();

	UFUNCTION()
	void OnRep_DizzinessTier();

private:
	FTimerHandle FastUpdateTimerHandle; // 10 Hz (0.1s)
	FTimerHandle SlowUpdateTimerHandle; // 2 Hz (0.5s)

	UPROPERTY(Replicated)
	bool bWasCritical = false;

	/** Fast updates: Power change rate, survival checks, dizziness calculations */
	void FastTickUpdate();

	/** Slow updates: Ambient temperature equilibration, substance metabolism */
	void SlowTickUpdate();

	/** Derives dizziness from physiological inputs */
	void RecalculateDizziness();

	/** Checks fatal conditions */
	void EvaluateCriticalState();
};
