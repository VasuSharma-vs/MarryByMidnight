#include "Components/RocketProjectileComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"
#include "CollisionQueryParams.h"
#include "TimerManager.h"
#include "GameFramework/GameStateBase.h"
#include "DrawDebugHelpers.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropManager.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "NavigationSystem.h"

URocketProjectileComponent::URocketProjectileComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	SetIsReplicatedByDefault(true);

	bEnableDebugLogs = true;
	bShowOnScreenDebug = true;
	bDrawDebugTrajectory = true;
	LastDebugTrailLocation = FVector::ZeroVector;

	// Default settings
	bTipOverToFly = true;
	TipOverDuration = 0.08f;
	TipOverHopSpeed = 150.0f;
	TipOverAngleMin = 35.0f;
	TipOverAngleMax = 70.0f;

	BaseSpeed = 2400.0f;
	MaxFuelDuration = 12.0f;
	MaxFuelDistance = 5000.0f;
	FlightBounceRandomDeflection = 45.0f;
	TotalFlightDistanceTraveled = 0.0f;
	TimeSpentFalling = 0.0f;
	GravityZ = -980.0f;
	InFlightGravityScale = 0.15f;

	NozzleAxis = ERocketMeshAxis::PositiveZ;
	bAlignRotationToVelocity = true;
	CosmeticWobbleFrequency = 20.0f;
	CosmeticWobbleDegrees = 4.0f;

	PivotCompensationMode = ERocketPivotCompensationMode::AutoDetectFromBounds;
	ManualCenterOffset = FVector::ZeroVector;
	bPreventGroundPenetrationOnTip = true;

	CollisionChannel = ECC_WorldDynamic;
	SweepRadius = 6.0f;
	Bounciness = 0.75f;
	bBounceAfterFuelDepleted = true;
	SpentBounciness = 0.28f;
	SpentFriction = 0.5f;
	RicochetRandomDeflection = 10.0f;
	MaxBounces = 25;
	bLimitFlightByMaxBounces = false;
	RicochetDebounceTime = 0.05f;
	RestVelocityThreshold = 45.0f;

	bEnableDebugLogs = true;
	bShowOnScreenDebug = true;
	bDrawDebugTrajectory = true;
	DebugHeartbeatTimer = 0.0f;
	TravelHistoryLogTimer = 0.0f;
	PhysicsSettledTimeAccumulated = 0.0f;

	ClientCatchupDuration = 0.0f;
	ClientCatchupTimeRemaining = 0.0f;
	ClientCatchupSpeedMultiplier = 1.0f;
	ClientReconciliationOffset = FVector::ZeroVector;

	LaunchSeed = -1;
	bPendingLaunch = false;
	PendingServerLaunchTime = 0.0f;
	PendingLaunchDirection = FVector::ForwardVector;
	SimulationTimeAccumulator = 0.0f;

	DragElapsedTime = 0.0f;
	TotalDragDuration = 0.45f;
	DragStartAngleRad = 0.0f;
	InitialGroundZOffset = 6.0f;
	DragCurrentSpeed = 0.0f;

	TippingElapsedTime = 0.0f;
	TotalTippingDuration = 0.22f;

	// Synchronize struct movement settings with baseline property defaults
	FlightSettings.BaseSpeed = BaseSpeed;
	FlightSettings.BaseSplineLength = MaxFuelDistance;
	FlightSettings.MaxFuelDuration = MaxFuelDuration;
	FlightSettings.MaxFuelDistance = MaxFuelDistance;
	FlightSettings.MinWaypointSpacing = 650.0f;
	FlightSettings.MaxWaypointSpacing = 1200.0f;
	FlightSettings.MinOffsetPointSpacing = MinOffsetPointSpacing;
	FlightSettings.MaxOffsetPointSpacing = MaxOffsetPointSpacing;
	FlightSettings.SplineCornerSmoothness = SplineCornerSmoothness;
	FlightSettings.LowFuelThrustDistanceMin = 250.0f;
	FlightSettings.LowFuelThrustDistanceMax = 450.0f;
	FlightSettings.FloorTraceMaxDistance = 20000.0f;
	FlightSettings.BounceTraceMaxDistance = 300.0f;
	FlightSettings.FlightBounceRandomDeflection = FlightBounceRandomDeflection;
	FlightSettings.GravityZ = GravityZ;
	FlightSettings.InFlightGravityScale = InFlightGravityScale;
	FlightSettings.bAlignRotationToVelocity = bAlignRotationToVelocity;

	DraggingSettings.MinDragDuration = MinDragDuration;
	DraggingSettings.MaxDragDuration = MaxDragDuration;

	TippingSettings.bEnableTipping = bTipOverToFly;

	LowFuelSettings.bEnableLowFuelSputter = bExhaustSputterNearEnd;
	LowFuelSettings.LowFuelPhaseDuration = ExhaustPhaseDuration;
	LowFuelSettings.PitchUpAngle = ExhaustPitchUpAngle;
	LowFuelSettings.UpwardThrustImpulse = ExhaustUpwardThrust;
	LowFuelSettings.WobbleMultiplier = ExhaustWobbleMultiplier;
	LowFuelSettings.MaintainUpDirectionThreshold = MaintainUpDirectionThreshold;

	FallingSettings.ApexHangDuration = 0.12f;
	FallingSettings.FallAcceleration = 3000.0f;
	FallingSettings.bHandoffToPhysicsOnDepleted = bHandoffToPhysicsOnDepleted;
	FallingSettings.bBounceAfterFuelDepleted = bBounceAfterFuelDepleted;
	FallingSettings.MinFallingBounces = 1;
	FallingSettings.MaxFallingBounces = 3;
	FallingSettings.SpentBounciness = SpentBounciness;
	FallingSettings.SpentFriction = SpentFriction;
	FallingSettings.RicochetRandomDeflection = RicochetRandomDeflection;
	FallingSettings.RicochetDebounceTime = RicochetDebounceTime;

	AtRestSettings.RestVelocityThreshold = RestVelocityThreshold;
}

void URocketProjectileComponent::InitializeTipping(const FVector& InLaunchHeading)
{
	AutoResolveTargetPrimitive();

	TipOverLaunchHeading = InLaunchHeading.GetSafeNormal();
	if (TipOverLaunchHeading.IsNearlyZero())
	{
		TipOverLaunchHeading = GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector;
	}

	TippingStartCenter = GetWorldCenterLocation();
	TippingStartRotation = TargetPrimitive ? TargetPrimitive->GetComponentRotation() : FRotator::ZeroRotator;
	TippingElapsedTime = 0.0f;
	TotalTippingDuration = FMath::Max(0.05f, TippingSettings.TippingDuration);

	// Find the floor underneath the bottle
	FHitResult GroundHit;
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(RocketTippingTrace), false, GetOwner());
	const FVector TraceStart = TippingStartCenter;
	const FVector TraceEnd = TraceStart - FVector(0.0f, 0.0f, 600.0f);

	FVector GroundHitPoint = TraceStart - FVector(0.0f, 0.0f, 15.0f);
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams))
	{
		GroundHitPoint = GroundHit.ImpactPoint;
		TippingFloorNormal = GroundHit.Normal;
	}
	else
	{
		TippingFloorNormal = FVector::UpVector;
	}

	// Floor heading: horizontal direction along the ground plane towards launch heading (Orange line in Image 1)
	TippingFloorHeading = (TipOverLaunchHeading - (TipOverLaunchHeading | TippingFloorNormal) * TippingFloorNormal).GetSafeNormal();
	if (TippingFloorHeading.IsNearlyZero())
	{
		TippingFloorHeading = GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector;
		TippingFloorHeading = (TippingFloorHeading - (TippingFloorHeading | TippingFloorNormal) * TippingFloorNormal).GetSafeNormal();
	}

	// Determine dimensions from bounding box
	float BottleRadius = 4.0f;
	float HalfHeight = 14.0f;
	if (TargetPrimitive)
	{
		const FBoxSphereBounds Bounds = TargetPrimitive->GetLocalBounds();
		const FVector Extent = Bounds.BoxExtent * TargetPrimitive->GetComponentScale().GetAbs();
		HalfHeight = FMath::Max(Extent.Z, 10.0f);
		BottleRadius = FMath::Max(FMath::Max(Extent.X, Extent.Y), 3.0f);
	}

	// Pivot point on the ground surface (Purple Circle in Image 1):
	// The contact edge at the bottom of the bottle in the direction of the tip
	if (TippingSettings.bAutoDetectFloorPivot)
	{
		TippingPivotPoint = GroundHitPoint + (TippingFloorHeading * BottleRadius);
	}
	else
	{
		TippingPivotPoint = GroundHitPoint + TippingSettings.ManualPivotOffset;
	}

	// Target rotation when fully flat on the ground (Image 3):
	// Bottle length is horizontal along TippingFloorHeading with nozzle pointing backwards (-TippingFloorHeading)
	TippingTargetRotation = ComputeTargetRotation(TippingFloorHeading, 0.016f);

	// Target center location when lying flat on the floor:
	// Rests horizontally on floor at height BottleRadius, positioned along the floor heading
	TippingTargetCenter = GroundHitPoint + (TippingFloorNormal * BottleRadius) + (TippingFloorHeading * (HalfHeight * 0.4f));

	CurrentVelocity = FVector::ZeroVector;

	LogDebug(FString::Printf(TEXT("InitializeTipping: Heading=%s, Normal=%s, Pivot=%s, Duration=%.2fs"),
		*TippingFloorHeading.ToString(), *TippingFloorNormal.ToString(), *TippingPivotPoint.ToString(), TotalTippingDuration));
}

void URocketProjectileComponent::InitializeDragging(float InServerLaunchTime, int32 InSeed)
{
	AutoResolveTargetPrimitive();

	FRandomStream DragStream(InSeed > 0 ? InSeed : 1);
	TotalDragDuration = DragStream.FRandRange(DraggingSettings.MinDragDuration, DraggingSettings.MaxDragDuration);
	DragStartAngleRad = DragStream.FRandRange(0.0f, TWO_PI);

	const bool bIsServer = GetOwner() ? GetOwner()->HasAuthority() : false;
	if (bIsServer)
	{
		DragElapsedTime = 0.0f;
	}
	else
	{
		// Ping absorption: if client received launch late, subtract lag so both machines exit drag at same moment
		const float ClientWorldTime = GetSynchronizedServerTime();
		const float NetLag = FMath::Max(0.0f, ClientWorldTime - InServerLaunchTime);
		DragElapsedTime = NetLag;
	}

	DragStartCenter = GetWorldCenterLocation();
	DragCenterLocation = DragStartCenter;

	// Find ground normal under dragging start
	FHitResult GroundHit;
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(RocketDragTrace), false, GetOwner());
	const FVector TraceEnd = DragStartCenter - FVector(0.0f, 0.0f, 500.0f);
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(GroundHit, DragStartCenter, TraceEnd, ECC_WorldStatic, TraceParams))
	{
		DragSurfaceNormal = GroundHit.Normal;
	}
	else
	{
		DragSurfaceNormal = TippingFloorNormal;
	}

	// Calculate tangent vectors U and V on the surface
	DragTangentU = FVector::CrossProduct(DragSurfaceNormal, FVector::ForwardVector);
	if (DragTangentU.SizeSquared() < 0.01f)
	{
		DragTangentU = FVector::CrossProduct(DragSurfaceNormal, FVector::RightVector);
	}
	DragTangentU.Normalize();
	DragTangentV = FVector::CrossProduct(DragSurfaceNormal, DragTangentU).GetSafeNormal();

	// Blue Circle Waypoint (Images 4 and 5): point in space where flying state starts
	DragTakeoffPoint = DragStartCenter + (TipOverLaunchHeading * DraggingSettings.TakeoffDistance);

	DragCurrentSpeed = FMath::Max(CurrentVelocity.Size(), 200.0f);

	LogDebug(FString::Printf(TEXT("InitializeDragging: Start=%s, TakeoffBlueCircle=%s, Duration=%.2fs, Elapsed=%.2fs"),
		*DragStartCenter.ToString(), *DragTakeoffPoint.ToString(), TotalDragDuration, DragElapsedTime));
}

void URocketProjectileComponent::InitializeFlightPath(const FVector& InStartLoc, const FVector& InFlightDir, int32 InSeed)
{
	AutoResolveTargetPrimitive();

	FlightStartLocation = InStartLoc;
	FlightBaselineDir = InFlightDir.GetSafeNormal();
	if (FlightBaselineDir.IsNearlyZero())
	{
		FlightBaselineDir = TipOverLaunchHeading;
	}
	FlightDistanceTraveled = 0.0f;
	FlightWaypoints.Empty();

	const float TravelSpeed = FlightSettings.BaseSpeed > 0.0f ? FlightSettings.BaseSpeed : 2400.0f;
	const float FlightDuration = MaxFuelDuration > 0.0f ? MaxFuelDuration : 10.0f;
	const float MaxBaselineDist = FMath::Clamp(TravelSpeed * FlightDuration, 1000.0f, 25000.0f);

	// Check if an authoritative pre-calculated node exists for the end of this leg
	float EffectiveBaselineLength = MaxBaselineDist;
	if (AuthoritativeFlightNodes.IsValidIndex(CurrentFlightPlanNodeIndex + 1))
	{
		const FVector TargetNode = AuthoritativeFlightNodes[CurrentFlightPlanNodeIndex + 1];
		const FVector ToTarget = TargetNode - FlightStartLocation;
		if (!ToTarget.IsNearlyZero())
		{
			FlightBaselineDir = ToTarget.GetSafeNormal();
		}
		EffectiveBaselineLength = FMath::Max(50.0f, ToTarget.Size());
	}
	else
	{
		// Fallback: perform a forward line trace along the flight heading to find obstacle hit point (Image 1: orange baseline)
		FHitResult BaselineHit;
		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(RocketFlightBaselineTrace), false, GetOwner());
		const FVector TraceEnd = FlightStartLocation + (FlightBaselineDir * MaxBaselineDist);
		if (GetWorld() && GetWorld()->LineTraceSingleByChannel(BaselineHit, FlightStartLocation, TraceEnd, ECC_Visibility, TraceParams))
		{
			EffectiveBaselineLength = FMath::Max(150.0f, BaselineHit.Distance);
		}
	}
	FlightBaselineLength = EffectiveBaselineLength;

	// Perpendicular basis in the plane normal to FlightBaselineDir
	// PerpendicularU is strictly horizontal in the world XY plane:
	FVector PerpendicularU = FVector::CrossProduct(FlightBaselineDir, FVector::UpVector);
	if (PerpendicularU.SizeSquared() < 0.01f)
	{
		PerpendicularU = FVector::CrossProduct(FlightBaselineDir, FVector::RightVector);
	}
	PerpendicularU.Normalize();
	const FVector PerpendicularV = FVector::CrossProduct(FlightBaselineDir, PerpendicularU).GetSafeNormal();

	// Waypoint generation
	FlightWaypoints.Add(FlightStartLocation);

	// Check if the next node has a subsequent node after it (open-air transition lookahead)
	FVector UpcomingTurnDir = FVector::ZeroVector;
	const bool bHasUpcomingTurn = AuthoritativeFlightNodes.IsValidIndex(CurrentFlightPlanNodeIndex + 2);
	if (bHasUpcomingTurn)
	{
		const FVector Node1 = AuthoritativeFlightNodes[CurrentFlightPlanNodeIndex + 1];
		const FVector Node2 = AuthoritativeFlightNodes[CurrentFlightPlanNodeIndex + 2];
		UpcomingTurnDir = (Node2 - Node1).GetSafeNormal();
	}

	if (FlightSettings.bEnableErraticPath)
	{
		FRandomStream PathStream(InSeed > 0 ? InSeed : 1);
		const float SpacingMin = FMath::Clamp(FMath::Min(FlightSettings.MinWaypointSpacing, FlightSettings.MaxWaypointSpacing), 50.0f, 2000.0f);
		const float SpacingMax = FMath::Clamp(FMath::Max(FlightSettings.MinWaypointSpacing, FlightSettings.MaxWaypointSpacing), 50.0f, 3000.0f);

		float CurrentDist = 0.0f;
		int32 SegmentIndex = 0;

		while (CurrentDist < EffectiveBaselineLength - SpacingMin * 0.4f)
		{
			const float StepDist = FlightSettings.bUseRandomWaypointSpacing
				? PathStream.FRandRange(SpacingMin, SpacingMax)
				: (FlightSettings.WaypointSpacing > 0.0f ? FlightSettings.WaypointSpacing : 300.0f);

			CurrentDist += StepDist;
			if (CurrentDist >= EffectiveBaselineLength - SpacingMin * 0.3f)
			{
				break;
			}
			SegmentIndex++;

			const float SegmentProgress = CurrentDist / EffectiveBaselineLength;
			const FVector BasePt = FlightStartLocation + (FlightBaselineDir * CurrentDist);

			FVector Offset = FVector::ZeroVector;

			if (FlightSettings.ZigZagMode == ERocketZigZagMode::Random3D)
			{
				// Full 3D random wander in perpendicular plane
				const float LatDev = PathStream.FRandRange(FlightSettings.MinPathDeviation, FlightSettings.MaxPathDeviation);
				const float RandomAngle = PathStream.FRandRange(0.0f, 2.0f * PI);
				const float LatOffset = FMath::Cos(RandomAngle) * LatDev;

				Offset += PerpendicularU * LatOffset;

				if (FlightSettings.bEnable3DZigZag)
				{
					const float VertDev = PathStream.FRandRange(FlightSettings.MinVerticalDeviation, FlightSettings.MaxVerticalDeviation);
					const float VertOffset = FMath::Sin(RandomAngle) * FMath::Abs(VertDev) * (VertDev >= 0.0f ? 1.0f : -1.0f);
					Offset += PerpendicularV * VertOffset;
				}
				else
				{
					// Slight upward wobble if 3D disabled
					const float VertOffset = PathStream.FRandRange(0.0f, FMath::Abs(LatOffset) * 0.25f);
					Offset += FVector::UpVector * VertOffset;
				}
			}
			else if (FlightSettings.ZigZagMode == ERocketZigZagMode::Corkscrew)
			{
				// 3D Helical spiral corkscrew
				const float Turns = (CurrentDist / 1000.0f) * FlightSettings.CorkscrewPitchFrequency;
				const float Angle = Turns * 2.0f * PI;
				const float RadiusU = PathStream.FRandRange(FlightSettings.MinPathDeviation, FlightSettings.MaxPathDeviation);
				Offset += PerpendicularU * (FMath::Cos(Angle) * RadiusU);

				if (FlightSettings.bEnable3DZigZag)
				{
					const float RadiusV = PathStream.FRandRange(FMath::Max(10.0f, FMath::Abs(FlightSettings.MinVerticalDeviation)), FlightSettings.MaxVerticalDeviation);
					Offset += PerpendicularV * (FMath::Sin(Angle) * RadiusV);
				}
			}
			else // ERocketZigZagMode::Alternating
			{
				// Alternating side-to-side and up-down
				const float SideSign = (SegmentIndex % 2 == 1) ? 1.0f : -1.0f;
				const float LatDev = PathStream.FRandRange(FlightSettings.MinPathDeviation, FlightSettings.MaxPathDeviation);
				Offset += PerpendicularU * (LatDev * SideSign);

				if (FlightSettings.bEnable3DZigZag)
				{
					const float VertSign = ((SegmentIndex / 2) % 2 == 1) ? 1.0f : -0.7f;
					const float VertDev = PathStream.FRandRange(FMath::Abs(FlightSettings.MinVerticalDeviation), FlightSettings.MaxVerticalDeviation);
					Offset += PerpendicularV * (VertDev * VertSign);
				}
				else
				{
					const float VertOffset = PathStream.FRandRange(0.0f, LatDev * 0.30f);
					Offset += FVector::UpVector * VertOffset;
				}
			}

			FVector Waypoint = BasePt + Offset;

			// If nearing the end of an open-air leg with an upcoming turn, smoothly blend the waypoint towards the next direction
			if (bHasUpcomingTurn && SegmentProgress > 0.65f)
			{
				const float TurnBlend = (SegmentProgress - 0.65f) / 0.35f;
				const float TurnLeadDist = FlightSettings.MaxPathDeviation * TurnBlend * 0.8f;
				Waypoint += UpcomingTurnDir * TurnLeadDist;
			}

			// Ground clearance safety trace for each waypoint:
			// Ensures waypoint never dips below ground level + sweep radius + 10cm
			FHitResult GroundCheck;
			FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(RocketWaypointGroundSafety), false, GetOwner());
			const FVector TraceStart = Waypoint + FVector(0.0f, 0.0f, 150.0f);
			const FVector TraceDown = Waypoint - FVector(0.0f, 0.0f, 300.0f);
			if (GetWorld() && GetWorld()->LineTraceSingleByChannel(GroundCheck, TraceStart, TraceDown, ECC_WorldStatic, GroundParams))
			{
				const float SafeMinZ = GroundCheck.ImpactPoint.Z + SweepRadius + 10.0f;
				if (Waypoint.Z < SafeMinZ)
				{
					Waypoint.Z = SafeMinZ;
				}
			}

			FlightWaypoints.Add(Waypoint);
		}
	}

	// Final destination waypoint on the baseline (exact target node if available)
	if (AuthoritativeFlightNodes.IsValidIndex(CurrentFlightPlanNodeIndex + 1))
	{
		FlightWaypoints.Add(AuthoritativeFlightNodes[CurrentFlightPlanNodeIndex + 1]);
	}
	else
	{
		FlightWaypoints.Add(FlightStartLocation + (FlightBaselineDir * EffectiveBaselineLength));
	}

	LogDebug(FString::Printf(TEXT("InitializeFlightPath: Start=%s, Dir=%s, BaselineLen=%.1f, Waypoints=%d, Seed=%d"),
		*FlightStartLocation.ToString(), *FlightBaselineDir.ToString(), FlightBaselineLength, FlightWaypoints.Num(), InSeed), true);

	if (bEnableDebugLogs || bShowOnScreenDebug)
	{
		for (int32 i = 0; i < FlightWaypoints.Num(); ++i)
		{
			LogDebug(FString::Printf(TEXT("  SplineWP[%d/%d]: Loc=%s"), i, FlightWaypoints.Num(), *FlightWaypoints[i].ToString()));
		}
	}
}

void URocketProjectileComponent::EvaluateFlightSpline(float Progress, FVector& OutLocation, FVector& OutTangent) const
{
	const int32 NumPts = FlightWaypoints.Num();
	if (NumPts == 0)
	{
		OutLocation = FlightStartLocation;
		OutTangent = FlightBaselineDir;
		return;
	}
	if (NumPts == 1)
	{
		OutLocation = FlightWaypoints[0];
		OutTangent = FlightBaselineDir;
		return;
	}

	// Progress is ALREADY normalized to [0.0, 1.0]!
	// Simply clamp it to [0.0, 1.0] - NEVER divide by FlightBaselineLength!
	const float AlphaTotal = FMath::Clamp(Progress, 0.0f, 1.0f);

	const int32 NumSegments = NumPts - 1;
	const float SegmentFloat = AlphaTotal * static_cast<float>(NumSegments);
	const int32 SegIndex = FMath::Clamp(FMath::FloorToInt(SegmentFloat), 0, NumSegments - 1);
	const float LocalAlpha = SegmentFloat - static_cast<float>(SegIndex);

	// Catmull-Rom 4-point cubic Hermite interpolation
	const FVector P0 = FlightWaypoints[FMath::Max(0, SegIndex - 1)];
	const FVector P1 = FlightWaypoints[SegIndex];
	const FVector P2 = FlightWaypoints[SegIndex + 1];
	const FVector P3 = FlightWaypoints[FMath::Min(NumPts - 1, SegIndex + 2)];

	const float SplineTension = FMath::Clamp(FlightSettings.SplineTension, 0.0f, 2.0f);
	const FVector T1 = SplineTension * (P2 - P0);
	const FVector T2 = SplineTension * (P3 - P1);

	OutLocation = FMath::CubicInterp(P1, T1, P2, T2, LocalAlpha);
	OutTangent = FMath::CubicInterpDerivative(P1, T1, P2, T2, LocalAlpha).GetSafeNormal();

	if (OutTangent.IsNearlyZero())
	{
		OutTangent = (P2 - P1).GetSafeNormal();
		if (OutTangent.IsNearlyZero())
		{
			OutTangent = FlightBaselineDir;
		}
	}
}

FRotator URocketProjectileComponent::ComputeGroundRestRotation(const FVector& GroundNormal, float InSeedYaw) const
{
	const FVector CleanNormal = GroundNormal.GetSafeNormal().IsNearlyZero() ? FVector::UpVector : GroundNormal.GetSafeNormal();

	// Construct ground heading from InSeedYaw perpendicular to GroundNormal
	const FRotator YawRot(0.0f, InSeedYaw, 0.0f);
	FVector HeadingDir = YawRot.Vector();
	HeadingDir = (HeadingDir - (HeadingDir | CleanNormal) * CleanNormal).GetSafeNormal();
	if (HeadingDir.IsNearlyZero())
	{
		HeadingDir = FVector::VectorPlaneProject(FVector::ForwardVector, CleanNormal).GetSafeNormal();
		if (HeadingDir.IsNearlyZero())
		{
			HeadingDir = FVector::VectorPlaneProject(FVector::RightVector, CleanNormal).GetSafeNormal();
		}
	}

	// Bottle lies flat on its side along HeadingDir:
	// Use base rotation alignment from NozzleAxis without any cosmetic wobble
	const FRotator BaseRot = FRotationMatrix::MakeFromX(HeadingDir).Rotator();

	FRotator AxisOffset = FRotator::ZeroRotator;
	switch (NozzleAxis)
	{
	case ERocketMeshAxis::PositiveZ: AxisOffset = FRotator(-90.0f, 0.0f, 0.0f); break;
	case ERocketMeshAxis::NegativeZ: AxisOffset = FRotator(90.0f, 0.0f, 0.0f); break;
	case ERocketMeshAxis::PositiveX: AxisOffset = FRotator(0.0f, 180.0f, 0.0f); break;
	case ERocketMeshAxis::NegativeX: AxisOffset = FRotator::ZeroRotator; break;
	case ERocketMeshAxis::PositiveY: AxisOffset = FRotator(0.0f, -90.0f, 0.0f); break;
	case ERocketMeshAxis::NegativeY: AxisOffset = FRotator(0.0f, 90.0f, 0.0f); break;
	}

	return (FRotationMatrix(AxisOffset) * FRotationMatrix(BaseRot)).Rotator();
}

void URocketProjectileComponent::AdvanceToNextFlightLeg()
{
	FlightLegIndex++;
	CurrentFlightPlanNodeIndex++;

	const float EffectiveFuelDist = FlightSettings.MaxFuelDistance > 0.0f ? FlightSettings.MaxFuelDistance : MaxFuelDistance;

	// Check if all nodes in authoritative flight plan are completed OR fuel distance is exhausted
	if ((AuthoritativeFlightNodes.Num() > 0 && CurrentFlightPlanNodeIndex >= AuthoritativeFlightNodes.Num() - 1)
		|| TotalFlightDistanceTraveled >= EffectiveFuelDist)
	{
		LogDebug(FString::Printf(TEXT("Flight plan completed! Reached final node %d/%d (FuelDist=%.0f/%.0f cm). Falling..."),
			CurrentFlightPlanNodeIndex, AuthoritativeFlightNodes.Num(), TotalFlightDistanceTraveled, EffectiveFuelDist), true);

		// Cut fuel and drop under gravity from this exact final point!
		bTargetRestRotationInitialized = false;
		FallingTumbleAngle = 0.0f;
		TimeSpentFalling = 0.0f;
		SetSimulationState(ERocketSimulationState::SpentFalling);
		return;
	}

	// Arrived at AuthoritativeFlightNodes[CurrentFlightPlanNodeIndex]!
	// Snap CurrentLoc to the arrived node to guarantee zero drift or snap-back
	FVector CurrentLoc = GetWorldCenterLocation();
	if (AuthoritativeFlightNodes.IsValidIndex(CurrentFlightPlanNodeIndex))
	{
		CurrentLoc = AuthoritativeFlightNodes[CurrentFlightPlanNodeIndex];
		SetTargetTransformAroundCenter(CurrentLoc, TargetPrimitive ? TargetPrimitive->GetComponentRotation() : FRotator::ZeroRotator);
	}

	FVector NewFlightDir = FVector::ForwardVector;
	if (AuthoritativeFlightNodes.IsValidIndex(CurrentFlightPlanNodeIndex + 1))
	{
		const FVector TargetNode = AuthoritativeFlightNodes[CurrentFlightPlanNodeIndex + 1];
		NewFlightDir = (TargetNode - CurrentLoc).GetSafeNormal();
	}
	else
	{
		// Pick a new random flight direction using the deterministic seed and leg index
		FRandomStream LegStream(LaunchSeed + FlightLegIndex * 79 + CurrentBounceCount * 37);

		// Semi-random cone around previous baseline direction (or forward)
		FVector PrevDir = FlightBaselineDir;
		if (PrevDir.IsNearlyZero())
		{
			PrevDir = TipOverLaunchHeading;
		}

		// Generate random heading in cone
		NewFlightDir = LegStream.VRandCone(PrevDir, FMath::DegreesToRadians(65.0f));

		// Keep heading airborne (prevent diving straight down into floor)
		if (NewFlightDir.Z < 0.05f)
		{
			NewFlightDir.Z = LegStream.FRandRange(0.12f, 0.45f);
			NewFlightDir.Normalize();
		}
	}

	LogDebug(FString::Printf(TEXT("AdvanceToNextFlightLeg #%d (Node %d/%d): Start=%s, Next Dir: %s, Seed=%d"),
		FlightLegIndex, CurrentFlightPlanNodeIndex, AuthoritativeFlightNodes.Num(), *CurrentLoc.ToString(), *NewFlightDir.ToString(), LaunchSeed + FlightLegIndex * 79));

	// Initialize new erratic flight spline along the new line trace
	InitializeFlightPath(CurrentLoc, NewFlightDir, LaunchSeed + FlightLegIndex * 79);

	CurrentVelocity = NewFlightDir * FlightSettings.BaseSpeed;

	if (FlightSettings.bAlignRotationToVelocity && TargetPrimitive)
	{
		const FRotator NewRot = ComputeTargetRotation(NewFlightDir, 0.016f);
		SetTargetTransformAroundCenter(CurrentLoc, NewRot);
	}
}

