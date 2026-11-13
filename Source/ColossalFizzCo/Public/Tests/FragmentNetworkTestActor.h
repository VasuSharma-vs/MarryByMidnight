#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FragmentNetworkTestActor.generated.h"

class APropManager;

/** Instrumentation for the isolated FragmentReplication test map; observations are local, not replicated. */
UCLASS(NotBlueprintable)
class COLOSSALFIZZCO_API AFragmentNetworkTestActor : public AActor
{
	GENERATED_BODY()
public:
	AFragmentNetworkTestActor();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** 1 = fracture another bottle; 3 = wake first settled fragment; 4 = allow it to settle. */
	UPROPERTY(EditAnywhere, Category = "Test") int32 Command = 0;
	UPROPERTY(Replicated, VisibleAnywhere, Category = "Test") int32 Stage = 0;
	UPROPERTY(Replicated, VisibleAnywhere, Category = "Test") int32 SourceISID = 0;
	UPROPERTY(Replicated, VisibleAnywhere, Category = "Test") TArray<int32> FragmentISIDs;
	UPROPERTY(Replicated, VisibleAnywhere, Category = "Test") TArray<int32> PreviousFragmentISIDs;
	UPROPERTY(VisibleAnywhere, Category = "Test") int32 LocalRecordCount = 0;
	UPROPERTY(VisibleAnywhere, Category = "Test") int32 LocalOriginalCount = 0;
	UPROPERTY(VisibleAnywhere, Category = "Test") int32 LocalISMCount = 0;
	UPROPERTY(VisibleAnywhere, Category = "Test") int32 LocalASMCount = 0;
	UPROPERTY(VisibleAnywhere, Category = "Test") int32 LocalVisibleOriginalCount = 0;
	UPROPERTY(VisibleAnywhere, Category = "Test") int32 LocalSimulatingOriginalCount = 0;
	UPROPERTY(VisibleAnywhere, Category = "Test") int32 LocalVisibleISMCount = 0;
	UPROPERTY(VisibleAnywhere, Category = "Test") int32 LocalDedicatedActorCount = 0;
	UPROPERTY(VisibleAnywhere, Category = "Test") TArray<FVector> LocalFragmentPositions;
	UPROPERTY(VisibleAnywhere, Category = "Test") int32 LocalPreviousRestCount = 0;
	UPROPERTY(VisibleAnywhere, Category = "Test") int32 LocalSlotChanges = 0;
	UPROPERTY(VisibleAnywhere, Category = "Test") int32 LocalReleasedLiquidCount = 0;
	UPROPERTY(VisibleAnywhere, Category = "Test") double LocalReleasedPortions = 0;
	UPROPERTY(VisibleAnywhere, Category = "Test") bool bLocalSourcePresent = false;
	UPROPERTY(VisibleAnywhere, Category = "Test") int32 LocalPlayerCount = 0;
	UPROPERTY(VisibleAnywhere, Category = "Test") FString LastFailure;
private:
	UPROPERTY(Transient) TObjectPtr<APropManager> Manager;
	TMap<int32, int32> InitialSlots;
};
