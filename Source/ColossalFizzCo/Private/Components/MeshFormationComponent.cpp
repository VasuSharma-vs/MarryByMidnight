#include "Components/MeshFormationComponent.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractablePropDatabase.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Async/Async.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

namespace MeshFormationInternal
{
	/**
	 * Automatically calculates how many objects fit around a circle of RingRadiusInSteps * RadialStep
	 * so adjacent objects along the circle have chord distance >= RadialStep (never overlapping).
	 */
	FORCEINLINE int32 ComputeAutoRingItemCountForFloatRadius(float RingRadiusInSteps)
	{
		if (RingRadiusInSteps < 0.5f)
		{
			return 1;
		}
		const float Ratio = FMath::Clamp(0.5f / RingRadiusInSteps, 0.0001f, 1.0f);
		const int32 MaxNonOverlappingItems = FMath::FloorToInt(PI / FMath::Asin(Ratio));
		return FMath::Max(1, MaxNonOverlappingItems);
	}

	FORCEINLINE int32 ComputeAutoRingItemCount(int32 RingIndex)
	{
		if (RingIndex <= 0)
		{
			return 1;
		}
		return ComputeAutoRingItemCountForFloatRadius(static_cast<float>(RingIndex));
	}

	FORCEINLINE int32 GetSafeMaxLimit(int32 RequestedLimit)
	{
		if (RequestedLimit <= 0)
		{
			return UMeshFormationComponent::AbsoluteMaxLocationsSafetyCap;
		}
		return FMath::Clamp(RequestedLimit, 1, UMeshFormationComponent::AbsoluteMaxLocationsSafetyCap);
	}

	FORCEINLINE bool ShouldStopAdding(const TArray<FVector>& Locations, int32 SafeLimit)
	{
		return Locations.Num() >= SafeLimit;
	}

	FORCEINLINE void AddCellCenter(TArray<FVector>& OutCenters, int32 SafeLimit, const FVector& Pos)
	{
		if (OutCenters.Num() < SafeLimit)
		{
			OutCenters.Add(Pos);
		}
	}

	/**
	 * 3D Spatial Voxel Grid that guarantees no two accepted cell centers ever have overlapping
	 * PaddedCellSize Axis-Aligned Bounding Boxes (AABBs).
	 */
	struct FNonOverlappingCellFilter
	{
		FVector InvCellSize;
		TMap<FIntVector, FVector> OccupiedVoxels;

		explicit FNonOverlappingCellFilter(const FVector& PaddedCellSize, int32 ExpectedCount = 0)
		{
			InvCellSize = FVector(
				1.0f / FMath::Max(0.01f, PaddedCellSize.X),
				1.0f / FMath::Max(0.01f, PaddedCellSize.Y),
				1.0f / FMath::Max(0.01f, PaddedCellSize.Z));
			if (ExpectedCount > 0)
			{
				OccupiedVoxels.Reserve(ExpectedCount);
			}
		}

		FORCEINLINE bool TryAcceptPoint(const FVector& LocalPos)
		{
			const FVector NormPos = LocalPos * InvCellSize;
			const FIntVector VoxelKey(
				FMath::FloorToInt(NormPos.X),
				FMath::FloorToInt(NormPos.Y),
				FMath::FloorToInt(NormPos.Z));

			// Check 3x3x3 neighborhood in normalized cell space (threshold 0.98f allows exact touching at 1.0f)
			constexpr float OverlapThreshold = 0.98f;
			for (int32 dz = -1; dz <= 1; ++dz)
			{
				for (int32 dy = -1; dy <= 1; ++dy)
				{
					for (int32 dx = -1; dx <= 1; ++dx)
					{
						if (const FVector* ExistingNorm = OccupiedVoxels.Find(FIntVector(VoxelKey.X + dx, VoxelKey.Y + dy, VoxelKey.Z + dz)))
						{
							if (FMath::Abs(NormPos.X - ExistingNorm->X) < OverlapThreshold &&
								FMath::Abs(NormPos.Y - ExistingNorm->Y) < OverlapThreshold &&
								FMath::Abs(NormPos.Z - ExistingNorm->Z) < OverlapThreshold)
							{
								return false;
							}
						}
					}
				}
			}

			OccupiedVoxels.Add(VoxelKey, NormPos);
			return true;
		}

		static void FilterInPlace(TArray<FVector>& InOutLocations, const FVector& PaddedCellSize)
		{
			const int32 InitialNum = InOutLocations.Num();
			if (InitialNum <= 1)
			{
				return;
			}

			FNonOverlappingCellFilter Filter(PaddedCellSize, InitialNum);
			int32 WriteIdx = 0;
			for (int32 ReadIdx = 0; ReadIdx < InitialNum; ++ReadIdx)
			{
				const FVector Candidate = InOutLocations[ReadIdx];
				if (Filter.TryAcceptPoint(Candidate))
				{
					if (WriteIdx != ReadIdx)
					{
						InOutLocations[WriteIdx] = Candidate;
					}
					++WriteIdx;
				}
			}

			if (WriteIdx < InitialNum)
			{
				InOutLocations.SetNum(WriteIdx, EAllowShrinking::No);
			}
		}
	};
}

UMeshFormationComponent::UMeshFormationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	FormationShape = EMeshFormationShape::CubeMatrix;
	LengthX = 5;
	WidthY = 4;
	HeightZ = 7;
	Radius = 4;
	bHollow = false;
	MaxLocationsLimit = 100000;

	FormationAlignment = EMeshFormationAlignment::CenterXY_BottomZ;
	Padding = FVector(10.0f, 10.0f, 10.0f);
	UniformPadding = 0.0f;
	LocationOffset = FVector::ZeroVector;
	RotationOffset = FRotator::ZeroRotator;
	MeshScale = FVector::OneVector;
	FallbackMeshSize = FVector(100.0f, 100.0f, 100.0f);
	bCompensateMeshPivotOffset = true;
	bOutputInWorldSpace = true;
	bAutoUseOwnerMeshIfUnset = true;
	bGenerateOnBeginPlay = false;
}

void UMeshFormationComponent::BeginPlay()
{
	Super::BeginPlay();

	DetermineMeshBounds(nullptr);

	if (bGenerateOnBeginPlay)
	{
		GenerateFormationLocationsAsync(nullptr);
	}
}

// =========================================================================
// 1. Bounds Extraction & Calculation
// =========================================================================

const UObject* UMeshFormationComponent::ResolveEffectiveMeshObject(const UObject* OptionalOverride) const
{
	if (IsValid(OptionalOverride))
	{
		return OptionalOverride;
	}
	if (IsValid(ReferenceStaticMesh))
	{
		return ReferenceStaticMesh;
	}
	if (IsValid(ReferenceMeshObject))
	{
		return ReferenceMeshObject;
	}
	if (bAutoUseOwnerMeshIfUnset)
	{
		if (const AActor* OwnerActor = GetOwner())
		{
			TArray<UStaticMeshComponent*> OwnerSMCs;
			OwnerActor->GetComponents<UStaticMeshComponent>(OwnerSMCs);
			for (const UStaticMeshComponent* OwnerSMC : OwnerSMCs)
			{
				if (OwnerSMC && OwnerSMC->GetStaticMesh())
				{
					return OwnerSMC;
				}
			}
		}
	}
	return nullptr;
}