void URocketProjectileComponent::TransitionToRocketing(const FVector& InLiftoffCenter)
{
	SetSimulationState(ERocketSimulationState::Rocketing);
	ElapsedFlightTime = 0.0f;
	CurrentFlightPlanNodeIndex = 0;
	CurrentSegmentDistTraveled = 0.0f;
	CurrentBounceReboundSpeed = 0.0f;
	LastImpactSpeed = 0.0f;
	FlightDistanceTraveled = 0.0f;

	// Lock PropActor into InMotion and ensure PhysicsSimComponent is uninstalled from settling queue
	if (AActor* OwnerActor = GetOwner())
	{
		if (AActivePropActor* PropActor = Cast<AActivePropActor>(OwnerActor))
		{
			PropActor->bIsInMotion = true;
			PropActor->bIsHeld = false;
			PropActor->State = EPropState::InMotion;
		}
		if (APropManager* PM = APropManager::Get(OwnerActor))
		{
			for (UActorComponent* Comp : OwnerActor->GetComponents())
			{
				if (Comp && Comp->GetClass()->GetName().Contains(TEXT("PhysicsSim")))
				{
					if (UStaticMeshPhysicsSimulationComponent* SimComp = Cast<UStaticMeshPhysicsSimulationComponent>(Comp))
					{
						SimComp->ClearUnsettledTimer();
						SimComp->bIsInMotion = true;
						SimComp->CurrentState = EPhysicsSimulationState::InMotion;
						SimComp->SetComponentTickEnabled(false);
						PM->UnregisterSettlingComponent(SimComp);
					}
				}
			}
		}
	}

	// Calculate liftoff heading directly towards first flight plan node if available, else LaunchTargetHitLocation
	FVector LiftoffHeading = FVector::ForwardVector;
	if (AuthoritativeFlightNodes.IsValidIndex(1))
	{
		LiftoffHeading = (AuthoritativeFlightNodes[1] - InLiftoffCenter).GetSafeNormal();
	}
	else
	{
		LiftoffHeading = (LaunchTargetHitLocation - InLiftoffCenter).GetSafeNormal();
	}

	if (LiftoffHeading.IsNearlyZero() || LiftoffHeading.Z < 0.08f)
	{
		LiftoffHeading.Z = FMath::Max(LiftoffHeading.Z, 0.18f);
		LiftoffHeading.Normalize();
	}

	CurrentVelocity = LiftoffHeading * FlightSettings.BaseSpeed;
	InitializeFlightPath(InLiftoffCenter, LiftoffHeading, LaunchSeed);

	if (FlightSettings.bAlignRotationToVelocity && TargetPrimitive)
	{
		const FRotator LiftoffRot = ComputeTargetRotation(LiftoffHeading, 0.016f);
		SetTargetTransformAroundCenter(InLiftoffCenter, LiftoffRot);
	}

	LogDebug(FString::Printf(TEXT("TransitionToRocketing: Center=%s, Heading=%s, Speed=%.0f, Seed=%d (AuthoritativeNodes=%d)"),
		*InLiftoffCenter.ToString(), *LiftoffHeading.ToString(), CurrentVelocity.Size(), LaunchSeed, AuthoritativeFlightNodes.Num()), true);
}

void URocketProjectileComponent::DrawRocketDebugVisuals(float DeltaTime)
{
#if !UE_BUILD_SHIPPING
	if (!GetWorld() || (!bDrawDebugTrajectory && !bShowOnScreenDebug))
	{
		return;
	}

	const bool bIsServer = GetOwner() ? GetOwner()->HasAuthority() : false;
	const FColor ThemeColor = bIsServer ? FColor::Green : FColor::Cyan;
	const FVector CurrentLoc = GetWorldCenterLocation();

	if (bDrawDebugTrajectory)
	{
		// 1. Continuous movement breadcrumb trail
		if (!LastDebugTrailLocation.IsNearlyZero() && FVector::DistSquared(LastDebugTrailLocation, CurrentLoc) > 4.0f)
		{
			DrawDebugLine(GetWorld(), LastDebugTrailLocation, CurrentLoc, ThemeColor, false, 5.0f, 0, 2.5f);
		}
		LastDebugTrailLocation = CurrentLoc;

		// 2. Erratic flight waypoints
		if (CurrentState == ERocketSimulationState::Rocketing && FlightWaypoints.Num() >= 2)
		{
			for (int32 i = 0; i < FlightWaypoints.Num() - 1; ++i)
			{
				DrawDebugLine(GetWorld(), FlightWaypoints[i], FlightWaypoints[i + 1], ThemeColor, false, 0.05f, 0, 1.2f);
				DrawDebugPoint(GetWorld(), FlightWaypoints[i], 7.0f, ThemeColor, false, 0.05f);
			}
			DrawDebugPoint(GetWorld(), FlightWaypoints.Last(), 10.0f, FColor::Orange, false, 0.05f);
		}

		// Pre-calculated authoritative 3-part flight plan backbone
		if (AuthoritativeFlightNodes.Num() >= 2)
		{
			for (int32 i = 0; i < AuthoritativeFlightNodes.Num() - 1; ++i)
			{
				FColor SegmentColor = FColor::Yellow;
				if (i >= RocketingEndIndex && i < LowFuelIndex)
				{
					SegmentColor = FColor::Magenta; // Low fuel upward thrust
				}
				else if (i >= LowFuelIndex && i < FirstGroundHitIndex)
				{
					SegmentColor = FColor::Blue; // Spent falling descent
				}
				else if (i >= FirstGroundHitIndex)
				{
					SegmentColor = FColor::Green; // Ground bounces
				}

				DrawDebugLine(GetWorld(), AuthoritativeFlightNodes[i], AuthoritativeFlightNodes[i + 1], SegmentColor, false, 0.05f, 0, 1.8f);

				// Blue circles for coarse server waypoints, Red circles for intermediate seeded offset points (as in reference diagram)
				const bool bIsCoarse = AuthoritativeNodeIsCoarse.IsValidIndex(i) ? AuthoritativeNodeIsCoarse[i] : true;
				if (bIsCoarse)
				{
					DrawDebugSphere(GetWorld(), AuthoritativeFlightNodes[i], 8.0f, 8, FColor::Blue, false, 0.05f, 0, 1.6f);
				}
				else
				{
					DrawDebugSphere(GetWorld(), AuthoritativeFlightNodes[i], 5.0f, 6, FColor::Red, false, 0.05f, 0, 1.2f);
				}
			}

			// Draw LowFuelThrustLocation apex marker
			if (!LowFuelThrustLocation.IsNearlyZero())
			{
				DrawDebugSphere(GetWorld(), LowFuelThrustLocation, 14.0f, 8, FColor::Magenta, false, 0.05f, 0, 1.5f);
			}

			// Draw final rest point
			DrawDebugPoint(GetWorld(), AuthoritativeFlightNodes.Last(), 12.0f, FColor::Red, false, 0.05f);
		}

		// 3. Landmarks
		if (bHasLaunchTargetHit && !LaunchTargetHitLocation.IsNearlyZero())
		{
			DrawDebugSphere(GetWorld(), LaunchTargetHitLocation, 14.0f, 8, FColor::Red, false, 0.05f, 0, 1.5f);
		}
		if (CurrentState == ERocketSimulationState::Dragging && !DragTakeoffPoint.IsNearlyZero())
		{
			DrawDebugSphere(GetWorld(), DragTakeoffPoint, 12.0f, 8, FColor::Blue, false, 0.05f, 0, 1.2f);
		}
		if (CurrentState == ERocketSimulationState::TippingOver && !TippingPivotPoint.IsNearlyZero())
		{
			DrawDebugSphere(GetWorld(), TippingPivotPoint, 10.0f, 8, FColor::Magenta, false, 0.05f, 0, 1.2f);
		}

		// 4. Direction arrow
		if (!CurrentVelocity.IsNearlyZero())
		{
			const FVector ArrowEnd = CurrentLoc + (CurrentVelocity.GetSafeNormal() * 50.0f);
			DrawDebugDirectionalArrow(GetWorld(), CurrentLoc, ArrowEnd, 14.0f, ThemeColor, false, 0.05f, 0, 2.0f);
		}
	}

	// 5. On-screen real-time telemetry HUD
	if (bShowOnScreenDebug && GEngine)
	{
		const int32 BaseKey = bIsServer ? 5100 : 5200;
		const int32 CompKey = BaseKey + (GetUniqueID() % 90);
		const FString RoleTag = bIsServer ? TEXT("[SERVER]") : TEXT("[CLIENT]");
		const FString StateStr = UEnum::GetValueAsString(CurrentState);
		const float Speed = CurrentVelocity.Size();
		const float ProgressPct = (FlightBaselineLength > 0.0f && CurrentState == ERocketSimulationState::Rocketing)
			? FMath::Clamp(FlightDistanceTraveled / FlightBaselineLength * 100.0f, 0.0f, 100.0f) : 0.0f;

		const float EffectiveFuelDist = FlightSettings.MaxFuelDistance > 0.0f ? FlightSettings.MaxFuelDistance : MaxFuelDistance;
		const FString Telemetry = FString::Printf(
			TEXT("%s %s | State: %s | Speed: %.0f | AirTime: %.2fs | Loc: (%.0f, %.0f, %.0f) | Fuel: %.0f/%.0f cm | Node: %d/%d | Catchup: %.2fx"),
			*RoleTag, *GetNameSafe(GetOwner()), *StateStr, Speed, TotalAirTime,
			CurrentLoc.X, CurrentLoc.Y, CurrentLoc.Z, TotalFlightDistanceTraveled, EffectiveFuelDist, CurrentFlightPlanNodeIndex, AuthoritativeFlightNodes.Num(), ClientCatchupSpeedMultiplier
		);

		GEngine->AddOnScreenDebugMessage(CompKey, 0.2f, ThemeColor, Telemetry);
	}
#endif
}

void URocketProjectileComponent::AutoResolveTargetPrimitive()
{
	if (TargetPrimitive)
	{
		return;
	}

	if (!GetOwner())
	{
		return;
	}

	// 1. Try RootComponent if it's a PrimitiveComponent
	TargetPrimitive = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());

	// 2. If RootComponent is a scene component (e.g. DefaultSceneRoot), find the first PrimitiveComponent attached to the actor
	if (!TargetPrimitive)
	{
		TargetPrimitive = GetOwner()->FindComponentByClass<UPrimitiveComponent>();
	}

	if (TargetPrimitive)
	{
		CacheMeshBoundsAndCenter();
		if (bEnableDebugLogs)
		{
			UE_LOG(LogTemp, Log, TEXT("URocketProjectileComponent: Auto-resolved TargetPrimitive to [%s] on Actor [%s]"),
				*TargetPrimitive->GetName(), *GetOwner()->GetName());
		}
	}
	else if (bEnableDebugLogs)
	{
		UE_LOG(LogTemp, Error, TEXT("URocketProjectileComponent on [%s]: TargetPrimitive is null and no UPrimitiveComponent found!"),
			*GetOwner()->GetName());
	}
}

void URocketProjectileComponent::LogDebug(const FString& Message, bool bOnScreen) const
{
	const bool bShouldLogOutput = bEnableDebugLogs;
	const bool bShouldPrintScreen = bShowOnScreenDebug && bOnScreen;

	if (!bShouldLogOutput && !bShouldPrintScreen)
	{
		return;
	}

	const bool bIsServer = GetOwner() ? GetOwner()->HasAuthority() : false;
	const FString RoleTag = bIsServer ? TEXT("[SERVER]") : TEXT("[CLIENT]");
	const FString ActorName = GetOwner() ? GetOwner()->GetName() : TEXT("None");
	const FString FullMessage = FString::Printf(TEXT("%s [%s] %s"), *RoleTag, *ActorName, *Message);

	if (bShouldLogOutput)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *FullMessage);
	}

	if (bShouldPrintScreen && GEngine)
	{
		const FColor DisplayColor = bIsServer ? FColor::Green : FColor::Cyan;
		GEngine->AddOnScreenDebugMessage(-1, 3.5f, DisplayColor, FullMessage);
	}
}

void URocketProjectileComponent::PrintRocketDebugInfo() const
{
	if (!bEnableDebugLogs && !bShowOnScreenDebug)
	{
		return;
	}

	const bool bIsServer = GetOwner() ? GetOwner()->HasAuthority() : false;
	const FString RoleTag = bIsServer ? TEXT("[SERVER]") : TEXT("[CLIENT]");
	const FString ActorName = GetOwner() ? GetOwner()->GetName() : TEXT("None");
	const FVector Loc = TargetPrimitive ? TargetPrimitive->GetComponentLocation() : FVector::ZeroVector;
	const FRotator Rot = TargetPrimitive ? TargetPrimitive->GetComponentRotation() : FRotator::ZeroRotator;
	const FString ColProfile = TargetPrimitive ? TargetPrimitive->GetCollisionProfileName().ToString() : TEXT("None");
	const bool bSimPhys = TargetPrimitive ? TargetPrimitive->IsSimulatingPhysics() : false;
	const int32 ColEnabled = TargetPrimitive ? static_cast<int32>(TargetPrimitive->GetCollisionEnabled()) : -1;
	const FString StateName = UEnum::GetValueAsString(CurrentState);

	const FString Info = FString::Printf(TEXT("=== ROCKET DEBUG INFO ===\n"
		"Role: %s | Actor: %s | State: %s\n"
		"Elapsed Flight: %.2f / %.2f s | Total Air: %.2f s | Bounces: %d\n"
		"Linear Speed: %.1f | Velocity: %s\n"
		"Location: %s | Rotation: %s\n"
		"SimulatingPhysics: %d | ColProfile: %s | ColEnabled: %d\n"
		"========================="),
		*RoleTag, *ActorName, *StateName,
		ElapsedFlightTime, MaxFuelDuration, TotalAirTime, CurrentBounceCount,
		CurrentVelocity.Size(), *CurrentVelocity.ToString(),
		*Loc.ToString(), *Rot.ToString(),
		bSimPhys ? 1 : 0, *ColProfile, ColEnabled);

	if (bEnableDebugLogs)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Info);
	}
	if (bShowOnScreenDebug && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.0f, bIsServer ? FColor::Yellow : FColor::Orange, Info);
	}
}

struct FRocketWorldBatchData
{
	TWeakObjectPtr<URocketProjectileComponent> Broadcaster = nullptr;
	TArray<FRocketBatchLaunchItem> PendingItems;
};

static TMap<TWeakObjectPtr<UWorld>, FRocketWorldBatchData> GWorldRocketLaunchBatches;

float URocketProjectileComponent::GetSynchronizedServerTime() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const AGameStateBase* GS = World->GetGameState())
		{
			return GS->GetServerWorldTimeSeconds();
		}
		return World->GetTimeSeconds();
	}
	return 0.0f;
}

void URocketProjectileComponent::QueueBatchLaunchItem(URocketProjectileComponent* Component, const FRocketBatchLaunchItem& Item)
{
	if (!Component)
	{
		return;
	}

	UWorld* World = Component->GetWorld();
	if (!World)
	{
		return;
	}

	// Purge stale destroyed world keys
	for (auto It = GWorldRocketLaunchBatches.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	FRocketWorldBatchData& BatchData = GWorldRocketLaunchBatches.FindOrAdd(World);
	if (!BatchData.Broadcaster.IsValid())
	{
		BatchData.Broadcaster = Component;
	}

	const bool bWasEmpty = BatchData.PendingItems.IsEmpty();
	BatchData.PendingItems.Add(Item);

	if (bWasEmpty)
	{
		TWeakObjectPtr<UWorld> WeakWorld = World;
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakWorld]()
		{
			URocketProjectileComponent::FlushBatchLaunchForWorld(WeakWorld);
		}));
	}
}

void URocketProjectileComponent::FlushBatchLaunchForWorld(TWeakObjectPtr<UWorld> WeakWorld)
{
	UWorld* World = WeakWorld.Get();
	if (!World)
	{
		GWorldRocketLaunchBatches.Remove(WeakWorld);
		return;
	}

	FRocketWorldBatchData* BatchData = GWorldRocketLaunchBatches.Find(World);
	if (!BatchData || BatchData->PendingItems.IsEmpty())
	{
		return;
	}

	TArray<FRocketBatchLaunchItem> ItemsToFlush = MoveTemp(BatchData->PendingItems);
	URocketProjectileComponent* Broadcaster = BatchData->Broadcaster.Get();
	BatchData->PendingItems.Reset();
	BatchData->Broadcaster = nullptr;

	// Pick first valid component to broadcast the RPC if Broadcaster was destroyed
	if (!Broadcaster)
	{
		for (const FRocketBatchLaunchItem& Item : ItemsToFlush)
		{
			if (Item.BottleActor)
			{
				Broadcaster = Item.BottleActor->FindComponentByClass<URocketProjectileComponent>();
				if (Broadcaster)
				{
					break;
				}
			}
		}
	}

	if (Broadcaster)
	{
		const float ServerLaunchTime = Broadcaster->GetSynchronizedServerTime();
		Broadcaster->Multicast_BatchLaunchRockets(ServerLaunchTime, ItemsToFlush);
		Broadcaster->LogDebug(FString::Printf(TEXT("Flushed batch launch of %d rockets! ServerLaunchTime=%.3f"),
			ItemsToFlush.Num(), ServerLaunchTime));
	}
}

void URocketProjectileComponent::LaunchRocketBatch(
	const TArray<URocketProjectileComponent*>& InRockets,
	const TArray<FVector>& InDirections,
	float InDistance)
{
	for (int32 i = 0; i < InRockets.Num(); ++i)
	{
		if (URocketProjectileComponent* Rocket = InRockets[i])
		{
			const FVector Dir = InDirections.IsValidIndex(i) ? InDirections[i] : FVector::ForwardVector;
			Rocket->LaunchRocket(Dir, false, 0, InDistance);
		}
	}
}

void URocketProjectileComponent::ConfigureRocket(
	float InDistance,
	float InSpeed,
	uint8 InSeed,
	FVector InLaunchDirection)
{
	if (InDistance > 0.0f)
	{
		FlightSettings.BaseSplineLength = InDistance;
		FlightSettings.MaxFuelDistance = InDistance;
		MaxFuelDistance = InDistance;
		FlightNetState.FuelDuration = InDistance / FMath::Max(BaseSpeed, 100.0f);
	}
	if (InSpeed > 0.0f)
	{
		BaseSpeed = InSpeed;
		FlightSettings.BaseSpeed = InSpeed;
		FlightNetState.Speed = BaseSpeed;
	}
	if (InSeed != 0)
	{
		LaunchSeed = InSeed;
		FlightNetState.LaunchSeed = InSeed;
	}
	else if (LaunchSeed <= 0)
	{
		LaunchSeed = FMath::RandRange(1, 255);
		FlightNetState.LaunchSeed = static_cast<uint8>(LaunchSeed & 0xFF);
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		Multicast_ConfigureRocket(MaxFuelDistance, BaseSpeed, static_cast<uint8>(LaunchSeed & 0xFF));
	}

	LogDebug(FString::Printf(TEXT("ConfigureRocket: Distance=%.1f cm, Speed=%.1f, Seed=%d (bLaunchOnInitialized=%d)"),
		MaxFuelDistance, BaseSpeed, LaunchSeed, bLaunchOnInitialized ? 1 : 0));

	if (bLaunchOnInitialized)
	{
		const FVector Dir = InLaunchDirection.IsNearlyZero() ? (GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector) : InLaunchDirection;
		LaunchRocket(Dir, bLaunchWithHighAccuracy, static_cast<uint8>(LaunchSeed & 0xFF), MaxFuelDistance, BaseSpeed);
	}
}

void URocketProjectileComponent::Multicast_ConfigureRocket_Implementation(
	float InDistance,
	float InSpeed,
	uint8 InSeed)
{
	if (InDistance > 0.0f)
	{
		FlightSettings.BaseSplineLength = InDistance;
		FlightSettings.MaxFuelDistance = InDistance;
		MaxFuelDistance = InDistance;
	}
	if (InSpeed > 0.0f)
	{
		BaseSpeed = InSpeed;
		FlightSettings.BaseSpeed = InSpeed;
	}
	if (InSeed != 0)
	{
		LaunchSeed = InSeed;
	}
	LogDebug(FString::Printf(TEXT("Multicast_ConfigureRocket on client: Distance=%.1f cm, Speed=%.1f, Seed=%d"),
		MaxFuelDistance, BaseSpeed, LaunchSeed));
}

void URocketProjectileComponent::Multicast_LaunchRocket_Implementation(
	float InServerLaunchTime,
	FVector_NetQuantizeNormal InLaunchDirection,
	uint8 InSeed)
{
	// Server already launched locally on the frame it was called; only clients need to apply client-side launch!
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return;
	}

	// Two-Stage Launch: Ultra-compact (~8 bytes, Time, Direction, Seed), NO starting location snap in mid-air
	ApplyClientLaunch(InServerLaunchTime, InLaunchDirection, false, FVector::ZeroVector, InSeed);
}

void URocketProjectileComponent::Multicast_LaunchRocketFull_Implementation(
	float InServerLaunchTime,
	FVector_NetQuantize InLaunchLocation,
	FVector_NetQuantizeNormal InLaunchDirection,
	uint8 InSeed,
	float InDistance,
	float InSpeed)
{
	// Server already launched locally on the frame it was called; only clients need to apply client-side launch!
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return;
	}

	// All-In-One Instant Launch: High-accuracy initial location and parameters
	ApplyClientLaunch(InServerLaunchTime, InLaunchDirection, true, InLaunchLocation, InSeed, InDistance, InSpeed);
}

void URocketProjectileComponent::Multicast_BatchLaunchRockets_Implementation(
	float InServerLaunchTime,
	const TArray<FRocketBatchLaunchItem>& BatchItems)
{
	// Server already launched these locally on the frame they were queued; only clients need to apply client-side launch!
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return;
	}

	for (const FRocketBatchLaunchItem& Item : BatchItems)
	{
		if (Item.BottleActor)
		{
			if (URocketProjectileComponent* Comp = Item.BottleActor->FindComponentByClass<URocketProjectileComponent>())
			{
				Comp->ApplyClientLaunch(InServerLaunchTime, Item.LaunchDirection, false, FVector::ZeroVector, Item.LaunchSeed);
			}
		}
	}
}

void URocketProjectileComponent::Multicast_SendFullFlightPlan_Implementation(
	const TArray<FVector_NetQuantize>& InRocketingLocations,
	FVector_NetQuantize InLowFuelThrustLocation,
	bool bInLowFuelHitObstacle,
	FVector_NetQuantizeNormal InLowFuelHitNormal,
	const TArray<FVector_NetQuantize>& InBounceLocations,
	FRotator InRestRotation)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return;
	}

	SetAuthoritativeFullFlightPlan(InRocketingLocations, InLowFuelThrustLocation, bInLowFuelHitObstacle, InLowFuelHitNormal, InBounceLocations, InRestRotation);
}

void URocketProjectileComponent::Multicast_SendFlightPlan_Implementation(const TArray<FVector_NetQuantize>& InFlightNodes)
{
	// Server already generated and activated the flight plan locally before broadcasting; clients now receive and activate it
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return;
	}

	SetAuthoritativeFlightPlan(InFlightNodes);
}

