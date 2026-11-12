#pragma once
#include "CoreMinimal.h"
class AActor;
class UStaticMesh;
namespace ShopInteractionGeometry
{
	MARRYBYMIDNIGHT_API double DistanceToStation(const AActor* Station,const FVector& Point);
	MARRYBYMIDNIGHT_API double CustomerDistanceToStation(const AActor* Customer,const AActor* Station);
	/** Distance from a world point to a character's actual capsule; other actors use their origin. */
	MARRYBYMIDNIGHT_API double DistanceToCustomer(const AActor* Customer,const FVector& Point);
	/** Grid cell size in output-local directions, accounting for the final world mesh scale. */
	MARRYBYMIDNIGHT_API FVector MeshGridSpacing(const UStaticMesh* Mesh,const FVector& Scale);
	/** A centered output grid keeps large deliveries separate without shrinking their meshes. */
	MARRYBYMIDNIGHT_API FTransform GridItemTransform(const FTransform& Output,const FVector& CellSize,int32 Index,int32 Count,const FVector& ItemScale=FVector::OneVector,int32 ColumnLimit=8,int32 RowLimit=8);
}
