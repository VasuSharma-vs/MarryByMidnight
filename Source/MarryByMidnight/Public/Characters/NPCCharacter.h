#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "NPCEnumsAndTypes.h"
#include "Interfaces/WorldAffordanceInterface.h"
#include "NPCCharacter.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNPCPhysicsStateChanged, bool, bIsSimulating);

class UNPCStateComponent;
class UNPCNeedsComponent;
class UNPCMentalStateComponent;
class UNPCPersonalityComponent;
class UNPCWorldModelComponent;
class UNPCAffordanceComponent;
class UNPCStrategyComponent;
class UNPCDecisionComponent;
class UNPCPlannerComponent;
class UNPCRagdollComponent;
class UWidgetComponent;
class UTextRenderComponent;
class ANPCSimulationTestZone;
class ANPCOperableObject;


/**
 * Base C++ NPC Character for MarryByMidnight.
 * Pre-equipped with the complete physiological, psychological, and affordance planning components.
 * Serves as the C++ native parent class for BP_NPC.
 */
UCLASS()
class MARRYBYMIDNIGHT_API ANPCCharacter : public ACharacter, public IWorldAffordanceInterface
{
	GENERATED_BODY()

public:
	ANPCCharacter();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	void DrawDebugOverheadStats();

public:
	/** Core Simulation Components */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Components")
	TObjectPtr<UNPCStateComponent> StateComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Components")
	TObjectPtr<UNPCNeedsComponent> NeedsComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Components")
	TObjectPtr<UNPCMentalStateComponent> MentalStateComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Components")
	TObjectPtr<UNPCPersonalityComponent> PersonalityComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Components")
	TObjectPtr<UNPCWorldModelComponent> WorldModelComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Components")
	TObjectPtr<UNPCAffordanceComponent> AffordanceComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Components")
	TObjectPtr<UNPCStrategyComponent> StrategyComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Components")
	TObjectPtr<UNPCDecisionComponent> DecisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Components")
	TObjectPtr<UNPCPlannerComponent> PlannerComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Components")
	TObjectPtr<UNPCRagdollComponent> RagdollComponent;

	// IWorldAffordanceInterface implementation
	virtual bool CanInteract_Implementation(AActor* InstigatorActor) override;
	virtual void QueryAffordanceOptions_Implementation(AActor* InstigatorActor, TArray<FAffordanceOption>& OutOptions) override;
	virtual bool ExecuteAffordanceOption_Implementation(FName OptionId, AActor* InstigatorActor) override;

	/** Helper to query current mood tier */
	UFUNCTION(BlueprintPure, Category = "NPC|Getters")
	ENPCMoodTier GetCurrentMoodTier() const;

	/** Helper to query current archetype */
	UFUNCTION(BlueprintPure, Category = "NPC|Getters")
	ENPCArchetype GetArchetype() const;

	/** Floating overhead debug UI widget component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Debug")
	TObjectPtr<UWidgetComponent> DebugWidgetComponent;

	/** Floating overhead 3D world-space text component that naturally scales with camera distance */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Debug")
	TObjectPtr<UTextRenderComponent> DebugTextComponent;

	/** Whether the overhead debug UI is visible for this NPC */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Debug")
	bool bShowDebugUI = false;

	/** Enables or disables the overhead debug UI for this NPC */
	UFUNCTION(BlueprintCallable, Category = "NPC|Debug")
	void SetDebugUIEnabled(bool bEnable);

	/** Toggles the overhead debug UI for this NPC */
	UFUNCTION(BlueprintCallable, Category = "NPC|Debug")
	void ToggleDebugUI();

	/** Checks if the overhead debug UI is currently enabled */
	UFUNCTION(BlueprintPure, Category = "NPC|Debug")
	bool IsDebugUIEnabled() const { return bShowDebugUI; }

	/** Generates a human-readable multiline debug string showing all live stats */
	UFUNCTION(BlueprintPure, Category = "NPC|Debug")
	FString GetDebugStatsFormattedString() const;

	/** Toggles debug UI for all NPCs currently spawned in the world */
	UFUNCTION(BlueprintCallable, Category = "NPC|Debug", meta = (WorldContext = "WorldContextObject"))
	static void ToggleAllNPCDebugUI(const UObject* WorldContextObject);

	/** Enables or disables debug UI for all NPCs in the world */
	UFUNCTION(BlueprintCallable, Category = "NPC|Debug", meta = (WorldContext = "WorldContextObject"))
	static void SetAllNPCDebugUIEnabled(const UObject* WorldContextObject, bool bEnable);

	/** Instantly changes an internal simulation stat by DeltaAmount */
	UFUNCTION(BlueprintCallable, Category = "NPC|Simulation")
	void ApplySimulationStatDelta(ENPCSimulationStat Stat, float DeltaAmount);

	/** Changes an internal simulation stat over time based on RatePerSecond * DeltaSeconds */
	UFUNCTION(BlueprintCallable, Category = "NPC|Simulation")
	void ApplySimulationStatRate(ENPCSimulationStat Stat, float RatePerSecond, float DeltaSeconds);

	/** Notified when NPC enters a simulation zone */
	UFUNCTION(BlueprintCallable, Category = "NPC|Simulation")
	void NotifyEnteredSimulationZone(ANPCSimulationTestZone* Zone);

	/** Notified when NPC exits a simulation zone */
	UFUNCTION(BlueprintCallable, Category = "NPC|Simulation")
	void NotifyExitedSimulationZone(ANPCSimulationTestZone* Zone);

	/** Returns true if NPC is currently inside any simulation zone */
	UFUNCTION(BlueprintPure, Category = "NPC|Simulation")
	bool IsInSimulationZone() const { return ActiveSimulationZones.Num() > 0; }

