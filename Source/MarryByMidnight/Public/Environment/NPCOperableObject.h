#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NPCEnumsAndTypes.h"
#include "Interfaces/WorldAffordanceInterface.h"
#include "NPCOperableObject.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class UTextRenderComponent;
class ANPCCharacter;
class ANPCConsumableProp;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnOperableInteracted, ANPCOperableObject*, Object, AActor*, User, FName, OfferingId);

/**
 * Interactive world machine, appliance, or furniture (Vending Machines, Massage Sofas, Water Coolers, Food Counters).
 * Supports both:
 *  1. Proximity volume / aura mode (automatic over-time effects when inside volume).
 *  2. Direct interaction / affordance query mode (NPC queries requirements and offerings, pays money, receives stimuli or dispensed props).
 */
UCLASS(BlueprintType, Blueprintable)
class MARRYBYMIDNIGHT_API ANPCOperableObject : public AActor, public IWorldAffordanceInterface
{
	GENERATED_BODY()

public:
	ANPCOperableObject();

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
	TObjectPtr<UStaticMeshComponent> MachineMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> InteractionVolume;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SeatTransform;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTextRenderComponent> OverheadDebugText;

	// ---------------------------------------------------------
	// Configuration & Operating Modes
	// ---------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operable|Identity")
	FText MachineDisplayName = FText::FromString(TEXT("Vending Machine"));

	/** If true, overlapping NPCs automatically receive stimuli over time (e.g. relaxation zone or sauna) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operable|Mode")
	bool bOperatesAsVolumeAura = false;

	/** If true, requires explicit interaction / button press / affordance execution */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operable|Mode")
	bool bRequiresDirectInteraction = true;

	/** If true, this object is a computer or laptop terminal that allows uploading/downloading knowledge */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operable|Internet")
	bool bIsComputerTerminal = false;

	/** If true, this computer/laptop terminal has an active internet connection to the global network */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operable|Internet")
	bool bHasInternetConnection = true;


	/** Maximum concurrent users allowed (e.g. 1 for massage chair, unlimited or high for vending machine) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operable|Occupancy", meta = (ClampMin = "1"))
	int32 MaxConcurrentUsers = 1;

	/** Offset from actor location where physical props (cans, food) are spawned */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operable|Dispenser")
	FVector DispenserSpawnOffset = FVector(60.0f, 0.0f, -20.0f);

	/** Available offerings provided by this machine / object */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operable|Offerings")
	TArray<FNPCOperableOffering> AvailableOfferings;

	/** Currently active continuous occupants (e.g. sitting in massage chair) */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> CurrentOccupants;

	/** Overlapping actors in volume aura mode */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> OverlappingActors;

	/** Returns current occupants for Blueprint */
	UFUNCTION(BlueprintPure, Category = "Operable|Occupancy", meta = (DisplayName = "Get Current Occupants"))
	TArray<AActor*> GetCurrentOccupantsBP() const;

	/** Returns overlapping actors for Blueprint */
	UFUNCTION(BlueprintPure, Category = "Operable|Occupancy", meta = (DisplayName = "Get Overlapping Actors"))
	TArray<AActor*> GetOverlappingActorsBP() const;


	/** Event fired when an interaction or purchase completes */
	UPROPERTY(BlueprintAssignable, Category = "Operable|Events")
	FOnOperableInteracted OnOperableInteracted;

	// ---------------------------------------------------------
	// Core Operations
	// ---------------------------------------------------------
	/** Checks if an NPC can afford the specified offering */
	UFUNCTION(BlueprintPure, Category = "Operable")
	bool CanNPCAffordOffering(const AActor* NPCActor, const FNPCOperableOffering& Offering) const;

	/** Starts operating a chosen offering on this machine */
	UFUNCTION(BlueprintCallable, Category = "Operable")
	bool StartOperatingOffering(AActor* UserActor, FName OfferingId, FString& OutFailureReason);

	/** Replicated occupants for network clients */
	UPROPERTY(ReplicatedUsing = OnRep_Occupants, VisibleAnywhere, BlueprintReadOnly, Category = "Operable|Occupancy")
	TArray<TObjectPtr<AActor>> ReplicatedOccupants;

	UFUNCTION()
	void OnRep_Occupants();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server RPC to request operating an offering from a client */
	UFUNCTION(Server, Reliable, WithValidation, Category = "Operable|Network")
	void Server_StartOperatingOffering(AActor* UserActor, FName OfferingId);

	/** Server RPC to stop operating an offering from a client */
	UFUNCTION(Server, Reliable, WithValidation, Category = "Operable|Network")
	void Server_StopOperatingOffering(AActor* UserActor);

	/** Multicast to notify all clients when an offering is successfully operated */
	UFUNCTION(NetMulticast, Unreliable, Category = "Operable|Network")
	void Multicast_OnOfferingOperated(AActor* UserActor, FName OfferingId);

	/** Stops operating (e.g. gets up from massage sofa) */
	UFUNCTION(BlueprintCallable, Category = "Operable")
	void StopOperatingOffering(AActor* UserActor);

	/** Returns true if the machine is at full user capacity */
	UFUNCTION(BlueprintPure, Category = "Operable")
	bool IsFull() const { return CurrentOccupants.Num() >= MaxConcurrentUsers; }

	/** Finds an offering by its OfferingId */
	UFUNCTION(BlueprintPure, Category = "Operable")
	bool FindOffering(FName OfferingId, FNPCOperableOffering& OutOffering) const;

	// ---------------------------------------------------------
	// Volume Overlap Handlers
	// ---------------------------------------------------------
	UFUNCTION()
	void OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnVolumeEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	// ---------------------------------------------------------
	// IWorldAffordanceInterface
	// ---------------------------------------------------------
	virtual bool CanInteract_Implementation(AActor* InstigatorActor) override;
	virtual void QueryAffordanceOptions_Implementation(AActor* InstigatorActor, TArray<FAffordanceOption>& OutOptions) override;
	virtual bool ExecuteAffordanceOption_Implementation(FName OptionId, AActor* InstigatorActor) override;

	/** Refreshes in-world 3D debug info */
	void UpdateDebugBillboard();

private:
	void SetupDefaultVendingOfferings();
};
