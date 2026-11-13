#include "Shop/ShopInteractionGeometry.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/SceneComponent.h"

double ShopInteractionGeometry::DistanceToStation(const AActor* Station,const FVector& Point)
{
	if (!IsValid(Station) || Point.ContainsNaN()) return TNumericLimits<double>::Max();
	double Distance=TNumericLimits<double>::Max();
	TArray<USceneComponent*> Points; Station->GetComponents(Points);
	for (const auto* Control:Points) if (Control->ComponentHasTag(TEXT("ShopInteractionPoint")))
		Distance=FMath::Min(Distance,FVector::Distance(Point,Control->GetComponentLocation()));
	return Distance==TNumericLimits<double>::Max() ? FVector::Distance(Point,Station->GetActorLocation()) : Distance;
}

double ShopInteractionGeometry::DistanceToCustomer(const AActor* Customer,const FVector& Point)
{
	if (!IsValid(Customer) || Point.ContainsNaN()) return TNumericLimits<double>::Max();
	if (const auto* Character=Cast<ACharacter>(Customer)) if (const auto* Capsule=Character->GetCapsuleComponent())
	{
		float Radius,HalfHeight; Capsule->GetScaledCapsuleSize(Radius,HalfHeight);
		const FVector Axis=Capsule->GetUpVector()*FMath::Max(0.f,HalfHeight-Radius), Center=Capsule->GetComponentLocation();
		return FMath::Max(0.,FMath::PointDistToSegment(Point,Center-Axis,Center+Axis)-Radius);
	}
	return FVector::Distance(Customer->GetActorLocation(),Point);
}

double ShopInteractionGeometry::CustomerDistanceToStation(const AActor* Customer,const AActor* Station)
{
	if (!IsValid(Station)) return TNumericLimits<double>::Max();
	double Distance=TNumericLimits<double>::Max();
	TArray<USceneComponent*> Points; Station->GetComponents(Points);
	for (const auto* Control:Points) if (Control->ComponentHasTag(TEXT("ShopCustomerInteractionPoint")))
		Distance=FMath::Min(Distance,DistanceToCustomer(Customer,Control->GetComponentLocation()));
	return Distance==TNumericLimits<double>::Max() ? DistanceToCustomer(Customer,Station->GetActorLocation()) : Distance;
}

FVector ShopInteractionGeometry::MeshGridSpacing(const UStaticMesh* Mesh,const FVector& Scale)
{
	const FVector Size=Mesh ? Mesh->GetBoundingBox().GetSize()*Scale.GetAbs() : FVector::ZeroVector;
	return Size*1.1+FVector(2);
}

FTransform ShopInteractionGeometry::GridItemTransform(const FTransform& Output,const FVector& CellSize,int32 Index,int32 Count,const FVector& ItemScale,int32 ColumnLimit,int32 RowLimit)
{
	const int32 Columns=FMath::Clamp(FMath::CeilToInt(FMath::Sqrt(float(FMath::Max(1,Count)))),1,FMath::Clamp(ColumnLimit,1,8));
	const int32 Rows=FMath::Clamp(FMath::DivideAndRoundUp(FMath::Max(1,Count),Columns),1,FMath::Clamp(RowLimit,1,16));
	const FVector Offset((Index%Columns-.5*(Columns-1))*CellSize.X,
		((Index/Columns)%Rows-.5*(Rows-1))*CellSize.Y,(Index/(Columns*Rows))*CellSize.Z);
	FTransform Result=Output;
	Result.SetScale3D(Output.GetScale3D()*ItemScale);
	Result.AddToTranslation(Output.TransformVectorNoScale(Offset));
	return Result;
}
