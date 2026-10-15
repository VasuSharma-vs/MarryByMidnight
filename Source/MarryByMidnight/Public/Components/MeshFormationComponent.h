#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "MeshFormationComponent.generated.h"

class UStaticMesh;
class USkeletalMesh;
class UPrimitiveComponent;

/**
 * Geometric shapes supported by UMeshFormationComponent for generating spatial locations.
 */
UENUM(BlueprintType)
enum class EMeshFormationShape : uint8
{
	/** Standard 3D box matrix using Length (X), Width (Y), and Height (Z) */
	CubeMatrix		UMETA(DisplayName = "1. Cube / Box Matrix (Length, Width, Height)"),

	/** Triangular / wedge formation using Length (X), Width (Y), and Height (Z) */
	Triangle		UMETA(DisplayName = "2. Triangle / Wedge (Length, Width, Height)"),

	/** 3D Pyramid using Base Length (X), Base Width (Y), and Height (Z) */
	Pyramid			UMETA(DisplayName = "3. Pyramid (Base Length, Base Width, Height)"),

	/** Circle / Cylinder using Radius and Height (Z) */
	Circle			UMETA(DisplayName = "4. Circle / Cylinder (Radius, Height)"),

	/** 3D Cone tapering from Base Radius at the bottom to a single tip at the top over Height (Z) */
	Cone			UMETA(DisplayName = "5. Cone (Base Radius, Height)"),

	/** Honeycomb staggered grid using Length (X), Width (Y), and Height (Z) */
	HexagonalGrid	UMETA(DisplayName = "6. Hexagonal Grid (Length, Width, Height)"),

	/** True 6-sided hexagon prism using Radius and Height (Z) */
	HexagonRing		UMETA(DisplayName = "7. Hexagon Shape (Radius, Height)"),

	/** 3D Sphere using Radius */
	Sphere			UMETA(DisplayName = "8. Sphere (Radius)"),

	/** Flat Spiral or 3D Helix using Radius and Height (Z) */
	SpiralHelix		UMETA(DisplayName = "9. Spiral / Helix (Radius, Height)"),

	/** Diamond / 3D Octahedron using Length (X), Width (Y), and Height (Z) */
	Diamond			UMETA(DisplayName = "10. Diamond / Octahedron (Length, Width, Height)")
};

/**
 * Controls how the generated formation is aligned relative to the origin + offset.
 */
UENUM(BlueprintType)
enum class EMeshFormationAlignment : uint8
{
	/** Centered in X and Y, with the bottom of the lowest layer resting at Z = 0 (ideal for stacking on surfaces) */
	CenterXY_BottomZ	UMETA(DisplayName = "Center X/Y, Bottom at Z=0"),

	/** Centered around the origin on all three axes (X, Y, Z) */
	CenteredXYZ			UMETA(DisplayName = "Centered X/Y/Z"),

	/** Starts at the minimum corner (0, 0, 0) and grows in +X, +Y, +Z */
	MinCorner			UMETA(DisplayName = "Minimum Corner (+X, +Y, +Z)")
};

/**
 * Holds the calculated bounding box, padded cell dimensions, and pivot compensation offset for a mesh.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FMeshBoundsWithPadding
{
	GENERATED_BODY()

	/** True if bounds were resolved from a valid mesh/component/actor; false if FallbackMeshSize was used */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Bounds")
	bool bHasValidMeshReference = false;

	/** Raw unscaled local-space bounding box of the source mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Bounds")
	FBox RawLocalBox = FBox(ForceInitToZero);

	/** Center of the scaled raw mesh bounding box relative to its pivot (0,0,0) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Bounds")
	FVector RawMeshCenterOffset = FVector::ZeroVector;

	/** Half-extents (Radius X, Y, Z) of the scaled mesh before padding */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Bounds")
	FVector RawMeshExtent = FVector(50.0f, 50.0f, 50.0f);

	/** Full dimensions (Length X, Width Y, Height Z) of the scaled mesh before padding */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Bounds")
	FVector RawMeshSize = FVector(100.0f, 100.0f, 100.0f);

	/** Total padding added to each axis (Per-axis Padding + UniformPadding) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Bounds")
	FVector TotalPadding = FVector::ZeroVector;

	/** Location offset configured for the formation */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Bounds")
	FVector LocationOffset = FVector::ZeroVector;

	/** Final size of a single cell in the formation (RawMeshSize + TotalPadding) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Bounds")
	FVector PaddedCellSize = FVector(100.0f, 100.0f, 100.0f);

	/** Half-extents of a single padded cell (PaddedCellSize * 0.5) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Bounds")
	FVector PaddedCellExtent = FVector(50.0f, 50.0f, 50.0f);

	/** Offset from the cell center to place the mesh's pivot so its geometry is centered inside the padded cell */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Bounds")
	FVector PivotCompensationOffset = FVector::ZeroVector;

	/** Padded local bounding box of a single mesh including offset */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Bounds")
	FBox PaddedSingleMeshBox = FBox(ForceInitToZero);
};