bool UMeshFormationComponent::ExtractRawBoundingBox(const UObject* MeshObject, FBox& OutRawBox, FVector& OutComponentScale) const
{
	OutComponentScale = FVector::OneVector;

	if (!IsValid(MeshObject))
	{
		return false;
	}

	// 1. Direct UStaticMesh asset
	if (const UStaticMesh* StaticMesh = Cast<UStaticMesh>(MeshObject))
	{
		OutRawBox = StaticMesh->GetBoundingBox();
		return OutRawBox.IsValid != 0 && !OutRawBox.GetSize().IsNearlyZero();
	}

	// 2. UInteractablePropDataAsset
	if (const UInteractablePropDataAsset* PropDataAsset = Cast<UInteractablePropDataAsset>(MeshObject))
	{
		if (PropDataAsset->StaticMesh)
		{
			OutRawBox = PropDataAsset->StaticMesh->GetBoundingBox();
			return OutRawBox.IsValid != 0 && !OutRawBox.GetSize().IsNearlyZero();
		}
		return false;
	}

	// 3. UIISMDB Data Asset (uses first valid product definition mesh)
	if (const UIISMDB* DatabaseAsset = Cast<UIISMDB>(MeshObject))
	{
		for (const TPair<int32, FPropProductDefinition>& Pair : DatabaseAsset->ProductDefinitions)
		{
			if (Pair.Value.StaticMesh)
			{
				OutRawBox = Pair.Value.StaticMesh->GetBoundingBox();
				if (OutRawBox.IsValid != 0 && !OutRawBox.GetSize().IsNearlyZero())
				{
					return true;
				}
			}
		}
		return false;
	}

	// 4. Direct USkeletalMesh asset
	if (const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(MeshObject))
	{
		OutRawBox = SkeletalMesh->GetImportedBounds().GetBox();
		return OutRawBox.IsValid != 0 && !OutRawBox.GetSize().IsNearlyZero();
	}

	// 5. UStaticMeshComponent
	if (const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(MeshObject))
	{
		OutComponentScale = SMC->GetComponentScale().GetAbs();
		if (const UStaticMesh* SM = SMC->GetStaticMesh())
		{
			OutRawBox = SM->GetBoundingBox();
			if (OutRawBox.IsValid != 0 && !OutRawBox.GetSize().IsNearlyZero())
			{
				return true;
			}
		}
		OutRawBox = SMC->CalcBounds(FTransform::Identity).GetBox();
		return OutRawBox.IsValid != 0 && !OutRawBox.GetSize().IsNearlyZero();
	}

	// 6. USkinnedMeshComponent / USkeletalMeshComponent
	if (const USkinnedMeshComponent* SkinnedComp = Cast<USkinnedMeshComponent>(MeshObject))
	{
		OutComponentScale = SkinnedComp->GetComponentScale().GetAbs();
		if (const USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(SkinnedComp->GetSkinnedAsset()))
		{
			OutRawBox = SkelMesh->GetImportedBounds().GetBox();
			if (OutRawBox.IsValid != 0 && !OutRawBox.GetSize().IsNearlyZero())
			{
				return true;
			}
		}
		OutRawBox = SkinnedComp->CalcBounds(FTransform::Identity).GetBox();
		return OutRawBox.IsValid != 0 && !OutRawBox.GetSize().IsNearlyZero();
	}

	// 7. Any other UPrimitiveComponent
	if (const UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(MeshObject))
	{
		OutComponentScale = PrimComp->GetComponentScale().GetAbs();
		OutRawBox = PrimComp->CalcBounds(FTransform::Identity).GetBox();
		return OutRawBox.IsValid != 0 && !OutRawBox.GetSize().IsNearlyZero();
	}

	// 8. AActor (calculate combined local-space bounds of its components)
	if (const AActor* Actor = Cast<AActor>(MeshObject))
	{
		OutComponentScale = Actor->GetActorScale3D().GetAbs();
		OutRawBox = Actor->CalculateComponentsBoundingBoxInLocalSpace(/*bNonColliding=*/true, /*bIncludeFromChildActors=*/true);
		if (OutRawBox.IsValid != 0 && !OutRawBox.GetSize().IsNearlyZero())
		{
			return true;
		}
	}

	return false;
}

FMeshBoundsWithPadding UMeshFormationComponent::SetMeshReferenceAndDetermineBounds(UObject* InMeshObject)
{
	ReferenceMeshObject = InMeshObject;
	if (UStaticMesh* AsStaticMesh = Cast<UStaticMesh>(InMeshObject))
	{
		ReferenceStaticMesh = AsStaticMesh;
	}
	return DetermineMeshBounds(InMeshObject);
}

FMeshBoundsWithPadding UMeshFormationComponent::DetermineMeshBounds(const UObject* OptionalMeshOverride)
{
	const UObject* EffectiveObject = ResolveEffectiveMeshObject(OptionalMeshOverride);
	const FVector ComponentScaleAbs = GetComponentScale().GetAbs();
	const FVector CombinedScale = MeshScale.Equals(FVector::OneVector, KINDA_SMALL_NUMBER)
		? ComponentScaleAbs
		: MeshScale;
	LastComputedBounds = DetermineMeshBoundsWithSettings(
		EffectiveObject,
		Padding,
		UniformPadding,
		LocationOffset,
		CombinedScale);
	return LastComputedBounds;
}

FMeshBoundsWithPadding UMeshFormationComponent::DetermineMeshBoundsWithSettings(
	const UObject* InMeshObject,
	FVector InPadding,
	float InUniformPadding,
	FVector InLocationOffset,
	FVector InMeshScale) const
{
	FMeshBoundsWithPadding Result;
	Result.LocationOffset = InLocationOffset;

	const FVector SafeUserScale = FVector(
		FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(InMeshScale.X)),
		FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(InMeshScale.Y)),
		FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(InMeshScale.Z)));

	FBox ExtractedBox(ForceInitToZero);
	FVector ExtractedCompScale = FVector::OneVector;

	const UObject* EffectiveObject = IsValid(InMeshObject) ? InMeshObject : ResolveEffectiveMeshObject(nullptr);
	if (ExtractRawBoundingBox(EffectiveObject, ExtractedBox, ExtractedCompScale))
	{
		Result.bHasValidMeshReference = true;
		Result.RawLocalBox = ExtractedBox;

		const FVector EffectiveScale = InMeshScale.Equals(FVector::OneVector, KINDA_SMALL_NUMBER)
			? ExtractedCompScale
			: SafeUserScale;

		Result.RawMeshSize = ExtractedBox.GetSize() * EffectiveScale;
		Result.RawMeshExtent = Result.RawMeshSize * 0.5f;
		Result.RawMeshCenterOffset = ExtractedBox.GetCenter() * EffectiveScale;
	}
	else
	{
		Result.bHasValidMeshReference = false;
		const FVector SafeFallback = FVector(
			FMath::Max(1.0f, FMath::Abs(FallbackMeshSize.X)),
			FMath::Max(1.0f, FMath::Abs(FallbackMeshSize.Y)),
			FMath::Max(1.0f, FMath::Abs(FallbackMeshSize.Z))) * SafeUserScale;

		Result.RawLocalBox = FBox(-SafeFallback * 0.5f, SafeFallback * 0.5f);
		Result.RawMeshSize = SafeFallback;
		Result.RawMeshExtent = SafeFallback * 0.5f;
		Result.RawMeshCenterOffset = FVector::ZeroVector;
	}

	Result.TotalPadding = InPadding + FVector(InUniformPadding);

	Result.PaddedCellSize = FVector(
		FMath::Max(1.0f, Result.RawMeshSize.X + Result.TotalPadding.X),
		FMath::Max(1.0f, Result.RawMeshSize.Y + Result.TotalPadding.Y),
		FMath::Max(1.0f, Result.RawMeshSize.Z + Result.TotalPadding.Z));

	Result.PaddedCellExtent = Result.PaddedCellSize * 0.5f;
	Result.PivotCompensationOffset = -Result.RawMeshCenterOffset;

	const FVector PaddedCenter = Result.RawMeshCenterOffset + InLocationOffset;
	Result.PaddedSingleMeshBox = FBox(
		PaddedCenter - Result.PaddedCellExtent,
		PaddedCenter + Result.PaddedCellExtent);

	return Result;
}

// =========================================================================
// 2. Dedicated ASYNC Functions Per Shape (Length, Width, Height, Radius, bHollow)
// =========================================================================

int32 UMeshFormationComponent::GenerateCubeMatrixAsync(
	int32 InLengthX,
	int32 InWidthY,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::CubeMatrix;
	LengthX = FMath::Max(1, InLengthX);
	WidthY = FMath::Max(1, InWidthY);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsAsync(OptionalMeshOverride);
}

int32 UMeshFormationComponent::GenerateTriangleFormationAsync(
	int32 InLengthX,
	int32 InWidthY,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::Triangle;
	LengthX = FMath::Max(1, InLengthX);
	WidthY = FMath::Max(1, InWidthY);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsAsync(OptionalMeshOverride);
}

int32 UMeshFormationComponent::GeneratePyramidFormationAsync(
	int32 InBaseLengthX,
	int32 InBaseWidthY,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::Pyramid;
	LengthX = FMath::Max(1, InBaseLengthX);
	WidthY = FMath::Max(1, InBaseWidthY);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsAsync(OptionalMeshOverride);
}

int32 UMeshFormationComponent::GenerateCircleFormationAsync(
	int32 InRadius,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::Circle;
	Radius = FMath::Max(1, InRadius);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsAsync(OptionalMeshOverride);
}

int32 UMeshFormationComponent::GenerateConeFormationAsync(
	int32 InBaseRadius,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::Cone;
	Radius = FMath::Max(1, InBaseRadius);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsAsync(OptionalMeshOverride);
}

