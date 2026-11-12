#pragma once
#include "CoreMinimal.h"
class UBoxComponent;
class APropManager;

/** Physical ISID query shared by prop activation volumes and shop trays/shelves. */
namespace PropInstanceVolumeQuery
{
	MARRYBYMIDNIGHT_API TSet<int32> Query(const UBoxComponent* Box, APropManager* Manager=nullptr);
}
