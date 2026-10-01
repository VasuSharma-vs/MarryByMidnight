#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Props/InteractablePropDatabase.h"
#include "InteractableInstancedStaticMeshComponent.generated.h"

class UInteractablePropDataAsset;
class ANPCCharacter;
class APawn;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnInstancePortionConsumed, int32, InstanceIndex, int32, RemainingPortions, AActor*, ConsumerActor);

/**
 * Custom Instanced Static Mesh Component integrated with a replicated Interactable Prop Database.
 * Allows thousands of instanced food, drinks, and interactable props to be rendered efficiently with
 * individual per-instance tracking of consumable temperature, current temperature, remaining sips/portions,
 * grabbability, simulation states, velocity tolerance, and stimuli.
 */
UCLASS(ClassGroup = (Rendering, Common), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UInteractableInstancedStaticMeshComponent : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	UInteractableInstancedStaticMeshComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	/** Default Data Asset used to initialize instances that lack custom database entries */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interactable Prop Database")
	TObjectPtr<UInteractablePropDataAsset> DefaultDataAsset;

	/** Replicated per-instance interactable prop database */
	UPROPERTY(ReplicatedUsing = OnRep_PropDatabase, EditAnywhere, BlueprintReadWrite, Category = "Interactable Prop Database")
	TArray<FInteractablePropData> PropDatabase;

	UFUNCTION()
	void OnRep_PropDatabase();

	/** Fired when an instance's portions are consumed */
	UPROPERTY(BlueprintAssignable, Category = "Interactable Prop Database|Events")
	FOnInstancePortionConsumed OnInstancePortionConsumed;

	/** Adds an interactable instance with associated prop database entry */
	UFUNCTION(BlueprintCallable, Category = "Interactable Prop Database")
	int32 AddInteractableInstance(const FTransform& InstanceTransform, const FInteractablePropData& InitialData);

	/** Removes an instance and its database entry */
	UFUNCTION(BlueprintCallable, Category = "Interactable Prop Database")
	bool RemoveInteractableInstance(int32 InstanceIndex);

	/** Gets prop data for a specific instance index */
	UFUNCTION(BlueprintPure, Category = "Interactable Prop Database")
	bool GetPropDataForInstance(int32 InstanceIndex, FInteractablePropData& OutData) const;

	/** Updates prop data for a specific instance index */
	UFUNCTION(BlueprintCallable, Category = "Interactable Prop Database")
	bool SetPropDataForInstance(int32 InstanceIndex, const FInteractablePropData& NewData);

	/**
	 * Consumes portions (e.g. player drinks 2 sips out of 4 sips).
	 * Updates the database entry remaining portions, quantity level, and applies stimuli/temperature effects to consumer.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interactable Prop Database")
	bool ConsumeInstancePortions(int32 InstanceIndex, int32 SipsToConsume, AActor* ConsumerActor, int32& OutSipsConsumed);

	/** Updates temperature for a specific instance toward ambient */
	UFUNCTION(BlueprintCallable, Category = "Interactable Prop Database")
	void UpdateInstanceTemperature(int32 InstanceIndex, float AmbientTemperature, float DeltaSeconds);

	/** Updates the simulation state for an instance */
	UFUNCTION(BlueprintCallable, Category = "Interactable Prop Database")
	void SetInstanceSimulationState(int32 InstanceIndex, EPhysicsSimulationState NewState);

	/** Updates the grabbable flag for an instance */
	UFUNCTION(BlueprintCallable, Category = "Interactable Prop Database")
	void SetInstanceGrabbable(int32 InstanceIndex, bool bNewGrabbable);

	/** Ensures PropDatabase array matches the instance count, filling any missing entries from DefaultDataAsset */
	UFUNCTION(BlueprintCallable, Category = "Interactable Prop Database")
	void SyncDatabaseWithInstanceCount();

	/** Applies a Data Asset to create an initial FInteractablePropData entry */
	UFUNCTION(BlueprintPure, Category = "Interactable Prop Database")
	FInteractablePropData CreatePropDataFromAsset(const UInteractablePropDataAsset* InAsset, int32 InInstanceId = -1) const;
};