int32 UMeshFormationComponent::GenerateHexagonalGridAsync(
	int32 InLengthX,
	int32 InWidthY,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::HexagonalGrid;
	LengthX = FMath::Max(1, InLengthX);
	WidthY = FMath::Max(1, InWidthY);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsAsync(OptionalMeshOverride);
}

int32 UMeshFormationComponent::GenerateHexagonRingAsync(
	int32 InRadius,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::HexagonRing;
	Radius = FMath::Max(1, InRadius);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsAsync(OptionalMeshOverride);
}

int32 UMeshFormationComponent::GenerateSphereFormationAsync(
	int32 InRadius,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::Sphere;
	Radius = FMath::Max(1, InRadius);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsAsync(OptionalMeshOverride);
}

int32 UMeshFormationComponent::GenerateSpiralHelixAsync(
	int32 InRadius,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::SpiralHelix;
	Radius = FMath::Max(1, InRadius);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsAsync(OptionalMeshOverride);
}

int32 UMeshFormationComponent::GenerateDiamondFormationAsync(
	int32 InLengthX,
	int32 InWidthY,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::Diamond;
	LengthX = FMath::Max(1, InLengthX);
	WidthY = FMath::Max(1, InWidthY);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsAsync(OptionalMeshOverride);
}

// =========================================================================
// 3. Dedicated SYNC Functions Per Shape
// =========================================================================

FMeshFormationResult UMeshFormationComponent::GenerateCubeMatrixSync(
	TArray<FVector>& OutLocations,
	FMeshBoundsWithPadding& OutMeshBounds,
	int32 InLengthX,
	int32 InWidthY,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::CubeMatrix;
	LengthX = FMath::Max(1, InLengthX);
	WidthY = FMath::Max(1, InWidthY);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsSync(OutLocations, OutMeshBounds, OptionalMeshOverride);
}

FMeshFormationResult UMeshFormationComponent::GenerateTriangleFormationSync(
	TArray<FVector>& OutLocations,
	FMeshBoundsWithPadding& OutMeshBounds,
	int32 InLengthX,
	int32 InWidthY,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::Triangle;
	LengthX = FMath::Max(1, InLengthX);
	WidthY = FMath::Max(1, InWidthY);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsSync(OutLocations, OutMeshBounds, OptionalMeshOverride);
}

FMeshFormationResult UMeshFormationComponent::GeneratePyramidFormationSync(
	TArray<FVector>& OutLocations,
	FMeshBoundsWithPadding& OutMeshBounds,
	int32 InBaseLengthX,
	int32 InBaseWidthY,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::Pyramid;
	LengthX = FMath::Max(1, InBaseLengthX);
	WidthY = FMath::Max(1, InBaseWidthY);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsSync(OutLocations, OutMeshBounds, OptionalMeshOverride);
}

FMeshFormationResult UMeshFormationComponent::GenerateCircleFormationSync(
	TArray<FVector>& OutLocations,
	FMeshBoundsWithPadding& OutMeshBounds,
	int32 InRadius,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::Circle;
	Radius = FMath::Max(1, InRadius);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsSync(OutLocations, OutMeshBounds, OptionalMeshOverride);
}

FMeshFormationResult UMeshFormationComponent::GenerateConeFormationSync(
	TArray<FVector>& OutLocations,
	FMeshBoundsWithPadding& OutMeshBounds,
	int32 InBaseRadius,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::Cone;
	Radius = FMath::Max(1, InBaseRadius);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsSync(OutLocations, OutMeshBounds, OptionalMeshOverride);
}

FMeshFormationResult UMeshFormationComponent::GenerateHexagonalGridSync(
	TArray<FVector>& OutLocations,
	FMeshBoundsWithPadding& OutMeshBounds,
	int32 InLengthX,
	int32 InWidthY,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::HexagonalGrid;
	LengthX = FMath::Max(1, InLengthX);
	WidthY = FMath::Max(1, InWidthY);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsSync(OutLocations, OutMeshBounds, OptionalMeshOverride);
}

FMeshFormationResult UMeshFormationComponent::GenerateHexagonRingSync(
	TArray<FVector>& OutLocations,
	FMeshBoundsWithPadding& OutMeshBounds,
	int32 InRadius,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::HexagonRing;
	Radius = FMath::Max(1, InRadius);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsSync(OutLocations, OutMeshBounds, OptionalMeshOverride);
}

FMeshFormationResult UMeshFormationComponent::GenerateSphereFormationSync(
	TArray<FVector>& OutLocations,
	FMeshBoundsWithPadding& OutMeshBounds,
	int32 InRadius,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::Sphere;
	Radius = FMath::Max(1, InRadius);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsSync(OutLocations, OutMeshBounds, OptionalMeshOverride);
}

FMeshFormationResult UMeshFormationComponent::GenerateSpiralHelixSync(
	TArray<FVector>& OutLocations,
	FMeshBoundsWithPadding& OutMeshBounds,
	int32 InRadius,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::SpiralHelix;
	Radius = FMath::Max(1, InRadius);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsSync(OutLocations, OutMeshBounds, OptionalMeshOverride);
}

FMeshFormationResult UMeshFormationComponent::GenerateDiamondFormationSync(
	TArray<FVector>& OutLocations,
	FMeshBoundsWithPadding& OutMeshBounds,
	int32 InLengthX,
	int32 InWidthY,
	int32 InHeightZ,
	bool bInHollow,
	FVector InPadding,
	FVector InLocationOffset,
	const UObject* OptionalMeshOverride)
{
	FormationShape = EMeshFormationShape::Diamond;
	LengthX = FMath::Max(1, InLengthX);
	WidthY = FMath::Max(1, InWidthY);
	HeightZ = FMath::Max(1, InHeightZ);
	bHollow = bInHollow;
	Padding = InPadding;
	LocationOffset = InLocationOffset;
	return GenerateFormationLocationsSync(OutLocations, OutMeshBounds, OptionalMeshOverride);
}

// =========================================================================
// 4. Generic Async / Sync Orchestration
// =========================================================================

FMeshFormationAsyncParams UMeshFormationComponent::BuildAsyncParamsSnapshot(const UObject* OptionalMeshOverride)
{
	FMeshFormationAsyncParams Params;
	Params.RequestID = NextRequestID++;
	Params.Shape = FormationShape;
	Params.LengthX = FMath::Max(1, LengthX);
	Params.WidthY = FMath::Max(1, WidthY);
	Params.HeightZ = FMath::Max(1, HeightZ);
	Params.Radius = FMath::Max(1, Radius);
	Params.Alignment = FormationAlignment;
	Params.MeshBounds = DetermineMeshBounds(OptionalMeshOverride);
	// Use unscaled OriginTransform so world-space positions are not double-scaled by component Scale3D
	Params.OriginTransform = FTransform(GetComponentQuat(), GetComponentLocation(), FVector::OneVector);
	Params.LocationOffset = LocationOffset;
	Params.RotationOffset = RotationOffset;
	const FVector ComponentScaleAbs = GetComponentScale().GetAbs();
	Params.MeshScale = MeshScale.Equals(FVector::OneVector, KINDA_SMALL_NUMBER)
		? ComponentScaleAbs
		: MeshScale;
	Params.bCompensateMeshPivotOffset = bCompensateMeshPivotOffset;
	Params.bHollow = bHollow;
	Params.bWorldSpace = bOutputInWorldSpace;
	Params.MaxLocationsLimit = MeshFormationInternal::GetSafeMaxLimit(MaxLocationsLimit);
	return Params;
}

int32 UMeshFormationComponent::GenerateFormationLocationsAsync(const UObject* OptionalMeshOverride)
{
	return GenerateFormationLocationsAsyncWithCallback(OptionalMeshOverride, nullptr);
}

