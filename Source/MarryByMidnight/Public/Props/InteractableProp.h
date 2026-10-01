#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NPCEnumsAndTypes.h"
#include "Interfaces/WorldAffordanceInterface.h"
#include "InteractableProp.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class UStaticMeshPhysicsSimulationComponent;
class ANPCCharacter;
class APawn;

/**
 * Universal interactive world object & prop (food, drinks, items, tools, grabbables).
 * Interacted with by both Players and NPCs via distinct interaction pathways:
 * - Players: Direct physical grab/hold/drop/throw and player interaction interface.
 * - NPCs: Evaluates via AI perception, memory, affordances, hunger/thirst satisfaction, taste profiling.
 * Integrated with UStaticMeshPhysicsSimulationComponent (At Rest, In Motion, Held, Settling, Unsettled).
 */
UCLASS(BlueprintType, Blueprintable)
class MARRYBYMIDNIGHT_API AInteractableProp : public AActor, public IWorldAffordanceInterface
{
	GENERATED_BODY()

public:
	AInteractableProp();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	// ---------------------------------------------------------
	// Components
	// ---------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> PropMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTextRenderComponent> DebugText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshPhysicsSimulationComponent> PhysicsSimComponent;

	// ---------------------------------------------------------
	// Interaction & Grabbability
	// ---------------------------------------------------------
	/** Whether this prop is currently grabbable by player or NPC (Instance editable & exposed on spawn) */
	UPROPERTY(ReplicatedUsing = OnRep_IsGrabbable, EditAnywhere, BlueprintReadWrite, meta = (ExposeOnSpawn = "true", DisplayName = "Is Grabable"), Category = "Interaction")
	bool bIsGrabbable = true;

	UFUNCTION()
	void OnRep_IsGrabbable();

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void SetGrabbable(bool bNewGrabbable);

	/** Retriggers/resets the unsettled timer back to 3.0s (or custom delay) */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void RetriggerUnsettled(float NewDelay = -1.0f);

	// ---------------------------------------------------------
	// Player Interaction Pathway
	// ---------------------------------------------------------
	/** Base interaction triggered by a Player Character / Controller */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction|Player")
	bool InteractWithPlayer(APawn* PlayerPawn);
	virtual bool InteractWithPlayer_Implementation(APawn* PlayerPawn);

	/** Triggered when a player grabs this prop into hands/socket */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction|Player")
	bool OnPlayerGrab(USceneComponent* AttachToParent, FName SocketName = NAME_None);
	virtual bool OnPlayerGrab_Implementation(USceneComponent* AttachToParent, FName SocketName = NAME_None);

	/** Triggered when a player drops or throws this prop */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction|Player")
	bool OnPlayerRelease(FVector LaunchVelocity = FVector::ZeroVector);
	virtual bool OnPlayerRelease_Implementation(FVector LaunchVelocity = FVector::ZeroVector);

	// ---------------------------------------------------------
	// NPC Interaction Pathway
	// ---------------------------------------------------------
	/** Base interaction triggered by an NPC character */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction|NPC")
	bool InteractWithNPC(ANPCCharacter* NPCCharacter, FName ActionName = NAME_None);
	virtual bool InteractWithNPC_Implementation(ANPCCharacter* NPCCharacter, FName ActionName = NAME_None);

	// ---------------------------------------------------------
	// Consumable & Physical Properties
	// ---------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Identity")
	FText ItemDisplayName = FText::FromString(TEXT("Cold Drink Can"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Identity")
	EConsumablePropType ConsumableType = EConsumablePropType::Drink;

	/** Monetary purchase price or value */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "Consumable|Economy")
	float Price = 2.0f;

	/** Temperature / Coldness [0.0 = Warm / Ambient, 1.0 = Ice Cold] */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "Consumable|Physical", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Coldness = 0.85f;

	/** Expiry freshness [1.0 = Fresh, 0.0 = Expired / Rotten] */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "Consumable|Freshness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExpiryFreshness = 1.0f;

	/** Rate at which freshness decays per minute */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Freshness")
	float ExpiryRatePerMinute = 0.02f;

	/** Energy / caloric value directly boosting internal Power stat */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Physical")
	float EnergyLevel = 10.0f;

	/** Quantity level fraction [0.0 = Empty, 1.0 = Full] */
	UPROPERTY(ReplicatedUsing = OnRep_QuantityLevel, VisibleAnywhere, BlueprintReadOnly, Category = "Consumable|Quantity", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float QuantityLevel = 1.0f;

	UFUNCTION()
	void OnRep_QuantityLevel();

	/** Maximum total portions (sips or bites) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Quantity", meta = (ClampMin = "1"))
	int32 MaxPortions = 4;

	/** Current remaining portions */
	UPROPERTY(ReplicatedUsing = OnRep_RemainingPortions, EditAnywhere, BlueprintReadWrite, Category = "Consumable|Quantity", meta = (ClampMin = "0"))
	int32 RemainingPortions = 4;

	UFUNCTION()
	void OnRep_RemainingPortions();

	/** Multi-dimensional taste profile */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Taste")
	FNPCTasteVector TasteProfile;

	/** Stimuli modifiers applied per portion consumed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Stimuli")
	TArray<FNPCSimulationStatModifier> StimuliPerPortion;

	/** Whether this prop is automatically destroyed when completely consumed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable")
	bool bDestroyWhenEmpty = true;

	// ---------------------------------------------------------
	// Consumption Logic & Networking
	// ---------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Consumable")
	bool ConsumePortion(AActor* ConsumerActor, float PortionRatio = 1.0f);

	UFUNCTION(BlueprintPure, Category = "Consumable")
	bool IsEmpty() const { return RemainingPortions <= 0 || QuantityLevel <= 0.0f; }

	UFUNCTION(BlueprintPure, Category = "Consumable")
	bool IsSpoiled() const { return ExpiryFreshness <= 0.25f; }

	UFUNCTION(Server, Reliable, WithValidation, Category = "Consumable|Network")
	void Server_ConsumePortion(AActor* ConsumerActor, float PortionRatio = 1.0f);

	UFUNCTION(NetMulticast, Unreliable, Category = "Consumable|Network")
	void Multicast_OnPortionConsumed(AActor* ConsumerActor, int32 PortionsLeft);

	// ---------------------------------------------------------
	// IWorldAffordanceInterface
	// ---------------------------------------------------------
	virtual bool CanInteract_Implementation(AActor* InstigatorActor) override;
	virtual void QueryAffordanceOptions_Implementation(AActor* InstigatorActor, TArray<FAffordanceOption>& OutOptions) override;
	virtual bool ExecuteAffordanceOption_Implementation(FName OptionId, AActor* InstigatorActor) override;

	void UpdateDebugBillboard();

private:
	void InitializeDefaultDrinkStimuli();
};