void URocketProjectileComponent::GenerateServerFlightPlan(
	TArray<FVector_NetQuantize>& OutRocketingLocations,
	FVector_NetQuantize& OutLowFuelThrustLocation,
	bool& bOutLowFuelHitObstacle,
	FVector_NetQuantizeNormal& OutLowFuelHitNormal,
	TArray<FVector_NetQuantize>& OutBounceLocations,
	FRotator& OutRestRotation)
{
	OutRocketingLocations.Empty();
	OutBounceLocations.Empty();
	OutLowFuelThrustLocation = FVector_NetQuantize(FVector::ZeroVector);
	bOutLowFuelHitObstacle = false;
	OutLowFuelHitNormal = FVector_NetQuantizeNormal(FVector::DownVector);
	OutRestRotation = FRotator::ZeroRotator;

	if (!GetWorld())
	{
		return;
	}

	// 1. Determine exact liftoff start location
	FVector LiftoffCenter = GetWorldCenterLocation();
	if (TippingSettings.bEnableTipping && bTipOverToFly && DraggingSettings.bEnableDragging)
	{
		LiftoffCenter = LiftoffCenter + (TipOverLaunchHeading * DraggingSettings.TakeoffDistance);
	}

	// 2. Determine initial liftoff heading (with guaranteed upward pitch scoop)
	FVector CurrentHeading = (LaunchTargetHitLocation - LiftoffCenter).GetSafeNormal();
	if (CurrentHeading.IsNearlyZero() || CurrentHeading.Z < 0.08f)
	{
		CurrentHeading = TipOverLaunchHeading;
		CurrentHeading.Z = FMath::Max(CurrentHeading.Z, 0.18f);
		CurrentHeading.Normalize();
	}

	// 3. Base spline rocketing trajectory
	OutRocketingLocations.Add(FVector_NetQuantize(LiftoffCenter));

	const float EffectiveBaseDist = FlightSettings.BaseSplineLength > 0.0f ? FlightSettings.BaseSplineLength : (FlightSettings.MaxFuelDistance > 0.0f ? FlightSettings.MaxFuelDistance : 5000.0f);
	float RemainingBudget = EffectiveBaseDist;
	const float EndDistThreshold = EffectiveBaseDist * 0.35f; // Last 35% of rocketing distance: fuel dying, gravity bends flight down
	const float MinSpacing = FMath::Clamp(FlightSettings.MinWaypointSpacing, 50.0f, 2000.0f);
	const float MaxSpacing = FMath::Clamp(FlightSettings.MaxWaypointSpacing, MinSpacing, 3000.0f);

	FRandomStream PlanStream(LaunchSeed > 0 ? LaunchSeed : 1);
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(RocketServerFlightPlanTrace), false, GetOwner());

	FVector CurrentLoc = LiftoffCenter;
	int32 IterationCount = 0;
	const int32 MaxIterations = 64;

	while (RemainingBudget > 1.0f && IterationCount < MaxIterations)
	{
		IterationCount++;
		const float RandomLength = PlanStream.FRandRange(MinSpacing, MaxSpacing);
		const float TraceDist = FMath::Min(RandomLength, RemainingBudget);

		// When approaching the end of fuel distance, gravity starts pulling the trajectory downward
		if (RemainingBudget <= EndDistThreshold && EndDistThreshold > 0.0f)
		{
			const float EndRatio = 1.0f - FMath::Clamp(RemainingBudget / EndDistThreshold, 0.0f, 1.0f);
			CurrentHeading.Z = FMath::Lerp(CurrentHeading.Z, -0.45f, EndRatio * 0.65f);
			CurrentHeading.Normalize();
		}

		const FVector TraceEnd = CurrentLoc + (CurrentHeading * TraceDist);
		FHitResult Hit;
		const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, CurrentLoc, TraceEnd, ECC_Visibility, TraceParams);

		if (bHit)
		{
			const float HitDist = FVector::Dist(CurrentLoc, Hit.ImpactPoint);
			RemainingBudget -= HitDist;

			const float Extent = GetProjectedExtentAlongDirection(Hit.Normal);
			CurrentLoc = Hit.ImpactPoint + (Hit.Normal * (Extent + 2.0f));
			OutRocketingLocations.Add(FVector_NetQuantize(CurrentLoc));

			if (RemainingBudget <= 1.0f)
			{
				break;
			}

			// Calculate reflection vector with randomized bounce angle
			FVector Reflected = CurrentHeading - 2.0f * (CurrentHeading | Hit.Normal) * Hit.Normal;
			const float DeflectionDeg = FMath::Clamp(FlightSettings.FlightBounceRandomDeflection > 0.0f ? FlightSettings.FlightBounceRandomDeflection : FlightBounceRandomDeflection, 25.0f, 65.0f);
			Reflected = PlanStream.VRandCone(Reflected.GetSafeNormal(), FMath::DegreesToRadians(DeflectionDeg));

			const float NormalComp = Reflected | Hit.Normal;
			if (NormalComp < 0.25f)
			{
				Reflected = (Reflected - (NormalComp * Hit.Normal)) + (Hit.Normal * 0.35f);
				Reflected.Normalize();
			}

			if (Hit.Normal.Z > 0.5f && Reflected.Z < 0.15f)
			{
				Reflected.Z = PlanStream.FRandRange(0.20f, 0.55f);
				Reflected.Normalize();
			}

			CurrentHeading = Reflected.GetSafeNormal();
		}
		else
		{
			RemainingBudget -= TraceDist;
			OutRocketingLocations.Add(FVector_NetQuantize(TraceEnd));

			if (RemainingBudget <= 1.0f)
			{
				break;
			}

			CurrentHeading = PlanStream.VRandCone(CurrentHeading, FMath::DegreesToRadians(PlanStream.FRandRange(25.0f, 45.0f)));
			// Only clamp Z upward if not in the final gravity-fall phase
			if (RemainingBudget > EndDistThreshold)
			{
				if (CurrentHeading.Z < 0.05f)
				{
					CurrentHeading.Z = PlanStream.FRandRange(0.12f, 0.40f);
					CurrentHeading.Normalize();
				}
			}
			CurrentLoc = TraceEnd;
		}
	}

	// 4. Low fuel thrust stage: thrust points DOWN pushing the bottle UP against gravity over a short, intense climb
	const FVector FinalRocketingLoc = FVector(OutRocketingLocations.Last().X, OutRocketingLocations.Last().Y, OutRocketingLocations.Last().Z);

	FVector ForwardDir = FVector(CurrentHeading.X, CurrentHeading.Y, 0.0f).GetSafeNormal();
	if (ForwardDir.IsNearlyZero())
	{
		ForwardDir = FVector(TipOverLaunchHeading.X, TipOverLaunchHeading.Y, 0.0f).GetSafeNormal();
	}
	if (ForwardDir.IsNearlyZero())
	{
		ForwardDir = FVector::ForwardVector;
	}

	FVector LowFuelHeading = (ForwardDir * 0.20f + FVector::UpVector * 0.95f).GetSafeNormal();
	const float UpwardConeDeg = PlanStream.FRandRange(5.0f, 15.0f);
	LowFuelHeading = PlanStream.VRandCone(LowFuelHeading, FMath::DegreesToRadians(UpwardConeDeg));
	LowFuelHeading.Z = FMath::Clamp(LowFuelHeading.Z, 0.70f, 0.98f);
	LowFuelHeading.Normalize();

	const float LowFuelDist = PlanStream.FRandRange(FlightSettings.LowFuelThrustDistanceMin, FlightSettings.LowFuelThrustDistanceMax);
	const FVector LowFuelTraceEnd = FinalRocketingLoc + (LowFuelHeading * LowFuelDist);

	FHitResult LowFuelHit;
	bOutLowFuelHitObstacle = false;
	OutLowFuelHitNormal = FVector_NetQuantizeNormal(FVector::DownVector);
	bLowFuelHitObstacle = false;
	LowFuelHitNormal = FVector::DownVector;

	FVector ReboundVelocityDir = FVector::DownVector;

	if (GetWorld()->LineTraceSingleByChannel(LowFuelHit, FinalRocketingLoc, LowFuelTraceEnd, ECC_Visibility, TraceParams))
	{
		// Struck obstacle (ceiling, wall, beam, prop, etc.) while losing thrust!
		bOutLowFuelHitObstacle = true;
		OutLowFuelHitNormal = FVector_NetQuantizeNormal(LowFuelHit.Normal);
		bLowFuelHitObstacle = true;
		LowFuelHitNormal = LowFuelHit.Normal;

		const float LowFuelExtent = GetProjectedExtentAlongDirection(LowFuelHit.Normal);
		OutLowFuelThrustLocation = FVector_NetQuantize(LowFuelHit.ImpactPoint + (LowFuelHit.Normal * (LowFuelExtent + 2.0f)));

		// Reflect off obstacle and blend with downward gravity
		FVector Reflected = LowFuelHeading - 2.0f * (LowFuelHeading | LowFuelHit.Normal) * LowFuelHit.Normal;
		ReboundVelocityDir = (Reflected * 0.40f + FVector::DownVector * 0.75f).GetSafeNormal();

		LogDebug(FString::Printf(TEXT("GenerateServerFlightPlan: LowFuel struck obstacle at %s! Normal=%s, ReboundDir=%s"),
			*LowFuelHit.ImpactPoint.ToString(), *LowFuelHit.Normal.ToString(), *ReboundVelocityDir.ToString()), true);
	}
	else
	{
		// Unobstructed low-fuel climb to apex
		bOutLowFuelHitObstacle = false;
		OutLowFuelHitNormal = FVector_NetQuantizeNormal(FVector::DownVector);
		bLowFuelHitObstacle = false;
		LowFuelHitNormal = FVector::DownVector;

		OutLowFuelThrustLocation = FVector_NetQuantize(LowFuelTraceEnd);

		// Natural forward-downward gravity arc from apex
		ReboundVelocityDir = (ForwardDir * 0.30f + FVector::DownVector * 0.85f).GetSafeNormal();
	}

	// 5. Floor line trace: traces from LowFuelApex along ReboundVelocityDir down to the ground
	const FVector LowFuelApex = FVector(OutLowFuelThrustLocation.X, OutLowFuelThrustLocation.Y, OutLowFuelThrustLocation.Z);
	const float FloorTraceDist = FlightSettings.FloorTraceMaxDistance > 0.0f ? FlightSettings.FloorTraceMaxDistance : 20000.0f;
	const FVector FloorTraceEnd = LowFuelApex + (ReboundVelocityDir * FloorTraceDist);

	FHitResult FloorHit;
	const bool bHitFloor = GetWorld()->LineTraceSingleByChannel(FloorHit, LowFuelApex, FloorTraceEnd, ECC_Visibility, TraceParams);

	FVector GroundContactLoc = FVector::ZeroVector;
	FVector GroundContactNormal = FVector::UpVector;

	if (bHitFloor)
	{
		const float GroundExtent = GetProjectedExtentAlongDirection(FloorHit.Normal);
		GroundContactLoc = FloorHit.ImpactPoint + (FloorHit.Normal * (GroundExtent + 1.5f));
		GroundContactNormal = FloorHit.Normal;
	}
	else
	{
		// Fallback straight down check if angled trace didn't hit
		FHitResult StraightDownHit;
		if (GetWorld()->LineTraceSingleByChannel(StraightDownHit, LowFuelApex, LowFuelApex - FVector(0.0f, 0.0f, FloorTraceDist), ECC_Visibility, TraceParams))
		{
			const float GroundExtent = GetProjectedExtentAlongDirection(StraightDownHit.Normal);
			GroundContactLoc = StraightDownHit.ImpactPoint + (StraightDownHit.Normal * (GroundExtent + 1.5f));
			GroundContactNormal = StraightDownHit.Normal;
		}
		else
		{
			bool bFoundReachable = false;
			if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
			{
				FNavLocation ReachableNavLoc;
				if (NavSys->GetRandomReachablePointInRadius(LowFuelApex, FloorTraceDist, ReachableNavLoc))
				{
					const float ReachableExtent = GetProjectedExtentAlongDirection(FVector::UpVector);
					GroundContactLoc = ReachableNavLoc.Location + FVector(0.0f, 0.0f, ReachableExtent + 1.5f);
					GroundContactNormal = FVector::UpVector;
					bFoundReachable = true;
				}
			}

			if (!bFoundReachable)
			{
				float ClosestDistSq = MAX_flt;
				for (int32 AngleDeg = 0; AngleDeg < 360; AngleDeg += 45)
				{
					const float Rad = FMath::DegreesToRadians(static_cast<float>(AngleDeg));
					const FVector TraceDir = FVector(FMath::Cos(Rad) * 0.707f, FMath::Sin(Rad) * 0.707f, -0.707f).GetSafeNormal();
					FHitResult AngledHit;
					if (GetWorld()->LineTraceSingleByChannel(AngledHit, LowFuelApex, LowFuelApex + (TraceDir * FloorTraceDist), ECC_Visibility, TraceParams))
					{
						const float DistSq = FVector::DistSquared(LowFuelApex, AngledHit.ImpactPoint);
						if (DistSq < ClosestDistSq)
						{
							ClosestDistSq = DistSq;
							const float HitExtent = GetProjectedExtentAlongDirection(AngledHit.Normal);
							GroundContactLoc = AngledHit.ImpactPoint + (AngledHit.Normal * (HitExtent + 1.5f));
							GroundContactNormal = AngledHit.Normal;
							bFoundReachable = true;
						}
					}
				}
			}

			if (!bFoundReachable)
			{
				GroundContactLoc = FloorTraceEnd;
			}
		}
	}

	// 6. Ground contact & bounces array (max 3 bounces, max 300 distance each)
	OutBounceLocations.Add(FVector_NetQuantize(GroundContactLoc));

	FVector CurrentGroundPoint = GroundContactLoc;
	FVector CurrentNormal = GroundContactNormal;
	FVector InboundDir = (GroundContactLoc - LowFuelApex).GetSafeNormal();

	const int32 NumBounces = FMath::Clamp(FallingSettings.MaxFallingBounces > 0 ? FallingSettings.MaxFallingBounces : 3, 1, 3);

	for (int32 b = 0; b < NumBounces; ++b)
	{
		FVector BounceHeading = InboundDir - 2.0f * (InboundDir | CurrentNormal) * CurrentNormal;
		BounceHeading.Z = FMath::Max(FMath::Abs(BounceHeading.Z), 0.60f);
		BounceHeading = PlanStream.VRandCone(BounceHeading.GetSafeNormal(), FMath::DegreesToRadians(PlanStream.FRandRange(10.0f, 25.0f)));
		BounceHeading.Normalize();

		const float XYLength = FVector(BounceHeading.X, BounceHeading.Y, 0.0f).Size();
		const float ZLength = FMath::Abs(BounceHeading.Z);
		const float CurrentMaxBounceDist = FlightSettings.BounceTraceMaxDistance > 0.0f ? FlightSettings.BounceTraceMaxDistance : 300.0f;
		float BounceStepDist = PlanStream.FRandRange(CurrentMaxBounceDist * 0.5f, CurrentMaxBounceDist) * FMath::Pow(0.65f, b);

		if (XYLength > ZLength)
		{
			BounceHeading.X *= 0.50f;
			BounceHeading.Y *= 0.50f;
			BounceHeading.Normalize();
			BounceStepDist *= 0.70f;
		}
		else
		{
			BounceHeading.Z = FMath::Max(BounceHeading.Z, 0.75f);
			BounceHeading.Normalize();
		}

		const FVector ApexStart = CurrentGroundPoint + (CurrentNormal * 2.0f);
		const FVector ApexEnd = ApexStart + (BounceHeading * (BounceStepDist * 0.5f));
		FHitResult ApexHit;
		FVector ApexPoint = ApexEnd;
		if (GetWorld()->LineTraceSingleByChannel(ApexHit, ApexStart, ApexEnd, ECC_Visibility, TraceParams))
		{
			const float ApexExtent = GetProjectedExtentAlongDirection(ApexHit.Normal);
			ApexPoint = ApexHit.ImpactPoint + (ApexHit.Normal * (ApexExtent + 1.5f));
		}
		OutBounceLocations.Add(FVector_NetQuantize(ApexPoint));

		const FVector DescentDir = FVector(BounceHeading.X * 0.5f, BounceHeading.Y * 0.5f, -1.0f).GetSafeNormal();
		const FVector LandEnd = ApexPoint + (DescentDir * (BounceStepDist * 0.75f + 150.0f));
		FHitResult LandHit;
		FVector NextGroundPoint = LandEnd;
		if (GetWorld()->LineTraceSingleByChannel(LandHit, ApexPoint, LandEnd, ECC_Visibility, TraceParams))
		{
			CurrentNormal = LandHit.Normal;
			const float LandExtent = GetProjectedExtentAlongDirection(CurrentNormal);
			NextGroundPoint = LandHit.ImpactPoint + (CurrentNormal * (LandExtent + 1.5f));
		}
		else
		{
			NextGroundPoint.Z = CurrentGroundPoint.Z;
		}

		OutBounceLocations.Add(FVector_NetQuantize(NextGroundPoint));
		CurrentGroundPoint = NextGroundPoint;
		InboundDir = (NextGroundPoint - ApexPoint).GetSafeNormal();
	}

	OutRestRotation = ComputeGroundRestRotation(CurrentNormal, PlanStream.FRandRange(0.0f, 360.0f));
	if (OutBounceLocations.Num() > 0)
	{
		const float RestExtent = GetProjectedExtentAlongDirectionWithRotation(CurrentNormal, OutRestRotation);
		FVector FinalRestLoc = CurrentGroundPoint;
		FHitResult FinalFloorCheck;
		if (GetWorld()->LineTraceSingleByChannel(FinalFloorCheck, CurrentGroundPoint + (CurrentNormal * 20.0f), CurrentGroundPoint - (CurrentNormal * 50.0f), ECC_Visibility, TraceParams))
		{
			FinalRestLoc = FinalFloorCheck.ImpactPoint + (FinalFloorCheck.Normal * RestExtent);
		}
		OutBounceLocations.Last() = FVector_NetQuantize(FinalRestLoc);
	}

	LogDebug(FString::Printf(TEXT("GenerateServerFlightPlan complete: %d rocketing nodes, LowFuel apex at %s (Obstacle=%d), %d bounce nodes. Seed=%d"),
		OutRocketingLocations.Num(), *OutLowFuelThrustLocation.ToString(), bOutLowFuelHitObstacle ? 1 : 0, OutBounceLocations.Num(), LaunchSeed), true);
}

void URocketProjectileComponent::GenerateServerFlightPlan(
	TArray<FVector_NetQuantize>& OutRocketingLocations,
	FVector_NetQuantize& OutLowFuelThrustLocation,
	TArray<FVector_NetQuantize>& OutBounceLocations,
	FRotator& OutRestRotation)
{
	bool bHitObstacle = false;
	FVector_NetQuantizeNormal HitNormal = FVector::DownVector;
	GenerateServerFlightPlan(OutRocketingLocations, OutLowFuelThrustLocation, bHitObstacle, HitNormal, OutBounceLocations, OutRestRotation);
}

void URocketProjectileComponent::GenerateServerFlightPlan(TArray<FVector_NetQuantize>& OutNodes)
{
	TArray<FVector_NetQuantize> Rocketing;
	FVector_NetQuantize LowFuel;
	TArray<FVector_NetQuantize> Bounces;
	FRotator RestRot;

	GenerateServerFlightPlan(Rocketing, LowFuel, Bounces, RestRot);

	OutNodes.Empty(Rocketing.Num() + 1 + Bounces.Num());
	for (const FVector_NetQuantize& Loc : Rocketing)
	{
		OutNodes.Add(Loc);
	}
	OutNodes.Add(LowFuel);
	for (const FVector_NetQuantize& Loc : Bounces)
	{
		OutNodes.Add(Loc);
	}
}

void URocketProjectileComponent::SubdivideRocketingPathWithSeededOffsets(
	const TArray<FVector>& InCoarsePoints,
	uint8 InSeed,
	TArray<FVector>& OutDetailedPoints,
	TArray<bool>& OutIsCoarseFlags)
{
	OutDetailedPoints.Empty();
	OutIsCoarseFlags.Empty();

	if (InCoarsePoints.Num() == 0)
	{
		return;
	}

	if (InCoarsePoints.Num() == 1)
	{
		OutDetailedPoints.Add(InCoarsePoints[0]);
		OutIsCoarseFlags.Add(true);
		return;
	}

	const float MaxDeviation = FMath::Clamp(FlightSettings.MaxPathDeviation > 0.0f ? FlightSettings.MaxPathDeviation : 50.0f, 20.0f, 100.0f);
	const float MinSpacing = FMath::Clamp(FlightSettings.MinOffsetPointSpacing > 0.0f ? FlightSettings.MinOffsetPointSpacing : 300.0f, 150.0f, 1000.0f);
	const float MaxSpacing = FMath::Clamp(FlightSettings.MaxOffsetPointSpacing >= MinSpacing ? FlightSettings.MaxOffsetPointSpacing : 500.0f, MinSpacing, 2000.0f);
	const float TargetSpacing = (MinSpacing + MaxSpacing) * 0.5f;

	for (int32 i = 0; i < InCoarsePoints.Num() - 1; ++i)
	{
		const FVector A = InCoarsePoints[i];
		const FVector B = InCoarsePoints[i + 1];
		const FVector AB = B - A;
		const float SegLength = AB.Size();

		// Add start coarse blue point
		OutDetailedPoints.Add(A);
		OutIsCoarseFlags.Add(true);

		if (SegLength < MinSpacing * 1.2f)
		{
			// Segment is too short to insert intermediate points without violating the 300-500cm distance requirement
			continue;
		}

		const FVector Tangent = AB / SegLength;

		// Orthonormal 3D frame perpendicular to Tangent
		FVector UpRef = FVector::UpVector;
		if (FMath::Abs(Tangent | UpRef) > 0.85f)
		{
			UpRef = FVector::RightVector;
		}
		const FVector U = (Tangent ^ UpRef).GetSafeNormal();
		const FVector V = (U ^ Tangent).GetSafeNormal();

		// Determine number of subsegments so consecutive points are spaced ~300 to 500 cm apart
		int32 NumSubsegments = FMath::Clamp(FMath::RoundToInt(SegLength / TargetSpacing), 2, 8);
		while (NumSubsegments > 1 && (SegLength / static_cast<float>(NumSubsegments)) < MinSpacing)
		{
			NumSubsegments--;
		}
		while ((SegLength / static_cast<float>(NumSubsegments)) > MaxSpacing && (SegLength / static_cast<float>(NumSubsegments + 1)) >= MinSpacing)
		{
			NumSubsegments++;
		}

		if (NumSubsegments <= 1)
		{
			continue;
		}

		// Deterministic seed per segment ensures identical points on server and client
		FRandomStream SegStream(static_cast<int32>(InSeed) * 1013 + i * 7919);
		const float BaseAngle = SegStream.FRandRange(0.0f, TWO_PI);
		const float OffsetMag = FMath::Min(MaxDeviation, SegLength * 0.18f);

		for (int32 k = 1; k < NumSubsegments; ++k)
		{
			const float t = static_cast<float>(k) / static_cast<float>(NumSubsegments);

			// Alternate side of the path for each offset point (waving trajectory as in user reference diagram)
			const float AlternateAngle = (k % 2 == 1) ? 0.0f : PI;
			const float Jitter = SegStream.FRandRange(-0.35f, 0.35f);
			const float Angle = BaseAngle + AlternateAngle + Jitter;

			const FVector OffsetDir = (U * FMath::Cos(Angle) + V * FMath::Sin(Angle)).GetSafeNormal();
			FVector RedPoint = FMath::Lerp(A, B, t) + (OffsetDir * OffsetMag);

			// Prevent dipping below ground level
			const float MinZ = FMath::Min(A.Z, B.Z);
			RedPoint.Z = FMath::Max(RedPoint.Z, MinZ + 4.0f);

			OutDetailedPoints.Add(RedPoint);
			OutIsCoarseFlags.Add(false);
		}
	}

	// Add final coarse blue point
	OutDetailedPoints.Add(InCoarsePoints.Last());
	OutIsCoarseFlags.Add(true);
}

void URocketProjectileComponent::SetAuthoritativeFullFlightPlan(
	const TArray<FVector_NetQuantize>& InRocketingLocations,
	FVector_NetQuantize InLowFuelThrustLocation,
	bool bInLowFuelHitObstacle,
	const FVector& InLowFuelHitNormal,
	const TArray<FVector_NetQuantize>& InBounceLocations,
	FRotator InRestRotation)
{
	bLowFuelHitObstacle = bInLowFuelHitObstacle;
	LowFuelHitNormal = InLowFuelHitNormal;

	RocketingLocations.Empty(InRocketingLocations.Num());
	for (const FVector_NetQuantize& Loc : InRocketingLocations)
	{
		RocketingLocations.Add(FVector(Loc.X, Loc.Y, Loc.Z));
	}

	LowFuelThrustLocation = FVector(InLowFuelThrustLocation.X, InLowFuelThrustLocation.Y, InLowFuelThrustLocation.Z);

	BounceLocations.Empty(InBounceLocations.Num());
	for (const FVector_NetQuantize& Loc : InBounceLocations)
	{
		BounceLocations.Add(FVector(Loc.X, Loc.Y, Loc.Z));
	}

	DeterministicRestRotation = InRestRotation;
	bTargetRestRotationInitialized = true;

	// Subdivide coarse rocketing waypoints into detailed spline points using LaunchSeed
	TArray<FVector> DetailedRocketingPoints;
	TArray<bool> IsCoarseFlags;
	SubdivideRocketingPathWithSeededOffsets(RocketingLocations, LaunchSeed, DetailedRocketingPoints, IsCoarseFlags);

	AuthoritativeFlightNodes.Empty(DetailedRocketingPoints.Num() + 3 + InBounceLocations.Num());
	AuthoritativeNodeIsCoarse.Empty(AuthoritativeFlightNodes.Max());
	CumulativeFlightPlanDistances.Empty(AuthoritativeFlightNodes.Max());

	for (int32 i = 0; i < DetailedRocketingPoints.Num(); ++i)
	{
		AuthoritativeFlightNodes.Add(DetailedRocketingPoints[i]);
		AuthoritativeNodeIsCoarse.Add(IsCoarseFlags[i]);
	}
	RocketingEndIndex = FMath::Max(0, AuthoritativeFlightNodes.Num() - 1);

	// Generate intermediate offset point(s) for the Low Fuel Upward Climb according to bounding distance
	const FVector FinalRocketingLoc = AuthoritativeFlightNodes[RocketingEndIndex];
	const FVector LowFuelApexLoc = LowFuelThrustLocation;
	const float LowFuelDist = FVector::Dist(FinalRocketingLoc, LowFuelApexLoc);

	if (LowFuelDist >= 350.0f)
	{
		const FVector LowFuelDir = (LowFuelApexLoc - FinalRocketingLoc) / LowFuelDist;
		FVector LowFuelPerp = (FVector::UpVector - (LowFuelDir * (FVector::UpVector | LowFuelDir))).GetSafeNormal();
		if (LowFuelPerp.IsNearlyZero())
		{
			LowFuelPerp = (LowFuelDir ^ FVector::RightVector).GetSafeNormal();
		}

		float LowFuelOffsetMag = 0.0f;
		if (bLowFuelHitObstacle)
		{
			// If an obstacle was struck at apex, keep offsets conservative and oriented along hit normal away from obstruction
			LowFuelOffsetMag = FMath::Clamp(LowFuelDist * 0.04f, 5.0f, 25.0f);
			LowFuelPerp = LowFuelHitNormal;
		}
		else
		{
			// Offset magnitude scaled by mesh bounding dimensions and segment distance
			const float BoundsScale = MeshBoundsExtent.Size() > 0.0f ? MeshBoundsExtent.Size() : 35.0f;
			LowFuelOffsetMag = FMath::Clamp(BoundsScale * 0.8f + LowFuelDist * 0.08f, 25.0f, 95.0f);
		}

		if (LowFuelDist < 700.0f)
		{
			// 1 intermediate offset point at midpoint
			const FVector LowFuelMid = FMath::Lerp(FinalRocketingLoc, LowFuelApexLoc, 0.5f) + (LowFuelPerp * LowFuelOffsetMag);
			AuthoritativeFlightNodes.Add(LowFuelMid);
			AuthoritativeNodeIsCoarse.Add(false);
		}
		else
		{
			// 2 intermediate offset points spaced ~300-500cm
			const FVector Mid1 = FMath::Lerp(FinalRocketingLoc, LowFuelApexLoc, 0.33f) + (LowFuelPerp * LowFuelOffsetMag * 0.75f);
			const FVector Mid2 = FMath::Lerp(FinalRocketingLoc, LowFuelApexLoc, 0.67f) + (LowFuelPerp * LowFuelOffsetMag);
			AuthoritativeFlightNodes.Add(Mid1);
			AuthoritativeNodeIsCoarse.Add(false);
			AuthoritativeFlightNodes.Add(Mid2);
			AuthoritativeNodeIsCoarse.Add(false);
		}
	}

	AuthoritativeFlightNodes.Add(LowFuelThrustLocation);
	AuthoritativeNodeIsCoarse.Add(true);
	LowFuelIndex = AuthoritativeFlightNodes.Num() - 1;

	FirstGroundHitIndex = AuthoritativeFlightNodes.Num();
	for (const FVector& Loc : BounceLocations)
	{
		AuthoritativeFlightNodes.Add(Loc);
		AuthoritativeNodeIsCoarse.Add(true);
	}

	float RunningDist = 0.0f;
	for (int32 i = 0; i < AuthoritativeFlightNodes.Num(); ++i)
	{
		if (i > 0)
		{
			RunningDist += FVector::Dist(AuthoritativeFlightNodes[i - 1], AuthoritativeFlightNodes[i]);
		}
		CumulativeFlightPlanDistances.Add(RunningDist);
	}

	CurrentFlightPlanNodeIndex = 0;
	CurrentSegmentDistTraveled = 0.0f;
	CurrentBounceReboundSpeed = 0.0f;
	LastImpactSpeed = 0.0f;

	LogDebug(FString::Printf(TEXT("SetAuthoritativeFullFlightPlan: %d rocketing nodes (%d detailed spline nodes), LowFuel apex: %s (ObstacleHit=%d, Normal=%s), %d bounce nodes. Total path: %.1f cm (RocketingEnd=%d, LowFuel=%d, FirstHit=%d, FinalRest=%d)"),
		RocketingLocations.Num(), DetailedRocketingPoints.Num(), *LowFuelThrustLocation.ToString(), bLowFuelHitObstacle ? 1 : 0, *LowFuelHitNormal.ToString(), BounceLocations.Num(), RunningDist,
		RocketingEndIndex, LowFuelIndex, FirstGroundHitIndex, AuthoritativeFlightNodes.Num() - 1), true);

	if (bEnableDebugLogs || bShowOnScreenDebug)
	{
		for (int32 i = 0; i < AuthoritativeFlightNodes.Num(); ++i)
		{
			FString StageTag = TEXT("Rocketing");
			if (i == LowFuelIndex) StageTag = TEXT("LowFuelThrust");
			else if (i == FirstGroundHitIndex) StageTag = TEXT("GroundHit");
			else if (i > FirstGroundHitIndex) StageTag = TEXT("Bounce");

			LogDebug(FString::Printf(TEXT("  FlightPlanNode[%d/%d] (%s): Loc=%s, DistFromStart=%.0f cm"),
				i, AuthoritativeFlightNodes.Num(), *StageTag, *AuthoritativeFlightNodes[i].ToString(), CumulativeFlightPlanDistances[i]));
		}
	}
}

void URocketProjectileComponent::SetAuthoritativeFullFlightPlan(
	const TArray<FVector_NetQuantize>& InRocketingLocations,
	FVector_NetQuantize InLowFuelThrustLocation,
	const TArray<FVector_NetQuantize>& InBounceLocations,
	FRotator InRestRotation)
{
	SetAuthoritativeFullFlightPlan(InRocketingLocations, InLowFuelThrustLocation, false, FVector::DownVector, InBounceLocations, InRestRotation);
}

void URocketProjectileComponent::SetAuthoritativeFlightPlan(const TArray<FVector_NetQuantize>& InFlightNodes)
{
	AuthoritativeFlightNodes.Empty(InFlightNodes.Num());
	CumulativeFlightPlanDistances.Empty(InFlightNodes.Num());

	float RunningDist = 0.0f;
	for (int32 i = 0; i < InFlightNodes.Num(); ++i)
	{
		const FVector NodePoint(InFlightNodes[i].X, InFlightNodes[i].Y, InFlightNodes[i].Z);
		AuthoritativeFlightNodes.Add(NodePoint);

		if (i > 0)
		{
			RunningDist += FVector::Dist(AuthoritativeFlightNodes[i - 1], NodePoint);
		}
		CumulativeFlightPlanDistances.Add(RunningDist);
	}

	CurrentFlightPlanNodeIndex = 0;
	CurrentSegmentDistTraveled = 0.0f;
	CurrentBounceReboundSpeed = 0.0f;
	LastImpactSpeed = 0.0f;
	RocketingEndIndex = FMath::Max(0, AuthoritativeFlightNodes.Num() - 1);
	LowFuelIndex = RocketingEndIndex;
	FirstGroundHitIndex = RocketingEndIndex;

	LogDebug(FString::Printf(TEXT("SetAuthoritativeFlightPlan: Stored %d rounded nodes, TotalPathDistance=%.1f cm"),
		AuthoritativeFlightNodes.Num(), RunningDist), true);
}

void URocketProjectileComponent::OnRep_LaunchSeed()
{
	LogDebug(FString::Printf(TEXT("OnRep_LaunchSeed: Received LaunchSeed=%d on [%s] (bPendingLaunch=%d)"),
		LaunchSeed, *GetNameSafe(GetOwner()), bPendingLaunch ? 1 : 0));

	if (bPendingLaunch && LaunchSeed > 0)
	{
		ApplyClientLaunch(PendingServerLaunchTime, PendingLaunchDirection, false);
	}
}