int32 UMeshFormationComponent::GenerateFormationLocationsAsyncWithCallback(
	const UObject* OptionalMeshOverride,
	TFunction<void(const FMeshFormationResult&)> OnCompletedCallback)
{
	const FMeshFormationAsyncParams Snapshot = BuildAsyncParamsSnapshot(OptionalMeshOverride);
	const int32 AssignedRequestID = Snapshot.RequestID;
	bIsAsyncCalculationRunning = true;

	TWeakObjectPtr<UMeshFormationComponent> WeakThis(this);

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Snapshot, WeakThis, OnCompletedCallback = MoveTemp(OnCompletedCallback)]() mutable
	{
		FMeshFormationResult ComputedResult = UMeshFormationComponent::ComputeFormationData(Snapshot);

		AsyncTask(ENamedThreads::GameThread, [ComputedResult = MoveTemp(ComputedResult), WeakThis, OnCompletedCallback = MoveTemp(OnCompletedCallback)]() mutable
		{
			if (UMeshFormationComponent* StrongThis = WeakThis.Get())
			{
				StrongThis->bIsAsyncCalculationRunning = false;
				StrongThis->LastComputedBounds = ComputedResult.MeshBounds;

				if (OnCompletedCallback)
				{
					// When invoked via Latent Blueprint Async node, fire the node callback first
					// and store by move into LastFormationResult to avoid duplicating large arrays
					OnCompletedCallback(ComputedResult);
					if (StrongThis->OnFormationLocationsReady.IsBound())
					{
						StrongThis->OnFormationLocationsReady.Broadcast(
							ComputedResult.RequestID,
							ComputedResult.Locations,
							ComputedResult.Transforms,
							ComputedResult.MeshBounds,
							ComputedResult.TotalCount);
					}
					StrongThis->LastFormationResult = MoveTemp(ComputedResult);
					return;
				}

				StrongThis->LastFormationResult = MoveTemp(ComputedResult);
				StrongThis->OnFormationLocationsReady.Broadcast(
					StrongThis->LastFormationResult.RequestID,
					StrongThis->LastFormationResult.Locations,
					StrongThis->LastFormationResult.Transforms,
					StrongThis->LastFormationResult.MeshBounds,
					StrongThis->LastFormationResult.TotalCount);
			}
			else if (OnCompletedCallback)
			{
				OnCompletedCallback(ComputedResult);
			}
		});
	});

	return AssignedRequestID;
}

FMeshFormationResult UMeshFormationComponent::GenerateFormationLocationsSync(
	TArray<FVector>& OutLocations,
	FMeshBoundsWithPadding& OutMeshBounds,
	const UObject* OptionalMeshOverride)
{
	const FMeshFormationAsyncParams Snapshot = BuildAsyncParamsSnapshot(OptionalMeshOverride);
	LastFormationResult = ComputeFormationData(Snapshot);
	LastComputedBounds = LastFormationResult.MeshBounds;

	OutLocations = LastFormationResult.Locations;
	OutMeshBounds = LastComputedBounds;
	return LastFormationResult;
}

// =========================================================================
// 5. Dedicated Static Shape Builders (Thread-Safe, Low-Memory In-Place)
// =========================================================================

void UMeshFormationComponent::BuildCubeMatrixLocations(
	int32 InLengthX,
	int32 InWidthY,
	int32 InHeightZ,
	const FVector& PaddedCellSize,
	bool bInHollow,
	int32 MaxLimit,
	TArray<FVector>& OutLocalCellCenters)
{
	const int32 LenX = FMath::Max(1, InLengthX);
	const int32 WidY = FMath::Max(1, InWidthY);
	const int32 HgtZ = FMath::Max(1, InHeightZ);
	const int32 SafeLimit = MeshFormationInternal::GetSafeMaxLimit(MaxLimit);

	for (int32 iz = 0; iz < HgtZ && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iz)
	{
		for (int32 iy = 0; iy < WidY && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iy)
		{
			for (int32 ix = 0; ix < LenX && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++ix)
			{
				if (bInHollow)
				{
					const bool bOnOuterShell =
						(ix == 0 || ix == LenX - 1) ||
						(iy == 0 || iy == WidY - 1) ||
						(iz == 0 || iz == HgtZ - 1);
					if (!bOnOuterShell)
					{
						continue;
					}
				}

				MeshFormationInternal::AddCellCenter(
					OutLocalCellCenters,
					SafeLimit,
					FVector(ix * PaddedCellSize.X, iy * PaddedCellSize.Y, iz * PaddedCellSize.Z));
			}
		}
	}
}

void UMeshFormationComponent::BuildTriangleLocations(
	int32 InLengthX,
	int32 InWidthY,
	int32 InHeightZ,
	const FVector& PaddedCellSize,
	bool bInHollow,
	int32 MaxLimit,
	TArray<FVector>& OutLocalCellCenters)
{
	const int32 LenX = FMath::Max(1, InLengthX);
	const int32 WidY = FMath::Max(1, InWidthY);
	const int32 HgtZ = FMath::Max(1, InHeightZ);
	const int32 SafeLimit = MeshFormationInternal::GetSafeMaxLimit(MaxLimit);

	for (int32 iz = 0; iz < HgtZ && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iz)
	{
		for (int32 ix = 0; ix < LenX && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++ix)
		{
			const float RowAlpha = (LenX > 1)
				? static_cast<float>(ix) / static_cast<float>(LenX - 1)
				: 1.0f;
			const int32 ItemsInRow = (LenX == WidY)
				? (ix + 1)
				: FMath::Max(1, FMath::RoundToInt(FMath::Lerp(1.0f, static_cast<float>(WidY), RowAlpha)));

			const float RowStartY = -0.5f * static_cast<float>(ItemsInRow - 1) * PaddedCellSize.Y;

			for (int32 iy = 0; iy < ItemsInRow && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iy)
			{
				if (bInHollow)
				{
					const bool bOnOutline =
						(ix == 0 || ix == LenX - 1) ||
						(iy == 0 || iy == ItemsInRow - 1) ||
						(iz == 0 || iz == HgtZ - 1);
					if (!bOnOutline)
					{
						continue;
					}
				}

				MeshFormationInternal::AddCellCenter(
					OutLocalCellCenters,
					SafeLimit,
					FVector(ix * PaddedCellSize.X, RowStartY + iy * PaddedCellSize.Y, iz * PaddedCellSize.Z));
			}
		}
	}
}

void UMeshFormationComponent::BuildPyramidLocations(
	int32 InBaseLengthX,
	int32 InBaseWidthY,
	int32 InHeightZ,
	const FVector& PaddedCellSize,
	bool bInHollow,
	int32 MaxLimit,
	TArray<FVector>& OutLocalCellCenters)
{
	const int32 BaseLenX = FMath::Max(1, InBaseLengthX);
	const int32 BaseWidY = FMath::Max(1, InBaseWidthY);
	const int32 HgtZ = FMath::Max(1, InHeightZ);
	const int32 SafeLimit = MeshFormationInternal::GetSafeMaxLimit(MaxLimit);

	for (int32 iz = 0; iz < HgtZ && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iz)
	{
		const float HeightAlpha = (HgtZ > 1) ? static_cast<float>(iz) / static_cast<float>(HgtZ - 1) : 0.0f;
		const int32 LayerLenX = FMath::Max(1, FMath::RoundToInt(FMath::Lerp(static_cast<float>(BaseLenX), 1.0f, HeightAlpha)));
		const int32 LayerWidY = FMath::Max(1, FMath::RoundToInt(FMath::Lerp(static_cast<float>(BaseWidY), 1.0f, HeightAlpha)));

		const float StartX = -0.5f * static_cast<float>(LayerLenX - 1) * PaddedCellSize.X;
		const float StartY = -0.5f * static_cast<float>(LayerWidY - 1) * PaddedCellSize.Y;

		for (int32 iy = 0; iy < LayerWidY && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iy)
		{
			for (int32 ix = 0; ix < LayerLenX && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++ix)
			{
				if (bInHollow)
				{
					const bool bOnShell =
						(iz == 0 || iz == HgtZ - 1) ||
						(ix == 0 || ix == LayerLenX - 1) ||
						(iy == 0 || iy == LayerWidY - 1);
					if (!bOnShell)
					{
						continue;
					}
				}

				MeshFormationInternal::AddCellCenter(
					OutLocalCellCenters,
					SafeLimit,
					FVector(StartX + ix * PaddedCellSize.X, StartY + iy * PaddedCellSize.Y, iz * PaddedCellSize.Z));
			}
		}
	}
}

