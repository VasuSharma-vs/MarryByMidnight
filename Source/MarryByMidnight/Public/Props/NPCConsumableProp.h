#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NPCEnumsAndTypes.h"
#include "Interfaces/WorldAffordanceInterface.h"
#include "NPCConsumableProp.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class ANPCCharacter;

/**
 * Physical consumable item in the world (or held in inventory / hands).
 * Represents drinks, food, alcohol, snacks, etc.
 * Holds properties like coldness, price, expiry date/freshness, energy level, quantity level, taste, and stimuli modifiers.
 */
UCLASS(BlueprintType, Blueprintable)
class MARRYBYMIDNIGHT_API ANPCConsumableProp : public AActor, public IWorldAffordanceInterface
{
	GENERATED_BODY()

public:
	ANPCConsumableProp();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

public:
	// ---------------------------------------------------------
	// Components
	// ---------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> PropMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTextRenderComponent> DebugText;

	// ---------------------------------------------------------
	// Consumable Identity & Physical Properties
	// ---------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Identity")
	FText ItemDisplayName = FText::FromString(TEXT("Cold Drink Can"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Identity")
	EConsumablePropType ConsumableType = EConsumablePropType::Drink;

	/** Monetary purchase price or resale value (e.g. $2.00) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Economy")
	float Price = 2.0f;

	/** Temperature / Coldness [0.0 = Warm / Ambient, 1.0 = Ice Cold] */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "Consumable|Physical", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Coldness = 0.85f;

	/** Expiry freshness [1.0 = Factory Fresh, 0.0 = Expired / Rotten] */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "Consumable|Freshness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExpiryFreshness = 1.0f;

	/** Rate at which freshness decays per minute (e.g. 0.02 = decays over 50 min) */
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

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server RPC to request consuming a portion from a client */
	UFUNCTION(Server, Reliable, WithValidation, Category = "Consumable|Network")
	void Server_ConsumePortion(AActor* ConsumerActor, float PortionRatio = 1.0f);

	/** Multicast to notify all clients when a portion is consumed */
	UFUNCTION(NetMulticast, Unreliable, Category = "Consumable|Network")
	void Multicast_OnPortionConsumed(AActor* ConsumerActor, int32 PortionsLeft);

	/** Multi-dimensional taste profile (Sweet, Salty, Sour, Bitter, Spicy, Meat, Vegetarian, Healthy, Luxury) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Taste")
	FNPCTasteVector TasteProfile;

	/** Stimuli modifiers applied per portion consumed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Stimuli")
	TArray<FNPCSimulationStatModifier> StimuliPerPortion;

	/** Whether this prop is automatically destroyed when completely consumed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable")
	bool bDestroyWhenEmpty = true;

	// ---------------------------------------------------------
	// Consumption Logic
	// ---------------------------------------------------------
	/**
	 * Consumes one portion (sip/bite) of this item.
	 * Applies stimuli deltas, taste compatibility dopamine, freshness sickness checks, and updates quantity.
	 */
	UFUNCTION(BlueprintCallable, Category = "Consumable")
	bool ConsumePortion(AActor* ConsumerActor, float PortionRatio = 1.0f);

	/** Returns true if this item is completely empty */
	UFUNCTION(BlueprintPure, Category = "Consumable")
	bool IsEmpty() const { return RemainingPortions <= 0 || QuantityLevel <= 0.0f; }

	/** Returns true if this item has spoiled / passed its expiration */
	UFUNCTION(BlueprintPure, Category = "Consumable")
	bool IsSpoiled() const { return ExpiryFreshness <= 0.25f; }

	// ---------------------------------------------------------
	// IWorldAffordanceInterface
	// ---------------------------------------------------------
	virtual bool CanInteract_Implementation(AActor* InstigatorActor) override;
	virtual void QueryAffordanceOptions_Implementation(AActor* InstigatorActor, TArray<FAffordanceOption>& OutOptions) override;
	virtual bool ExecuteAffordanceOption_Implementation(FName OptionId, AActor* InstigatorActor) override;

	/** Refreshes the in-world 3D debug billboard */
	void UpdateDebugBillboard();

private:
	void InitializeDefaultDrinkStimuli();
};