/**
 * Lightweight result of a synchronous or asynchronous formation calculation.
 * Avoids duplicating large Transform/Coordinate arrays to keep memory usage minimal.
 */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FMeshFormationResult
{
	GENERATED_BODY()

	/** Monotonically increasing request ID matching the async call */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Formation|Result")
	int32 RequestID = 0;

	/** Shape used to generate the locations */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Formation|Result")
	EMeshFormationShape Shape = EMeshFormationShape::CubeMatrix;

	/** Dimensions used for generation (LengthX / Radius, WidthY, HeightZ) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Formation|Result")
	FIntVector MatrixCounts = FIntVector(5, 4, 7);

	/** Whether the formation was generated as hollow (outer shell only) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Formation|Result")
	bool bHollow = false;

	/** Calculated mesh bounds including padding and offset */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Formation|Result")
	FMeshBoundsWithPadding MeshBounds;

	/** Generated target locations (in World Space if bWorldSpace=true, otherwise Local Space) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Formation|Result")
	TArray<FVector> Locations;

	/** Generated target transforms (including Location, Rotation, and MeshScale) for each item */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Formation|Result")
	TArray<FTransform> Transforms;

	/** Combined local-space bounding box of the entire generated formation */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Formation|Result")
	FBox TotalFormationLocalBounds = FBox(ForceInitToZero);

	/** Combined world-space bounding box of the entire generated formation */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Formation|Result")
	FBox TotalFormationWorldBounds = FBox(ForceInitToZero);

	/** Total number of locations generated */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Formation|Result")
	int32 TotalCount = 0;
};

/**
 * Snapshot parameters passed to background worker threads for thread-safe async calculation.
 */
struct FMeshFormationAsyncParams
{
	int32 RequestID = 0;
	EMeshFormationShape Shape = EMeshFormationShape::CubeMatrix;
	int32 LengthX = 5;
	int32 WidthY = 4;
	int32 HeightZ = 7;
	int32 Radius = 4;
	EMeshFormationAlignment Alignment = EMeshFormationAlignment::CenterXY_BottomZ;
	FMeshBoundsWithPadding MeshBounds;
	FTransform OriginTransform = FTransform::Identity;
	FVector LocationOffset = FVector::ZeroVector;
	FRotator RotationOffset = FRotator::ZeroRotator;
	FVector MeshScale = FVector::OneVector;
	bool bCompensateMeshPivotOffset = true;
	bool bOrientItemsToShape = false;
	bool bHollow = false;
	bool bWorldSpace = true;
	int32 MaxLocationsLimit = 100000;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(
	FOnMeshFormationLocationsReady,
	int32, RequestID,
	const TArray<FVector>&, Locations,
	const TArray<FTransform>&, Transforms,
	const FMeshBoundsWithPadding&, MeshBounds,
	int32, TotalCount);

/**
 * UMeshFormationComponent (Mesh Formation Matrix Component)
 *
 * Measures a reference mesh's bounding box, applies padding and offset, and generates
 * non-overlapping locations using simple, intuitive parameters:
 *  - LengthX, WidthY, HeightZ (for Cube, Triangle, Pyramid, HexGrid, Diamond)
 *  - Radius, HeightZ (for Circle, Cone, Hexagon, Sphere, Spiral/Helix)
 *  - bHollow (on every function to switch between solid and hollow shell)
 */
UCLASS(ClassGroup = (Custom, Common, Layout), meta = (BlueprintSpawnableComponent, DisplayName = "Mesh Formation Matrix Component"))
class MARRYBYMIDNIGHT_API UMeshFormationComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMeshFormationComponent();

protected:
	virtual void BeginPlay() override;

public:
	/** Absolute safety ceiling on generated locations per call to prevent out-of-memory crashes */
	static constexpr int32 AbsoluteMaxLocationsSafetyCap = 250000;

	// =====================================================================
	// 1. Mesh Object Reference & Scale
	// =====================================================================

	/** Direct Static Mesh asset reference to measure bounds from */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|1. Mesh Reference")
	TObjectPtr<UStaticMesh> ReferenceStaticMesh = nullptr;

	/**
	 * Flexible object reference (accepts UStaticMesh, USkeletalMesh, UStaticMeshComponent,
	 * UPrimitiveComponent, or AActor). Used if ReferenceStaticMesh is not set or when overridden.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|1. Mesh Reference")
	TObjectPtr<UObject> ReferenceMeshObject = nullptr;

	/** If true and no explicit mesh reference is set, checks the owning Actor for a StaticMeshComponent */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|1. Mesh Reference")
	bool bAutoUseOwnerMeshIfUnset = true;