void UMeshFormationComponent::BuildCircleLocations(
	int32 InRadius,
	int32 InHeightZ,
	const FVector& PaddedCellSize,
	bool bInHollow,
	int32 MaxLimit,
	TArray<FVector>& OutLocalCellCenters)
{
	const int32 MaxRadius = FMath::Max(1, InRadius);
	const int32 HgtZ = FMath::Max(1, InHeightZ);
	const int32 SafeLimit = MeshFormationInternal::GetSafeMaxLimit(MaxLimit);
	// Use 2D XY bounding diagonal so axis-aligned bounding boxes never overlap at 45-degree angles
	const float RadialStep = FVector2D(PaddedCellSize.X, PaddedCellSize.Y).Size();

	const int32 StartRing = (bInHollow && MaxRadius > 1) ? (MaxRadius - 1) : 0;

	for (int32 iz = 0; iz < HgtZ && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iz)
	{
		for (int32 iRing = StartRing; iRing < MaxRadius && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iRing)
		{
			const int32 ItemsInRing = MeshFormationInternal::ComputeAutoRingItemCount(iRing);
			const float RingRadius = static_cast<float>(iRing) * RadialStep;

			for (int32 iSeg = 0; iSeg < ItemsInRing && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iSeg)
			{
				const float AngleRad = (2.0f * PI * static_cast<float>(iSeg)) / static_cast<float>(ItemsInRing);
				MeshFormationInternal::AddCellCenter(
					OutLocalCellCenters,
					SafeLimit,
					FVector(RingRadius * FMath::Cos(AngleRad), RingRadius * FMath::Sin(AngleRad), iz * PaddedCellSize.Z));
			}
		}
	}
}

void UMeshFormationComponent::BuildConeLocations(
	int32 InBaseRadius,
	int32 InHeightZ,
	const FVector& PaddedCellSize,
	bool bInHollow,
	int32 MaxLimit,
	TArray<FVector>& OutLocalCellCenters)
{
	const int32 BaseRadius = FMath::Max(1, InBaseRadius);
	const int32 HgtZ = FMath::Max(1, InHeightZ);
	const int32 SafeLimit = MeshFormationInternal::GetSafeMaxLimit(MaxLimit);
	// Use 2D XY bounding diagonal so axis-aligned bounding boxes never overlap at 45-degree angles
	const float RadialStep = FVector2D(PaddedCellSize.X, PaddedCellSize.Y).Size();

	for (int32 iz = 0; iz < HgtZ && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iz)
	{
		const float HeightAlpha = (HgtZ > 1) ? static_cast<float>(iz) / static_cast<float>(HgtZ - 1) : 0.0f;
		const float TaperFactor = 1.0f - HeightAlpha;

		if (HgtZ > 1 && iz == HgtZ - 1)
		{
			MeshFormationInternal::AddCellCenter(
				OutLocalCellCenters,
				SafeLimit,
				FVector(0.0f, 0.0f, iz * PaddedCellSize.Z));
			continue;
		}

		// Smooth continuous outer ring radius in steps so cones with HeightZ > BaseRadius taper smoothly on every layer
		const float ContinuousOuterRing = static_cast<float>(BaseRadius - 1) * TaperFactor;

		if (bInHollow)
		{
			const int32 ItemsInRing = MeshFormationInternal::ComputeAutoRingItemCountForFloatRadius(ContinuousOuterRing);
			const float RingRadius = (ItemsInRing <= 1) ? 0.0f : (ContinuousOuterRing * RadialStep);

			for (int32 iSeg = 0; iSeg < ItemsInRing && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iSeg)
			{
				const float AngleRad = (2.0f * PI * static_cast<float>(iSeg)) / static_cast<float>(ItemsInRing);
				MeshFormationInternal::AddCellCenter(
					OutLocalCellCenters,
					SafeLimit,
					FVector(RingRadius * FMath::Cos(AngleRad), RingRadius * FMath::Sin(AngleRad), iz * PaddedCellSize.Z));
			}
		}
		else
		{
			const int32 MaxFullRing = FMath::FloorToInt(ContinuousOuterRing);
			for (int32 iRing = 0; iRing <= MaxFullRing && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iRing)
			{
				const int32 ItemsInRing = MeshFormationInternal::ComputeAutoRingItemCount(iRing);
				const float RingRadius = static_cast<float>(iRing) * RadialStep;

				for (int32 iSeg = 0; iSeg < ItemsInRing && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iSeg)
				{
					const float AngleRad = (2.0f * PI * static_cast<float>(iSeg)) / static_cast<float>(ItemsInRing);
					MeshFormationInternal::AddCellCenter(
						OutLocalCellCenters,
						SafeLimit,
						FVector(RingRadius * FMath::Cos(AngleRad), RingRadius * FMath::Sin(AngleRad), iz * PaddedCellSize.Z));
				}
			}
		}
	}
}

void UMeshFormationComponent::BuildHexagonalGridLocations(
	int32 InLengthX,
	int32 InWidthY,
	int32 InHeightZ,
	const FVector& PaddedCellSize,
	bool bInHollow,
	int32 MaxLimit,
	TArray<FVector>& OutLocalCellCenters)
{
	const int32 LenX = FMath::Max(1, InLengthX);
	const int32 WidY = FMath::Max(1, InWidthY);
	const int32 HgtZ = FMath::Max(1, InHeightZ);
	const int32 SafeLimit = MeshFormationInternal::GetSafeMaxLimit(MaxLimit);
	// Staggered rows are shifted by 0.5 * PaddedCellSize.X, so Y row spacing must be >= PaddedCellSize.Y to prevent AABB overlap
	const float HexRowStepY = PaddedCellSize.Y;

	for (int32 iz = 0; iz < HgtZ && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iz)
	{
		for (int32 iy = 0; iy < WidY && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iy)
		{
			const float RowOffsetX = (iy % 2 != 0) ? (0.5f * PaddedCellSize.X) : 0.0f;

			for (int32 ix = 0; ix < LenX && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++ix)
			{
				if (bInHollow)
				{
					const bool bOnBoundary =
						(ix == 0 || ix == LenX - 1) ||
						(iy == 0 || iy == WidY - 1) ||
						(iz == 0 || iz == HgtZ - 1);
					if (!bOnBoundary)
					{
						continue;
					}
				}

				MeshFormationInternal::AddCellCenter(
					OutLocalCellCenters,
					SafeLimit,
					FVector(ix * PaddedCellSize.X + RowOffsetX, iy * HexRowStepY, iz * PaddedCellSize.Z));
			}
		}
	}
}

void UMeshFormationComponent::BuildHexagonRingLocations(
	int32 InRadius,
	int32 InHeightZ,
	const FVector& PaddedCellSize,
	bool bInHollow,
	int32 MaxLimit,
	TArray<FVector>& OutLocalCellCenters)
{
	const int32 HexRadius = FMath::Max(0, InRadius - 1);
	const int32 HgtZ = FMath::Max(1, InHeightZ);
	const int32 SafeLimit = MeshFormationInternal::GetSafeMaxLimit(MaxLimit);
	// Staggered axial rows shift X by 0.5 * PaddedCellSize.X, so Y step must be >= PaddedCellSize.Y to prevent AABB overlap
	const float HexStepY = PaddedCellSize.Y;

	for (int32 iz = 0; iz < HgtZ && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iz)
	{
		for (int32 q = -HexRadius; q <= HexRadius && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++q)
		{
			const int32 RMin = FMath::Max(-HexRadius, -q - HexRadius);
			const int32 RMax = FMath::Min(HexRadius, -q + HexRadius);

			for (int32 r = RMin; r <= RMax && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++r)
			{
				const int32 RingDist = FMath::Max3(FMath::Abs(q), FMath::Abs(r), FMath::Abs(q + r));
				if (bInHollow && HexRadius > 0 && RingDist < HexRadius)
				{
					continue;
				}

				const float PosX = PaddedCellSize.X * (static_cast<float>(q) + 0.5f * static_cast<float>(r));
				const float PosY = HexStepY * static_cast<float>(r);
				MeshFormationInternal::AddCellCenter(
					OutLocalCellCenters,
					SafeLimit,
					FVector(PosX, PosY, iz * PaddedCellSize.Z));
			}
		}
	}
}

