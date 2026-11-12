#include "Props/PropActivationVolume.h"
#include "Components/PropActivationVolumeComponent.h"

APropActivationVolume::APropActivationVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	ActivationVolume = CreateDefaultSubobject<UPropActivationVolumeComponent>(TEXT("ActivationVolume"));
	SetRootComponent(ActivationVolume);
}