void URocketProjectileComponent::ApplyClientLaunch(
	float InServerLaunchTime,
	const FVector& InLaunchDirection,
	bool bSetStartingLocation,
	const FVector& InLaunchLocation,
	uint8 InSeed,
	float InDistance,
	float InSpeed)
{
	if (InServerLaunchTime <= LastReplicatedServerLaunchTime + 0.001f && InServerLaunchTime > 0.0f)
	{
		return;
	}

	if (InSeed != 0)
	{
		LaunchSeed = InSeed;
	}
	else if (FlightNetState.LaunchSeed != 0)
	{
		LaunchSeed = FlightNetState.LaunchSeed;
	}
	else if (LaunchSeed <= 0)
	{
		LaunchSeed = 1;
	}

	if (InDistance > 0.0f)
	{
		FlightSettings.BaseSplineLength = InDistance;
		FlightSettings.MaxFuelDistance = InDistance;
		MaxFuelDistance = InDistance;
	}
	if (InSpeed > 0.0f)
	{
		BaseSpeed = InSpeed;
		FlightSettings.BaseSpeed = InSpeed;
	}

	bPendingLaunch = false;
	LastReplicatedServerLaunchTime = InServerLaunchTime;

	SavePrimitiveSettings();

	AutoResolveTargetPrimitive();

	// Only snap starting position if explicitly requested (All-in-one high accuracy on spawn/fresh launch)
	// Never snap during regular two-stage launches!
	if (bSetStartingLocation && !InLaunchLocation.IsNearlyZero() && TargetPrimitive)
	{
		SetTargetTransformAroundCenter(InLaunchLocation, TargetPrimitive->GetComponentRotation());
	}

	if (GetOwner())
	{
		GetOwner()->SetActorHiddenInGame(false);
		GetOwner()->SetActorEnableCollision(true);
		GetOwner()->SetReplicateMovement(false);
		GetOwner()->SetActorTickEnabled(true);

		// If attached to a player pawn or socket, detach so it flies freely
		if (GetOwner()->GetAttachParentActor() || (GetOwner()->GetRootComponent() && GetOwner()->GetRootComponent()->GetAttachParent()))
		{
			GetOwner()->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}

		if (AActivePropActor* PropActor = Cast<AActivePropActor>(GetOwner()))
		{
			PropActor->SetHeld(false);
			PropActor->SetInMotion(true);
			PropActor->State = EPropState::InMotion;
		}

		// Suspend any PhysicsSimComponent while the rocket is in flight so it does not interfere or freeze the mesh
		APropManager* PM = APropManager::Get(GetOwner());
		for (UActorComponent* Comp : GetOwner()->GetComponents())
		{
			if (Comp && Comp->GetClass()->GetName().Contains(TEXT("PhysicsSim")))
			{
				if (UStaticMeshPhysicsSimulationComponent* SimComp = Cast<UStaticMeshPhysicsSimulationComponent>(Comp))
				{
					SimComp->ClearUnsettledTimer();
					SimComp->SetHeld(false);
					SimComp->SetInMotion(true);
					SimComp->SetStateInMotion();
					if (PM)
					{
						PM->UnregisterSettlingComponent(SimComp);
					}
				}
				Comp->SetComponentTickEnabled(false);
			}
		}
	}

	if (TargetPrimitive)
	{
		TargetPrimitive->SetHiddenInGame(false, true);
		TargetPrimitive->SetVisibility(true, true);
		TargetPrimitive->SetMobility(EComponentMobility::Movable);
		TargetPrimitive->SetSimulatePhysics(false);
		TargetPrimitive->SetEnableGravity(false);
		TargetPrimitive->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
		TargetPrimitive->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		TargetPrimitive->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		TargetPrimitive->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		TargetPrimitive->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		TargetPrimitive->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);

		const FBoxSphereBounds LocalBounds = TargetPrimitive->GetLocalBounds();
		const float ScaledMinZ = (LocalBounds.Origin.Z - LocalBounds.BoxExtent.Z) * TargetPrimitive->GetComponentScale().Z;
		InitialGroundZ = TargetPrimitive->GetComponentLocation().Z + ScaledMinZ;
	}

	// Calculate latency using synchronized server time
	const float ClientCurrentTime = GetSynchronizedServerTime();
	const float NetLag = FMath::Clamp(ClientCurrentTime - InServerLaunchTime, 0.0f, 1.5f);

	if (NetLag > 0.008f)
	{
		// Smooth catch-up over 0.18 to 0.40 seconds: moves faster to match server without any visual snapping!
		ClientCatchupDuration = FMath::Clamp(NetLag * 2.2f, 0.18f, 0.40f);
		ClientCatchupTimeRemaining = ClientCatchupDuration;
		ClientCatchupSpeedMultiplier = FMath::Clamp(1.0f + (NetLag / ClientCatchupDuration), 1.05f, 2.5f);
	}
	else
	{
		ClientCatchupDuration = 0.0f;
		ClientCatchupTimeRemaining = 0.0f;
		ClientCatchupSpeedMultiplier = 1.0f;
	}

	ElapsedFlightTime = 0.0f;
	TotalFlightDistanceTraveled = 0.0f;
	TimeSpentFalling = 0.0f;
	TotalAirTime = 0.0f;
	SimulationTimeAccumulator = 0.0f;

	RandomStream.Initialize(LaunchSeed);
	CurrentBounceCount = 0;
	TipOverElapsedTime = 0.0f;
	TimeSinceLastBounce = 100.0f;
	DebugHeartbeatTimer = 0.0f;
	TravelHistoryLogTimer = 0.0f;
	PhysicsSettledTimeAccumulated = 0.0f;
	LastHitComponent = nullptr;
	LastHitNormal = FVector::ZeroVector;
	ClientReconciliationOffset = FVector::ZeroVector;
	bTargetRestRotationInitialized = false;
	FallingTumbleAngle = 0.0f;
	FlightWaypoints.Empty();
	FlightDistanceTraveled = 0.0f;
	CurrentFlightPlanNodeIndex = 0;
	CurrentSegmentDistTraveled = 0.0f;
	CurrentBounceReboundSpeed = 0.0f;
	LastImpactSpeed = 0.0f;
	LastDebugTrailLocation = GetWorldCenterLocation();

	TipOverLaunchHeading = InLaunchDirection.GetSafeNormal();
	if (TipOverLaunchHeading.IsNearlyZero())
	{
		TipOverLaunchHeading = GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector;
	}

	// Line trace along launch heading to find initial target hit position using ECC_Visibility
	const FVector LaunchTraceStart = GetWorldCenterLocation();
	const FVector LaunchTraceEnd = LaunchTraceStart + (TipOverLaunchHeading * 15000.0f);
	FHitResult LaunchHit;
	FCollisionQueryParams LaunchTraceParams(SCENE_QUERY_STAT(RocketClientLaunchHitTrace), false, GetOwner());
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(LaunchHit, LaunchTraceStart, LaunchTraceEnd, ECC_Visibility, LaunchTraceParams))
	{
		LaunchTargetHitLocation = LaunchHit.ImpactPoint;
		bHasLaunchTargetHit = true;
	}
	else
	{
		LaunchTargetHitLocation = LaunchTraceEnd;
		bHasLaunchTargetHit = false;
	}
	FlightLegIndex = 0;

	const ERocketSimulationState PrevState = CurrentState;
	if (TippingSettings.bEnableTipping && bTipOverToFly)
	{
		InitializeTipping(TipOverLaunchHeading);
		CurrentState = ERocketSimulationState::TippingOver;
	}
	else if (DraggingSettings.bEnableDragging)
	{
		InitializeDragging(InServerLaunchTime, LaunchSeed);
		CurrentState = ERocketSimulationState::Dragging;
	}
	else
	{
		TransitionToRocketing(GetWorldCenterLocation());
	}

	SetActive(true);
	SetComponentTickEnabled(true);

	LogDebug(FString::Printf(TEXT("ApplyClientLaunch SUCCESS! NetLag=%.3fs, Catchup=%.2fx (for %.2fs), Speed=%.1f, Seed=%d"),
		NetLag, ClientCatchupSpeedMultiplier, ClientCatchupDuration, BaseSpeed, LaunchSeed));

	OnRocketStateChanged.Broadcast(CurrentState, PrevState);
	OnRocketLaunched.Broadcast();
}

void URocketProjectileComponent::BeginPlay()
{
	Super::BeginPlay();

	AutoResolveTargetPrimitive();

	if (CurrentState == ERocketSimulationState::Idle || CurrentState == ERocketSimulationState::AtRest)
	{
		SetComponentTickEnabled(false);
	}
	else
	{
		SetComponentTickEnabled(true);
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (LaunchSeed <= 0)
		{
			LaunchSeed = FMath::RandRange(1, 255);
		}
	}
}

void URocketProjectileComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(URocketProjectileComponent, FlightNetState);
	DOREPLIFETIME(URocketProjectileComponent, MaxFuelDuration);
	DOREPLIFETIME(URocketProjectileComponent, MaxFuelDistance);
	DOREPLIFETIME(URocketProjectileComponent, BaseSpeed);
	DOREPLIFETIME(URocketProjectileComponent, LaunchSeed);
}

void URocketProjectileComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// If server handed off to physics simulation, monitor the physics body until it settles
	if (FlightNetState.bPhysicsSimulating)
	{
		if (GetOwner() && GetOwner()->HasAuthority() && TargetPrimitive && TargetPrimitive->IsSimulatingPhysics())
		{
			TotalAirTime += DeltaTime;
			if (bEnableDebugLogs || bShowOnScreenDebug)
			{
				DebugHeartbeatTimer += DeltaTime;
				if (DebugHeartbeatTimer >= 1.0f)
				{
					DebugHeartbeatTimer = 0.0f;
					const FVector Loc = TargetPrimitive->GetComponentLocation();
					const float LinSpeed = TargetPrimitive->GetPhysicsLinearVelocity().Size();
					const float AngSpeed = TargetPrimitive->GetPhysicsAngularVelocityInDegrees().Size();
					LogDebug(FString::Printf(TEXT("Physics Tumble: LinSpeed=%.1f, AngSpeed=%.1f, Loc=(X=%.0f, Y=%.0f, Z=%.0f), AirTime=%.1f s"),
						LinSpeed, AngSpeed, Loc.X, Loc.Y, Loc.Z, TotalAirTime));
				}
			}

			const float LinSpeed = TargetPrimitive->GetPhysicsLinearVelocity().Size();
			const float AngSpeed = TargetPrimitive->GetPhysicsAngularVelocityInDegrees().Size();

			// If nearly motionless, verify ground contact before settling (never freeze in mid-air!)
			if (LinSpeed < 25.0f && AngSpeed < 25.0f)
			{
				FHitResult GroundCheck;
				FCollisionQueryParams QP(SCENE_QUERY_STAT(RocketGroundCheckBeforeSettle), false, GetOwner());
				const FVector CenterLoc = TargetPrimitive->GetComponentLocation();
				const bool bNearGround = GetWorld() && GetWorld()->LineTraceSingleByChannel(GroundCheck, CenterLoc, CenterLoc - FVector(0.0f, 0.0f, 150.0f), ECC_WorldStatic, QP);

				if (bNearGround)
				{
					PhysicsSettledTimeAccumulated += DeltaTime;
					if (PhysicsSettledTimeAccumulated >= 0.25f)
					{
						const FVector FinalLoc = TargetPrimitive->GetComponentLocation();
						const FRotator FinalRot = TargetPrimitive->GetComponentRotation();
						LogDebug(FString::Printf(TEXT("Physics body settled on surface! LinSpeed=%.1f, AngSpeed=%.1f. Broadcasting Multicast_SettleRocket."), LinSpeed, AngSpeed));
						Multicast_SettleRocket(FinalLoc, FinalRot);
						return;
					}
				}
				else
				{
					// Floating/falling in mid air: do not accumulate settled time, keep falling!
					PhysicsSettledTimeAccumulated = 0.0f;
				}
			}
			else
			{
				PhysicsSettledTimeAccumulated = 0.0f;
			}
		}
		return;
	}

	if (!IsActive() || CurrentState == ERocketSimulationState::Idle || CurrentState == ERocketSimulationState::AtRest)
	{
		if (CurrentState == ERocketSimulationState::AtRest && !ClientReconciliationOffset.IsNearlyZero())
		{
			// Seamless visual slide to rest location: no snap!
			ClientReconciliationOffset = FMath::VInterpTo(ClientReconciliationOffset, FVector::ZeroVector, DeltaTime, 12.0f);
			if (ClientReconciliationOffset.SizeSquared() < 0.25f)
			{
				ClientReconciliationOffset = FVector::ZeroVector;
				SetComponentTickEnabled(false);
			}
			else if (TargetPrimitive)
			{
				const FVector RestCenter = FlightNetState.RestLocation.IsNearlyZero() ? GetWorldCenterLocation() : FlightNetState.RestLocation;
				SetTargetTransformAroundCenter(RestCenter, FlightNetState.RestRotation);
			}
			return;
		}

		SetComponentTickEnabled(false);
		return;
	}

	TimeSinceLastBounce += DeltaTime;

	// In-flight debug heartbeat every 1 second
	if (bEnableDebugLogs || bShowOnScreenDebug)
	{
		DebugHeartbeatTimer += DeltaTime;
		if (DebugHeartbeatTimer >= 1.0f)
		{
			DebugHeartbeatTimer = 0.0f;
			const FString StateStr = UEnum::GetValueAsString(CurrentState);
			const FVector Loc = TargetPrimitive ? TargetPrimitive->GetComponentLocation() : FVector::ZeroVector;
			LogDebug(FString::Printf(TEXT("Heartbeat: State=%s, Flight=%.1f/%.1f s, Speed=%.1f, Loc=(X=%.0f, Y=%.0f, Z=%.0f), Bounces=%d"),
				*StateStr, ElapsedFlightTime, MaxFuelDuration, CurrentVelocity.Size(), Loc.X, Loc.Y, Loc.Z, CurrentBounceCount));
		}
	}

	float StepDeltaTime = DeltaTime;

	// Client dead-reckoning catch-up calculated from InServerLaunchTime:
	// Client dead-reckoning catch-up calculated from InServerLaunchTime:
	// Only accelerates during powered rocketing flight! Never accelerates during spent falling, at rest, or when slow-motion is active!
	const bool bIsServer = GetOwner() ? GetOwner()->HasAuthority() : false;
	const float GlobalDilation = (GetWorld() && GetWorld()->GetWorldSettings()) ? GetWorld()->GetWorldSettings()->TimeDilation : 1.0f;
	const float ActorDilation = GetOwner() ? GetOwner()->CustomTimeDilation : 1.0f;
	const bool bIsSlowMotionActive = (GlobalDilation < 0.95f) || (ActorDilation < 0.95f);

	if (!bIsServer && LastReplicatedServerLaunchTime > 0.0f)
	{
		if (bIsSlowMotionActive || CurrentState == ERocketSimulationState::SpentFalling || CurrentState == ERocketSimulationState::AtRest)
		{
			// When slow-motion is active or projectile is spent/resting, never force catch-up so slow-motion remains silky smooth!
			ClientCatchupSpeedMultiplier = 1.0f;
		}
		else
		{
			const float ServerWorldTime = GetSynchronizedServerTime();
			const float ServerElapsedSinceLaunch = FMath::Max(0.0f, ServerWorldTime - LastReplicatedServerLaunchTime);
			const float SimulationLag = ServerElapsedSinceLaunch - TotalAirTime;

			if (SimulationLag > 0.005f)
			{
				// Speed up simulation to catch up to server world time smoothly without skipping steps
				ClientCatchupSpeedMultiplier = FMath::Clamp(1.0f + (SimulationLag * 3.5f), 1.05f, 3.0f);
				StepDeltaTime = DeltaTime * ClientCatchupSpeedMultiplier;
			}
			else
			{
				ClientCatchupSpeedMultiplier = 1.0f;
			}
		}
	}
	else if (!bIsServer && ClientCatchupTimeRemaining > 0.0f)
	{
		if (bIsSlowMotionActive || CurrentState == ERocketSimulationState::SpentFalling || CurrentState == ERocketSimulationState::AtRest)
		{
			ClientCatchupSpeedMultiplier = 1.0f;
			ClientCatchupTimeRemaining = 0.0f;
		}
		else
		{
			StepDeltaTime = DeltaTime * ClientCatchupSpeedMultiplier;
			ClientCatchupTimeRemaining -= DeltaTime;
			if (ClientCatchupTimeRemaining <= 0.0f)
			{
				ClientCatchupSpeedMultiplier = 1.0f;
			}
		}
	}

	// Visual offset decay for seamless mid-flight drift correction
	if (!ClientReconciliationOffset.IsNearlyZero())
	{
		ClientReconciliationOffset = FMath::VInterpTo(ClientReconciliationOffset, FVector::ZeroVector, DeltaTime, 8.0f);
		if (ClientReconciliationOffset.SizeSquared() < 1.0f)
		{
			ClientReconciliationOffset = FVector::ZeroVector;
		}
	}

	// Continuous sub-stepping that supports arbitrary slow-motion & high frame rates:
	// In slow-motion (e.g. DeltaTime = 0.0016s), steps directly each frame with no accumulation delay.
	// For large frame times (> 33ms hitch), splits into smaller sub-steps up to a max limit.
	constexpr float MaxSubStepDelta = 1.0f / 60.0f;
	if (StepDeltaTime <= MaxSubStepDelta)
	{
		// Normal, high-refresh, or slow-motion: advance smoothly every single frame!
		PerformSimulationStep(StepDeltaTime);
	}
	else
	{
		// Large delta time hitch: sub-step to prevent jumping over waypoints
		float RemainingTime = StepDeltaTime;
		int32 SubStepCount = 0;
		while (RemainingTime > 0.0001f && SubStepCount < 8)
		{
			const float SubStep = FMath::Min(RemainingTime, MaxSubStepDelta);
			PerformSimulationStep(SubStep);
			RemainingTime -= SubStep;
			SubStepCount++;

			if (CurrentState == ERocketSimulationState::AtRest || CurrentState == ERocketSimulationState::Idle)
			{
				break;
			}
		}
	}

	// Draw real-time in-world 3D trajectories and on-screen HUD
	DrawRocketDebugVisuals(DeltaTime);
}

void URocketProjectileComponent::SavePrimitiveSettings()
{
	if (bHasSavedSettings)
	{
		return;
	}

	AutoResolveTargetPrimitive();

	if (TargetPrimitive)
	{
		bSavedSimulatePhysics = TargetPrimitive->IsSimulatingPhysics();
		bSavedEnableGravity = TargetPrimitive->IsGravityEnabled();
		SavedMobility = TargetPrimitive->Mobility;
		SavedCollisionProfileName = TargetPrimitive->GetCollisionProfileName();
		SavedCollisionEnabled = TargetPrimitive->GetCollisionEnabled();
		SavedCollisionResponses = TargetPrimitive->GetCollisionResponseToChannels();
	}

	if (AActor* OwnerActor = GetOwner())
	{
		bSavedReplicateMovement = OwnerActor->IsReplicatingMovement();
	}

	bHasSavedSettings = true;
	LogDebug(FString::Printf(TEXT("SavePrimitiveSettings: Saved (SimPhys=%d, Grav=%d, Mobility=%d, Profile=%s, ColEnabled=%d, RepMov=%d)"),
		bSavedSimulatePhysics ? 1 : 0, bSavedEnableGravity ? 1 : 0, static_cast<int32>(SavedMobility),
		*SavedCollisionProfileName.ToString(), static_cast<int32>(SavedCollisionEnabled), bSavedReplicateMovement ? 1 : 0));
}

void URocketProjectileComponent::RestorePrimitiveSettings()
{
	if (!bHasSavedSettings)
	{
		return;
	}

	AutoResolveTargetPrimitive();

	if (TargetPrimitive)
	{
		TargetPrimitive->SetMobility(SavedMobility);
		if (!SavedCollisionProfileName.IsNone())
		{
			TargetPrimitive->SetCollisionProfileName(SavedCollisionProfileName);
		}
		TargetPrimitive->SetCollisionResponseToChannels(SavedCollisionResponses);
		TargetPrimitive->SetCollisionEnabled(SavedCollisionEnabled);
		TargetPrimitive->SetSimulatePhysics(bSavedSimulatePhysics);
		TargetPrimitive->SetEnableGravity(bSavedEnableGravity);
		TargetPrimitive->SetPhysicsLinearVelocity(FVector::ZeroVector);
		TargetPrimitive->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}

	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->SetReplicateMovement(bSavedReplicateMovement);
	}

	bHasSavedSettings = false;
	LogDebug(TEXT("RestorePrimitiveSettings: Restored original primitive and actor settings."));
}

void URocketProjectileComponent::ResetRocketSimulation()
{
	CurrentState = ERocketSimulationState::Idle;
	CurrentVelocity = FVector::ZeroVector;
	ElapsedFlightTime = 0.0f;
	TotalFlightDistanceTraveled = 0.0f;
	TimeSpentFalling = 0.0f;
	TotalAirTime = 0.0f;
	CurrentBounceCount = 0;
	TipOverElapsedTime = 0.0f;
	TimeSinceLastBounce = 100.0f;
	DebugHeartbeatTimer = 0.0f;
	TravelHistoryLogTimer = 0.0f;
	PhysicsSettledTimeAccumulated = 0.0f;
	LastHitComponent = nullptr;
	LastHitNormal = FVector::ZeroVector;
	LastReplicatedServerLaunchTime = -1.0f;
	NetSyncTimer = 0.0f;
	ClientCatchupDuration = 0.0f;
	ClientCatchupTimeRemaining = 0.0f;
	ClientCatchupSpeedMultiplier = 1.0f;
	ClientReconciliationOffset = FVector::ZeroVector;

	bPendingLaunch = false;
	PendingServerLaunchTime = 0.0f;
	SimulationTimeAccumulator = 0.0f;
	DragElapsedTime = 0.0f;
	TotalDragDuration = 0.45f;
	FlightWaypoints.Empty();
	FlightDistanceTraveled = 0.0f;
	AuthoritativeFlightNodes.Empty();
	CumulativeFlightPlanDistances.Empty();
	CurrentFlightPlanNodeIndex = 0;
	CurrentSegmentDistTraveled = 0.0f;
	CurrentBounceReboundSpeed = 0.0f;
	LastImpactSpeed = 0.0f;
	FallingTumbleAngle = 0.0f;
	bTargetRestRotationInitialized = false;
	DeterministicRestRotation = FRotator::ZeroRotator;
	LaunchTargetHitLocation = FVector::ZeroVector;
	bHasLaunchTargetHit = false;
	FlightLegIndex = 0;
	LastDebugTrailLocation = FVector::ZeroVector;

	FlightNetState = FRocketFlightNetState();

	SetComponentTickEnabled(false);
	SetActive(false);

	RestorePrimitiveSettings();
}

void URocketProjectileComponent::Deactivate()
{
	Super::Deactivate();

	CurrentVelocity = FVector::ZeroVector;
	SimulationTimeAccumulator = 0.0f;
	ClientCatchupDuration = 0.0f;
	ClientCatchupTimeRemaining = 0.0f;
	ClientCatchupSpeedMultiplier = 1.0f;
	ClientReconciliationOffset = FVector::ZeroVector;
	LastDebugTrailLocation = FVector::ZeroVector;
	bPendingLaunch = false;
	PendingServerLaunchTime = 0.0f;
	SetSimulationState(ERocketSimulationState::AtRest);
	SetComponentTickEnabled(false);
	RestorePrimitiveSettings();
}

void URocketProjectileComponent::LaunchRocketHighAccuracy(
	FVector InDirection,
	uint8 InSeed,
	float InDistance,
	float InSpeed)
{
	LaunchRocket(InDirection, true, InSeed, InDistance, InSpeed);
}

void URocketProjectileComponent::LaunchRocket(
	FVector InDirection,
	bool bHighAccuracy,
	uint8 InSeed,
	float InDistance,
	float InSpeed)
{
	const bool bUseAllInOne = bHighAccuracy || bLaunchWithHighAccuracy || (bLaunchOnInitialized && InSeed != 0 && InDistance > 0.0f);

	if (InDistance > 0.0f)
	{
		FlightSettings.BaseSplineLength = InDistance;
		FlightSettings.MaxFuelDistance = InDistance;
		MaxFuelDistance = InDistance;
	}
	if (InSpeed > 0.0f)
	{
		BaseSpeed = InSpeed;
		FlightSettings.BaseSpeed = InSpeed;
	}

	SavePrimitiveSettings();

	AutoResolveTargetPrimitive();

	if (TargetPrimitive)
	{
		// Disable physics simulation and lock to movable so manual sweep movement is not overridden by Chaos physics
		TargetPrimitive->SetMobility(EComponentMobility::Movable);
		TargetPrimitive->SetSimulatePhysics(false);
		TargetPrimitive->SetEnableGravity(false);

		// Enforce block collision so the rocket and player can interact
		TargetPrimitive->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
		TargetPrimitive->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		TargetPrimitive->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		TargetPrimitive->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		TargetPrimitive->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		TargetPrimitive->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);

		// Record initial ground contact height
		const FBoxSphereBounds LocalBounds = TargetPrimitive->GetLocalBounds();
		const float ScaledMinZ = (LocalBounds.Origin.Z - LocalBounds.BoxExtent.Z) * TargetPrimitive->GetComponentScale().Z;
		InitialGroundZ = TargetPrimitive->GetComponentLocation().Z + ScaledMinZ;

		// Disable Actor movement replication while the rocket projectile component kinematically simulates flight
		// to prevent AActor::OnRep_ReplicatedMovement from fighting client-side prediction and causing jitter.
		if (AActor* OwnerActor = GetOwner())
		{
			OwnerActor->SetActorHiddenInGame(false);
			OwnerActor->SetActorEnableCollision(true);
			OwnerActor->SetReplicateMovement(false);
			if (OwnerActor->HasAuthority())
			{
				OwnerActor->SetReplicates(true);
				OwnerActor->bAlwaysRelevant = true;
				OwnerActor->NetPriority = 3.0f;
				OwnerActor->SetNetUpdateFrequency(60.0f);
			}

			// If attached to a player pawn or socket, detach so it flies freely
			if (OwnerActor->GetAttachParentActor() || (OwnerActor->GetRootComponent() && OwnerActor->GetRootComponent()->GetAttachParent()))
			{
				OwnerActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			}

			if (AActivePropActor* PropActor = Cast<AActivePropActor>(OwnerActor))
			{
				PropActor->SetHeld(false);
				PropActor->SetInMotion(true);
				PropActor->State = EPropState::InMotion;
			}

			// Suspend any PhysicsSimComponent while the rocket is in flight so it does not interfere or freeze the mesh
			APropManager* PM = APropManager::Get(OwnerActor);
			for (UActorComponent* Comp : OwnerActor->GetComponents())
			{
				if (Comp && Comp->GetClass()->GetName().Contains(TEXT("PhysicsSim")))
				{
					if (UStaticMeshPhysicsSimulationComponent* SimComp = Cast<UStaticMeshPhysicsSimulationComponent>(Comp))
					{
						SimComp->ClearUnsettledTimer();
						SimComp->SetHeld(false);
						SimComp->SetInMotion(true);
						SimComp->SetStateInMotion();
						if (PM)
						{
							PM->UnregisterSettlingComponent(SimComp);
						}
					}
					Comp->SetComponentTickEnabled(false);
				}
			}
		}
	}
	else
	{
		if (bEnableDebugLogs)
		{
			UE_LOG(LogTemp, Error, TEXT("URocketProjectileComponent::LaunchRocket aborted: No TargetPrimitive found on [%s]! Please add a StaticMeshComponent."),
				*GetNameSafe(GetOwner()));
		}
		return;
	}

	// Resolve seed deterministically
	if (InSeed != 0)
	{
		LaunchSeed = InSeed;
	}
	else if (LaunchSeed <= 0)
	{
		LaunchSeed = FMath::RandRange(1, 255);
	}
	RandomStream.Initialize(LaunchSeed);
	SimulationTimeAccumulator = 0.0f;

	ElapsedFlightTime = 0.0f;
	TotalFlightDistanceTraveled = 0.0f;
	TimeSpentFalling = 0.0f;
	TotalAirTime = 0.0f;
	CurrentBounceCount = 0;
	TipOverElapsedTime = 0.0f;
	TimeSinceLastBounce = 100.0f;
	DebugHeartbeatTimer = 0.0f;
	TravelHistoryLogTimer = 0.0f;
	PhysicsSettledTimeAccumulated = 0.0f;
	LastHitComponent = nullptr;
	LastHitNormal = FVector::ZeroVector;
	NetSyncTimer = 0.0f;
	bTargetRestRotationInitialized = false;
	FallingTumbleAngle = 0.0f;
	FlightWaypoints.Empty();
	FlightDistanceTraveled = 0.0f;

	// Resolve launch heading (Orange arrow in Images 1-5)
	FVector CleanHeading = InDirection.GetSafeNormal();
	if (CleanHeading.IsNearlyZero())
	{
		CleanHeading = GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector;
	}
	TipOverLaunchHeading = CleanHeading;

	// Line trace along launch heading to find initial target hit position using ECC_Visibility
	const FVector LaunchTraceStart = GetWorldCenterLocation();
	const FVector LaunchTraceEnd = LaunchTraceStart + (TipOverLaunchHeading * 15000.0f);
	FHitResult LaunchHit;
	FCollisionQueryParams LaunchTraceParams(SCENE_QUERY_STAT(RocketLaunchHitTrace), false, GetOwner());
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(LaunchHit, LaunchTraceStart, LaunchTraceEnd, ECC_Visibility, LaunchTraceParams))
	{
		LaunchTargetHitLocation = LaunchHit.ImpactPoint;
		bHasLaunchTargetHit = true;
	}
	else
	{
		LaunchTargetHitLocation = LaunchTraceEnd;
		bHasLaunchTargetHit = false;
	}
	FlightLegIndex = 0;
	LastDebugTrailLocation = GetWorldCenterLocation();

	if (TippingSettings.bEnableTipping && bTipOverToFly)
	{
		InitializeTipping(TipOverLaunchHeading);
		SetSimulationState(ERocketSimulationState::TippingOver);
	}
	else if (DraggingSettings.bEnableDragging)
	{
		InitializeDragging(GetSynchronizedServerTime(), LaunchSeed);
		SetSimulationState(ERocketSimulationState::Dragging);
	}
	else
	{
		TransitionToRocketing(GetWorldCenterLocation());
	}

	// Update network state on server (fallback & persistent replication)
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		const float ServerTime = GetSynchronizedServerTime();
		FlightNetState.State = CurrentState;
		FlightNetState.LaunchSeed = static_cast<uint8>(LaunchSeed & 0xFF);
		FlightNetState.BounceCount = 0;
		FlightNetState.FuelDuration = MaxFuelDuration;
		FlightNetState.Speed = BaseSpeed;
		FlightNetState.bPhysicsSimulating = false;
		FlightNetState.LaunchLocation = GetWorldCenterLocation();
		FlightNetState.LaunchDirection = TipOverLaunchHeading;
		FlightNetState.LaunchTargetHitLocation = LaunchTargetHitLocation;
		FlightNetState.RestLocation = FVector::ZeroVector;
		FlightNetState.RestRotation = FRotator::ZeroRotator;
		FlightNetState.ServerLaunchTime = ServerTime;
		if (bUseAllInOne)
		{
			// All-In-One Instant Launch (High Accuracy): Transmits Location, Direction, Seed, Distance, Speed (~22 bytes)
			Multicast_LaunchRocketFull(
				ServerTime,
				FlightNetState.LaunchLocation,
				FlightNetState.LaunchDirection,
				FlightNetState.LaunchSeed,
				MaxFuelDistance,
				FlightNetState.Speed
			);
		}
		else
		{
			// Two-Stage Launch (Ultra-Compact): Transmits Time, Direction & Seed (~8 bytes, NO location snapping in mid-air)
			Multicast_LaunchRocket(ServerTime, FlightNetState.LaunchDirection, FlightNetState.LaunchSeed);
		}

		// 1. Pre-calculate 3-part flight plan on server: rocketing locations, low fuel upward thrust point, obstacle hit info, and ground bounce array
		TArray<FVector_NetQuantize> ServerRocketingLocations;
		FVector_NetQuantize ServerLowFuelThrustLocation;
		TArray<FVector_NetQuantize> ServerBounceLocations;
		FRotator ServerRestRotation;
		bool bServerLowFuelHitObstacle = false;
		FVector_NetQuantizeNormal ServerLowFuelHitNormal = FVector::DownVector;

		GenerateServerFlightPlan(ServerRocketingLocations, ServerLowFuelThrustLocation, bServerLowFuelHitObstacle, ServerLowFuelHitNormal, ServerBounceLocations, ServerRestRotation);

		// 2. Server activates the 3-part authoritative flight plan immediately
		SetAuthoritativeFullFlightPlan(ServerRocketingLocations, ServerLowFuelThrustLocation, bServerLowFuelHitObstacle, ServerLowFuelHitNormal, ServerBounceLocations, ServerRestRotation);

		// 3. Broadcast the datasets to all clients
		Multicast_SendFullFlightPlan(ServerRocketingLocations, ServerLowFuelThrustLocation, bServerLowFuelHitObstacle, ServerLowFuelHitNormal, ServerBounceLocations, ServerRestRotation);
	}

	SetActive(true);
	SetComponentTickEnabled(true);

	LogDebug(FString::Printf(TEXT("LaunchRocket SUCCESS! Heading: %s, Speed: %.1f, Seed: %d, Distance: %.1f cm, Mode=%s"),
		*TipOverLaunchHeading.ToString(), CurrentVelocity.Size(), InSeed, MaxFuelDistance, bUseAllInOne ? TEXT("AllInOne_HighAccuracy") : TEXT("TwoStage_Compact")));

	OnRocketLaunched.Broadcast();
}