void UMeshFormationComponent::BuildSphereLocations(
	int32 InRadius,
	const FVector& PaddedCellSize,
	bool bInHollow,
	int32 MaxLimit,
	TArray<FVector>& OutLocalCellCenters)
{
	const int32 MaxRadius = FMath::Max(1, InRadius);
	const int32 SafeLimit = MeshFormationInternal::GetSafeMaxLimit(MaxLimit);
	// Use 3D bounding box diagonal so items on any spherical angle never overlap
	const float SphereStep = PaddedCellSize.Size();

	const int32 StartShell = (bInHollow && MaxRadius > 1) ? (MaxRadius - 1) : 0;

	for (int32 iShell = StartShell; iShell < MaxRadius && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iShell)
	{
		if (iShell == 0)
		{
			MeshFormationInternal::AddCellCenter(OutLocalCellCenters, SafeLimit, FVector::ZeroVector);
			continue;
		}

		const float ShellRadius = static_cast<float>(iShell) * SphereStep;
		// Calculate how many latitude intervals fit from -PI/2 to +PI/2 with chord distance >= SphereStep
		const float HalfRatio = FMath::Clamp(0.5f / static_cast<float>(iShell), 0.0001f, 1.0f);
		const int32 LatIntervals = FMath::Max(2, FMath::FloorToInt(PI / (2.0f * FMath::Asin(HalfRatio))));
		const int32 LatitudeRings = LatIntervals + 1;

		for (int32 iLat = 0; iLat < LatitudeRings && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iLat)
		{
			if (iLat == 0)
			{
				MeshFormationInternal::AddCellCenter(OutLocalCellCenters, SafeLimit, FVector(0.0f, 0.0f, -ShellRadius));
				continue;
			}
			if (iLat == LatitudeRings - 1)
			{
				MeshFormationInternal::AddCellCenter(OutLocalCellCenters, SafeLimit, FVector(0.0f, 0.0f, ShellRadius));
				continue;
			}

			const float LatAlpha = static_cast<float>(iLat) / static_cast<float>(LatitudeRings - 1);
			const float ElevationRad = FMath::Lerp(-HALF_PI, HALF_PI, LatAlpha);
			const float CosElev = FMath::Cos(ElevationRad);
			const float SinElev = FMath::Sin(ElevationRad);

			const float RingRadiusXY = ShellRadius * CosElev;
			const float PosZ = ShellRadius * SinElev;

			const float EffectiveLatRingSteps = static_cast<float>(iShell) * CosElev;
			const int32 AzimuthCount = MeshFormationInternal::ComputeAutoRingItemCountForFloatRadius(EffectiveLatRingSteps);

			for (int32 iAzimuth = 0; iAzimuth < AzimuthCount && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iAzimuth)
			{
				const float AzimuthRad = (2.0f * PI * static_cast<float>(iAzimuth)) / static_cast<float>(AzimuthCount);
				MeshFormationInternal::AddCellCenter(
					OutLocalCellCenters,
					SafeLimit,
					FVector(RingRadiusXY * FMath::Cos(AzimuthRad), RingRadiusXY * FMath::Sin(AzimuthRad), PosZ));
			}
		}
	}
}

void UMeshFormationComponent::BuildSpiralHelixLocations(
	int32 InRadius,
	int32 InHeightZ,
	const FVector& PaddedCellSize,
	bool bInHollow,
	int32 MaxLimit,
	TArray<FVector>& OutLocalCellCenters)
{
	const int32 MaxRadius = FMath::Max(1, InRadius);
	const int32 TurnsZ = FMath::Max(1, InHeightZ);
	const int32 SafeLimit = MeshFormationInternal::GetSafeMaxLimit(MaxLimit);
	const float RadialStep = FVector2D(PaddedCellSize.X, PaddedCellSize.Y).Size();

	if (bInHollow || MaxRadius <= 1)
	{
		const int32 ItemsPerTurn = MeshFormationInternal::ComputeAutoRingItemCount(MaxRadius);
		const int32 TotalSteps = ItemsPerTurn * TurnsZ;

		for (int32 StepIdx = 0; StepIdx < TotalSteps && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++StepIdx)
		{
			const float TurnProgress = static_cast<float>(StepIdx) / static_cast<float>(ItemsPerTurn);
			const float AngleRad = TurnProgress * 2.0f * PI;
			const float RadiusVal = (TurnsZ == 1)
				? ((1.0f + TurnProgress) * RadialStep)
				: (static_cast<float>(MaxRadius) * RadialStep);
			const float PosZ = (TurnsZ == 1) ? 0.0f : (TurnProgress * PaddedCellSize.Z);

			MeshFormationInternal::AddCellCenter(
				OutLocalCellCenters,
				SafeLimit,
				FVector(RadiusVal * FMath::Cos(AngleRad), RadiusVal * FMath::Sin(AngleRad), PosZ));
		}
	}
	else
	{
		// Solid filled spiral/helix: each radial ring r steps at its own non-overlapping angular count
		for (int32 r = 1; r <= MaxRadius && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++r)
		{
			const int32 RingItemsPerTurn = MeshFormationInternal::ComputeAutoRingItemCount(r);
			const int32 RingTotalSteps = (TurnsZ == 1) ? RingItemsPerTurn : (RingItemsPerTurn * TurnsZ);
			const float RadiusVal = static_cast<float>(r) * RadialStep;

			for (int32 StepIdx = 0; StepIdx < RingTotalSteps && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++StepIdx)
			{
				const float TurnProgress = static_cast<float>(StepIdx) / static_cast<float>(RingItemsPerTurn);
				const float AngleRad = TurnProgress * 2.0f * PI;
				const float PosZ = (TurnsZ == 1) ? 0.0f : (TurnProgress * PaddedCellSize.Z);

				MeshFormationInternal::AddCellCenter(
					OutLocalCellCenters,
					SafeLimit,
					FVector(RadiusVal * FMath::Cos(AngleRad), RadiusVal * FMath::Sin(AngleRad), PosZ));
			}
		}
	}
}

void UMeshFormationComponent::BuildDiamondLocations(
	int32 InLengthX,
	int32 InWidthY,
	int32 InHeightZ,
	const FVector& PaddedCellSize,
	bool bInHollow,
	int32 MaxLimit,
	TArray<FVector>& OutLocalCellCenters)
{
	const int32 MaxColsX = FMath::Max(1, InLengthX);
	const int32 MaxRowsY = FMath::Max(1, InWidthY);
	const int32 LayersZ = FMath::Max(1, InHeightZ);
	const int32 SafeLimit = MeshFormationInternal::GetSafeMaxLimit(MaxLimit);

	const float MidZ = 0.5f * static_cast<float>(LayersZ - 1);
	for (int32 iz = 0; iz < LayersZ && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iz)
	{
		const float ZDistRatio = (MidZ > KINDA_SMALL_NUMBER)
			? (FMath::Abs(static_cast<float>(iz) - MidZ) / MidZ)
			: 0.0f;
		const float LayerScale = 1.0f - ZDistRatio;

		const int32 LayerCountX = FMath::Max(1, FMath::RoundToInt(static_cast<float>(MaxColsX) * LayerScale));
		const float MidX = 0.5f * static_cast<float>(LayerCountX - 1);

		for (int32 ix = 0; ix < LayerCountX && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++ix)
		{
			const float XDistRatio = (MidX > KINDA_SMALL_NUMBER)
				? (FMath::Abs(static_cast<float>(ix) - MidX) / MidX)
				: 0.0f;
			const int32 RowCountY = FMath::Max(1, FMath::RoundToInt(static_cast<float>(MaxRowsY) * LayerScale * (1.0f - XDistRatio)));

			const float StartX = -0.5f * static_cast<float>(LayerCountX - 1) * PaddedCellSize.X;
			const float StartY = -0.5f * static_cast<float>(RowCountY - 1) * PaddedCellSize.Y;

			for (int32 iy = 0; iy < RowCountY && !MeshFormationInternal::ShouldStopAdding(OutLocalCellCenters, SafeLimit); ++iy)
			{
				if (bInHollow && (iy > 0 && iy < RowCountY - 1) && (ix > 0 && ix < LayerCountX - 1))
				{
					continue;
				}

				MeshFormationInternal::AddCellCenter(
					OutLocalCellCenters,
					SafeLimit,
					FVector(StartX + ix * PaddedCellSize.X, StartY + iy * PaddedCellSize.Y, iz * PaddedCellSize.Z));
			}
		}
	}
}