	/** Scale applied to the reference mesh when computing bounds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|1. Mesh Reference")
	FVector MeshScale = FVector::OneVector;

	/** Fallback mesh dimensions (cm) used if no valid mesh reference is provided */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|1. Mesh Reference")
	FVector FallbackMeshSize = FVector(100.0f, 100.0f, 100.0f);

	// =====================================================================
	// 2. Simple Shape & Size Settings (Length, Width, Height, Radius, Hollow)
	// =====================================================================

	/** Shape pattern used to place the objects */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|2. Shape & Size")
	EMeshFormationShape FormationShape = EMeshFormationShape::CubeMatrix;

	/**
	 * Length (X) in number of objects (NOT centimeters).
	 * Used by: CubeMatrix, Triangle, Pyramid, HexagonalGrid, Diamond.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|2. Shape & Size", meta = (ClampMin = "1", ClampMax = "500", EditCondition = "FormationShape == EMeshFormationShape::CubeMatrix || FormationShape == EMeshFormationShape::Triangle || FormationShape == EMeshFormationShape::Pyramid || FormationShape == EMeshFormationShape::HexagonalGrid || FormationShape == EMeshFormationShape::Diamond", EditConditionHides))
	int32 LengthX = 5;

	/**
	 * Width (Y) in number of objects (NOT centimeters).
	 * Used by: CubeMatrix, Triangle, Pyramid, HexagonalGrid, Diamond.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|2. Shape & Size", meta = (ClampMin = "1", ClampMax = "500", EditCondition = "FormationShape == EMeshFormationShape::CubeMatrix || FormationShape == EMeshFormationShape::Triangle || FormationShape == EMeshFormationShape::Pyramid || FormationShape == EMeshFormationShape::HexagonalGrid || FormationShape == EMeshFormationShape::Diamond", EditConditionHides))
	int32 WidthY = 4;

	/**
	 * Radius in number of object rings from the center to the outer edge (NOT centimeters).
	 * Example: Radius = 4 creates 4 rings from center to edge.
	 * Used by: Circle, Cone, HexagonRing, Sphere, SpiralHelix.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|2. Shape & Size", meta = (ClampMin = "1", ClampMax = "200", EditCondition = "FormationShape == EMeshFormationShape::Circle || FormationShape == EMeshFormationShape::Cone || FormationShape == EMeshFormationShape::HexagonRing || FormationShape == EMeshFormationShape::Sphere || FormationShape == EMeshFormationShape::SpiralHelix", EditConditionHides))
	int32 Radius = 4;

	/**
	 * Height (Z) in number of vertical layers stacked upward (NOT centimeters).
	 * Used by all shapes except Sphere (where height is controlled by Radius).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|2. Shape & Size", meta = (ClampMin = "1", ClampMax = "500", EditCondition = "FormationShape != EMeshFormationShape::Sphere", EditConditionHides))
	int32 HeightZ = 7;

	/**
	 * If true, only generates the outer shell/outline of the shape (leaves the inside empty/hollow).
	 * If false, fills the entire inside of the shape.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|2. Shape & Size")
	bool bHollow = false;

	/** Alignment of the formation relative to the component/origin transform */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|2. Shape & Size")
	EMeshFormationAlignment FormationAlignment = EMeshFormationAlignment::CenterXY_BottomZ;

	/**
	 * Safety cap on the maximum number of generated locations per call (default: 100,000; max: 250,000)
	 * to prevent out-of-memory crashes if huge numbers are entered for Radius/Height.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|2. Shape & Size", meta = (ClampMin = "1", ClampMax = "250000"))
	int32 MaxLocationsLimit = 100000;

	// =====================================================================
	// 3. Padding & Offset Settings
	// =====================================================================

	/** Per-axis padding (cm) added around each mesh's bounding box in X (Length), Y (Width), and Z (Height) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|3. Padding & Offset")
	FVector Padding = FVector(10.0f, 10.0f, 10.0f);

	/** Additional uniform padding (cm) added equally to X, Y, and Z */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|3. Padding & Offset")
	float UniformPadding = 0.0f;

	/** Spatial offset (cm) applied to the formation locations */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|3. Padding & Offset")
	FVector LocationOffset = FVector::ZeroVector;

	/** Rotation offset applied to the formation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|3. Padding & Offset")
	FRotator RotationOffset = FRotator::ZeroRotator;

	/**
	 * When true, compensates for meshes whose pivot is not at the center of their bounding box
	 * (e.g. bottom-pivoted props) so the actual mesh geometry sits centered inside each padded cell.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|3. Padding & Offset")
	bool bCompensateMeshPivotOffset = true;

	/** When true, output locations are in World Space (using this component's transform) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|3. Padding & Offset")
	bool bOutputInWorldSpace = true;

	/** Automatically trigger an async formation calculation on BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Formation|Async")
	bool bGenerateOnBeginPlay = false;

	// =====================================================================
	// 4. Cached Output & Events
	// =====================================================================

	/** Most recently calculated mesh bounds with padding and offset */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Formation|Output")
	FMeshBoundsWithPadding LastComputedBounds;

