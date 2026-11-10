#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Props/InteractablePropDatabase.h"
#include "FragmentAssemblyDataAsset.generated.h"

class UInteractablePropDataAsset;
class APropManager;

USTRUCT(BlueprintType)
struct FFragmentAssemblyPiece
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UInteractablePropDataAsset> Product;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FTransform LocalTransform = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector ScatterDirection = FVector::UpVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0")) float MinSpeed = 120;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0")) float MaxSpeed = 380;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", ClampMax = "1")) float VelocityInheritance = .65f;
};

/** References product assets, allowing the manager to allocate collision-free PIDs per world. */
UCLASS(BlueprintType)
class MARRYBYMIDNIGHT_API UFragmentAssemblyDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FFragmentAssemblyPiece> Pieces;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Fragments")
	bool BuildRecipe(APropManager* Manager, TArray<FPropFractureFragmentEntry>& Recipe) const;
};