FMeshFormationResult UMeshFormationComponent::ComputeFormationData(const FMeshFormationAsyncParams& Params)
{
	FMeshFormationResult Result;
	Result.RequestID = Params.RequestID;
	Result.Shape = Params.Shape;
	Result.MatrixCounts = FIntVector(Params.LengthX, Params.WidthY, Params.HeightZ);
	Result.bHollow = Params.bHollow;
	Result.MeshBounds = Params.MeshBounds;

	const FVector Step = Params.MeshBounds.PaddedCellSize;
	const int32 SafeLimit = MeshFormationInternal::GetSafeMaxLimit(Params.MaxLocationsLimit);

	switch (Params.Shape)
	{
	case EMeshFormationShape::CubeMatrix:
		BuildCubeMatrixLocations(Params.LengthX, Params.WidthY, Params.HeightZ, Step, Params.bHollow, SafeLimit, Result.Locations);
		break;

	case EMeshFormationShape::Triangle:
		BuildTriangleLocations(Params.LengthX, Params.WidthY, Params.HeightZ, Step, Params.bHollow, SafeLimit, Result.Locations);
		break;

	case EMeshFormationShape::Pyramid:
		BuildPyramidLocations(Params.LengthX, Params.WidthY, Params.HeightZ, Step, Params.bHollow, SafeLimit, Result.Locations);
		break;

	case EMeshFormationShape::Circle:
		Result.MatrixCounts = FIntVector(Params.Radius, Params.Radius, Params.HeightZ);
		BuildCircleLocations(Params.Radius, Params.HeightZ, Step, Params.bHollow, SafeLimit, Result.Locations);
		break;

	case EMeshFormationShape::Cone:
		Result.MatrixCounts = FIntVector(Params.Radius, Params.Radius, Params.HeightZ);
		BuildConeLocations(Params.Radius, Params.HeightZ, Step, Params.bHollow, SafeLimit, Result.Locations);
		break;

	case EMeshFormationShape::HexagonalGrid:
		BuildHexagonalGridLocations(Params.LengthX, Params.WidthY, Params.HeightZ, Step, Params.bHollow, SafeLimit, Result.Locations);
		break;

	case EMeshFormationShape::HexagonRing:
		Result.MatrixCounts = FIntVector(Params.Radius, Params.Radius, Params.HeightZ);
		BuildHexagonRingLocations(Params.Radius, Params.HeightZ, Step, Params.bHollow, SafeLimit, Result.Locations);
		break;

	case EMeshFormationShape::Sphere:
		Result.MatrixCounts = FIntVector(Params.Radius, Params.Radius, Params.Radius);
		BuildSphereLocations(Params.Radius, Step, Params.bHollow, SafeLimit, Result.Locations);
		break;

	case EMeshFormationShape::SpiralHelix:
		Result.MatrixCounts = FIntVector(Params.Radius, Params.Radius, Params.HeightZ);
		BuildSpiralHelixLocations(Params.Radius, Params.HeightZ, Step, Params.bHollow, SafeLimit, Result.Locations);
		break;

	case EMeshFormationShape::Diamond:
		BuildDiamondLocations(Params.LengthX, Params.WidthY, Params.HeightZ, Step, Params.bHollow, SafeLimit, Result.Locations);
		break;
	}

	// Enforce strict 3D AABB non-overlap across all generated cell centers
	MeshFormationInternal::FNonOverlappingCellFilter::FilterInPlace(Result.Locations, Step);

	const int32 NumLocations = Result.Locations.Num();
	Result.TotalCount = NumLocations;

	if (NumLocations >= SafeLimit)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UMeshFormationComponent] Formation hit MaxLocationsLimit (%d). Note: Radius/Length/Width/Height are object counts (not centimeters)."), SafeLimit);
	}

	if (NumLocations == 0)
	{
		return Result;
	}

	// 1. Compute bounding box of raw cell centers
	FBox CentersBox(ForceInit);
	for (const FVector& Pos : Result.Locations)
	{
		CentersBox += Pos;
	}

	const FBox UnalignedFullBox(
		CentersBox.Min - Params.MeshBounds.PaddedCellExtent,
		CentersBox.Max + Params.MeshBounds.PaddedCellExtent);

	FVector AlignmentShift = FVector::ZeroVector;
	switch (Params.Alignment)
	{
	case EMeshFormationAlignment::CenterXY_BottomZ:
		AlignmentShift = FVector(
			-UnalignedFullBox.GetCenter().X,
			-UnalignedFullBox.GetCenter().Y,
			-UnalignedFullBox.Min.Z);
		break;

	case EMeshFormationAlignment::CenteredXYZ:
		AlignmentShift = -UnalignedFullBox.GetCenter();
		break;

	case EMeshFormationAlignment::MinCorner:
		AlignmentShift = -UnalignedFullBox.Min;
		break;
	}

	const FVector PivotAdjustment = Params.bCompensateMeshPivotOffset
		? Params.MeshBounds.PivotCompensationOffset
		: FVector::ZeroVector;

	const FQuat OffsetQuat = Params.RotationOffset.Quaternion();
	const FQuat FinalQuat = Params.bWorldSpace
		? (Params.OriginTransform.GetRotation() * OffsetQuat)
		: OffsetQuat;
	const FVector RotatedPivotAdjustment = OffsetQuat.RotateVector(PivotAdjustment);

	Result.Transforms.Reserve(NumLocations);

	FBox FinalLocalBox(ForceInit);
	FBox FinalWorldBox(ForceInit);

	// 2. Transform all locations in-place and populate Transforms
	for (FVector& Loc : Result.Locations)
	{
		const FVector AlignedCellCenter = Loc + AlignmentShift;
		const FVector OffsetCellCenter = OffsetQuat.RotateVector(AlignedCellCenter) + Params.LocationOffset;
		const FVector LocalPlacementPos = OffsetCellCenter + RotatedPivotAdjustment;

		Loc = Params.bWorldSpace
			? Params.OriginTransform.TransformPosition(LocalPlacementPos)
			: LocalPlacementPos;

		Result.Transforms.Emplace(FinalQuat, Loc, Params.MeshScale);

		FinalLocalBox += FBox(
			OffsetCellCenter - Params.MeshBounds.PaddedCellExtent,
			OffsetCellCenter + Params.MeshBounds.PaddedCellExtent);

		const FVector WorldCellCenter = Params.bWorldSpace
			? Params.OriginTransform.TransformPosition(OffsetCellCenter)
			: OffsetCellCenter;
		FinalWorldBox += FBox(
			WorldCellCenter - Params.MeshBounds.PaddedCellExtent,
			WorldCellCenter + Params.MeshBounds.PaddedCellExtent);
	}

	Result.TotalFormationLocalBounds = FinalLocalBox;
	Result.TotalFormationWorldBounds = FinalWorldBox;

	return Result;
}

// =========================================================================
// 6. Debug Visualization
// =========================================================================

void UMeshFormationComponent::DrawDebugFormation(
	float Duration,
	bool bDrawPaddedCells,
	bool bDrawLocationPoints,
	bool bDrawTotalBounds,
	FLinearColor CellColor,
	FLinearColor BoundsColor) const
{
	UWorld* World = GetWorld();
	if (!World || LastFormationResult.Locations.Num() == 0)
	{
		return;
	}

	const FColor CellDrawColor = CellColor.ToFColor(true);
	const FColor BoundsDrawColor = BoundsColor.ToFColor(true);
	const FVector CellExtent = LastFormationResult.MeshBounds.PaddedCellExtent;
	const FQuat CompRot = bOutputInWorldSpace
		? (GetComponentQuat() * RotationOffset.Quaternion())
		: RotationOffset.Quaternion();
	const FVector RawCenterOffset = bCompensateMeshPivotOffset
		? LastFormationResult.MeshBounds.RawMeshCenterOffset
		: FVector::ZeroVector;

	// Limit debug box drawing to at most 2,500 boxes so viewport doesn't lag
	const int32 MaxDebugDrawCount = FMath::Min(LastFormationResult.Locations.Num(), 2500);
	for (int32 i = 0; i < MaxDebugDrawCount; ++i)
	{
		const FVector& PivotLoc = LastFormationResult.Locations[i];
		const FVector CellCenter = PivotLoc + CompRot.RotateVector(RawCenterOffset);

		if (bDrawPaddedCells)
		{
			DrawDebugBox(World, CellCenter, CellExtent, CompRot, CellDrawColor, false, Duration, 0, 1.5f);
		}
		if (bDrawLocationPoints)
		{
			DrawDebugPoint(World, PivotLoc, 8.0f, FColor::Yellow, false, Duration);
		}
	}

	if (bDrawTotalBounds && LastFormationResult.TotalFormationWorldBounds.IsValid)
	{
		DrawDebugBox(
			World,
			LastFormationResult.TotalFormationWorldBounds.GetCenter(),
			LastFormationResult.TotalFormationWorldBounds.GetExtent(),
			BoundsDrawColor,
			false,
			Duration,
			0,
			3.0f);
	}
}

// =========================================================================
// 7. Dedicated Latent Blueprint Async Action Nodes Per Shape
// =========================================================================