	/** Most recently generated formation result */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Formation|Output")
	FMeshFormationResult LastFormationResult;

	/** True while an async background calculation is in flight */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh Formation|Output")
	bool bIsAsyncCalculationRunning = false;

	/** Fired on the Game Thread when any asynchronous location calculation finishes */
	UPROPERTY(BlueprintAssignable, Category = "Mesh Formation|Events")
	FOnMeshFormationLocationsReady OnFormationLocationsReady;

	// =====================================================================
	// 5. Public API — Bounds Determination
	// =====================================================================

	/**
	 * Sets the mesh reference object (UStaticMesh, USkeletalMesh, UPrimitiveComponent, or AActor)
	 * and immediately recalculates LastComputedBounds with current Padding and LocationOffset.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Bounds")
	FMeshBoundsWithPadding SetMeshReferenceAndDetermineBounds(UObject* InMeshObject);

	/**
	 * Determines the mesh bounds, padded cell size, and pivot offset using the component's current settings.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Bounds")
	FMeshBoundsWithPadding DetermineMeshBounds(const UObject* OptionalMeshOverride = nullptr);

	/**
	 * Pure helper to compute mesh bounds with explicit padding, uniform padding, offset, and scale.
	 */
	UFUNCTION(BlueprintPure, Category = "Mesh Formation|Bounds")
	FMeshBoundsWithPadding DetermineMeshBoundsWithSettings(
		const UObject* InMeshObject,
		FVector InPadding,
		float InUniformPadding,
		FVector InLocationOffset,
		FVector InMeshScale) const;

	// =====================================================================
	// 6. Dedicated ASYNC Functions Per Shape (Length, Width, Height, Radius, bHollow)
	// =====================================================================

