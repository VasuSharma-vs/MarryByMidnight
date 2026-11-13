#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PropActivationVolume.generated.h"

class UPropActivationVolumeComponent;

/** Optional native placement actor; the same component can be added to any existing actor. */
UCLASS()
class COLOSSALFIZZCO_API APropActivationVolume : public AActor
{
	GENERATED_BODY()
public:
	APropActivationVolume();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prop Activation")
	TObjectPtr<UPropActivationVolumeComponent> ActivationVolume;
};