	/** Checks if currently in a zone that recharges power */
	UFUNCTION(BlueprintPure, Category = "NPC|Simulation")
	bool IsInPowerRechargeZone() const;

	/** Whether the NPC is actively resting / stationary charging inside a simulation zone */
	UPROPERTY(ReplicatedUsing = OnRep_IsRestingInSimulationZone, VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Simulation")
	bool bIsRestingInSimulationZone = false;

	UFUNCTION()
	void OnRep_IsRestingInSimulationZone();

	/** Returns the active simulation zones in C++ */
	const TArray<TWeakObjectPtr<ANPCSimulationTestZone>>& GetActiveSimulationZones() const { return ActiveSimulationZones; }

	/** Returns valid active simulation zones for Blueprint */
	UFUNCTION(BlueprintPure, Category = "NPC|Simulation", meta = (DisplayName = "Get Active Simulation Zones"))
	TArray<ANPCSimulationTestZone*> GetActiveSimulationZonesBP() const;

	/** Currently operated machine or object (e.g. massage chair, vending machine) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Interaction")
	TWeakObjectPtr<ANPCOperableObject> CurrentOperatingObject;

	/** Replicated operating object for network clients */
	UPROPERTY(ReplicatedUsing = OnRep_OperatingObject, VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Interaction")
	TObjectPtr<ANPCOperableObject> ReplicatedOperatingObject;

	UFUNCTION()
	void OnRep_OperatingObject();

	/** Interacts with an operable object (vending machine, massage sofa, etc.) */
	UFUNCTION(BlueprintCallable, Category = "NPC|Interaction")
	bool InteractWithOperableObject(ANPCOperableObject* OperableObject, FName OfferingId);

	/** Stops operating the current machine/sofa */
	UFUNCTION(BlueprintCallable, Category = "NPC|Interaction")
	void StopInteractingWithOperableObject();

	/** Server RPC to request operating an object from client */
	UFUNCTION(Server, Reliable, WithValidation, Category = "NPC|Network")
	void Server_InteractWithOperableObject(ANPCOperableObject* OperableObject, FName OfferingId);

	/** Server RPC to stop operating an object from client */
	UFUNCTION(Server, Reliable, WithValidation, Category = "NPC|Network")
	void Server_StopInteractingWithOperableObject();

	// ---------------------------------------------------------
	// Inventory & Item Handling
	// ---------------------------------------------------------
	/** Items, tokens, and ingredients carried by the NPC */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Inventory")
	TMap<FGameplayTag, int32> ItemInventory;

	/** Replicated inventory array for network clients (synced from ItemInventory) */
	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedInventory, VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Inventory")
	TArray<FNPCItemRequirement> ReplicatedInventory;

	UFUNCTION()
	void OnRep_ReplicatedInventory();

	/** Adds item quantity to inventory */
	UFUNCTION(BlueprintCallable, Category = "NPC|Inventory")
	void AddInventoryItem(FGameplayTag ItemTag, int32 Quantity = 1);

	/** Removes item quantity from inventory; returns false if insufficient quantity */
	UFUNCTION(BlueprintCallable, Category = "NPC|Inventory")
	bool RemoveInventoryItem(FGameplayTag ItemTag, int32 Quantity = 1);

	/** Returns current count of an item tag in inventory */
	UFUNCTION(BlueprintPure, Category = "NPC|Inventory")
	int32 GetInventoryItemCount(FGameplayTag ItemTag) const;

	/** Checks if NPC possesses all required items and quantities */
	UFUNCTION(BlueprintPure, Category = "NPC|Inventory")
	bool HasRequiredItems(const TArray<FNPCItemRequirement>& Requirements) const;



	/** Event fired when the NPC enters or exits physics simulation / collapse */
	UPROPERTY(BlueprintAssignable, Category = "NPC|Physics")
	FOnNPCPhysicsStateChanged OnPhysicsStateChanged;

	/** Whether this NPC is currently simulating physics (collapsed / ragdoll), replicated across network */
	UPROPERTY(ReplicatedUsing = OnRep_IsSimulatingPhysics, VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Physics")
	bool bIsSimulatingPhysics = false;

	/** Handles replicated changes to physics simulation state on network clients */
	UFUNCTION()
	void OnRep_IsSimulatingPhysics();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Enables physics simulation on the NPC body (both SkeletalMesh and any StaticMesh like PSO_BaseMesh), stops movement and collapses to ground */
	UFUNCTION(BlueprintCallable, Category = "NPC|Physics")
	void StartPhysicsSimulation();

	/** Disables physics simulation, restores mesh attachment, capsule collision, and walking movement */
	UFUNCTION(BlueprintCallable, Category = "NPC|Physics")
	void StopPhysicsSimulation();

	/** Blueprint alias for StartPhysicsSimulation matching user terminology */
	UFUNCTION(BlueprintCallable, Category = "NPC|Physics")
	void StartRagdoll();

	/** Blueprint alias for StopPhysicsSimulation matching user terminology */
	UFUNCTION(BlueprintCallable, Category = "NPC|Physics")
	void StopRagdoll();

	/** Checks if the NPC is currently simulating physics / collapsed */
	UFUNCTION(BlueprintPure, Category = "NPC|Physics")
	bool IsSimulatingPhysics() const;

	/** Checks if ragdoll is active */
	UFUNCTION(BlueprintPure, Category = "NPC|Physics")
	bool IsRagdollActive() const;

protected:
	UPROPERTY()
	FVector MeshInitialRelativeLocation = FVector::ZeroVector;

	UPROPERTY()
	FRotator MeshInitialRelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ANPCSimulationTestZone>> ActiveSimulationZones;
};

