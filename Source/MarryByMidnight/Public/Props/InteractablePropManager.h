#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Props/InteractablePropDatabase.h"
#include "InteractablePropManager.generated.h"

class AInteractableProp;
class UInteractableInstancedStaticMeshComponent;
class UStaticMeshPhysicsSimulationComponent;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSettlingObjectRested, UStaticMeshPhysicsSimulationComponent*, SimComp, AActor*, OwnerActor);

/**
 * Central Prop Manager managing world interactable props and instanced static meshes.
 * Features:
 * 1. Batched / Distributed Tick: Dispatches temperature updates one object per frame across registered props, eliminating per-prop tick cost.
 * 2. Mesh Query API: Answers player and NPC queries on whether any static mesh or ISM instance is interactable and returns its properties.
 * 3. Asynchronous / Batched Settling Queue: Monitors physics simulation components in the Settling state and transitions them to At Rest when linear speed falls below tolerance.
 */
UCLASS(BlueprintType, Blueprintable)
class MARRYBYMIDNIGHT_API AInteractablePropManager : public AActor
{
	GENERATED_BODY()

public:
	AInteractablePropManager();

	/** Static helper to obtain the active Prop Manager in the world */
	UFUNCTION(BlueprintPure, Category = "Prop Manager", meta = (WorldContext = "WorldContextObject"))
	static AInteractablePropManager* Get(const UObject* WorldContextObject);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

public:
	// -------------------------------------------------------------
	// Environment Settings
	// -------------------------------------------------------------
	/** Ambient environment temperature in Celsius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Environment", meta = (Units = "Celsius"))
	float AmbientTemperature = 21.0f;

	/** Number of props to update per frame in the distributed round-robin loop (default: 1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Performance", meta = (ClampMin = "1"))
	int32 PropsUpdatedPerFrame = 1;

	/** Interval in seconds for checking settling objects (default: 0.05s / 20 Hz) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Performance", meta = (ClampMin = "0.01"))
	float SettlingCheckInterval = 0.05f;

	/** Whether to evaluate settling object velocities on an asynchronous background task */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop Manager|Performance")
	bool bUseAsyncSettlingCheck = true;

	// -------------------------------------------------------------
	// Events
	// -------------------------------------------------------------
	UPROPERTY(BlueprintAssignable, Category = "Prop Manager|Events")
	FOnSettlingObjectRested OnSettlingObjectRested;

	// -------------------------------------------------------------
	// Registration
	// -------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Registration")
	void RegisterProp(AInteractableProp* Prop);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Registration")
	void UnregisterProp(AInteractableProp* Prop);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Registration")
	void RegisterISMComponent(UInteractableInstancedStaticMeshComponent* ISMComp);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Registration")
	void UnregisterISMComponent(UInteractableInstancedStaticMeshComponent* ISMComp);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Registration")
	void RegisterSettlingComponent(UStaticMeshPhysicsSimulationComponent* SimComp);

	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Registration")
	void UnregisterSettlingComponent(UStaticMeshPhysicsSimulationComponent* SimComp);

	// -------------------------------------------------------------
	// Interaction Query API (Player & NPC)
	// -------------------------------------------------------------
	/**
	 * Queries whether a hit primitive component or ISM instance is an interactable prop.
	 * Returns true if interactable, outputting whether it is grabbable and its full prop data.
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Interaction")
	bool CanInteractWithMesh(UPrimitiveComponent* TargetComponent, int32 InstanceIndex, AActor* InstigatorActor, bool& bOutIsGrabbable, FInteractablePropData& OutData);

	/**
	 * Executes an interaction on a target component or ISM instance (e.g. drink 2 sips, grab, or inspect).
	 */
	UFUNCTION(BlueprintCallable, Category = "Prop Manager|Interaction")
	bool InteractWithMesh(UPrimitiveComponent* TargetComponent, int32 InstanceIndex, AActor* InstigatorActor, FName ActionName, int32 PortionsToConsume, FInteractablePropData& OutUpdatedData);

	/** Returns total registered props count */
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Stats")
	int32 GetRegisteredPropsCount() const { return RegisteredProps.Num(); }

	/** Returns total registered ISM components count */
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Stats")
	int32 GetRegisteredISMCount() const { return RegisteredISMComponents.Num(); }

	/** Returns number of objects currently actively settling */
	UFUNCTION(BlueprintPure, Category = "Prop Manager|Stats")
	int32 GetActiveSettlingCount() const { return ActiveSettlingComponents.Num(); }

protected:
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AInteractableProp>> RegisteredProps;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UInteractableInstancedStaticMeshComponent>> RegisteredISMComponents;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UStaticMeshPhysicsSimulationComponent>> ActiveSettlingComponents;

	int32 CurrentPropUpdateIndex = 0;
	int32 CurrentISMUpdateIndex = 0;
	int32 CurrentISMInstanceIndex = 0;

	FTimerHandle SettlingTimerHandle;

	void UpdateDistributedProps(float DeltaSeconds);
	void UpdateDistributedISM(float DeltaSeconds);
	void CheckSettlingQueue();
	void ProcessSettlingQueueAsync();
};