void URocketProjectileComponent::StopRocket(bool bImmediate)
{
	if (bImmediate)
	{
		if (!GetOwner() || GetOwner()->HasAuthority())
		{
			const FVector FinalLoc = TargetPrimitive ? TargetPrimitive->GetComponentLocation() : FVector::ZeroVector;
			const FRotator FinalRot = TargetPrimitive ? TargetPrimitive->GetComponentRotation() : FRotator::ZeroRotator;
			Multicast_SettleRocket(FinalLoc, FinalRot);
		}
		else
		{
			CurrentVelocity = FVector::ZeroVector;
			SetSimulationState(ERocketSimulationState::AtRest);
			SetComponentTickEnabled(false);
		}
	}
	else
	{
		SetSimulationState(ERocketSimulationState::SpentFalling);
	}
}

void URocketProjectileComponent::HandoffToPhysicsSimulation(FVector AdditionalImpulse, FVector AngularVelocityInDegrees)
{
	AutoResolveTargetPrimitive();

	if (!TargetPrimitive)
	{
		LogDebug(TEXT("HandoffToPhysicsSimulation: TargetPrimitive is null!"));
		return;
	}

	// 1. Capture final velocity (rocket velocity + any extra impulse)
	const FVector FinalLinearVelocity = CurrentVelocity + AdditionalImpulse;

	// 2. Generate natural bottle tumble if angular velocity is zero
	FVector FinalAngularVelocity = AngularVelocityInDegrees;
	if (FinalAngularVelocity.IsNearlyZero() && !CurrentVelocity.IsNearlyZero())
	{
		const FVector CrossAxis = FVector::CrossProduct(CurrentVelocity.GetSafeNormal(), FVector::UpVector);
		FinalAngularVelocity = (CrossAxis * 360.0f) + FVector(RandomStream.FRandRange(-120.0f, 120.0f), RandomStream.FRandRange(-120.0f, 120.0f), 0.0f);
	}

	LogDebug(FString::Printf(TEXT("Initiating HandoffToPhysicsSimulation: FinalLinVel: %s, FinalAngVel: %s"),
		*FinalLinearVelocity.ToString(), *FinalAngularVelocity.ToString()));

	// 3. Replicate handoff to all machines so clients never freeze in air
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		FlightNetState.bPhysicsSimulating = true;
		FlightNetState.LastBounceVelocity = FinalLinearVelocity;
		Multicast_HandoffToPhysicsSimulation(FinalLinearVelocity, FinalAngularVelocity);
	}
	else
	{
		ApplyPhysicsHandoff(FinalLinearVelocity, FinalAngularVelocity);
	}
}

void URocketProjectileComponent::Multicast_HandoffToPhysicsSimulation_Implementation(FVector FinalLinearVelocity, FVector FinalAngularVelocity)
{
	ApplyPhysicsHandoff(FinalLinearVelocity, FinalAngularVelocity);
}

void URocketProjectileComponent::ApplyPhysicsHandoff(const FVector& FinalLinearVelocity, const FVector& FinalAngularVelocity)
{
	AutoResolveTargetPrimitive();

	// 1. Mark component state
	CurrentVelocity = FVector::ZeroVector;
	FlightNetState.bPhysicsSimulating = true;
	PhysicsSettledTimeAccumulated = 0.0f;
	SetSimulationState(ERocketSimulationState::SpentFalling);

	const bool bIsServer = GetOwner() ? GetOwner()->HasAuthority() : false;

	// 2. Configure target primitive collision
	if (TargetPrimitive)
	{
		TargetPrimitive->SetMobility(EComponentMobility::Movable);
		TargetPrimitive->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
		TargetPrimitive->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		TargetPrimitive->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		TargetPrimitive->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		TargetPrimitive->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		TargetPrimitive->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);

		// On both Server and Client: engage physics simulation and gravity so the bottle tumbles to the ground naturally!
		TargetPrimitive->SetSimulatePhysics(true);
		TargetPrimitive->SetEnableGravity(true);
		TargetPrimitive->WakeAllRigidBodies();
		TargetPrimitive->SetPhysicsLinearVelocity(FinalLinearVelocity);
		if (!FinalAngularVelocity.IsNearlyZero())
		{
			TargetPrimitive->SetPhysicsAngularVelocityInDegrees(FinalAngularVelocity);
		}
	}

	// 3. Keep Actor collision enabled and transition Prop Actor & PhysicsSimComponent into Unsettled state
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->SetActorEnableCollision(true);
		// Keep replicate movement false during tumbling so network movement replication does not fight local Chaos physics
		OwnerActor->SetReplicateMovement(false);

		if (AActivePropActor* PropActor = Cast<AActivePropActor>(OwnerActor))
		{
			PropActor->SetHeld(false);
			PropActor->SetInMotion(false);
			PropActor->bIsHeld = false;
			PropActor->bIsInMotion = false;
			PropActor->SetGrabbable(true);
			PropActor->InteractionState = EPropInteractionLockState::Free;
			PropActor->InteractionOwner = nullptr;
		}

		// Hand off to PhysicsSimComponent and set it in Unsettled state so the bottle continues simulating until it reaches the floor!
		for (UActorComponent* Comp : OwnerActor->GetComponents())
		{
			if (Comp && Comp->GetClass()->GetName().Contains(TEXT("PhysicsSim")))
			{
				if (UStaticMeshPhysicsSimulationComponent* SimComp = Cast<UStaticMeshPhysicsSimulationComponent>(Comp))
				{
					SimComp->SetComponentTickEnabled(true);
					SimComp->SetHeld(false);
					SimComp->SetInMotion(false);
					SimComp->SetStateUnsettled();
					LogDebug(TEXT("ApplyPhysicsHandoff: Transitioned PhysicsSimComponent into Unsettled state!"), true);
				}
			}
		}
	}

	// 4. Disable RocketProjectileComponent tick so its internal timers never prematurely freeze the physics simulation in mid-air
	SetComponentTickEnabled(false);

	LogDebug(FString::Printf(TEXT("ApplyPhysicsHandoff complete! IsServer: %d, LinearVel: %s, AngularVel: %s"),
		bIsServer ? 1 : 0, *FinalLinearVelocity.ToString(), *FinalAngularVelocity.ToString()));
}

void URocketProjectileComponent::Multicast_SettleRocket_Implementation(FVector InRestLocation, FRotator InRestRotation)
{
	AutoResolveTargetPrimitive();

	// 1. Update internal state
	CurrentVelocity = FVector::ZeroVector;
	SimulationTimeAccumulator = 0.0f;
	ClientCatchupSpeedMultiplier = 1.0f;
	FlightNetState.bPhysicsSimulating = false;
	FlightNetState.RestLocation = InRestLocation;
	FlightNetState.RestRotation = InRestRotation;
	SetSimulationState(ERocketSimulationState::AtRest);

	const bool bIsServer = GetOwner() ? GetOwner()->HasAuthority() : false;

	// 2. Lock TargetPrimitive flush to rest transform and zero velocities
	if (TargetPrimitive)
	{
		TargetPrimitive->SetSimulatePhysics(false);
		TargetPrimitive->SetEnableGravity(false);
		TargetPrimitive->SetPhysicsLinearVelocity(FVector::ZeroVector);
		TargetPrimitive->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);

		// Place mesh flush at rest location
		if (!InRestLocation.IsNearlyZero())
		{
			if (!bIsServer)
			{
				const FVector CurrentLoc = TargetPrimitive->GetComponentLocation();
				const float DistToRest = FVector::Dist(CurrentLoc, InRestLocation);

				if (DistToRest > 0.5f && DistToRest < 150.0f)
				{
					// Seamless visual slide to rest location: no snap!
					ClientReconciliationOffset = CurrentLoc - InRestLocation;
					TargetPrimitive->SetWorldLocationAndRotation(InRestLocation, InRestRotation, false, nullptr, ETeleportType::None);
					SetComponentTickEnabled(true);
				}
				else
				{
					ClientReconciliationOffset = FVector::ZeroVector;
					TargetPrimitive->SetWorldLocationAndRotation(InRestLocation, InRestRotation, false, nullptr, ETeleportType::TeleportPhysics);
					SetComponentTickEnabled(false);
				}
			}
			else
			{
				ClientReconciliationOffset = FVector::ZeroVector;
				TargetPrimitive->SetWorldLocationAndRotation(InRestLocation, InRestRotation, false, nullptr, ETeleportType::TeleportPhysics);
				SetComponentTickEnabled(false);
			}
		}
		else
		{
			SetComponentTickEnabled(false);
		}
	}
	else
	{
		SetComponentTickEnabled(false);
	}

	// 3. Ensure Owner location and collision, clear Held and InMotion states
	if (AActor* OwnerActor = GetOwner())
	{
		if (!InRestLocation.IsNearlyZero())
		{
			OwnerActor->SetActorLocationAndRotation(InRestLocation, InRestRotation, false, nullptr, ETeleportType::None);
		}
		OwnerActor->SetActorEnableCollision(true);

		if (AActivePropActor* PropActor = Cast<AActivePropActor>(OwnerActor))
		{
			PropActor->SetInMotion(false);
			PropActor->SetHeld(false);
			PropActor->State = EPropState::AtRest;
			PropActor->bIsHeld = false;
			PropActor->bIsInMotion = false;
			PropActor->SetGrabbable(true);
			PropActor->InteractionState = EPropInteractionLockState::Free;
			PropActor->InteractionOwner = nullptr;
		}

		// Re-enable any PhysicsSimComponent that was suspended during rocket flight and put it at rest
		APropManager* PM = APropManager::Get(OwnerActor);
		for (UActorComponent* Comp : OwnerActor->GetComponents())
		{
			if (Comp && Comp->GetClass()->GetName().Contains(TEXT("PhysicsSim")))
			{
				if (UStaticMeshPhysicsSimulationComponent* SimComp = Cast<UStaticMeshPhysicsSimulationComponent>(Comp))
				{
					SimComp->ClearUnsettledTimer();
					SimComp->SetHeld(false);
					SimComp->SetInMotion(false);
					SimComp->SetStateAtRest();
					if (PM)
					{
						PM->UnregisterSettlingComponent(SimComp);
					}
				}
				Comp->SetComponentTickEnabled(true);
			}
		}
	}

	// 4. Restore original settings (mobility, collision profile/responses/enabled, replicate movement)
	// Ensure physics simulation remains false at rest so the bottle sticks to the ground firmly
	bSavedSimulatePhysics = false;
	bSavedEnableGravity = false;
	RestorePrimitiveSettings();

	LogDebug(FString::Printf(TEXT("Multicast_SettleRocket: Locked at Loc: %s, Rot: %s"),
		*InRestLocation.ToString(), *InRestRotation.ToString()), true);

	OnRocketSettled.Broadcast();
}

void URocketProjectileComponent::Multicast_StartSpentFalling_Implementation(
	FVector_NetQuantize FallingStartLocation,
	FVector_NetQuantize FallingStartVelocity,
	int32 InTargetBounces,
	bool bInGliding,
	FVector_NetQuantize InGlidingTarget)
{
	const bool bIsServer = GetOwner() ? GetOwner()->HasAuthority() : false;
	if (bIsServer || AuthoritativeFlightNodes.Num() >= 2)
	{
		return;
	}

	AutoResolveTargetPrimitive();

	// Smoothly reconcile starting location without visual popping:
	const FVector CurrentCenter = GetWorldCenterLocation();
	const float Dist = FVector::Dist(CurrentCenter, FallingStartLocation);

	if (Dist > 1.0f && Dist < 250.0f)
	{
		// Smoothly absorb any small flight endpoint difference:
		ClientReconciliationOffset = CurrentCenter - FallingStartLocation;
		SetTargetTransformAroundCenter(FallingStartLocation, TargetPrimitive ? TargetPrimitive->GetComponentRotation() : FRotator::ZeroRotator);
	}
	else if (Dist >= 250.0f)
	{
		ClientReconciliationOffset = FVector::ZeroVector;
		SetTargetTransformAroundCenter(FallingStartLocation, TargetPrimitive ? TargetPrimitive->GetComponentRotation() : FRotator::ZeroRotator);
	}

	CurrentVelocity = FallingStartVelocity;
	TargetFallingBounces = InTargetBounces;
	bIsGlidingToReachablePoint = bInGliding;
	GlidingTargetLocation = InGlidingTarget;
	FallingBounceCount = 0;
	TimeSpentFalling = 0.0f;
	ClientCatchupSpeedMultiplier = 1.0f;
	bTargetRestRotationInitialized = false;

	if (CurrentState != ERocketSimulationState::SpentFalling)
	{
		const ERocketSimulationState PrevState = CurrentState;
		CurrentState = ERocketSimulationState::SpentFalling;
		OnRocketStateChanged.Broadcast(CurrentState, PrevState);
		OnRocketFuelDepleted.Broadcast();
	}

	if (TargetPrimitive)
	{
		TargetPrimitive->SetMobility(EComponentMobility::Movable);
		TargetPrimitive->SetSimulatePhysics(false);
		TargetPrimitive->SetEnableGravity(false);
		TargetPrimitive->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
		TargetPrimitive->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		TargetPrimitive->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		TargetPrimitive->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		TargetPrimitive->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		TargetPrimitive->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	}

	SetActive(true);
	SetComponentTickEnabled(true);

	LogDebug(FString::Printf(TEXT("Multicast_StartSpentFalling: Client synced start. StartLoc=%s (Dist=%.1f), StartVel=%s, TargetBounces=%d, Gliding=%d"),
		*FallingStartLocation.ToString(), Dist, *FallingStartVelocity.ToString(), TargetFallingBounces, bInGliding ? 1 : 0), true);
}

void URocketProjectileComponent::Multicast_FallingBounce_Implementation(
	int32 InBounceCount,
	FVector_NetQuantize InBounceLocation,
	FVector_NetQuantize InBounceVelocity)
{
	const bool bIsServer = GetOwner() ? GetOwner()->HasAuthority() : false;
	if (bIsServer || AuthoritativeFlightNodes.Num() >= 2)
	{
		return;
	}

	AutoResolveTargetPrimitive();

	const FVector CurrentCenter = GetWorldCenterLocation();
	const float Dist = FVector::Dist(CurrentCenter, InBounceLocation);

	if (Dist > 1.0f && Dist < 250.0f)
	{
		// Smoothly absorb difference so bounce visually transitions without a teleport snap:
		ClientReconciliationOffset = CurrentCenter - InBounceLocation;
		SetTargetTransformAroundCenter(InBounceLocation, TargetPrimitive ? TargetPrimitive->GetComponentRotation() : FRotator::ZeroRotator);
	}
	else if (Dist >= 250.0f)
	{
		ClientReconciliationOffset = FVector::ZeroVector;
		SetTargetTransformAroundCenter(InBounceLocation, TargetPrimitive ? TargetPrimitive->GetComponentRotation() : FRotator::ZeroRotator);
	}

	CurrentVelocity = InBounceVelocity;
	FallingBounceCount = InBounceCount;
	TimeSinceLastBounce = 0.0f;
	bIsGlidingToReachablePoint = false;

	LogDebug(FString::Printf(TEXT("Multicast_FallingBounce: Client synced Bounce %d/%d at %s (Dist=%.1f), Vel=%s"),
		FallingBounceCount, TargetFallingBounces, *InBounceLocation.ToString(), Dist, *InBounceVelocity.ToString()), true);
}

float URocketProjectileComponent::GetNormalizedRemainingFuel() const
{
	const float EffectiveFuelDist = FlightSettings.MaxFuelDistance > 0.0f ? FlightSettings.MaxFuelDistance : MaxFuelDistance;
	if (EffectiveFuelDist <= 0.0f)
	{
		return 0.0f;
	}
	return FMath::Clamp(1.0f - (TotalFlightDistanceTraveled / EffectiveFuelDist), 0.0f, 1.0f);
}

void URocketProjectileComponent::SetSimulationState(ERocketSimulationState NewState)
{
	if (CurrentState == NewState)
	{
		return;
	}

	const ERocketSimulationState PrevState = CurrentState;
	CurrentState = NewState;

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		FlightNetState.State = CurrentState;
	}

	LogDebug(FString::Printf(TEXT("SetSimulationState: %s -> %s (ElapsedFlight: %.2f s, TotalAir: %.2f s)"),
		*UEnum::GetValueAsString(PrevState), *UEnum::GetValueAsString(NewState), ElapsedFlightTime, TotalAirTime));

	OnRocketStateChanged.Broadcast(NewState, PrevState);

	if (NewState == ERocketSimulationState::SpentFalling)
	{
		if (AuthoritativeFlightNodes.Num() < 2)
		{
			InitializeSpentFalling();
		}
		OnRocketFuelDepleted.Broadcast();
	}
	else if (NewState == ERocketSimulationState::AtRest)
	{
		SetComponentTickEnabled(false);
		if (GetOwner() && GetOwner()->HasAuthority() && GetOwner()->GetIsReplicated())
		{
			GetOwner()->SetReplicateMovement(true);
		}
		OnRocketSettled.Broadcast();
	}
}

void URocketProjectileComponent::PerformSimulationStep(float DeltaTime)
{
	switch (CurrentState)
	{
	case ERocketSimulationState::TippingOver:
		ProcessTippingStep(DeltaTime);
		break;
	case ERocketSimulationState::Dragging:
		ProcessDraggingStep(DeltaTime);
		break;
	case ERocketSimulationState::Rocketing:
		ProcessRocketStep(DeltaTime);
		break;
	case ERocketSimulationState::LowFuelSputter:
		ProcessRocketStep(DeltaTime);
		break;
	case ERocketSimulationState::SpentFalling:
		if (AuthoritativeFlightNodes.Num() >= 2)
		{
			ProcessRocketStep(DeltaTime);
		}
		else
		{
			ProcessSpentFallingStep(DeltaTime);
		}
		break;
	default:
		break;
	}
}

void URocketProjectileComponent::ProcessTippingStep(float DeltaTime)
{
	TippingElapsedTime += DeltaTime;
	TotalAirTime += DeltaTime;

	const float Alpha = FMath::Clamp(TippingElapsedTime / TotalTippingDuration, 0.0f, 1.0f);
	const float SmoothAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 1.5f);

	// Slerp rotation from upright to flat on floor (Images 1 to 3)
	const FQuat StartQuat = TippingStartRotation.Quaternion();
	const FQuat TargetQuat = TippingTargetRotation.Quaternion();
	const FQuat CurrentQuat = FQuat::Slerp(StartQuat, TargetQuat, SmoothAlpha);

	// Precession spiral around purple pivot point (Images 1 to 2):
	// As thruster vents, add subtle spiral around pivot as the bottle drops down
	FVector PrecessionOffset = FVector::ZeroVector;
	if (TippingSettings.PrecessionSpiralRadius > 0.01f)
	{
		const FVector TangentU = FVector::CrossProduct(TippingFloorNormal, TippingFloorHeading).GetSafeNormal();
		const FVector TangentV = TippingFloorHeading;
		const float PrecessionAngle = TippingElapsedTime * TippingSettings.PrecessionWobbleFrequency * TWO_PI;
		const float PrecessionRadius = TippingSettings.PrecessionSpiralRadius * (1.0f - SmoothAlpha);
		PrecessionOffset = (TangentU * FMath::Cos(PrecessionAngle) + TangentV * FMath::Sin(PrecessionAngle)) * PrecessionRadius;
	}

	// Current center position
	FVector CurrentCenter = FMath::Lerp(TippingStartCenter, TippingTargetCenter, SmoothAlpha) + PrecessionOffset;

	// Ensure height does not penetrate the floor
	float BottleRadius = 4.0f;
	if (TargetPrimitive)
	{
		const FBoxSphereBounds Bounds = TargetPrimitive->GetLocalBounds();
		const FVector Extent = Bounds.BoxExtent * TargetPrimitive->GetComponentScale().GetAbs();
		BottleRadius = FMath::Max(FMath::Max(Extent.X, Extent.Y), 3.0f);
	}
	const float MinAllowedZ = TippingPivotPoint.Z + (TippingFloorNormal.Z * BottleRadius);
	if (CurrentCenter.Z < MinAllowedZ)
	{
		CurrentCenter.Z = MinAllowedZ;
	}

	SetTargetTransformAroundCenter(CurrentCenter, CurrentQuat.Rotator());

	// Continuous velocity tracking along floor heading
	CurrentVelocity = TippingFloorHeading * (FlightSettings.BaseSpeed * 0.12f * SmoothAlpha);

	// Transition to State 2 (Dragging) when fully flat on the floor (Image 3)
	if (Alpha >= 1.0f)
	{
		LogDebug(TEXT("Tipping Complete: Bottle is fully flat on the floor. Beginning Dragging stage!"));
		InitializeDragging(GetSynchronizedServerTime(), LaunchSeed);
		SetSimulationState(ERocketSimulationState::Dragging);
	}
}

void URocketProjectileComponent::ProcessDraggingStep(float DeltaTime)
{
	DragElapsedTime += DeltaTime;
	TotalAirTime += DeltaTime;

	const float DragAlpha = FMath::Clamp(DragElapsedTime / TotalDragDuration, 0.0f, 1.0f);

	// Slowly increase thrust power to catch up to the target takeoff point (Images 4 & 5)
	const float ThrustAlpha = FMath::Pow(DragAlpha, DraggingSettings.ThrustRampExponent);
	const float CurrentAccel = FMath::Lerp(DraggingSettings.InitialDragAcceleration, DraggingSettings.MaxDragAcceleration, ThrustAlpha);
	DragCurrentSpeed += CurrentAccel * DeltaTime;

	const FVector CurrentCenter = GetWorldCenterLocation();

	// Steer towards Blue Circle point in space (where flying state starts)
	const FVector DirToTakeoff = (DragTakeoffPoint - CurrentCenter).GetSafeNormal();
	const FVector DragHeading = FMath::Lerp(TippingFloorHeading, DirToTakeoff, DragAlpha * 0.85f).GetSafeNormal();

	CurrentVelocity = DragHeading * DragCurrentSpeed;

	// Check floor underneath: does the floor support the bottle, or did it run off the shelf edge? (Image 4 vs 5)
	FHitResult FloorHit;
	FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(RocketDragFloorCheck), false, GetOwner());
	const FVector TraceDown = CurrentCenter - (FVector::UpVector * (SweepRadius + 25.0f));
	const bool bHasFloor = GetWorld() && GetWorld()->LineTraceSingleByChannel(FloorHit, CurrentCenter, TraceDown, ECC_WorldStatic, FloorParams);

	if (bHasFloor)
	{
		// Image 4: Dragging on the floor
		// Constrain velocity along the floor plane to prevent dipping through surface
		CurrentVelocity = CurrentVelocity - (CurrentVelocity | FloorHit.Normal) * FloorHit.Normal;

		// Apply floor sliding friction
		CurrentVelocity *= (1.0f - FMath::Clamp(DraggingSettings.GroundFriction * DeltaTime, 0.0f, 0.5f));
	}
	else
	{
		// Image 5: Bottle ran off the edge of the shelf/table with no floor underneath!
		// Gravity pulls it down while forward thrust continues steering towards the takeoff point
		CurrentVelocity.Z += FlightSettings.GravityZ * DraggingSettings.GravityScaleWithoutFloor * DeltaTime;
	}

	// Move bottle with continuous physics sweep
	const FVector MoveDelta = CurrentVelocity * DeltaTime;
	FHitResult SweepHit;
	if (SweepAndMove(MoveDelta, SweepHit))
	{
		HandleRicochet(SweepHit);
	}

	// Smoothly align rotation to velocity heading with zero snapping
	if (FlightSettings.bAlignRotationToVelocity && TargetPrimitive && !CurrentVelocity.IsNearlyZero())
	{
		const FRotator TargetRot = ComputeTargetRotation(CurrentVelocity.GetSafeNormal(), DeltaTime);
		const FRotator SmoothRot = FMath::RInterpTo(TargetPrimitive->GetComponentRotation(), TargetRot, DeltaTime, 18.0f);
		SetTargetTransformAroundCenter(GetWorldCenterLocation(), SmoothRot);
	}

	// Transition to State 3: Rocketing / Flying when drag completes or bottle reaches takeoff waypoint
	const bool bDurationExpired = DragElapsedTime >= TotalDragDuration;
	const bool bReachedTakeoff = (DragAlpha >= 0.90f) && (FVector::DistSquared(GetWorldCenterLocation(), DragTakeoffPoint) < 900.0f);

	if (bDurationExpired || bReachedTakeoff)
	{
		const FVector LiftoffCenter = GetWorldCenterLocation();
		TransitionToRocketing(LiftoffCenter);
	}
}

