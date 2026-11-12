#include "Shop/ShopInstanceVolumeQuery.h"
#include "Props/PropInstanceVolumeQuery.h"

TSet<int32> ShopInstanceVolumeQuery::Query(const UBoxComponent* Box)
{
	return PropInstanceVolumeQuery::Query(Box);
}
