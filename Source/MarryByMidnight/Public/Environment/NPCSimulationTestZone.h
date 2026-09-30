#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NPCEnumsAndTypes.h"
#include "NPCSimulationTestZone.generated.h"

class UBoxComponent;
class UBillboardComponent;
class UTextRenderComponent;
class ANPCCharacter;

/**
 * World testing object with a box collision volume.
 * When an NPC overlaps this box, it modifies the selected simulation stat(s) by ChangeRate per second.
 *
 * Example:
 *  - TargetStat = Power, ChangeRate = 0.2  -> increases NPC's power by 0.2 per second.
 *  - TargetStat = Thirst, ChangeRate = 0.5 -> increases NPC's thirst by 0.5 per second.
 *  - TargetStat = Hunger, ChangeRate = -1.0 -> decreases NPC's hunger by 1.0 per second.
 */
UCLASS()
class MARRYBYMIDNIGHT_API ANPCSimulationTestZone : public AActor
{
	GENERATED_BODY()

public:
	ANPCSimulationTestZone();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

#if WITH_EDITOR
	virtual void OnConstruction(const FTransform& Transform) override;
#endif

public:
	/** Box collision component defining the test area */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> TriggerBox;

	/** Floating 3D world-space text label that naturally scales with distance */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTextRenderComponent> LabelTextComponent;

	/** Array of stimuli and their change rates applied to any NPC inside this volume.
	 *  Add or subtract multiple stimuli directly in the Details panel!
	 *  Example:
	/** Array of stimuli and their change rates applied to any NPC inside this volume.
	 *  Add or subtract multiple stimuli directly in the Details panel!
	 *  Example:
	 *   - [0] Stat = Power, ChangeRate = +15.0  (Recharges power)
	 *   - [1] Stat = WalletCash, ChangeRate = -2.0  (Costs $2 per second to charge)
	 *   - [2] Stat = Stress, ChangeRate = -1.0  (Calming atmosphere)
	 */
	UPROPERTY(ReplicatedUsing = OnRep_StimuliModifiers, EditAnywhere, BlueprintReadWrite, Category = "Simulation Test|Stimuli", meta = (TitleProperty = "Stat", DisplayName = "Stimuli & Change Rates"))
	TArray<FNPCSimulationStatModifier> StimuliModifiers;

	UFUNCTION()
	void OnRep_StimuliModifiers();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Checks if this zone has an active stimulus that increases the specified stat */
	UFUNCTION(BlueprintPure, Category = "Simulation Test")
	bool HasPositiveStimulusFor(ENPCSimulationStat Stat) const;

	/** Gets the total net change rate for the specified stat in this zone */
	UFUNCTION(BlueprintPure, Category = "Simulation Test")
	float GetChangeRateFor(ENPCSimulationStat Stat) const;

	/** Gets the monetary cost per second charged to NPCs in this volume (returns positive cost rate, 0 if free) */
	UFUNCTION(BlueprintPure, Category = "Simulation Test")
	float GetMoneyCostRate() const;

	/** Checks if the specified NPC has sufficient funds to afford staying in this volume */
	UFUNCTION(BlueprintPure, Category = "Simulation Test")
	bool CanNPCAfford(const ANPCCharacter* NPC) const;

	/** Primary simulation stat to modify (Legacy/Fallback) */
	UPROPERTY(ReplicatedUsing = OnRep_StimuliModifiers, EditAnywhere, BlueprintReadWrite, Category = "Simulation Test|Legacy", meta = (DisplayName = "Select Simulation Stat"))
	ENPCSimulationStat TargetStat = ENPCSimulationStat::Power;

	/** Rate of change per second (Legacy/Fallback) */
	UPROPERTY(ReplicatedUsing = OnRep_StimuliModifiers, EditAnywhere, BlueprintReadWrite, Category = "Simulation Test|Legacy", meta = (DisplayName = "Change Rate Per Second"))
	float ChangeRate = 0.2f;

	/** Optional additional stat modifiers to apply simultaneously (Legacy/Fallback) */
	UPROPERTY(ReplicatedUsing = OnRep_StimuliModifiers, EditAnywhere, BlueprintReadWrite, Category = "Simulation Test|Legacy")
	TArray<FNPCSimulationStatModifier> AdditionalModifiers;

	/** Unique identifier or name for this testing place */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Test|Memory", meta = (DisplayName = "Place / Zone ID"))
	FName PlaceId = FName("TestStation");

	/** When enabled, any NPC entering this zone remembers the location, affected stat, and change rate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Test|Memory", meta = (DisplayName = "NPCs Remember Place & Effect"))
	bool bRememberPlaceAndEffect = true;

	/** When enabled, this place and its effect are saved permanently to disk storage immediately */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Test|Memory", meta = (DisplayName = "Save Directly To Disk"))
	bool bSaveToDisk = false;

	/** Whether to draw in-world visual debug bounds and floating information label */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Test|Debug")
	bool bDrawDebugVisuals = true;

	/** Color of the debug boundary */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Test|Debug")
	FColor ZoneColor = FColor(0, 255, 128);

	/** Returns the currently overlapping NPCs */
	UFUNCTION(BlueprintPure, Category = "Simulation Test")
	int32 GetOverlappingNPCCount() const { return OverlappingNPCs.Num(); }

protected:
	UFUNCTION()
	void HandleBoxBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult
	);

	UFUNCTION()
	void HandleBoxEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex
	);

	void DrawZoneDebugDisplay();

private:
	/** NPCs currently inside the trigger box */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ANPCCharacter>> OverlappingNPCs;
};