	/**
	 * [1. CubeMatrix Async] Generates a 3D box matrix of (InLengthX * InWidthY * InHeightZ) objects.
	 * Example: InLengthX=5, InWidthY=4, InHeightZ=7 -> 140 locations.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Async")
	int32 GenerateCubeMatrixAsync(
		int32 InLengthX = 5,
		int32 InWidthY = 4,
		int32 InHeightZ = 7,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/**
	 * [2. Triangle Async] Generates a triangle formation with InLengthX rows from tip to back,
	 * InWidthY objects wide at the back base, stacked InHeightZ layers high.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Async")
	int32 GenerateTriangleFormationAsync(
		int32 InLengthX = 5,
		int32 InWidthY = 5,
		int32 InHeightZ = 1,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/**
	 * [3. Pyramid Async] Generates a 3D pyramid with base object counts (InBaseLengthX * InBaseWidthY) at the bottom,
	 * tapering upward over InHeightZ layers to a single point at the top.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Async")
	int32 GeneratePyramidFormationAsync(
		int32 InBaseLengthX = 5,
		int32 InBaseWidthY = 5,
		int32 InHeightZ = 5,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/**
	 * [4. Circle / Cylinder Async] Generates a circle/cylinder of InRadius object rings from center to edge,
	 * stacked InHeightZ layers tall.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Async")
	int32 GenerateCircleFormationAsync(
		int32 InRadius = 4,
		int32 InHeightZ = 1,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/**
	 * [5. Cone Async] Generates a 3D cone starting with InBaseRadius object rings at the bottom
	 * and tapering upward over InHeightZ layers to a single tip at the top.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Async")
	int32 GenerateConeFormationAsync(
		int32 InBaseRadius = 4,
		int32 InHeightZ = 6,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/**
	 * [6. HexagonalGrid Async] Generates a honeycomb staggered grid of InLengthX * InWidthY * InHeightZ objects.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Async")
	int32 GenerateHexagonalGridAsync(
		int32 InLengthX = 5,
		int32 InWidthY = 4,
		int32 InHeightZ = 1,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/**
	 * [7. HexagonRing Async] Generates a true 6-sided hexagon shape of InRadius object rings from center to corner,
	 * stacked InHeightZ layers tall.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Async")
	int32 GenerateHexagonRingAsync(
		int32 InRadius = 3,
		int32 InHeightZ = 1,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/**
	 * [8. Sphere Async] Generates a 3D sphere of InRadius object shells from center to surface.
	 * Set bInHollow = true for a hollow sphere shell, or false for a solid filled sphere.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Async")
	int32 GenerateSphereFormationAsync(
		int32 InRadius = 4,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/**
	 * [9. SpiralHelix Async] Generates a flat spiral (if InHeightZ == 1) or a 3D ascending helix (if InHeightZ > 1)
	 * with radius InRadius object rings.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Async")
	int32 GenerateSpiralHelixAsync(
		int32 InRadius = 3,
		int32 InHeightZ = 5,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/**
	 * [10. Diamond Async] Generates a Diamond / 3D Octahedron with middle object counts (InLengthX * InWidthY)
	 * and total height InHeightZ layers.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Async")
	int32 GenerateDiamondFormationAsync(
		int32 InLengthX = 5,
		int32 InWidthY = 5,
		int32 InHeightZ = 5,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	// =====================================================================
	// 7. Dedicated SYNC Functions Per Shape (Immediate Return)
	// =====================================================================

	/** [1. CubeMatrix Sync] Immediately generates a 3D box matrix of (InLengthX * InWidthY * InHeightZ) */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Sync")
	FMeshFormationResult GenerateCubeMatrixSync(
		TArray<FVector>& OutLocations,
		FMeshBoundsWithPadding& OutMeshBounds,
		int32 InLengthX = 5,
		int32 InWidthY = 4,
		int32 InHeightZ = 7,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/** [2. Triangle Sync] Immediately generates a triangle formation (InLengthX, InWidthY, InHeightZ) */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Sync")
	FMeshFormationResult GenerateTriangleFormationSync(
		TArray<FVector>& OutLocations,
		FMeshBoundsWithPadding& OutMeshBounds,
		int32 InLengthX = 5,
		int32 InWidthY = 5,
		int32 InHeightZ = 1,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/** [3. Pyramid Sync] Immediately generates a 3D pyramid (InBaseLengthX, InBaseWidthY, InHeightZ) */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Sync")
	FMeshFormationResult GeneratePyramidFormationSync(
		TArray<FVector>& OutLocations,
		FMeshBoundsWithPadding& OutMeshBounds,
		int32 InBaseLengthX = 5,
		int32 InBaseWidthY = 5,
		int32 InHeightZ = 5,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/** [4. Circle / Cylinder Sync] Immediately generates a circle/cylinder (InRadius, InHeightZ) */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Sync")
	FMeshFormationResult GenerateCircleFormationSync(
		TArray<FVector>& OutLocations,
		FMeshBoundsWithPadding& OutMeshBounds,
		int32 InRadius = 4,
		int32 InHeightZ = 1,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/** [5. Cone Sync] Immediately generates a 3D cone (InBaseRadius, InHeightZ) */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Sync")
	FMeshFormationResult GenerateConeFormationSync(
		TArray<FVector>& OutLocations,
		FMeshBoundsWithPadding& OutMeshBounds,
		int32 InBaseRadius = 4,
		int32 InHeightZ = 6,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/** [6. HexagonalGrid Sync] Immediately generates a staggered honeycomb grid (InLengthX, InWidthY, InHeightZ) */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Sync")
	FMeshFormationResult GenerateHexagonalGridSync(
		TArray<FVector>& OutLocations,
		FMeshBoundsWithPadding& OutMeshBounds,
		int32 InLengthX = 5,
		int32 InWidthY = 4,
		int32 InHeightZ = 1,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/** [7. HexagonRing Sync] Immediately generates a 6-sided hexagonal prism (InRadius, InHeightZ) */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Sync")
	FMeshFormationResult GenerateHexagonRingSync(
		TArray<FVector>& OutLocations,
		FMeshBoundsWithPadding& OutMeshBounds,
		int32 InRadius = 3,
		int32 InHeightZ = 1,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/** [8. Sphere Sync] Immediately generates a 3D sphere (InRadius) */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Sync")
	FMeshFormationResult GenerateSphereFormationSync(
		TArray<FVector>& OutLocations,
		FMeshBoundsWithPadding& OutMeshBounds,
		int32 InRadius = 4,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/** [9. SpiralHelix Sync] Immediately generates a spiral or 3D helix (InRadius, InHeightZ) */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Sync")
	FMeshFormationResult GenerateSpiralHelixSync(
		TArray<FVector>& OutLocations,
		FMeshBoundsWithPadding& OutMeshBounds,
		int32 InRadius = 3,
		int32 InHeightZ = 5,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	/** [10. Diamond Sync] Immediately generates a Diamond / 3D Octahedron (InLengthX, InWidthY, InHeightZ) */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Shapes Sync")
	FMeshFormationResult GenerateDiamondFormationSync(
		TArray<FVector>& OutLocations,
		FMeshBoundsWithPadding& OutMeshBounds,
		int32 InLengthX = 5,
		int32 InWidthY = 5,
		int32 InHeightZ = 5,
		bool bInHollow = false,
		FVector InPadding = FVector(10.0f, 10.0f, 10.0f),
		FVector InLocationOffset = FVector::ZeroVector,
		const UObject* OptionalMeshOverride = nullptr);

	// =====================================================================
	// 8. Generic / Component-Configured Generation & Debug
	// =====================================================================

	/**
	 * Uses the currently selected FormationShape and settings configured on this component
	 * to calculate locations on a background thread. Broadcasts OnFormationLocationsReady when complete.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Async")
	int32 GenerateFormationLocationsAsync(const UObject* OptionalMeshOverride = nullptr);

	/**
	 * Synchronously generates locations using the component's currently selected FormationShape and settings.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation")
	FMeshFormationResult GenerateFormationLocationsSync(
		TArray<FVector>& OutLocations,
		FMeshBoundsWithPadding& OutMeshBounds,
		const UObject* OptionalMeshOverride = nullptr);

	/**
	 * C++ async overload accepting a completion callback executed on the Game Thread.
	 */
	int32 GenerateFormationLocationsAsyncWithCallback(
		const UObject* OptionalMeshOverride,
		TFunction<void(const FMeshFormationResult&)> OnCompletedCallback);

	/**
	 * Draws debug wireframes in the viewport showing each padded mesh cell, location point, and total bounds.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Debug")
	void DrawDebugFormation(
		float Duration = 5.0f,
		bool bDrawPaddedCells = true,
		bool bDrawLocationPoints = true,
		bool bDrawTotalBounds = true,
		FLinearColor CellColor = FLinearColor(0.1f, 0.8f, 1.0f, 1.0f),
		FLinearColor BoundsColor = FLinearColor(1.0f, 0.5f, 0.0f, 1.0f)) const;

	// =====================================================================
	// 9. Dedicated Static Shape Builders (Thread-Safe, Low-Memory In-Place)
	// =====================================================================

	static FMeshFormationResult ComputeFormationData(const FMeshFormationAsyncParams& Params);

	static void BuildCubeMatrixLocations(
		int32 InLengthX,
		int32 InWidthY,
		int32 InHeightZ,
		const FVector& PaddedCellSize,
		bool bInHollow,
		int32 MaxLimit,
		TArray<FVector>& OutLocalCellCenters);

	static void BuildTriangleLocations(
		int32 InLengthX,
		int32 InWidthY,
		int32 InHeightZ,
		const FVector& PaddedCellSize,
		bool bInHollow,
		int32 MaxLimit,
		TArray<FVector>& OutLocalCellCenters);

	static void BuildPyramidLocations(
		int32 InBaseLengthX,
		int32 InBaseWidthY,
		int32 InHeightZ,
		const FVector& PaddedCellSize,
		bool bInHollow,
		int32 MaxLimit,
		TArray<FVector>& OutLocalCellCenters);

	static void BuildCircleLocations(
		int32 InRadius,
		int32 InHeightZ,
		const FVector& PaddedCellSize,
		bool bInHollow,
		int32 MaxLimit,
		TArray<FVector>& OutLocalCellCenters);

	static void BuildConeLocations(
		int32 InBaseRadius,
		int32 InHeightZ,
		const FVector& PaddedCellSize,
		bool bInHollow,
		int32 MaxLimit,
		TArray<FVector>& OutLocalCellCenters);

	static void BuildHexagonalGridLocations(
		int32 InLengthX,
		int32 InWidthY,
		int32 InHeightZ,
		const FVector& PaddedCellSize,
		bool bInHollow,
		int32 MaxLimit,
		TArray<FVector>& OutLocalCellCenters);

	static void BuildHexagonRingLocations(
		int32 InRadius,
		int32 InHeightZ,
		const FVector& PaddedCellSize,
		bool bInHollow,
		int32 MaxLimit,
		TArray<FVector>& OutLocalCellCenters);

	static void BuildSphereLocations(
		int32 InRadius,
		const FVector& PaddedCellSize,
		bool bInHollow,
		int32 MaxLimit,
		TArray<FVector>& OutLocalCellCenters);

	static void BuildSpiralHelixLocations(
		int32 InRadius,
		int32 InHeightZ,
		const FVector& PaddedCellSize,
		bool bInHollow,
		int32 MaxLimit,
		TArray<FVector>& OutLocalCellCenters);

	static void BuildDiamondLocations(
		int32 InLengthX,
		int32 InWidthY,
		int32 InHeightZ,
		const FVector& PaddedCellSize,
		bool bInHollow,
		int32 MaxLimit,
		TArray<FVector>& OutLocalCellCenters);

protected:
	/** Resolves the effective UObject to extract mesh bounds from */
	const UObject* ResolveEffectiveMeshObject(const UObject* OptionalOverride) const;

	/** Extracts the raw unscaled FBox from a UStaticMesh, USkeletalMesh, UPrimitiveComponent, or AActor */
	bool ExtractRawBoundingBox(const UObject* MeshObject, FBox& OutRawBox, FVector& OutComponentScale) const;

	/** Builds the thread-safe parameter snapshot for current component state */
	FMeshFormationAsyncParams BuildAsyncParamsSnapshot(const UObject* OptionalMeshOverride);

private:
	int32 NextRequestID = 1;
};

// =========================================================================
// Latent Blueprint Async Action Nodes (Dedicated Per Shape, Memory-Safe)
// =========================================================================

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FOnGenerateMeshFormationAsyncOutput,
	const TArray<FVector>&, Locations,
	const TArray<FTransform>&, Transforms,
	const FMeshBoundsWithPadding&, MeshBounds,
	int32, TotalCount);

/**
 * Latent Blueprint Async nodes providing simple Length, Width, Height, Radius, and bHollow inputs for each shape.
 */
UCLASS()
class MARRYBYMIDNIGHT_API UGenerateMeshFormationAsyncAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/** Fired on the Game Thread when the background formation calculation completes */
	UPROPERTY(BlueprintAssignable)
	FOnGenerateMeshFormationAsyncOutput OnCompleted;

	/** [1. CubeMatrix] Async node with LengthX, WidthY, HeightZ, and bHollow */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Async Shapes", meta = (BlueprintInternalUseOnly = "true", DisplayName = "1. Generate Cube Matrix Locations (Async)"))
	static UGenerateMeshFormationAsyncAction* GenerateCubeMatrixAsyncNode(
		UMeshFormationComponent* FormationComponent,
		const UObject* OptionalMeshOverride = nullptr,
		int32 LengthX = 5,
		int32 WidthY = 4,
		int32 HeightZ = 7,
		bool bHollow = false,
		FVector Padding = FVector(10.0f, 10.0f, 10.0f),
		FVector LocationOffset = FVector::ZeroVector,
		FVector MeshScale = FVector(1.0f, 1.0f, 1.0f));

	/** [2. Triangle] Async node with LengthX, WidthY, HeightZ, and bHollow */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Async Shapes", meta = (BlueprintInternalUseOnly = "true", DisplayName = "2. Generate Triangle Formation Locations (Async)"))
	static UGenerateMeshFormationAsyncAction* GenerateTriangleAsyncNode(
		UMeshFormationComponent* FormationComponent,
		const UObject* OptionalMeshOverride = nullptr,
		int32 LengthX = 5,
		int32 WidthY = 5,
		int32 HeightZ = 1,
		bool bHollow = false,
		FVector Padding = FVector(10.0f, 10.0f, 10.0f),
		FVector LocationOffset = FVector::ZeroVector,
		FVector MeshScale = FVector(1.0f, 1.0f, 1.0f));

	/** [3. Pyramid] Async node with BaseLengthX, BaseWidthY, HeightZ, and bHollow */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Async Shapes", meta = (BlueprintInternalUseOnly = "true", DisplayName = "3. Generate Pyramid Formation Locations (Async)"))
	static UGenerateMeshFormationAsyncAction* GeneratePyramidAsyncNode(
		UMeshFormationComponent* FormationComponent,
		const UObject* OptionalMeshOverride = nullptr,
		int32 BaseLengthX = 5,
		int32 BaseWidthY = 5,
		int32 HeightZ = 5,
		bool bHollow = false,
		FVector Padding = FVector(10.0f, 10.0f, 10.0f),
		FVector LocationOffset = FVector::ZeroVector,
		FVector MeshScale = FVector(1.0f, 1.0f, 1.0f));

	/** [4. Circle / Cylinder] Async node with Radius, HeightZ, and bHollow */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Async Shapes", meta = (BlueprintInternalUseOnly = "true", DisplayName = "4. Generate Circle / Cylinder Locations (Async)"))
	static UGenerateMeshFormationAsyncAction* GenerateCircleAsyncNode(
		UMeshFormationComponent* FormationComponent,
		const UObject* OptionalMeshOverride = nullptr,
		int32 Radius = 4,
		int32 HeightZ = 1,
		bool bHollow = false,
		FVector Padding = FVector(10.0f, 10.0f, 10.0f),
		FVector LocationOffset = FVector::ZeroVector,
		FVector MeshScale = FVector(1.0f, 1.0f, 1.0f));

	/** [5. Cone] Async node with BaseRadius, HeightZ, and bHollow */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Async Shapes", meta = (BlueprintInternalUseOnly = "true", DisplayName = "5. Generate Cone Formation Locations (Async)"))
	static UGenerateMeshFormationAsyncAction* GenerateConeAsyncNode(
		UMeshFormationComponent* FormationComponent,
		const UObject* OptionalMeshOverride = nullptr,
		int32 BaseRadius = 4,
		int32 HeightZ = 6,
		bool bHollow = false,
		FVector Padding = FVector(10.0f, 10.0f, 10.0f),
		FVector LocationOffset = FVector::ZeroVector,
		FVector MeshScale = FVector(1.0f, 1.0f, 1.0f));

	/** [6. HexagonalGrid] Async node with LengthX, WidthY, HeightZ, and bHollow */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Async Shapes", meta = (BlueprintInternalUseOnly = "true", DisplayName = "6. Generate Hexagonal Grid Locations (Async)"))
	static UGenerateMeshFormationAsyncAction* GenerateHexagonalGridAsyncNode(
		UMeshFormationComponent* FormationComponent,
		const UObject* OptionalMeshOverride = nullptr,
		int32 LengthX = 5,
		int32 WidthY = 4,
		int32 HeightZ = 1,
		bool bHollow = false,
		FVector Padding = FVector(10.0f, 10.0f, 10.0f),
		FVector LocationOffset = FVector::ZeroVector,
		FVector MeshScale = FVector(1.0f, 1.0f, 1.0f));

	/** [7. HexagonRing] Async node with Radius, HeightZ, and bHollow */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Async Shapes", meta = (BlueprintInternalUseOnly = "true", DisplayName = "7. Generate Hexagon Shape Locations (Async)"))
	static UGenerateMeshFormationAsyncAction* GenerateHexagonRingAsyncNode(
		UMeshFormationComponent* FormationComponent,
		const UObject* OptionalMeshOverride = nullptr,
		int32 Radius = 3,
		int32 HeightZ = 1,
		bool bHollow = false,
		FVector Padding = FVector(10.0f, 10.0f, 10.0f),
		FVector LocationOffset = FVector::ZeroVector,
		FVector MeshScale = FVector(1.0f, 1.0f, 1.0f));

	/** [8. Sphere] Async node with Radius and bHollow */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Async Shapes", meta = (BlueprintInternalUseOnly = "true", DisplayName = "8. Generate Sphere Formation Locations (Async)"))
	static UGenerateMeshFormationAsyncAction* GenerateSphereAsyncNode(
		UMeshFormationComponent* FormationComponent,
		const UObject* OptionalMeshOverride = nullptr,
		int32 Radius = 4,
		bool bHollow = false,
		FVector Padding = FVector(10.0f, 10.0f, 10.0f),
		FVector LocationOffset = FVector::ZeroVector,
		FVector MeshScale = FVector(1.0f, 1.0f, 1.0f));

	/** [9. SpiralHelix] Async node with Radius, HeightZ, and bHollow */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Async Shapes", meta = (BlueprintInternalUseOnly = "true", DisplayName = "9. Generate Spiral / Helix Locations (Async)"))
	static UGenerateMeshFormationAsyncAction* GenerateSpiralHelixAsyncNode(
		UMeshFormationComponent* FormationComponent,
		const UObject* OptionalMeshOverride = nullptr,
		int32 Radius = 3,
		int32 HeightZ = 5,
		bool bHollow = false,
		FVector Padding = FVector(10.0f, 10.0f, 10.0f),
		FVector LocationOffset = FVector::ZeroVector,
		FVector MeshScale = FVector(1.0f, 1.0f, 1.0f));

	/** [10. Diamond] Async node with LengthX, WidthY, HeightZ, and bHollow */
	UFUNCTION(BlueprintCallable, Category = "Mesh Formation|Async Shapes", meta = (BlueprintInternalUseOnly = "true", DisplayName = "10. Generate Diamond / Octahedron Locations (Async)"))
	static UGenerateMeshFormationAsyncAction* GenerateDiamondAsyncNode(
		UMeshFormationComponent* FormationComponent,
		const UObject* OptionalMeshOverride = nullptr,
		int32 LengthX = 5,
		int32 WidthY = 5,
		int32 HeightZ = 5,
		bool bHollow = false,
		FVector Padding = FVector(10.0f, 10.0f, 10.0f),
		FVector LocationOffset = FVector::ZeroVector,
		FVector MeshScale = FVector(1.0f, 1.0f, 1.0f));

	virtual void Activate() override;

private:
	static UGenerateMeshFormationAsyncAction* CreateShapeAction(
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
		const FVector& InMeshScale);

	UPROPERTY()
	TObjectPtr<UMeshFormationComponent> TargetComponent = nullptr;

	UPROPERTY()
	TObjectPtr<const UObject> MeshOverride = nullptr;

	EMeshFormationShape RequestedShape = EMeshFormationShape::CubeMatrix;
	int32 RequestedLengthX = 5;
	int32 RequestedWidthY = 4;
	int32 RequestedHeightZ = 7;
	int32 RequestedRadius = 4;
	bool bRequestedHollow = false;
	FVector RequestedPadding = FVector(10.0f, 10.0f, 10.0f);
	FVector RequestedOffset = FVector::ZeroVector;
	FVector RequestedMeshScale = FVector::OneVector;
};