static void ComputeNormalizedCatmullRomTangents(
	const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3,
	float Smoothness, FVector& OutT1, FVector& OutT2)
{
	const float Chord01 = FMath::Max(FVector::Dist(P0, P1), 1.0f);
	const float Chord12 = FMath::Max(FVector::Dist(P1, P2), 1.0f);
	const float Chord23 = FMath::Max(FVector::Dist(P2, P3), 1.0f);

	const FVector D01 = (P1 - P0) / Chord01;
	const FVector D12 = (P2 - P1) / Chord12;
	const FVector D23 = (P3 - P2) / Chord23;

	const float Tension = FMath::Lerp(0.15f, 0.85f, FMath::Clamp(Smoothness, 0.0f, 1.0f));

	const FVector TangentDir1 = (D01 + D12).GetSafeNormal();
	const FVector TangentDir2 = (D12 + D23).GetSafeNormal();

	OutT1 = TangentDir1 * (Chord12 * Tension);
	OutT2 = TangentDir2 * (Chord12 * Tension);
}

static FVector EvaluateCatmullRom(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, float t, float Smoothness = 0.65f)
{
	FVector T1, T2;
	ComputeNormalizedCatmullRomTangents(P0, P1, P2, P3, Smoothness, T1, T2);
	return FMath::CubicInterp(P1, T1, P2, T2, t);
}

static FVector EvaluateCatmullRomTangent(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, float t, float Smoothness = 0.65f)
{
	FVector T1, T2;
	ComputeNormalizedCatmullRomTangents(P0, P1, P2, P3, Smoothness, T1, T2);
	return FMath::CubicInterpDerivative(P1, T1, P2, T2, t);
}

void URocketProjectileComponent::ProcessRocketStep(float DeltaTime)
{
	ElapsedFlightTime += DeltaTime;
	TotalAirTime += DeltaTime;

	// Guarantee prop actor remains in motion and cannot be demoted to ISM while active rocket simulation is running!
	if (AActivePropActor* PropActor = Cast<AActivePropActor>(GetOwner()))
	{
		PropActor->bIsInMotion = true;
		PropActor->bIsHeld = false;
		if (PropActor->State != EPropState::InMotion)
		{
			PropActor->State = EPropState::InMotion;
		}
	}

	// Check if unified pre-calculated 3-part flight plan is active
	if (AuthoritativeFlightNodes.Num() >= 2)
	{
		// 1. Check if we already reached the final destination node
		if (CurrentFlightPlanNodeIndex >= AuthoritativeFlightNodes.Num() - 1)
		{
			const FVector FinalRestLoc = AuthoritativeFlightNodes.Last();
			const FRotator FinalRestRot = DeterministicRestRotation;
			SetTargetTransformAroundCenter(FinalRestLoc, FinalRestRot);
			if (!GetOwner() || GetOwner()->HasAuthority())
			{
				const FVector FinalPivotLoc = TargetPrimitive ? TargetPrimitive->GetComponentLocation() : FinalRestLoc;
				Multicast_SettleRocket(FinalPivotLoc, FinalRestRot);
			}
			return;
		}

		const int32 CurrentIdx = CurrentFlightPlanNodeIndex;
		const int32 NextIdx = CurrentFlightPlanNodeIndex + 1;
		const FVector StartNode = AuthoritativeFlightNodes[CurrentIdx];
		const FVector TargetNode = AuthoritativeFlightNodes[NextIdx];
		const FVector CurrentCenter = GetWorldCenterLocation();
		const float SegmentLength = FMath::Max(FVector::Dist(StartNode, TargetNode), 1.0f);

		// 2. Determine target speed by stage
		float TargetSpeed = FlightSettings.BaseSpeed;

		if (CurrentIdx < RocketingEndIndex)
		{
			// Region 1 (Pink Box): Rocketing State
			if (CurrentState != ERocketSimulationState::Rocketing)
			{
				SetSimulationState(ERocketSimulationState::Rocketing);
			}

			const float RocketingTotalDist = CumulativeFlightPlanDistances.IsValidIndex(RocketingEndIndex) ? CumulativeFlightPlanDistances[RocketingEndIndex] : (FlightSettings.BaseSplineLength > 0.0f ? FlightSettings.BaseSplineLength : 5000.0f);
			const float CurrentDist = (CumulativeFlightPlanDistances.IsValidIndex(CurrentIdx) ? CumulativeFlightPlanDistances[CurrentIdx] : 0.0f) + CurrentSegmentDistTraveled;
			const float NormFuel = FMath::Clamp(CurrentDist / FMath::Max(RocketingTotalDist, 1.0f), 0.0f, 1.0f);

			if (FuelThrustCurve)
			{
				TargetSpeed *= FuelThrustCurve->GetFloatValue(NormFuel);
			}
			else
			{
				// In the final 35% of distance: fuel is running low, movement slows down smoothly towards low-fuel transition
				if (NormFuel >= 0.65f)
				{
					const float EndAlpha = (NormFuel - 0.65f) / 0.35f;
					const float SpeedDecay = FMath::Lerp(1.0f, 0.40f, FMath::SmoothStep(0.0f, 1.0f, EndAlpha));
					TargetSpeed *= SpeedDecay;
				}
			}
		}
		else if (CurrentIdx >= RocketingEndIndex && CurrentIdx < LowFuelIndex)
		{
			// Region 2 (Red Box): Low Fuel Upward Climb (Two forces: forward thrust degrades, gravity increases)
			if (CurrentState != ERocketSimulationState::LowFuelSputter)
			{
				SetSimulationState(ERocketSimulationState::LowFuelSputter);
			}

			// Starts with residual speed from end of rocketing (~900 cm/s) and bleeds smoothly towards apex
			const float V_start = FMath::Max(FlightSettings.BaseSpeed * 0.40f, 900.0f);
			const float V_apex = 280.0f;

			const float Region2StartDist = CumulativeFlightPlanDistances.IsValidIndex(RocketingEndIndex) ? CumulativeFlightPlanDistances[RocketingEndIndex] : 0.0f;
			const float Region2EndDist = CumulativeFlightPlanDistances.IsValidIndex(LowFuelIndex) ? CumulativeFlightPlanDistances[LowFuelIndex] : (Region2StartDist + SegmentLength);
			const float Region2TotalDist = FMath::Max(Region2EndDist - Region2StartDist, 1.0f);
			const float CurrentClimbDist = ((CumulativeFlightPlanDistances.IsValidIndex(CurrentIdx) ? CumulativeFlightPlanDistances[CurrentIdx] : Region2StartDist) - Region2StartDist) + CurrentSegmentDistTraveled;
			const float ClimbAlpha = FMath::Clamp(CurrentClimbDist / Region2TotalDist, 0.0f, 1.0f);

			// Forward thrust degrades smoothly while gravity decelerates the bottle towards apex
			TargetSpeed = FMath::Lerp(V_start, V_apex, FMath::Pow(ClimbAlpha, 1.2f));
		}
		else if (CurrentIdx == LowFuelIndex)
		{
			// Region 3 (Brown Box): Spent Falling Drop (Gravitational acceleration from apex / obstacle hit)
			if (CurrentState != ERocketSimulationState::SpentFalling)
			{
				SetSimulationState(ERocketSimulationState::SpentFalling);
				OnRocketFuelDepleted.Broadcast();
				FallingBounceCount = 0;
				TimeSpentFalling = 0.0f;
			}

			TimeSpentFalling += DeltaTime;

			const float ApexHang = bLowFuelHitObstacle ? 0.0f : (FallingSettings.ApexHangDuration > 0.0f ? FallingSettings.ApexHangDuration : 0.08f);
			const float V_terminal = FMath::Clamp(FallingSettings.TerminalVelocity > 0.0f ? FallingSettings.TerminalVelocity : 2800.0f, 1800.0f, 3500.0f);
			const float FallAccel = FallingSettings.FallAcceleration > 0.0f ? FallingSettings.FallAcceleration : 3800.0f;

			if (TimeSpentFalling <= ApexHang && ApexHang > 0.0f)
			{
				// Brief time frame to show apex stall / zero-g crest (gentle rollover from 280 to 420 cm/s)
				const float HangAlpha = FMath::Clamp(TimeSpentFalling / ApexHang, 0.0f, 1.0f);
				TargetSpeed = FMath::Lerp(280.0f, 420.0f, FMath::SmoothStep(0.0f, 1.0f, HangAlpha));
			}
			else
			{
				// Rapidly add falling velocity downward under gravity!
				const float ActiveFallTime = TimeSpentFalling - ApexHang;
				const float GravitySpeed = 420.0f + (FallAccel * ActiveFallTime);
				TargetSpeed = FMath::Min(GravitySpeed, V_terminal);
			}
		}
		else
		{
			// Region 4 (Black Box): Ground Bounces (Parabolic ascent & descent)
			if (CurrentState != ERocketSimulationState::SpentFalling)
			{
				SetSimulationState(ERocketSimulationState::SpentFalling);
			}

			const int32 RelativeIdx = CurrentIdx - FirstGroundHitIndex;
			const float BounceAlpha = FMath::Clamp(CurrentSegmentDistTraveled / SegmentLength, 0.0f, 1.0f);

			if (RelativeIdx % 2 == 0)
			{
				// Ascent Leg: Rebounds from ground and decelerates until speed is almost zero at apex
				if (CurrentBounceReboundSpeed < 200.0f)
				{
					CurrentBounceReboundSpeed = FMath::Max(FlightSettings.BaseSpeed * 0.40f, 500.0f);
				}
				TargetSpeed = FMath::Lerp(CurrentBounceReboundSpeed, 20.0f, FMath::Sqrt(BounceAlpha));
			}
			else
			{
				// Descent Leg: From apex (near-zero speed) accelerates downward to next impact
				const float ExpectedImpactSpeed = FMath::Max(CurrentBounceReboundSpeed * 0.95f, 250.0f);
				TargetSpeed = FMath::Lerp(20.0f, ExpectedImpactSpeed, FMath::Sqrt(BounceAlpha));
			}
		}

		// 3. Step forward along trajectory
		const float StepDist = TargetSpeed * DeltaTime;
		CurrentSegmentDistTraveled += StepDist;
		TotalFlightDistanceTraveled += StepDist;
		const float SegmentAlpha = FMath::Clamp(CurrentSegmentDistTraveled / SegmentLength, 0.0f, 1.0f);

		// 4. Compute 3D position and rotation according to each region
		FVector NewCenter = CurrentCenter;
		FRotator TargetRot = TargetPrimitive ? TargetPrimitive->GetComponentRotation() : FRotator::ZeroRotator;

		if (CurrentIdx < RocketingEndIndex)
		{
			// Region 1 (Pink Box): Catmull-Rom spline curving smoothly through seeded red offset points and blue waypoints
			const FVector P1 = StartNode;
			const FVector P2 = TargetNode;
			const FVector P0 = (CurrentIdx > 0) ? AuthoritativeFlightNodes[CurrentIdx - 1] : (P1 - (P2 - P1));
			const FVector P3 = AuthoritativeFlightNodes.IsValidIndex(CurrentIdx + 2) ? AuthoritativeFlightNodes[CurrentIdx + 2] : (P2 + (P2 - P1));

			const FVector SplinePos = EvaluateCatmullRom(P0, P1, P2, P3, SegmentAlpha, FlightSettings.SplineCornerSmoothness);
			FVector SplineTangent = EvaluateCatmullRomTangent(P0, P1, P2, P3, SegmentAlpha, FlightSettings.SplineCornerSmoothness).GetSafeNormal();
			if (SplineTangent.IsNearlyZero())
			{
				SplineTangent = (P2 - P1).GetSafeNormal();
			}

			NewCenter = SplinePos;

			if (FlightSettings.bAlignRotationToVelocity)
			{
				TargetRot = ComputeTargetRotation(SplineTangent, DeltaTime);
			}
		}
		else if (CurrentIdx >= RocketingEndIndex && CurrentIdx < LowFuelIndex)
		{
			// Region 2 (Red Box): Low Fuel Upward Climb to Apex
			const FVector P1 = StartNode;
			const FVector P2 = TargetNode;
			const FVector P0 = AuthoritativeFlightNodes.IsValidIndex(CurrentIdx - 1) ? AuthoritativeFlightNodes[CurrentIdx - 1] : (P1 - (P2 - P1));
			const FVector P3 = AuthoritativeFlightNodes.IsValidIndex(CurrentIdx + 2) ? AuthoritativeFlightNodes[CurrentIdx + 2] : (P2 + (P2 - P1));

			const FVector SplinePos = EvaluateCatmullRom(P0, P1, P2, P3, SegmentAlpha, FlightSettings.SplineCornerSmoothness);
			FVector SplineTangent = EvaluateCatmullRomTangent(P0, P1, P2, P3, SegmentAlpha, FlightSettings.SplineCornerSmoothness).GetSafeNormal();
			if (SplineTangent.IsNearlyZero())
			{
				SplineTangent = (P2 - P1).GetSafeNormal();
			}
			NewCenter = SplinePos;

			// Continuously check Z position / vertical velocity to determine ascending vs descending
			const float VerticalVelocity = (NewCenter.Z - CurrentCenter.Z) / FMath::Max(DeltaTime, 0.0001f);
			const float UpThreshold = LowFuelSettings.MaintainUpDirectionThreshold > 0.0f ? LowFuelSettings.MaintainUpDirectionThreshold : 50.0f;

			if (VerticalVelocity >= 0.0f)
			{
				// Ascending: Make thrust point straight DOWN, pushing the bottle UP against gravity!
				// Smoothly pitch upright from rocketing flight into steep UpVector over first 35% of climb
				const float PitchUpAlpha = FMath::SmoothStep(0.0f, 1.0f, FMath::Min(SegmentAlpha * 3.0f, 1.0f));
				const FVector ThrustDownPushUpDir = FMath::Lerp(SplineTangent, FVector::UpVector, PitchUpAlpha).GetSafeNormal();
				TargetRot = ComputeTargetRotation(ThrustDownPushUpDir, DeltaTime);
			}
			else
			{
				// Descending / cresting (Z begins to go negative):
				const float DownwardSpeed = -VerticalVelocity;
				if (DownwardSpeed < UpThreshold)
				{
					// Downward speed < 50 cm/s: maintain bottle facing UP (thrust pointing down)
					TargetRot = ComputeTargetRotation(FVector::UpVector, DeltaTime);
				}
				else
				{
					// Downward speed >= 50 cm/s: tip over and orient to downward velocity
					const float TipAlpha = FMath::Clamp((DownwardSpeed - UpThreshold) / 100.0f, 0.0f, 1.0f);
					const FVector TipDir = FMath::Lerp(FVector::UpVector, SplineTangent, FMath::SmoothStep(0.0f, 1.0f, TipAlpha)).GetSafeNormal();
					TargetRot = ComputeTargetRotation(TipDir, DeltaTime);
				}
			}

			// Add intense dying fizz sputter flutter while thrusting down
			const float SputterFrequency = 36.0f;
			const float SputterMag = LowFuelSettings.WobbleMultiplier * 3.0f;
			TargetRot.Pitch += FMath::Sin(ElapsedFlightTime * SputterFrequency) * SputterMag;
			TargetRot.Roll += FMath::Cos(ElapsedFlightTime * (SputterFrequency * 1.25f)) * (SputterMag * 1.2f);
		}
		else if (CurrentIdx == LowFuelIndex)
		{
			// Region 3 (Brown Box): Gravitational Falling Drop from apex
			const FVector P1 = StartNode;
			const FVector P2 = TargetNode;
			const FVector P0 = AuthoritativeFlightNodes.IsValidIndex(CurrentIdx - 1) ? AuthoritativeFlightNodes[CurrentIdx - 1] : (P1 - (P2 - P1));
			const FVector P3 = AuthoritativeFlightNodes.IsValidIndex(CurrentIdx + 2) ? AuthoritativeFlightNodes[CurrentIdx + 2] : (P2 + (P2 - P1));

			const FVector SplinePos = EvaluateCatmullRom(P0, P1, P2, P3, SegmentAlpha, FlightSettings.SplineCornerSmoothness);
			FVector SplineTangent = EvaluateCatmullRomTangent(P0, P1, P2, P3, SegmentAlpha, FlightSettings.SplineCornerSmoothness).GetSafeNormal();
			if (SplineTangent.IsNearlyZero())
			{
				SplineTangent = (P2 - P1).GetSafeNormal();
			}
			NewCenter = SplinePos;

			// Immediate post-apex hang: monitor downward velocity and smoothly blend into falling tumble
			const float VerticalVelocity = (NewCenter.Z - CurrentCenter.Z) / FMath::Max(DeltaTime, 0.0001f);
			const float DownwardSpeed = -VerticalVelocity;
			const float UpThreshold = LowFuelSettings.MaintainUpDirectionThreshold > 0.0f ? LowFuelSettings.MaintainUpDirectionThreshold : 50.0f;

			if (DownwardSpeed < UpThreshold)
			{
				if (bLowFuelHitObstacle)
				{
					// Rebound away from obstacle surface immediately
					const FVector ReboundOrient = (LowFuelHitNormal * 0.7f + SplineTangent * 0.3f).GetSafeNormal();
					TargetRot = ComputeTargetRotation(ReboundOrient, DeltaTime);
				}
				else
				{
					// Near-zero downward speed right at apex: maintain UpVector (thrust pointing down)
					TargetRot = ComputeTargetRotation(FVector::UpVector, DeltaTime);
				}
			}
			else
			{
				// Downward speed >= UpThreshold: smoothly transition from UpVector into downward velocity + tumble
				FallingTumbleAngle += FallingSettings.TumbleRate * DeltaTime;

				// Blend factor from 0.0 (at 50 cm/s) to 1.0 (at 180 cm/s) for silky-smooth rotation handoff
				const float FallBlend = FMath::Clamp((DownwardSpeed - UpThreshold) / 130.0f, 0.0f, 1.0f);
				const float SmoothBlend = FMath::SmoothStep(0.0f, 1.0f, FallBlend);

				const FVector FallOrientDir = FMath::Lerp(FVector::UpVector, SplineTangent, SmoothBlend).GetSafeNormal();
				const FRotator BaseOrientRot = ComputeTargetRotation(FallOrientDir, DeltaTime);

				const float DistToGround = FVector::Dist(NewCenter, P2);
				if (DistToGround < FallingSettings.GroundInterpDistance && FallingSettings.GroundInterpDistance > 0.0f)
				{
					const float GroundAlpha = 1.0f - FMath::Clamp(DistToGround / FallingSettings.GroundInterpDistance, 0.0f, 1.0f);
					const FRotator TumbleRot = BaseOrientRot + FRotator(FMath::Sin(FMath::DegreesToRadians(FallingTumbleAngle)) * (25.0f * SmoothBlend), 0.0f, 0.0f);
					TargetRot = FMath::Lerp(TumbleRot, DeterministicRestRotation, FMath::SmoothStep(0.0f, 1.0f, GroundAlpha));
				}
				else
				{
					const float TumblePitch = FMath::Sin(FMath::DegreesToRadians(FallingTumbleAngle)) * (35.0f * SmoothBlend);
					const float TumbleRoll = FallingTumbleAngle * SmoothBlend;
					TargetRot = BaseOrientRot + FRotator(TumblePitch, 0.0f, TumbleRoll);
				}
			}
		}
		else
		{
			// Region 4 (Black Box): Parabolic Bounces
			const int32 RelativeIdx = CurrentIdx - FirstGroundHitIndex;
			FVector Ground1 = FVector::ZeroVector;
			FVector Apex = FVector::ZeroVector;
			FVector Ground2 = FVector::ZeroVector;
			float ParabolicT = 0.0f;

			if (RelativeIdx % 2 == 0)
			{
				// Ascent Leg (Ground -> Apex)
				Ground1 = StartNode;
				Apex = TargetNode;
				Ground2 = AuthoritativeFlightNodes.IsValidIndex(CurrentIdx + 2) ? AuthoritativeFlightNodes[CurrentIdx + 2] : Ground1;
				ParabolicT = 0.5f * SegmentAlpha;
			}
			else
			{
				// Descent Leg (Apex -> Ground)
				Ground1 = AuthoritativeFlightNodes.IsValidIndex(CurrentIdx - 1) ? AuthoritativeFlightNodes[CurrentIdx - 1] : StartNode;
				Apex = StartNode;
				Ground2 = TargetNode;
				ParabolicT = 0.5f + (0.5f * SegmentAlpha);
			}

			const FVector ApexControl = (2.0f * Apex) - (0.5f * (Ground1 + Ground2));
			const float OneMinusT = 1.0f - ParabolicT;
			const FVector ParabolicPos = (OneMinusT * OneMinusT * Ground1) + (2.0f * OneMinusT * ParabolicT * ApexControl) + (ParabolicT * ParabolicT * Ground2);
			FVector ParabolicTangent = (2.0f * OneMinusT * (ApexControl - Ground1) + 2.0f * ParabolicT * (Ground2 - ApexControl)).GetSafeNormal();
			if (ParabolicTangent.IsNearlyZero())
			{
				ParabolicTangent = (TargetNode - StartNode).GetSafeNormal();
			}

			NewCenter = ParabolicPos;
			const float TotalBounceProgress = static_cast<float>(CurrentIdx - FirstGroundHitIndex) / FMath::Max(1.0f, static_cast<float>(AuthoritativeFlightNodes.Num() - 1 - FirstGroundHitIndex));
			TargetRot = FMath::Lerp(ComputeTargetRotation(ParabolicTangent, DeltaTime), DeterministicRestRotation, FMath::Clamp(TotalBounceProgress, 0.0f, 1.0f));
		}

		// 5. Update velocity and transform
		CurrentVelocity = (NewCenter - CurrentCenter) / FMath::Max(DeltaTime, 0.0001f);
		SetTargetTransformAroundCenter(NewCenter, TargetRot);

		// 6. Check segment completion
		if (CurrentSegmentDistTraveled >= SegmentLength)
		{
			CurrentSegmentDistTraveled = FMath::Max(0.0f, CurrentSegmentDistTraveled - SegmentLength);
			CurrentFlightPlanNodeIndex++;

			// If completing LowFuel climb (arrived at apex / obstacle hit point):
			if (CurrentFlightPlanNodeIndex == LowFuelIndex)
			{
				if (bLowFuelHitObstacle)
				{
					// Hit obstacle during low fuel upward climb!
					FHitResult ObstacleHit;
					ObstacleHit.ImpactPoint = TargetNode;
					ObstacleHit.ImpactNormal = LowFuelHitNormal;
					ObstacleHit.Normal = LowFuelHitNormal;

					const FVector InboundVel = CurrentVelocity;
					const FVector ReboundVel = (InboundVel - 2.0f * (InboundVel | LowFuelHitNormal) * LowFuelHitNormal) * 0.40f;
					CurrentVelocity = ReboundVel;

					OnRocketRicochet.Broadcast(ObstacleHit, ReboundVel);
					LogDebug(FString::Printf(TEXT("ProcessRocketStep: LowFuel struck obstacle! Normal=%s, ReboundSpeed=%.1f"),
						*LowFuelHitNormal.ToString(), ReboundVel.Size()), true);
				}
			}

			// Ricochet events on ground impacts
			if (CurrentFlightPlanNodeIndex == FirstGroundHitIndex)
			{
				LastImpactSpeed = TargetSpeed;
				const float Restitution = FMath::Clamp(FallingSettings.SpentBounciness > 0.0f ? FallingSettings.SpentBounciness : 0.45f, 0.25f, 0.65f);
				CurrentBounceReboundSpeed = LastImpactSpeed * Restitution;
				CurrentBounceCount = 1;

				FHitResult SyntheticHit;
				SyntheticHit.ImpactPoint = TargetNode;
				SyntheticHit.ImpactNormal = FVector::UpVector;
				OnRocketRicochet.Broadcast(SyntheticHit, CurrentVelocity);
			}
			else if (CurrentFlightPlanNodeIndex > FirstGroundHitIndex)
			{
				const int32 CompletedRelativeIdx = (CurrentFlightPlanNodeIndex - 1) - FirstGroundHitIndex;
				if (CompletedRelativeIdx % 2 == 1)
				{
					LastImpactSpeed = TargetSpeed;
					const float Restitution = FMath::Clamp(FallingSettings.SpentBounciness > 0.0f ? FallingSettings.SpentBounciness : 0.45f, 0.25f, 0.65f);
					CurrentBounceReboundSpeed = LastImpactSpeed * Restitution;
					CurrentBounceCount++;

					FHitResult SyntheticHit;
					SyntheticHit.ImpactPoint = TargetNode;
					SyntheticHit.ImpactNormal = FVector::UpVector;
					OnRocketRicochet.Broadcast(SyntheticHit, CurrentVelocity);
				}
			}

			// Check if full flight plan completed
			if (CurrentFlightPlanNodeIndex >= AuthoritativeFlightNodes.Num() - 1)
			{
				const FVector FinalRestLoc = AuthoritativeFlightNodes.Last();
				const FRotator FinalRestRot = DeterministicRestRotation;
				SetTargetTransformAroundCenter(FinalRestLoc, FinalRestRot);
				if (!GetOwner() || GetOwner()->HasAuthority())
				{
					const FVector FinalPivotLoc = TargetPrimitive ? TargetPrimitive->GetComponentLocation() : FinalRestLoc;
					Multicast_SettleRocket(FinalPivotLoc, FinalRestRot);
				}
				return;
			}
		}

		// Travel history logging
		if (bEnableDebugLogs || bShowOnScreenDebug)
		{
			TravelHistoryLogTimer += DeltaTime;
			if (TravelHistoryLogTimer >= 0.25f)
			{
				TravelHistoryLogTimer = 0.0f;
				const FString StageName = (CurrentIdx < RocketingEndIndex) ? TEXT("Rocketing") :
					(CurrentIdx >= RocketingEndIndex && CurrentIdx < LowFuelIndex) ? TEXT("LowFuelThrust") :
					(CurrentIdx == LowFuelIndex) ? TEXT("FallingDrop") :
					((CurrentIdx - FirstGroundHitIndex) % 2 == 0 ? TEXT("BounceAscent") : TEXT("BounceDescent"));

				LogDebug(FString::Printf(TEXT("TravelHistory: Air=%.2fs, Stage=%s, Node=%d/%d, Loc=%s, Spd=%.0f, Alpha=%.2f, Dist=%.0f cm, Catchup=%.2fx"),
					TotalAirTime, *StageName, CurrentFlightPlanNodeIndex, AuthoritativeFlightNodes.Num(), *NewCenter.ToString(), CurrentVelocity.Size(), SegmentAlpha, TotalFlightDistanceTraveled, ClientCatchupSpeedMultiplier));
			}
		}

		return;
	}

	const float EffectiveFuelDist = FlightSettings.MaxFuelDistance > 0.0f ? FlightSettings.MaxFuelDistance : MaxFuelDistance;

	// Check fuel expiration (governed by distance, with MaxFuelDuration safety fallback)
	if (TotalFlightDistanceTraveled >= EffectiveFuelDist || ElapsedFlightTime >= MaxFuelDuration)
	{
		LogDebug(FString::Printf(TEXT("Fuel Expired! Distance: %.1f / %.1f cm (AirTime: %.2fs). Falling..."),
			TotalFlightDistanceTraveled, EffectiveFuelDist, TotalAirTime), true);

		// Immediate synchronized fuel cutoff on both server and client at exact same sub-step
		if (FallingSettings.bUsePhysicsSimulation || FallingSettings.bHandoffToPhysicsOnDepleted || bHandoffToPhysicsOnDepleted)
		{
			if (!GetOwner() || GetOwner()->HasAuthority())
			{
				HandoffToPhysicsSimulation();
			}
			else
			{
				ApplyPhysicsHandoff(CurrentVelocity, FVector(0.0f, 180.0f, 90.0f));
			}
			return;
		}

		bTargetRestRotationInitialized = false;
		FallingTumbleAngle = 0.0f;
		TimeSpentFalling = 0.0f;
		SetSimulationState(ERocketSimulationState::SpentFalling);
		return;
	}

	// Evaluate fuel curve based on distance ratio
	float SpeedMultiplier = 1.0f;
	if (FuelThrustCurve)
	{
		const float NormalizedFuel = FMath::Clamp(TotalFlightDistanceTraveled / FMath::Max(EffectiveFuelDist, 1.0f), 0.0f, 1.0f);
		SpeedMultiplier = FuelThrustCurve->GetFloatValue(NormalizedFuel);
	}

	// Client dead-reckoning & catch-up: match server position along the authoritative flight plan
	if (GetOwner() && !GetOwner()->HasAuthority() && AuthoritativeFlightNodes.Num() >= 2 && LastReplicatedServerLaunchTime > 0.0f)
	{
		const float ClientServerTime = GetSynchronizedServerTime();
		const float PreFlightDuration = TotalTippingDuration + TotalDragDuration;
		const float ServerElapsedFlight = FMath::Max(0.0f, ClientServerTime - LastReplicatedServerLaunchTime - PreFlightDuration);

		const float ExpectedServerDist = FMath::Min(ServerElapsedFlight * FlightSettings.BaseSpeed, EffectiveFuelDist);
		const float ClientTraveledDist = TotalFlightDistanceTraveled;

		const float DistDelta = ExpectedServerDist - ClientTraveledDist;
		if (DistDelta > 15.0f)
		{
			ClientCatchupSpeedMultiplier = FMath::Clamp(1.0f + (DistDelta / 350.0f), 1.05f, 1.40f);
		}
		else if (DistDelta < -15.0f)
		{
			ClientCatchupSpeedMultiplier = FMath::Clamp(1.0f + (DistDelta / 350.0f), 0.85f, 0.98f);
		}
		else
		{
			ClientCatchupSpeedMultiplier = 1.0f;
		}
		SpeedMultiplier *= ClientCatchupSpeedMultiplier;
	}

	const float TargetSpeed = FlightSettings.BaseSpeed * SpeedMultiplier;
	const float StepDistance = TargetSpeed * DeltaTime;

	// Maintain forward momentum with slight gravity drop
	FVector CurrentHeading = CurrentVelocity.GetSafeNormal();
	if (CurrentHeading.IsNearlyZero())
	{
		CurrentHeading = TipOverLaunchHeading;
	}

	const float RemainingFuelDist = EffectiveFuelDist - TotalFlightDistanceTraveled;
	const float SputterDistThreshold = FMath::Min(LowFuelSettings.LowFuelPhaseDuration * FlightSettings.BaseSpeed, EffectiveFuelDist * 0.35f);

	if (LowFuelSettings.bEnableLowFuelSputter && RemainingFuelDist <= SputterDistThreshold && RemainingFuelDist > 0.0f)
	{
		// State 4: Low Fuel / Sputter (almost empty thrust upward struggle)
		if (CurrentState != ERocketSimulationState::LowFuelSputter)
		{
			SetSimulationState(ERocketSimulationState::LowFuelSputter);
		}

		TotalFlightDistanceTraveled += StepDistance;
		FlightDistanceTraveled += StepDistance;

		const float ExhaustAlpha = 1.0f - FMath::Clamp(RemainingFuelDist / FMath::Max(SputterDistThreshold, 1.0f), 0.0f, 1.0f);

		// 1. Nose tilts upward into an arc as thrust angles up
		FVector HorizHeading = FVector(CurrentHeading.X, CurrentHeading.Y, 0.0f).GetSafeNormal();
		if (HorizHeading.IsNearlyZero())
		{
			HorizHeading = FVector::ForwardVector;
		}
		const float PitchRad = FMath::DegreesToRadians(FMath::Lerp(0.0f, LowFuelSettings.PitchUpAngle, FMath::Sin(ExhaustAlpha * PI * 0.5f)));
		const FVector TargetExhaustHeading = (HorizHeading * FMath::Cos(PitchRad) + FVector::UpVector * FMath::Sin(PitchRad)).GetSafeNormal();
		CurrentHeading = FMath::VInterpNormalRotationTo(CurrentHeading, TargetExhaustHeading, DeltaTime, 4.0f);

		// 2. Fading thrust power vs full gravity: starts strong enough to climb, then rapidly weakens
		const float FadingPower = FMath::Square(1.0f - ExhaustAlpha);
		const float UpwardThrustZ = LowFuelSettings.UpwardThrustImpulse * FadingPower;
		const float EffectiveGravityScale = FMath::Lerp(FlightSettings.InFlightGravityScale, 1.0f, ExhaustAlpha);

		// Gravity pulls down while dying pressure vents upward
		CurrentVelocity.Z += (FlightSettings.GravityZ * EffectiveGravityScale + UpwardThrustZ) * DeltaTime;

		// Forward speed bleeds off as power dies
		const float SpeedDamping = FMath::Lerp(1.0f, 0.20f, ExhaustAlpha);
		const float SputterSpeed = TargetSpeed * SpeedDamping;

		// Re-project horizontal velocity along tilted heading while preserving vertical momentum
		const FVector HorizVelocity = FVector(CurrentHeading.X, CurrentHeading.Y, 0.0f).GetSafeNormal() * (SputterSpeed * FMath::Cos(PitchRad));
		CurrentVelocity.X = HorizVelocity.X;
		CurrentVelocity.Y = HorizVelocity.Y;
		CurrentVelocity.Z = FMath::Max(CurrentVelocity.Z, SputterSpeed * FMath::Sin(PitchRad) * FadingPower + CurrentVelocity.Z * 0.5f);

		const FVector MoveDelta = CurrentVelocity * DeltaTime;
		FHitResult Hit;
		if (SweepAndMove(MoveDelta, Hit))
		{
			HandleRicochet(Hit);
		}
		else if (FlightSettings.bAlignRotationToVelocity && TargetPrimitive && !CurrentVelocity.IsNearlyZero())
		{
			const FRotator TargetRot = ComputeTargetRotation(CurrentVelocity.GetSafeNormal(), DeltaTime);
			SetTargetTransformAroundCenter(GetWorldCenterLocation(), TargetRot);
		}
	}
	else
	{
		// State 3: Rocketing / Flying
		// Check if erratic waypoint spline path is active (Image 1)
		if (FlightSettings.bEnableErraticPath && FlightWaypoints.Num() >= 2)
		{
			FlightDistanceTraveled += StepDistance;
			TotalFlightDistanceTraveled += StepDistance;
			const float NormalizedProgress = FlightBaselineLength > 0.0f ? (FlightDistanceTraveled / FlightBaselineLength) : 1.0f;

			FVector NextSplineLoc;
			FVector NextSplineTangent;
			EvaluateFlightSpline(NormalizedProgress, NextSplineLoc, NextSplineTangent);

			const FVector CurrentCenter = GetWorldCenterLocation();
			FVector MoveDelta = NextSplineLoc - CurrentCenter;

			CurrentVelocity = MoveDelta / FMath::Max(DeltaTime, 0.0001f);

			// Continuous travel history logging every 0.25s
			if (bEnableDebugLogs || bShowOnScreenDebug)
			{
				TravelHistoryLogTimer += DeltaTime;
				if (TravelHistoryLogTimer >= 0.25f)
				{
					TravelHistoryLogTimer = 0.0f;
					LogDebug(FString::Printf(TEXT("TravelHistory: Air=%.2fs, Prog=%.1f%%, Loc=%s, Vel=%s, Spd=%.0f, Leg=%d (Node %d/%d), FuelDist=%.0f/%.0f cm, Catchup=%.2fx"),
						TotalAirTime, NormalizedProgress * 100.0f, *CurrentCenter.ToString(), *CurrentVelocity.ToString(), CurrentVelocity.Size(), FlightLegIndex, CurrentFlightPlanNodeIndex, AuthoritativeFlightNodes.Num(), TotalFlightDistanceTraveled, EffectiveFuelDist, ClientCatchupSpeedMultiplier));
				}
			}

			FHitResult Hit;
			if (SweepAndMove(MoveDelta, Hit))
			{
				HandleRicochet(Hit);
			}
			else if (NormalizedProgress >= 1.0f)
			{
				// Only advance to next flight leg if the full baseline was traveled with no obstacle hit!
				AdvanceToNextFlightLeg();
			}
			else if (FlightSettings.bAlignRotationToVelocity && TargetPrimitive && !NextSplineTangent.IsNearlyZero())
			{
				const FRotator TargetRot = ComputeTargetRotation(NextSplineTangent, DeltaTime);
				SetTargetTransformAroundCenter(GetWorldCenterLocation(), TargetRot);
			}
		}
		else
		{
			// Baseline straight rocketing
			FlightDistanceTraveled += StepDistance;
			TotalFlightDistanceTraveled += StepDistance;
			const float NormalizedProgress = FlightBaselineLength > 0.0f ? (FlightDistanceTraveled / FlightBaselineLength) : 1.0f;

			if (bEnableDebugLogs || bShowOnScreenDebug)
			{
				TravelHistoryLogTimer += DeltaTime;
				if (TravelHistoryLogTimer >= 0.25f)
				{
					TravelHistoryLogTimer = 0.0f;
					const FVector Center = GetWorldCenterLocation();
					LogDebug(FString::Printf(TEXT("TravelHistory: Air=%.2fs, Prog=%.1f%%, Loc=%s, Vel=%s, Spd=%.0f, Leg=%d (Node %d/%d), FuelDist=%.0f/%.0f cm, Catchup=%.2fx"),
						TotalAirTime, NormalizedProgress * 100.0f, *Center.ToString(), *CurrentVelocity.ToString(), CurrentVelocity.Size(), FlightLegIndex, CurrentFlightPlanNodeIndex, AuthoritativeFlightNodes.Num(), TotalFlightDistanceTraveled, EffectiveFuelDist, ClientCatchupSpeedMultiplier));
				}
			}

			CurrentVelocity.Z += FlightSettings.GravityZ * FlightSettings.InFlightGravityScale * DeltaTime;
			CurrentVelocity = CurrentVelocity.GetSafeNormal() * TargetSpeed;

			const FVector MoveDelta = CurrentVelocity * DeltaTime;
			FHitResult Hit;
			if (SweepAndMove(MoveDelta, Hit))
			{
				HandleRicochet(Hit);
			}
			else if (NormalizedProgress >= 1.0f)
			{
				AdvanceToNextFlightLeg();
			}
			else if (FlightSettings.bAlignRotationToVelocity && TargetPrimitive && !CurrentVelocity.IsNearlyZero())
			{
				const FRotator TargetRot = ComputeTargetRotation(CurrentVelocity.GetSafeNormal(), DeltaTime);
				SetTargetTransformAroundCenter(GetWorldCenterLocation(), TargetRot);
			}
		}
	}
}

