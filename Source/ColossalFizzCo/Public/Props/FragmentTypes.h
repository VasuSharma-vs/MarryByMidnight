#pragma once

#include "CoreMinimal.h"
#include "FragmentTypes.generated.h"

/** Original components use the manager's channel, never per-component actor replication. */
USTRUCT()
struct FFragmentPhysicsSync
{
	GENERATED_BODY()
	UPROPERTY() int32 ISID = 0;
	UPROPERTY() int32 Version = 0;
	UPROPERTY() FGuid GroupID;
	UPROPERTY() double WorldTime = 0;
	UPROPERTY() FTransform Transform;
};
