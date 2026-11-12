#pragma once
#include "CoreMinimal.h"
class UBoxComponent;

/** Canonical physical identity query for station trays, shelves, scanners and cash volumes. */
namespace ShopInstanceVolumeQuery
{
	MARRYBYMIDNIGHT_API TSet<int32> Query(const UBoxComponent* Box);
}