void URocketProjectileComponent::InitializeSpentFalling()
{
	FallingBounceCount = 0;
	TimeSpentFalling = 0.0f;
	bTargetRestRotationInitialized = false;
	FallingTumbleAngle = 0.0f;

	const FVector CurrentCenter = GetWorldCenterLocation();
	const float FloorTraceDistance = 15000.0f;
	const FVector TraceDown = CurrentCenter - FVector(0.0f, 0.0f, FloorTraceDistance);

	FHitResult FloorHit;
	FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(RocketFallingFloorTrace), false, GetOwner());
	const bool bHitFloor = GetWorld() && GetWorld()->LineTraceSingleByChannel(FloorHit, CurrentCenter, TraceDown, CollisionChannel, FloorParams);

	if (bHitFloor)
	{
		InitialFallHeight = FloorHit.Distance;
		bIsGlidingToReachablePoint = false;
		GlidingTargetLocation = FVector::ZeroVector;

		// Calculate bounces depending on settings and falling height
		if (FallingSettings.bUseFixedFallingBounces)
		{
			TargetFallingBounces = FMath::Clamp(FallingSettings.FixedFallingBounces, 1, 15);
		}
		else
		{
			const int32 MinB = FMath::Min(FallingSettings.MinFallingBounces, FallingSettings.MaxFallingBounces);
			const int32 MaxB = FMath::Max(FallingSettings.MinFallingBounces, FallingSettings.MaxFallingBounces);

			if (InitialFallHeight < 400.0f)
			{
				TargetFallingBounces = MinB;
			}
			else if (InitialFallHeight < 1500.0f)
			{
				TargetFallingBounces = FMath::Clamp(MinB + 1, MinB, MaxB);
			}
			else
			{
				TargetFallingBounces = MaxB;
			}
		}

		LogDebug(FString::Printf(TEXT("InitializeSpentFalling: Floor detected at %.1f cm down (Impact: %s). TargetBounces=%d"),
			InitialFallHeight, *FloorHit.ImpactPoint.ToString(), TargetFallingBounces));
	}
	else
	{
		// Floor not hit within 15,000 cm straight down! Find closest reachable point to glide to
		bool bFoundReachable = false;
		FVector FoundTarget = FVector::ZeroVector;

		if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			FNavLocation ReachableNavLoc;
			if (NavSys->GetRandomReachablePointInRadius(CurrentCenter, 8000.0f, ReachableNavLoc))
			{
				FoundTarget = ReachableNavLoc.Location;
				bFoundReachable = true;
			}
		}

		if (!bFoundReachable && GetWorld())
		{
			float ClosestDistSq = MAX_flt;
			for (int32 AngleDeg = 0; AngleDeg < 360; AngleDeg += 45)
			{
				const float Rad = FMath::DegreesToRadians(static_cast<float>(AngleDeg));
				const FVector TraceDir = FVector(FMath::Cos(Rad) * 0.707f, FMath::Sin(Rad) * 0.707f, -0.707f).GetSafeNormal();
				FHitResult AngledHit;
				if (GetWorld()->LineTraceSingleByChannel(AngledHit, CurrentCenter, CurrentCenter + (TraceDir * FloorTraceDistance), CollisionChannel, FloorParams))
				{
					const float DistSq = FVector::DistSquared(CurrentCenter, AngledHit.ImpactPoint);
					if (DistSq < ClosestDistSq)
					{
						ClosestDistSq = DistSq;
						FoundTarget = AngledHit.ImpactPoint;
						bFoundReachable = true;
					}
				}
			}
		}

		if (bFoundReachable)
		{
			bIsGlidingToReachablePoint = true;
			GlidingTargetLocation = FoundTarget;
			InitialFallHeight = FMath::Max(FVector::Dist(CurrentCenter, FoundTarget), 1500.0f);
			TargetFallingBounces = FallingSettings.bUseFixedFallingBounces
				? FMath::Clamp(FallingSettings.FixedFallingBounces, 1, 15)
				: FMath::Clamp(FallingSettings.MaxFallingBounces - 1, FallingSettings.MinFallingBounces, FallingSettings.MaxFallingBounces);

			LogDebug(FString::Printf(TEXT("InitializeSpentFalling: No floor below within 15,000 cm! Gliding to reachable point at %s (Dist=%.1f cm, TargetBounces=%d)"),
				*GlidingTargetLocation.ToString(), InitialFallHeight, TargetFallingBounces));
		}
		else
		{
			bIsGlidingToReachablePoint = false;
			GlidingTargetLocation = FVector::ZeroVector;
			InitialFallHeight = FloorTraceDistance;
			TargetFallingBounces = FallingSettings.bUseFixedFallingBounces
				? FMath::Clamp(FallingSettings.FixedFallingBounces, 1, 15)
				: FallingSettings.MaxFallingBounces;

			LogDebug(FString::Printf(TEXT("InitializeSpentFalling: No floor or reachable point found within 15,000 cm. Natural descent with %d bounces."), TargetFallingBounces));
		}
	}

	ClientCatchupSpeedMultiplier = 1.0f;

	const bool bIsServer = GetOwner() ? GetOwner()->HasAuthority() : false;
	if (bIsServer)
	{
		FlightNetState.State = ERocketSimulationState::SpentFalling;
		FlightNetState.LastBounceLocation = CurrentCenter;
		FlightNetState.LastBounceVelocity = CurrentVelocity;
		FlightNetState.RestLocation = GlidingTargetLocation;
		FlightNetState.BounceCount = static_cast<uint8>(TargetFallingBounces & 0xFF);

		Multicast_StartSpentFalling(CurrentCenter, CurrentVelocity, TargetFallingBounces, bIsGlidingToReachablePoint, GlidingTargetLocation);
	}
}

void URocketProjectileComponent::ProcessSpentFallingStep(float DeltaTime)
{
	// Option 1: Full Chaos physics simulation handoff (Image 2 - option 1)
	if (FallingSettings.bUsePhysicsSimulation)
	{
		if (!GetOwner() || GetOwner()->HasAuthority())
		{
			HandoffToPhysicsSimulation();
		}
		else
		{
			ApplyPhysicsHandoff(CurrentVelocity, FVector(0.0f, 180.0f, 90.0f));
		}
		return;
	}

	// Option 2: Kinematic ballistic projectile trace with ground proximity landing interpolation (Image 2 - option 2)
	TotalAirTime += DeltaTime;
	TimeSpentFalling += DeltaTime;

	const FVector CurrentCenter = GetWorldCenterLocation();

	if (bIsGlidingToReachablePoint && !GlidingTargetLocation.IsNearlyZero())
	{
		const FVector ToTarget = GlidingTargetLocation - CurrentCenter;
		const FVector ToTarget2D = FVector(ToTarget.X, ToTarget.Y, 0.0f);
		const float Dist2D = ToTarget2D.Size();

		if (Dist2D > 60.0f)
		{
			const FVector DesiredVel2D = ToTarget2D.GetSafeNormal() * 650.0f;
			CurrentVelocity.X = FMath::FInterpTo(CurrentVelocity.X, DesiredVel2D.X, DeltaTime, 3.5f);
			CurrentVelocity.Y = FMath::FInterpTo(CurrentVelocity.Y, DesiredVel2D.Y, DeltaTime, 3.5f);
		}
		else
		{
			CurrentVelocity.X *= FMath::Clamp(1.0f - (2.0f * DeltaTime), 0.0f, 1.0f);
			CurrentVelocity.Y *= FMath::Clamp(1.0f - (2.0f * DeltaTime), 0.0f, 1.0f);
		}

		// Smooth, controlled glide descent
		const float TargetDescentSpeed = -450.0f;
		CurrentVelocity.Z = FMath::FInterpTo(CurrentVelocity.Z, TargetDescentSpeed, DeltaTime, 2.5f);

		// Align bottle smoothly with glide heading
		if (TargetPrimitive && !CurrentVelocity.IsNearlyZero())
		{
			const FRotator GlideRot = FRotationMatrix::MakeFromX(CurrentVelocity.GetSafeNormal()).Rotator();
			const FRotator CurrentRot = TargetPrimitive->GetComponentRotation();
			const FRotator SmoothRot = FMath::RInterpTo(CurrentRot, GlideRot, DeltaTime, 6.0f);
			SetTargetTransformAroundCenter(CurrentCenter, SmoothRot);
		}

		// Check if ground is now directly below within 1500 cm
		FHitResult RecheckHit;
		FCollisionQueryParams RecheckParams(SCENE_QUERY_STAT(RocketGlideGroundCheck), false, GetOwner());
		if (GetWorld() && GetWorld()->LineTraceSingleByChannel(RecheckHit, CurrentCenter, CurrentCenter - FVector(0.0f, 0.0f, 1500.0f), CollisionChannel, RecheckParams))
		{
			bIsGlidingToReachablePoint = false;
		}
	}
	else
	{
		// Ballistic drop under gravity
		CurrentVelocity.Z += FlightSettings.GravityZ * FallingSettings.SpentGravityScale * DeltaTime;

		// Air drag
		CurrentVelocity *= FMath::Clamp(1.0f - (0.4f * DeltaTime), 0.0f, 1.0f);

		// Line trace downward to measure height above the ground
		FHitResult GroundCheckHit;
		FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(RocketFallingGroundCheck), false, GetOwner());
		const float CheckDistance = FMath::Max(FallingSettings.GroundInterpDistance * 2.5f, 800.0f);
		const FVector TraceDown = CurrentCenter - FVector(0.0f, 0.0f, CheckDistance);
		const bool bFoundFloor = GetWorld() && GetWorld()->LineTraceSingleByChannel(GroundCheckHit, CurrentCenter, TraceDown, CollisionChannel, GroundParams);

		if (bFoundFloor)
		{
			if (!bTargetRestRotationInitialized)
			{
				const float SeedYaw = static_cast<float>((LaunchSeed * 67 + CurrentBounceCount * 13) % 360);
				DeterministicRestRotation = ComputeGroundRestRotation(GroundCheckHit.Normal, SeedYaw);
				bTargetRestRotationInitialized = true;
			}

			const float DistToGround = GroundCheckHit.Distance;
			// Only smoothly align to rest rotation on the final bounce when near ground
			if (FallingBounceCount >= (TargetFallingBounces - 1) && DistToGround <= FallingSettings.GroundInterpDistance && TargetPrimitive)
			{
				const float InterpAlpha = 1.0f - FMath::Clamp(DistToGround / FallingSettings.GroundInterpDistance, 0.0f, 1.0f);
				const FRotator CurrentRot = TargetPrimitive->GetComponentRotation();
				const FRotator SmoothRot = FMath::RInterpTo(CurrentRot, DeterministicRestRotation, DeltaTime, 6.0f + (InterpAlpha * 14.0f));
				SetTargetTransformAroundCenter(CurrentCenter, SmoothRot);
			}
			else if (TargetPrimitive && !CurrentVelocity.IsNearlyZero())
			{
				FallingTumbleAngle += FallingSettings.TumbleRate * DeltaTime;
				const FRotator TumbleDelta(FallingSettings.TumbleRate * DeltaTime, FallingSettings.TumbleRate * 1.33f * DeltaTime, 0.0f);
				const FRotator NewRot = (FRotationMatrix(TumbleDelta) * FRotationMatrix(TargetPrimitive->GetComponentRotation())).Rotator();
				SetTargetTransformAroundCenter(CurrentCenter, NewRot);
			}
		}
		else if (TargetPrimitive && !CurrentVelocity.IsNearlyZero())
		{
			FallingTumbleAngle += FallingSettings.TumbleRate * DeltaTime;
			const FRotator TumbleDelta(FallingSettings.TumbleRate * DeltaTime, FallingSettings.TumbleRate * 1.33f * DeltaTime, 0.0f);
			const FRotator NewRot = (FRotationMatrix(TumbleDelta) * FRotationMatrix(TargetPrimitive->GetComponentRotation())).Rotator();
			SetTargetTransformAroundCenter(CurrentCenter, NewRot);
		}
	}

	// Move bottle with continuous physics sweep
	const FVector MoveDelta = CurrentVelocity * DeltaTime;
	FHitResult Hit;
	if (SweepAndMove(MoveDelta, Hit))
	{
		HandleRicochet(Hit);
	}

	// Safety fallback timeout: only after 30.0s of continuous falling (failsafe for void drops)
	if (TimeSpentFalling >= 30.0f)
	{
		FHitResult FloorHit;
		FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(RocketFloorTrace), false, GetOwner());
		const FVector CenterLoc = GetWorldCenterLocation();
		const FVector TraceDownFallback = CenterLoc - FVector(0.0f, 0.0f, 15000.0f);
		if (GetWorld() && GetWorld()->LineTraceSingleByChannel(FloorHit, CenterLoc, TraceDownFallback, CollisionChannel, FloorParams))
		{
			const float SeedYaw = static_cast<float>((LaunchSeed * 67) % 360);
			DeterministicRestRotation = ComputeGroundRestRotation(FloorHit.Normal, SeedYaw);
			bTargetRestRotationInitialized = true;

			const float GroundExtent = GetProjectedExtentAlongDirectionWithRotation(FloorHit.Normal, DeterministicRestRotation);
			const FVector RestCenter = FloorHit.ImpactPoint + (FloorHit.Normal * GroundExtent);
			SetTargetTransformAroundCenter(RestCenter, DeterministicRestRotation);

			if (!GetOwner() || GetOwner()->HasAuthority())
			{
				const FVector FinalLoc = TargetPrimitive ? TargetPrimitive->GetComponentLocation() : RestCenter;
				Multicast_SettleRocket(FinalLoc, DeterministicRestRotation);
			}
			else
			{
				SetSimulationState(ERocketSimulationState::AtRest);
			}
		}
	}
}

bool URocketProjectileComponent::SweepAndMove(const FVector& MoveDelta, const FRotator& InRotation, FHitResult& OutHit)
{
	if (!TargetPrimitive)
	{
		return false;
	}

	const FVector MoveDir = MoveDelta.GetSafeNormal();
	if (MoveDir.IsNearlyZero())
	{
		return false;
	}

	const FVector CurrentCenter = GetWorldCenterLocation();
	const float LeadingExtent = GetProjectedExtentAlongDirectionWithRotation(MoveDir, InRotation);

	// Sweep starts safely at CurrentCenter (which is guaranteed outside collision geometry)
	// and extends forward to encompass the travel distance plus the leading extent of the mesh.
	const float SphereOffset = FMath::Max(0.0f, LeadingExtent - SweepRadius);
	const FVector SweepStart = CurrentCenter;
	const FVector SweepEnd = CurrentCenter + MoveDelta + (MoveDir * SphereOffset);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RocketSweep), false, GetOwner());
	FCollisionShape SphereShape = FCollisionShape::MakeSphere(SweepRadius);

	const bool bHit = GetWorld()->SweepSingleByChannel(
		OutHit,
		SweepStart,
		SweepEnd,
		InRotation.Quaternion(),
		CollisionChannel,
		SphereShape,
		QueryParams
	);

	if (bHit)
	{
		if (OutHit.bStartPenetrating)
		{
			// Immediate graceful depenetration if sweep initiated inside geometry
			const FVector DepenDir = OutHit.Normal.IsNearlyZero() ? -MoveDir : OutHit.Normal;
			const float ExtentAlongNormal = (CurrentState == ERocketSimulationState::SpentFalling)
				? FMath::Max(SweepRadius, 4.0f)
				: GetProjectedExtentAlongDirectionWithRotation(DepenDir, InRotation);
			const FVector DepenCenter = CurrentCenter + (DepenDir * (OutHit.PenetrationDepth + ExtentAlongNormal + 1.5f));
			SetTargetTransformAroundCenter(DepenCenter, InRotation);
			return true;
		}

		// Calculate safe non-penetrating center: push center back out along surface normal by the mesh extent along that normal
		const float ExtentAlongNormal = (CurrentState == ERocketSimulationState::SpentFalling)
			? FMath::Max(SweepRadius, 4.0f)
			: GetProjectedExtentAlongDirectionWithRotation(OutHit.Normal, InRotation);
		const FVector SafeCenter = OutHit.ImpactPoint + (OutHit.Normal * (ExtentAlongNormal + 1.5f));

		SetTargetTransformAroundCenter(SafeCenter, InRotation);
		return true;
	}
	else
	{
		const FVector EndCenter = CurrentCenter + MoveDelta;
		SetTargetTransformAroundCenter(EndCenter, InRotation);
		return false;
	}
}

bool URocketProjectileComponent::SweepAndMove(const FVector& MoveDelta, FHitResult& OutHit)
{
	const FRotator CurrentRot = TargetPrimitive ? TargetPrimitive->GetComponentRotation() : FRotator::ZeroRotator;
	return SweepAndMove(MoveDelta, CurrentRot, OutHit);
}

