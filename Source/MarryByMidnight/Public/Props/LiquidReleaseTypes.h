#pragma once
#include "CoreMinimal.h"
#include "LiquidReleaseTypes.generated.h"

/** Accounted liquid outside its original container; quantity is never rounded to whole portions. */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FReleasedLiquid
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, SaveGame) FGuid ReleaseID;
	UPROPERTY(BlueprintReadOnly, SaveGame) int32 SourceISID = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame) int32 SourceProductID = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame) int32 ContentID = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame) double Portions = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame) float Temperature = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame) double ContentCreationTime = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame) double ReleaseTime = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame) FTransform Transform;
};
