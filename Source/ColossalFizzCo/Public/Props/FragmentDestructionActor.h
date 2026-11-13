#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Props/InteractableProp.h"
#include "FragmentDestructionActor.generated.h"

class UStaticMeshComponent;
class UStaticMeshPhysicsSimulationComponent;

/** Class-specific pool identity for interactions after a fragment's initial settlement. */
UCLASS(BlueprintType, Blueprintable)
class COLOSSALFIZZCO_API AFragmentPropActor : public AActivePropActor
{
	GENERATED_BODY()
};

/** Original multi-component carrier. Logical pieces and their ISM slots belong to the manager. */
UCLASS(BlueprintType)
class COLOSSALFIZZCO_API AFragmentDestructionActor : public AActor
{
	GENERATED_BODY()
public:
	AFragmentDestructionActor();
	void BeginGroup(APropManager* Manager, FGuid GroupID);
	bool PreparePiece(int32 PieceIndex, const FPropProductDefinition& Product, const FTransform& Transform);
	void ActivatePiece(int32 PieceIndex, int32 ISID, const FVector& Velocity, const FVector& AngularVelocity);
	void ReleasePiece(int32 ISID);
	void ResetForPool();
	UStaticMeshComponent* FindPiece(int32 ISID) const;
	int32 FindISID(const UPrimitiveComponent* Component) const;
	bool IsPieceSettled(int32 ISID) const;
	void CheckSettlement(float DeltaSeconds);
	void SetPoseTarget(int32 ISID, const FTransform& Transform);
	int32 GetActivePieceCount() const { return PieceIDs.Num(); }
	FGuid GetGroupID() const { return GroupID; }
	virtual void Tick(float DeltaSeconds) override;
private:
	UPROPERTY(Transient) TObjectPtr<APropManager> PropManager;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Meshes;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshPhysicsSimulationComponent>> SettlementProbes;
	FGuid GroupID;
	TMap<int32, int32> PieceIDs;
	TMap<int32, FTransform> PoseTargets;
	TMap<int32, float> Elapsed;
	TMap<int32, float> Stationary;
};