void URocketProjectileComponent::HandleRicochet(const FHitResult& Hit)
{
	const bool bSameComponent = (Hit.GetComponent() && Hit.GetComponent() == LastHitComponent.Get());
	const bool bSimilarNormal = (!LastHitNormal.IsNearlyZero()) && ((Hit.Normal | LastHitNormal) > 0.65f);

	// DEBOUNCE FILTER: Ignore rapid micro-collisions on the exact same surface within debounce window
	const float DebounceTime = FallingSettings.RicochetDebounceTime > 0.0f ? FallingSettings.RicochetDebounceTime : RicochetDebounceTime;
	if (bSameComponent && bSimilarNormal && TimeSinceLastBounce < DebounceTime)
	{
		// Safely adjust standoff distance so it doesn't stay stuck/penetrating
		const float ExtentAlongNormal = (CurrentState == ERocketSimulationState::SpentFalling)
			? FMath::Max(SweepRadius, 4.0f)
			: GetProjectedExtentAlongDirection(Hit.Normal);
		const FVector SafeCenter = Hit.ImpactPoint + (Hit.Normal * (ExtentAlongNormal + 1.5f));
		SetTargetTransformAroundCenter(SafeCenter, TargetPrimitive ? TargetPrimitive->GetComponentRotation() : FRotator::ZeroRotator);
		return;
	}

	TimeSinceLastBounce = 0.0f;
	LastHitComponent = Hit.GetComponent();
	LastHitNormal = Hit.Normal;
	CurrentBounceCount++;

	// Notify if hit an actor (player, another bottle)
	if (Hit.GetActor())
	{
		OnRocketHitTarget.Broadcast(Hit.GetActor(), Hit);
	}

	// Standard elastic reflection vector: V_new = V - 2*(V . N)*N
	const FVector Incident = CurrentVelocity;
	FVector Reflected = Incident - 2.0f * (Incident | Hit.Normal) * Hit.Normal;

	// Add chaotic random deflection cone using deterministic seed-based stream so clients match server
	const float DeflectionDeg = (CurrentState == ERocketSimulationState::Rocketing)
		? (FlightSettings.FlightBounceRandomDeflection > 0.0f ? FlightSettings.FlightBounceRandomDeflection : FlightBounceRandomDeflection)
		: FMath::Max(FallingSettings.RicochetRandomDeflection, RicochetRandomDeflection);
	if (DeflectionDeg > 0.0f)
	{
		const float DeflectionRad = FMath::DegreesToRadians(DeflectionDeg);
		const int32 DeflectionSeed = LaunchSeed * 1013 + CurrentBounceCount * 37 + FallingBounceCount * 179;
		FRandomStream BounceDeflectionStream(DeflectionSeed > 0 ? DeflectionSeed : 1);
		Reflected = BounceDeflectionStream.VRandCone(Reflected.GetSafeNormal(), DeflectionRad);
	}

	// ANTI-STICK GUARD: Enforce that the reflected trajectory points decisively away from the wall
	const float NormalComponent = Reflected | Hit.Normal;
	if (NormalComponent < 0.25f)
	{
		Reflected = (Reflected - (NormalComponent * Hit.Normal)) + (Hit.Normal * 0.35f);
		Reflected.Normalize();
	}

	// If reflecting off floor/ground while rocketing, bounce upward into the air
	if (CurrentState == ERocketSimulationState::Rocketing && Hit.Normal.Z > 0.5f && Reflected.Z < 0.15f)
	{
		const int32 UpwardSeed = LaunchSeed * 701 + CurrentBounceCount * 53;
		FRandomStream UpwardStream(UpwardSeed > 0 ? UpwardSeed : 1);
		Reflected.Z = UpwardStream.FRandRange(0.20f, 0.55f);
		Reflected.Normalize();
	}

	// Scale velocity by bounciness
	const float IncidentSpeed = Incident.Size();
	float NewSpeed = IncidentSpeed * Bounciness;

	if (CurrentState == ERocketSimulationState::SpentFalling)
	{
		FallingBounceCount++;
		bIsGlidingToReachablePoint = false; // Physical impact occurred; glide completed

		if (TargetFallingBounces < 3)
		{
			TargetFallingBounces = 3;
		}

		const bool bCanSettle = (FallingBounceCount >= TargetFallingBounces);

		// If minimum bounce count (3 to 5 based on falling height) is fulfilled and surface faces upward: settle flush!
		if (bCanSettle && (IncidentSpeed < AtRestSettings.RestVelocityThreshold * 2.0f || FallingBounceCount >= (TargetFallingBounces + 1)) && Hit.Normal.Z > 0.5f)
		{
			const float SeedYaw = static_cast<float>((LaunchSeed * 67 + CurrentBounceCount * 13) % 360);
			DeterministicRestRotation = ComputeGroundRestRotation(Hit.Normal, SeedYaw);
			bTargetRestRotationInitialized = true;

			// Stick to ground properly using DeterministicRestRotation extent
			const float GroundExtent = GetProjectedExtentAlongDirectionWithRotation(Hit.Normal, DeterministicRestRotation);
			const FVector RestCenter = Hit.ImpactPoint + (Hit.Normal * GroundExtent);
			SetTargetTransformAroundCenter(RestCenter, DeterministicRestRotation);
			CurrentVelocity = FVector::ZeroVector;

			LogDebug(FString::Printf(TEXT("SpentFalling: Completed %d bounces (Target: %d). Settling flush to ground at: %s"),
				FallingBounceCount, TargetFallingBounces, *RestCenter.ToString()));

			if (!GetOwner() || GetOwner()->HasAuthority())
			{
				const FVector FinalLoc = TargetPrimitive ? TargetPrimitive->GetComponentLocation() : RestCenter;
				Multicast_SettleRocket(FinalLoc, DeterministicRestRotation);
			}
			else
			{
				SetSimulationState(ERocketSimulationState::AtRest);
			}
			return;
		}

		// Calculate bounce rebound:
		// Progressive restitution decay across remaining bounces (3 to 5)
		const float RemainingRatio = FMath::Clamp(float(TargetFallingBounces - FallingBounceCount + 1) / float(TargetFallingBounces), 0.15f, 1.0f);
		const float EffectiveRestitution = FMath::Clamp(0.50f * RemainingRatio, 0.18f, 0.55f);
		const float EffectiveFriction = FMath::Clamp(0.65f - (0.25f * (1.0f - RemainingRatio)), 0.35f, 0.70f);

		const FVector NormalVelocity = (Incident | Hit.Normal) * Hit.Normal;
		const FVector TangentVelocity = Incident - NormalVelocity;

		Reflected = (-NormalVelocity * EffectiveRestitution) + (TangentVelocity * EffectiveFriction);

		// Ensure clear upward bounce off floors
		if (Hit.Normal.Z > 0.5f)
		{
			const float MinUpwardVel = 120.0f + (240.0f * RemainingRatio);
			Reflected.Z = FMath::Max(Reflected.Z, MinUpwardVel);
		}

		NewSpeed = Reflected.Size();
		CurrentVelocity = Reflected;

		LogDebug(FString::Printf(TEXT("SpentFalling: Bounce %d/%d (Total Bounces: %d): IncidentSpeed=%.0f, ReboundSpeed=%.0f, UpwardZ=%.0f"),
			FallingBounceCount, TargetFallingBounces, CurrentBounceCount, IncidentSpeed, NewSpeed, CurrentVelocity.Z));
	}
	else if (CurrentState == ERocketSimulationState::Rocketing)
	{
		float CurveMultiplier = 1.0f;
		if (FuelThrustCurve)
		{
			const float EffectiveFuelDist = FlightSettings.MaxFuelDistance > 0.0f ? FlightSettings.MaxFuelDistance : MaxFuelDistance;
			const float NormalizedFuel = FMath::Clamp(TotalFlightDistanceTraveled / FMath::Max(EffectiveFuelDist, 1.0f), 0.0f, 1.0f);
			CurveMultiplier = FuelThrustCurve->GetFloatValue(NormalizedFuel);
		}
		NewSpeed = FMath::Max(NewSpeed, FlightSettings.BaseSpeed * CurveMultiplier * 0.85f);
	}

	CurrentVelocity = Reflected.GetSafeNormal() * NewSpeed;

	// Instantly place bottle at a safe standoff distance off the wall along normal to prevent start-penetration on next tick
	const float ExtentAlongNormal = (CurrentState == ERocketSimulationState::SpentFalling)
		? FMath::Max(SweepRadius, 4.0f)
		: GetProjectedExtentAlongDirection(Hit.Normal);
	const FVector SafeCenter = Hit.ImpactPoint + (Hit.Normal * (ExtentAlongNormal + 1.5f));

	if (CurrentState == ERocketSimulationState::SpentFalling)
	{
		if (!GetOwner() || GetOwner()->HasAuthority())
		{
			Multicast_FallingBounce(FallingBounceCount, SafeCenter, CurrentVelocity);
		}
	}

	// Re-initialize flight spline for the bounce leg!
	if (CurrentState == ERocketSimulationState::Rocketing)
	{
		FlightLegIndex++;
		CurrentFlightPlanNodeIndex++;

		const float EffectiveFuelDist = FlightSettings.MaxFuelDistance > 0.0f ? FlightSettings.MaxFuelDistance : MaxFuelDistance;

		// Check if we completed all flight plan nodes OR if fuel distance is reached OR max flight bounces reached
		if ((AuthoritativeFlightNodes.Num() > 0 && CurrentFlightPlanNodeIndex >= AuthoritativeFlightNodes.Num() - 1)
			|| TotalFlightDistanceTraveled >= EffectiveFuelDist
			|| CurrentBounceCount >= FlightSettings.MaxFlightBounces)
		{
			LogDebug(FString::Printf(TEXT("Flight plan completed on ricochet! Node %d/%d (FuelDist=%.0f/%.0f cm, Bounces=%d/%d). Falling..."),
				CurrentFlightPlanNodeIndex, AuthoritativeFlightNodes.Num(), TotalFlightDistanceTraveled, EffectiveFuelDist,
				CurrentBounceCount, FlightSettings.MaxFlightBounces), true);

			bTargetRestRotationInitialized = false;
			FallingTumbleAngle = 0.0f;
			TimeSpentFalling = 0.0f;
			SetSimulationState(ERocketSimulationState::SpentFalling);
			return;
		}

		FVector NextHeading = CurrentVelocity.GetSafeNormal();
		if (AuthoritativeFlightNodes.IsValidIndex(CurrentFlightPlanNodeIndex + 1))
		{
			const FVector NextNode = AuthoritativeFlightNodes[CurrentFlightPlanNodeIndex + 1];
			NextHeading = (NextNode - SafeCenter).GetSafeNormal();
			CurrentVelocity = NextHeading * NewSpeed;
		}

		InitializeFlightPath(SafeCenter, NextHeading, LaunchSeed + CurrentBounceCount * 31);
	}

	// Re-orient mesh to the new flight direction around center of mass
	if (FlightSettings.bAlignRotationToVelocity && TargetPrimitive && !CurrentVelocity.IsNearlyZero())
	{
		const FRotator TargetRot = ComputeTargetRotation(CurrentVelocity.GetSafeNormal(), 0.016f);
		SetTargetTransformAroundCenter(SafeCenter, TargetRot);
	}
	else
	{
		SetTargetTransformAroundCenter(SafeCenter, TargetPrimitive ? TargetPrimitive->GetComponentRotation() : FRotator::ZeroRotator);
	}

	LogDebug(FString::Printf(TEXT("Ricochet #%d! HitActor=%s, InVel=%s (Speed=%.1f), OutVel=%s (Speed=%.1f), Normal=%s, Seed=%d"),
		CurrentBounceCount, *GetNameSafe(Hit.GetActor()), *Incident.ToString(), IncidentSpeed, *CurrentVelocity.ToString(), NewSpeed, *Hit.Normal.ToString(), LaunchSeed + CurrentBounceCount * 31), true);

	OnRocketRicochet.Broadcast(Hit, CurrentVelocity);

	// Check bounce limit only if explicitly enabled
	if (bLimitFlightByMaxBounces && CurrentBounceCount >= MaxBounces && CurrentState == ERocketSimulationState::Rocketing)
	{
		SetSimulationState(ERocketSimulationState::SpentFalling);
	}
}

FRotator URocketProjectileComponent::ComputeTargetRotation(const FVector& FlightDirection, float DeltaTime) const
{
	if (FlightDirection.IsNearlyZero())
	{
		return TargetPrimitive ? TargetPrimitive->GetComponentRotation() : FRotator::ZeroRotator;
	}

	// Direction opposite to nozzle where thrust pushes
	// Standard bottle: nozzle is at +Z. When thrust shoots out +Z, bottle flies in -Z.
	FVector DesiredForward = FlightDirection;

	// Base rotation aligning forward to flight direction
	FRotator BaseRot = FRotationMatrix::MakeFromX(DesiredForward).Rotator();

	// Local rotational offset depending on which mesh axis represents the nozzle
	FRotator AxisOffset = FRotator::ZeroRotator;
	switch (NozzleAxis)
	{
	case ERocketMeshAxis::PositiveZ:
		AxisOffset = FRotator(-90.0f, 0.0f, 0.0f);
		break;
	case ERocketMeshAxis::NegativeZ:
		AxisOffset = FRotator(90.0f, 0.0f, 0.0f);
		break;
	case ERocketMeshAxis::PositiveX:
		AxisOffset = FRotator(0.0f, 180.0f, 0.0f);
		break;
	case ERocketMeshAxis::NegativeX:
		AxisOffset = FRotator::ZeroRotator;
		break;
	case ERocketMeshAxis::PositiveY:
		AxisOffset = FRotator(0.0f, -90.0f, 0.0f);
		break;
	case ERocketMeshAxis::NegativeY:
		AxisOffset = FRotator(0.0f, 90.0f, 0.0f);
		break;
	}

	FRotator FinalRot = (FRotationMatrix(AxisOffset) * FRotationMatrix(BaseRot)).Rotator();

	// Add cosmetic corkscrew roll / wobble with enhanced flutter during dying exhaust sputter
	float WobbleFreq = CosmeticWobbleFrequency;
	float WobbleDeg = CosmeticWobbleDegrees;

	if (bExhaustSputterNearEnd && MaxFuelDuration > 0.0f)
	{
		const float RemainingFuel = MaxFuelDuration - ElapsedFlightTime;
		const float EffectiveExhaustDuration = FMath::Min(ExhaustPhaseDuration, MaxFuelDuration * 0.4f);
		if (RemainingFuel <= EffectiveExhaustDuration && RemainingFuel > 0.0f)
		{
			const float ExhaustAlpha = 1.0f - FMath::Clamp(RemainingFuel / EffectiveExhaustDuration, 0.0f, 1.0f);
			WobbleFreq *= FMath::Lerp(1.0f, ExhaustWobbleMultiplier, ExhaustAlpha);
			WobbleDeg *= FMath::Lerp(1.0f, ExhaustWobbleMultiplier * 1.5f, FMath::Sin(ExhaustAlpha * PI));
		}
	}

	if (CurrentState != ERocketSimulationState::LowFuelSputter)
	{
		// Wobble dampens directly with velocity during normal flight: slows and reduces to zero as the bottle decelerates
		const float CurrentSpeed = CurrentVelocity.Size();
		const float BaseSpeedRef = FMath::Max(FlightSettings.BaseSpeed * 0.65f, 250.0f);
		const float VelocityWobbleScale = FMath::Clamp(CurrentSpeed / BaseSpeedRef, 0.0f, 1.0f);
		WobbleDeg *= VelocityWobbleScale;
		WobbleFreq *= FMath::Lerp(0.20f, 1.0f, VelocityWobbleScale);
	}
	else
	{
		// During LowFuelSputter, dying fizz causes violent, rapid jitter
		WobbleDeg *= LowFuelSettings.WobbleMultiplier;
		WobbleFreq *= 1.5f;
	}

	if (WobbleFreq > 0.0f && WobbleDeg > 0.0f)
	{
		const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		const float WobblePitch = FMath::Sin(Time * WobbleFreq * 2.0f * PI) * WobbleDeg;
		const float WobbleYaw = FMath::Cos(Time * WobbleFreq * 2.0f * PI) * WobbleDeg;

		FinalRot.Pitch += WobblePitch;
		FinalRot.Yaw += WobbleYaw;
	}

	return FinalRot;
}

FVector URocketProjectileComponent::GetNozzleLocalDirection() const
{
	switch (NozzleAxis)
	{
	case ERocketMeshAxis::PositiveZ: return FVector::UpVector;
	case ERocketMeshAxis::NegativeZ: return FVector::DownVector;
	case ERocketMeshAxis::PositiveX: return FVector::ForwardVector;
	case ERocketMeshAxis::NegativeX: return FVector::BackwardVector;
	case ERocketMeshAxis::PositiveY: return FVector::RightVector;
	case ERocketMeshAxis::NegativeY: return FVector::LeftVector;
	default: return FVector::UpVector;
	}
}

void URocketProjectileComponent::CacheMeshBoundsAndCenter()
{
	if (!TargetPrimitive)
	{
		return;
	}

	const FBoxSphereBounds LocalBounds = TargetPrimitive->GetLocalBounds();
	CalculatedCenterOffset = LocalBounds.Origin;
	MeshBoundsExtent = LocalBounds.BoxExtent;
}

FVector URocketProjectileComponent::GetEffectiveCenterOffset() const
{
	if (!TargetPrimitive)
	{
		return FVector::ZeroVector;
	}

	const FVector Scale = TargetPrimitive->GetComponentScale();

	switch (PivotCompensationMode)
	{
	case ERocketPivotCompensationMode::AutoDetectFromBounds:
		return CalculatedCenterOffset * Scale;
	case ERocketPivotCompensationMode::ManualOffset:
		return ManualCenterOffset * Scale;
	case ERocketPivotCompensationMode::None:
	default:
		return FVector::ZeroVector;
	}
}

FVector URocketProjectileComponent::GetWorldCenterLocation() const
{
	if (!TargetPrimitive)
	{
		return FVector::ZeroVector;
	}

	const FVector LocalOffset = GetEffectiveCenterOffset();
	return TargetPrimitive->GetComponentTransform().TransformPosition(LocalOffset);
}

float URocketProjectileComponent::GetProjectedExtentAlongDirectionWithRotation(const FVector& Direction, const FRotator& InRotation) const
{
	if (!TargetPrimitive)
	{
		return SweepRadius;
	}

	const FVector NormalizedDir = Direction.GetSafeNormal();
	if (NormalizedDir.IsNearlyZero())
	{
		return SweepRadius;
	}

	const FVector Scale = TargetPrimitive->GetComponentScale();
	const FVector ScaledExtent = MeshBoundsExtent * Scale;

	const FVector AxisX = InRotation.RotateVector(FVector::ForwardVector);
	const FVector AxisY = InRotation.RotateVector(FVector::RightVector);
	const FVector AxisZ = InRotation.RotateVector(FVector::UpVector);

	const float Extent =
		FMath::Abs(AxisX | NormalizedDir) * ScaledExtent.X +
		FMath::Abs(AxisY | NormalizedDir) * ScaledExtent.Y +
		FMath::Abs(AxisZ | NormalizedDir) * ScaledExtent.Z;

	return FMath::Max(Extent, SweepRadius);
}

float URocketProjectileComponent::GetProjectedExtentAlongDirection(const FVector& Direction) const
{
	return GetProjectedExtentAlongDirectionWithRotation(Direction, TargetPrimitive ? TargetPrimitive->GetComponentRotation() : FRotator::ZeroRotator);
}

void URocketProjectileComponent::SetTargetTransformAroundCenter(const FVector& NewCenterLocation, const FRotator& NewRotation)
{
	if (!TargetPrimitive)
	{
		return;
	}

	FVector EffectiveCenter = NewCenterLocation;
	if (!ClientReconciliationOffset.IsNearlyZero())
	{
		EffectiveCenter += ClientReconciliationOffset;
	}

	const FVector LocalOffset = GetEffectiveCenterOffset();
	const FVector WorldOffset = NewRotation.RotateVector(LocalOffset);
	FVector NewPivotLocation = EffectiveCenter - WorldOffset;

	// Anti-dip ground penetration clamp during tip-over and ground drag
	if (bPreventGroundPenetrationOnTip && (CurrentState == ERocketSimulationState::TippingOver || CurrentState == ERocketSimulationState::Dragging))
	{
		const float RotatedHalfExtentZ = GetProjectedExtentAlongDirection(FVector::UpVector);
		const float LowestPointZ = EffectiveCenter.Z - RotatedHalfExtentZ;
		if (LowestPointZ < InitialGroundZ)
		{
			const float GroundPenetration = InitialGroundZ - LowestPointZ;
			NewPivotLocation.Z += GroundPenetration;
		}
	}

	TargetPrimitive->SetWorldLocationAndRotation(NewPivotLocation, NewRotation);
}

void URocketProjectileComponent::OnRep_FlightNetState()
{
	AutoResolveTargetPrimitive();

	LogDebug(FString::Printf(TEXT("OnRep_FlightNetState: State=%s, Bounces=%d, PhysSim=%d, FuelMax=%.1f, LaunchTime=%.2f, RestLoc=%s"),
		*UEnum::GetValueAsString(FlightNetState.State), FlightNetState.BounceCount, FlightNetState.bPhysicsSimulating ? 1 : 0,
		FlightNetState.FuelDuration, FlightNetState.ServerLaunchTime, *FlightNetState.RestLocation.ToString()));

	// 1. If server handed off to physics simulation, engage physics handoff on client
	if (FlightNetState.bPhysicsSimulating)
	{
		ApplyPhysicsHandoff(FlightNetState.LastBounceVelocity, FVector(0.0f, 180.0f, 90.0f));
		return;
	}

	// 2. Settle rocket if at rest
	if (FlightNetState.State == ERocketSimulationState::AtRest)
	{
		CurrentVelocity = FVector::ZeroVector;
		FlightNetState.bPhysicsSimulating = false;
		bIsGlidingToReachablePoint = false;
		if (TargetPrimitive)
		{
			TargetPrimitive->SetSimulatePhysics(false);
			TargetPrimitive->SetEnableGravity(false);
			TargetPrimitive->SetPhysicsLinearVelocity(FVector::ZeroVector);
			TargetPrimitive->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			if (!FlightNetState.RestLocation.IsNearlyZero())
			{
				TargetPrimitive->SetWorldLocationAndRotation(FlightNetState.RestLocation, FlightNetState.RestRotation, false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
		if (GetOwner() && !FlightNetState.RestLocation.IsNearlyZero())
		{
			GetOwner()->SetActorLocationAndRotation(FlightNetState.RestLocation, FlightNetState.RestRotation, false, nullptr, ETeleportType::TeleportPhysics);
		}

		if (AActivePropActor* PropActor = Cast<AActivePropActor>(GetOwner()))
		{
			PropActor->SetInMotion(false);
			PropActor->SetHeld(false);
			PropActor->State = EPropState::AtRest;
			PropActor->bIsHeld = false;
			PropActor->bIsInMotion = false;
			PropActor->SetGrabbable(true);
			PropActor->InteractionState = EPropInteractionLockState::Free;
			PropActor->InteractionOwner = nullptr;
		}

		if (GetOwner())
		{
			for (UActorComponent* Comp : GetOwner()->GetComponents())
			{
				if (Comp && Comp->GetClass()->GetName().Contains(TEXT("PhysicsSim")))
				{
					if (UStaticMeshPhysicsSimulationComponent* SimComp = Cast<UStaticMeshPhysicsSimulationComponent>(Comp))
					{
						SimComp->SetHeld(false);
						SimComp->SetInMotion(false);
						SimComp->SetStateAtRest();
					}
					Comp->SetComponentTickEnabled(true);
				}
			}
		}

		SetSimulationState(ERocketSimulationState::AtRest);
		SetComponentTickEnabled(false);
		bSavedSimulatePhysics = false;
		bSavedEnableGravity = false;
		RestorePrimitiveSettings();
		OnRocketSettled.Broadcast();
		return;
	}

	// 3. Fallback Launch Check: if Multicast hasn't executed yet (or for late joiners)
	const bool bIsNewServerLaunch = (FlightNetState.ServerLaunchTime > LastReplicatedServerLaunchTime + 0.001f) && (FlightNetState.ServerLaunchTime > 0.0f);
	if (bIsNewServerLaunch && (FlightNetState.State == ERocketSimulationState::Rocketing || FlightNetState.State == ERocketSimulationState::TippingOver || FlightNetState.State == ERocketSimulationState::Dragging))
	{
		if (LaunchSeed <= 0 && FlightNetState.LaunchSeed != 0)
		{
			LaunchSeed = FlightNetState.LaunchSeed;
		}

		if (!FlightNetState.LaunchTargetHitLocation.IsNearlyZero())
		{
			LaunchTargetHitLocation = FlightNetState.LaunchTargetHitLocation;
			bHasLaunchTargetHit = true;
		}

		ApplyClientLaunch(
			FlightNetState.ServerLaunchTime,
			FlightNetState.LaunchDirection,
			false,
			FlightNetState.LaunchLocation,
			FlightNetState.LaunchSeed,
			FlightNetState.FuelDuration,
			FlightNetState.Speed
		);
		return;
	}

	// 4. Normal state transitions (e.g. TippingOver/Dragging -> Rocketing or Rocketing -> SpentFalling)
	if (FlightNetState.State != CurrentState)
	{
		const ERocketSimulationState PrevState = CurrentState;
		CurrentState = FlightNetState.State;
		OnRocketStateChanged.Broadcast(CurrentState, PrevState);

		if (CurrentState == ERocketSimulationState::Rocketing && (PrevState == ERocketSimulationState::TippingOver || PrevState == ERocketSimulationState::Dragging))
		{
			TransitionToRocketing(GetWorldCenterLocation());
		}
		else if (CurrentState == ERocketSimulationState::Dragging && (PrevState == ERocketSimulationState::TippingOver || PrevState == ERocketSimulationState::AtRest || PrevState == ERocketSimulationState::Idle))
		{
			InitializeDragging(FlightNetState.ServerLaunchTime, LaunchSeed);
		}
		else if (CurrentState == ERocketSimulationState::SpentFalling)
		{
			if (TargetPrimitive)
			{
				TargetPrimitive->SetMobility(EComponentMobility::Movable);
				TargetPrimitive->SetSimulatePhysics(false);
				TargetPrimitive->SetEnableGravity(false);
				TargetPrimitive->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
				TargetPrimitive->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				TargetPrimitive->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
				TargetPrimitive->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
				TargetPrimitive->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
				TargetPrimitive->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
			}

			SetActive(true);
			SetComponentTickEnabled(true);
			ClientCatchupSpeedMultiplier = 1.0f;

			if (AuthoritativeFlightNodes.Num() < 2)
			{
				if (!FlightNetState.LastBounceLocation.IsNearlyZero())
				{
					const FVector CurrentCenter = GetWorldCenterLocation();
					const float Dist = FVector::Dist(CurrentCenter, FlightNetState.LastBounceLocation);
					if (Dist > 1.0f && Dist < 250.0f)
					{
						ClientReconciliationOffset = CurrentCenter - FlightNetState.LastBounceLocation;
						SetTargetTransformAroundCenter(FlightNetState.LastBounceLocation, TargetPrimitive ? TargetPrimitive->GetComponentRotation() : FRotator::ZeroRotator);
					}
					else if (Dist >= 250.0f)
					{
						ClientReconciliationOffset = FVector::ZeroVector;
						SetTargetTransformAroundCenter(FlightNetState.LastBounceLocation, TargetPrimitive ? TargetPrimitive->GetComponentRotation() : FRotator::ZeroRotator);
					}
				}

				if (!FlightNetState.LastBounceVelocity.IsNearlyZero())
				{
					CurrentVelocity = FlightNetState.LastBounceVelocity;
				}
				else if (CurrentVelocity.IsNearlyZero())
				{
					CurrentVelocity = FVector(0.0f, 0.0f, -50.0f);
				}

				if (FlightNetState.BounceCount > 0)
				{
					TargetFallingBounces = FlightNetState.BounceCount;
				}
			}

			if (!FlightNetState.RestLocation.IsNearlyZero())
			{
				bIsGlidingToReachablePoint = true;
				GlidingTargetLocation = FlightNetState.RestLocation;
			}

			OnRocketFuelDepleted.Broadcast();
		}
	}

	// 5. Parameter updates if replicated
	if (FlightNetState.FuelDuration > 0.0f)
	{
		MaxFuelDuration = FlightNetState.FuelDuration;
	}
	if (FlightNetState.Speed > 0.0f)
	{
		BaseSpeed = FlightNetState.Speed;
	}
}

void URocketProjectileComponent::SetFallingBounceCount(int32 InMinBounces, int32 InMaxBounces)
{
	FallingSettings.bUseFixedFallingBounces = false;
	FallingSettings.MinFallingBounces = FMath::Clamp(FMath::Min(InMinBounces, InMaxBounces), 1, 15);
	FallingSettings.MaxFallingBounces = FMath::Clamp(FMath::Max(InMinBounces, InMaxBounces), 1, 15);
}

void URocketProjectileComponent::SetFixedFallingBounces(int32 InFixedBounces)
{
	FallingSettings.bUseFixedFallingBounces = true;
	FallingSettings.FixedFallingBounces = FMath::Clamp(InFixedBounces, 1, 15);
}

void URocketProjectileComponent::SetMaxFlightBounces(int32 InMaxBounces)
{
	FlightSettings.MaxFlightBounces = FMath::Clamp(InMaxBounces, 1, 32);
}

void URocketProjectileComponent::SetSplineTension(float InTension)
{
	FlightSettings.SplineTension = FMath::Clamp(InTension, 0.0f, 2.0f);
}

void URocketProjectileComponent::SetSplineCornerSmoothness(float InSmoothness)
{
	SplineCornerSmoothness = FMath::Clamp(InSmoothness, 0.0f, 1.0f);
	FlightSettings.SplineCornerSmoothness = SplineCornerSmoothness;
}

void URocketProjectileComponent::SetOffsetPointSpacing(float InMinSpacing, float InMaxSpacing)
{
	MinOffsetPointSpacing = FMath::Clamp(InMinSpacing, 100.0f, 2000.0f);
	MaxOffsetPointSpacing = FMath::Clamp(InMaxSpacing, MinOffsetPointSpacing, 3000.0f);
	FlightSettings.MinOffsetPointSpacing = MinOffsetPointSpacing;
	FlightSettings.MaxOffsetPointSpacing = MaxOffsetPointSpacing;
}

void URocketProjectileComponent::SetMaintainUpDirectionThreshold(float InThreshold)
{
	MaintainUpDirectionThreshold = FMath::Clamp(InThreshold, 5.0f, 500.0f);
	LowFuelSettings.MaintainUpDirectionThreshold = MaintainUpDirectionThreshold;
}

void URocketProjectileComponent::ConfigureZigZag(
	ERocketZigZagMode InMode,
	bool bInEnable3D,
	bool bInRandomSpacing,
	float InMinSpacing,
	float InMaxSpacing,
	float InMinLatDev,
	float InMaxLatDev,
	float InMinVertDev,
	float InMaxVertDev)
{
	FlightSettings.bEnableErraticPath = true;
	FlightSettings.ZigZagMode = InMode;
	FlightSettings.bEnable3DZigZag = bInEnable3D;
	FlightSettings.bUseRandomWaypointSpacing = bInRandomSpacing;
	FlightSettings.MinWaypointSpacing = FMath::Clamp(FMath::Min(InMinSpacing, InMaxSpacing), 50.0f, 2000.0f);
	FlightSettings.MaxWaypointSpacing = FMath::Clamp(FMath::Max(InMinSpacing, InMaxSpacing), 50.0f, 3000.0f);
	FlightSettings.MinPathDeviation = FMath::Clamp(FMath::Min(InMinLatDev, InMaxLatDev), 0.0f, 500.0f);
	FlightSettings.MaxPathDeviation = FMath::Clamp(FMath::Max(InMinLatDev, InMaxLatDev), 0.0f, 1000.0f);
	FlightSettings.MinVerticalDeviation = FMath::Clamp(InMinVertDev, -500.0f, 500.0f);
	FlightSettings.MaxVerticalDeviation = FMath::Clamp(InMaxVertDev, 0.0f, 1000.0f);
}
