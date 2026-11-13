#pragma once
#include "CoreMinimal.h"
class AActor;
class UStaticMesh;
namespace ShopInteractionGeometry
{
	COLOSSALFIZZCO_API double DistanceToStation(const AActor* Station,const FVector& Point);
	COLOSSALFIZZCO_API double CustomerDistanceToStation(const AActor* Customer,const AActor* Station);
	/** Distance from a world point to a character's actual capsule; other actors use their origin. */
	COLOSSALFIZZCO_API double DistanceToCustomer(const AActor* Customer,const FVector& Point);
	/** Grid cell size in output-local directions, accounting for the final world mesh scale. */
	COLOSSALFIZZCO_API FVector MeshGridSpacing(const UStaticMesh* Mesh,const FVector& Scale);
	/** A centered output grid keeps large deliveries separate without shrinking their meshes. */
	COLOSSALFIZZCO_API FTransform GridItemTransform(const FTransform& Output,const FVector& CellSize,int32 Index,int32 Count,const FVector& ItemScale=FVector::OneVector,int32 ColumnLimit=8,int32 RowLimit=8);
}