UGenerateMeshFormationAsyncAction* UGenerateMeshFormationAsyncAction::CreateShapeAction(
	UMeshFormationComponent* FormationComponent,
	const UObject* OptionalMeshOverride,
	EMeshFormationShape Shape,
	int32 InLengthX,
	int32 InWidthY,
	int32 InHeightZ,
	int32 InRadius,
	bool bInHollow,
	const FVector& Padding,
	const FVector& LocationOffset,
	const FVector& InMeshScale)
{
	UGenerateMeshFormationAsyncAction* Action = NewObject<UGenerateMeshFormationAsyncAction>();
	Action->TargetComponent = FormationComponent;
	Action->MeshOverride = OptionalMeshOverride;
	Action->RequestedShape = Shape;
	Action->RequestedLengthX = FMath::Max(1, InLengthX);
	Action->RequestedWidthY = FMath::Max(1, InWidthY);
	Action->RequestedHeightZ = FMath::Max(1, InHeightZ);
	Action->RequestedRadius = FMath::Max(1, InRadius);
	Action->bRequestedHollow = bInHollow;
	Action->RequestedPadding = Padding;
	Action->RequestedOffset = LocationOffset;
	Action->RequestedMeshScale = InMeshScale;
	Action->RegisterWithGameInstance(FormationComponent ? FormationComponent->GetWorld() : nullptr);
	return Action;
}

UGenerateMeshFormationAsyncAction* UGenerateMeshFormationAsyncAction::GenerateCubeMatrixAsyncNode(
	UMeshFormationComponent* FormationComponent,
	const UObject* OptionalMeshOverride,
	int32 LengthX,
	int32 WidthY,
	int32 HeightZ,
	bool bHollow,
	FVector Padding,
	FVector LocationOffset,
	FVector MeshScale)
{
	return CreateShapeAction(FormationComponent, OptionalMeshOverride, EMeshFormationShape::CubeMatrix, LengthX, WidthY, HeightZ, 1, bHollow, Padding, LocationOffset, MeshScale);
}

UGenerateMeshFormationAsyncAction* UGenerateMeshFormationAsyncAction::GenerateTriangleAsyncNode(
	UMeshFormationComponent* FormationComponent,
	const UObject* OptionalMeshOverride,
	int32 LengthX,
	int32 WidthY,
	int32 HeightZ,
	bool bHollow,
	FVector Padding,
	FVector LocationOffset,
	FVector MeshScale)
{
	return CreateShapeAction(FormationComponent, OptionalMeshOverride, EMeshFormationShape::Triangle, LengthX, WidthY, HeightZ, 1, bHollow, Padding, LocationOffset, MeshScale);
}

UGenerateMeshFormationAsyncAction* UGenerateMeshFormationAsyncAction::GeneratePyramidAsyncNode(
	UMeshFormationComponent* FormationComponent,
	const UObject* OptionalMeshOverride,
	int32 BaseLengthX,
	int32 BaseWidthY,
	int32 HeightZ,
	bool bHollow,
	FVector Padding,
	FVector LocationOffset,
	FVector MeshScale)
{
	return CreateShapeAction(FormationComponent, OptionalMeshOverride, EMeshFormationShape::Pyramid, BaseLengthX, BaseWidthY, HeightZ, 1, bHollow, Padding, LocationOffset, MeshScale);
}

UGenerateMeshFormationAsyncAction* UGenerateMeshFormationAsyncAction::GenerateCircleAsyncNode(
	UMeshFormationComponent* FormationComponent,
	const UObject* OptionalMeshOverride,
	int32 Radius,
	int32 HeightZ,
	bool bHollow,
	FVector Padding,
	FVector LocationOffset,
	FVector MeshScale)
{
	return CreateShapeAction(FormationComponent, OptionalMeshOverride, EMeshFormationShape::Circle, Radius, Radius, HeightZ, Radius, bHollow, Padding, LocationOffset, MeshScale);
}

UGenerateMeshFormationAsyncAction* UGenerateMeshFormationAsyncAction::GenerateConeAsyncNode(
	UMeshFormationComponent* FormationComponent,
	const UObject* OptionalMeshOverride,
	int32 BaseRadius,
	int32 HeightZ,
	bool bHollow,
	FVector Padding,
	FVector LocationOffset,
	FVector MeshScale)
{
	return CreateShapeAction(FormationComponent, OptionalMeshOverride, EMeshFormationShape::Cone, BaseRadius, BaseRadius, HeightZ, BaseRadius, bHollow, Padding, LocationOffset, MeshScale);
}

UGenerateMeshFormationAsyncAction* UGenerateMeshFormationAsyncAction::GenerateHexagonalGridAsyncNode(
	UMeshFormationComponent* FormationComponent,
	const UObject* OptionalMeshOverride,
	int32 LengthX,
	int32 WidthY,
	int32 HeightZ,
	bool bHollow,
	FVector Padding,
	FVector LocationOffset,
	FVector MeshScale)
{
	return CreateShapeAction(FormationComponent, OptionalMeshOverride, EMeshFormationShape::HexagonalGrid, LengthX, WidthY, HeightZ, 1, bHollow, Padding, LocationOffset, MeshScale);
}

UGenerateMeshFormationAsyncAction* UGenerateMeshFormationAsyncAction::GenerateHexagonRingAsyncNode(
	UMeshFormationComponent* FormationComponent,
	const UObject* OptionalMeshOverride,
	int32 Radius,
	int32 HeightZ,
	bool bHollow,
	FVector Padding,
	FVector LocationOffset,
	FVector MeshScale)
{
	return CreateShapeAction(FormationComponent, OptionalMeshOverride, EMeshFormationShape::HexagonRing, Radius, Radius, HeightZ, Radius, bHollow, Padding, LocationOffset, MeshScale);
}

UGenerateMeshFormationAsyncAction* UGenerateMeshFormationAsyncAction::GenerateSphereAsyncNode(
	UMeshFormationComponent* FormationComponent,
	const UObject* OptionalMeshOverride,
	int32 Radius,
	bool bHollow,
	FVector Padding,
	FVector LocationOffset,
	FVector MeshScale)
{
	return CreateShapeAction(FormationComponent, OptionalMeshOverride, EMeshFormationShape::Sphere, Radius, Radius, Radius, Radius, bHollow, Padding, LocationOffset, MeshScale);
}

UGenerateMeshFormationAsyncAction* UGenerateMeshFormationAsyncAction::GenerateSpiralHelixAsyncNode(
	UMeshFormationComponent* FormationComponent,
	const UObject* OptionalMeshOverride,
	int32 Radius,
	int32 HeightZ,
	bool bHollow,
	FVector Padding,
	FVector LocationOffset,
	FVector MeshScale)
{
	return CreateShapeAction(FormationComponent, OptionalMeshOverride, EMeshFormationShape::SpiralHelix, Radius, Radius, HeightZ, Radius, bHollow, Padding, LocationOffset, MeshScale);
}

UGenerateMeshFormationAsyncAction* UGenerateMeshFormationAsyncAction::GenerateDiamondAsyncNode(
	UMeshFormationComponent* FormationComponent,
	const UObject* OptionalMeshOverride,
	int32 LengthX,
	int32 WidthY,
	int32 HeightZ,
	bool bHollow,
	FVector Padding,
	FVector LocationOffset,
	FVector MeshScale)
{
	return CreateShapeAction(FormationComponent, OptionalMeshOverride, EMeshFormationShape::Diamond, LengthX, WidthY, HeightZ, 1, bHollow, Padding, LocationOffset, MeshScale);
}

void UGenerateMeshFormationAsyncAction::Activate()
{
	if (!IsValid(TargetComponent))
	{
		FMeshFormationResult EmptyResult;
		OnCompleted.Broadcast(EmptyResult.Locations, EmptyResult.Transforms, EmptyResult.MeshBounds, 0);
		SetReadyToDestroy();
		return;
	}

	TargetComponent->FormationShape = RequestedShape;
	TargetComponent->LengthX = RequestedLengthX;
	TargetComponent->WidthY = RequestedWidthY;
	TargetComponent->HeightZ = RequestedHeightZ;
	TargetComponent->Radius = RequestedRadius;
	TargetComponent->bHollow = bRequestedHollow;
	TargetComponent->Padding = RequestedPadding;
	TargetComponent->LocationOffset = RequestedOffset;
	if (!RequestedMeshScale.Equals(FVector::OneVector, KINDA_SMALL_NUMBER))
	{
		TargetComponent->MeshScale = RequestedMeshScale;
	}

	TWeakObjectPtr<UGenerateMeshFormationAsyncAction> WeakAction(this);

	TargetComponent->GenerateFormationLocationsAsyncWithCallback(
		MeshOverride.Get(),
		[WeakAction](const FMeshFormationResult& Result)
		{
			if (UGenerateMeshFormationAsyncAction* StrongAction = WeakAction.Get())
			{
				StrongAction->OnCompleted.Broadcast(
					Result.Locations,
					Result.Transforms,
					Result.MeshBounds,
					Result.TotalCount);
				StrongAction->SetReadyToDestroy();
			}
		});
}
