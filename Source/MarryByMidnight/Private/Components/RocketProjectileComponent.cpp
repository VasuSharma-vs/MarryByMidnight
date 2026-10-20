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
	bShowOnScreenDebug = false;
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
	FlightSettings.MaxFuelDuration = MaxFuelDuration;
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

	FallingSettings.bHandoffToPhysicsOnDepleted = bHandoffToPhysicsOnDepleted;
	FallingSettings.bBounceAfterFuelDepleted = bBounceAfterFuelDepleted;
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

	if (FlightSettings.bEnableErraticPath)
	{
		FRandomStream PathStream(InSeed > 0 ? InSeed : 1);
		const float Spacing = FMath::Clamp(FlightSettings.WaypointSpacing, 100.0f, 800.0f);
		const int32 NumSegments = FMath::FloorToInt(EffectiveBaselineLength / Spacing);

		for (int32 i = 1; i <= NumSegments; ++i)
		{
			const float Dist = i * Spacing;
			const FVector BasePt = FlightStartLocation + (FlightBaselineDir * Dist);

			// Alternating lateral offsets primarily along horizontal PerpendicularU (Yellow circles in Image 1)
			const float SideSign = (i % 2 == 1) ? 1.0f : -1.0f;
			const float DevDist = PathStream.FRandRange(FlightSettings.MinPathDeviation, FlightSettings.MaxPathDeviation);

			// Vertical wobble strictly upwards so it NEVER dives into the floor
			const float VertOffset = PathStream.FRandRange(0.0f, DevDist * 0.30f);

			FVector Waypoint = BasePt + (PerpendicularU * (DevDist * SideSign)) + (FVector::UpVector * VertOffset);

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

	for (int32 i = 0; i < FlightWaypoints.Num(); ++i)
	{
		LogDebug(FString::Printf(TEXT("  SplineWP[%d/%d]: Loc=%s"), i, FlightWaypoints.Num(), *FlightWaypoints[i].ToString()));
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

	// Normalize Progress to [0.0, 1.0]
	float AlphaTotal = Progress;
	if (Progress > 1.0f && FlightBaselineLength > 0.0f)
	{
		AlphaTotal = Progress / FlightBaselineLength;
	}
	AlphaTotal = FMath::Clamp(AlphaTotal, 0.0f, 1.0f);

	const int32 NumSegments = NumPts - 1;
	const float SegmentFloat = AlphaTotal * static_cast<float>(NumSegments);
	const int32 SegIndex = FMath::Clamp(FMath::FloorToInt(SegmentFloat), 0, NumSegments - 1);
	const float LocalAlpha = SegmentFloat - static_cast<float>(SegIndex);

	// Catmull-Rom 4-point cubic Hermite interpolation
	const FVector P0 = FlightWaypoints[FMath::Max(0, SegIndex - 1)];
	const FVector P1 = FlightWaypoints[SegIndex];
	const FVector P2 = FlightWaypoints[SegIndex + 1];
	const FVector P3 = FlightWaypoints[FMath::Min(NumPts - 1, SegIndex + 2)];

	const FVector T1 = 0.5f * (P2 - P0);
	const FVector T2 = 0.5f * (P3 - P1);

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

	const FVector CurrentLoc = GetWorldCenterLocation();

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

	LogDebug(FString::Printf(TEXT("AdvanceToNextFlightLeg #%d (Node %d/%d): Next Dir: %s, Seed=%d"),
		FlightLegIndex, CurrentFlightPlanNodeIndex, AuthoritativeFlightNodes.Num(), *NewFlightDir.ToString(), LaunchSeed + FlightLegIndex * 79));

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
	if (!GetWorld())
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

		// Pre-calculated authoritative multi-bounce flight plan backbone
		if (AuthoritativeFlightNodes.Num() >= 2)
		{
			for (int32 i = 0; i < AuthoritativeFlightNodes.Num() - 1; ++i)
			{
				DrawDebugLine(GetWorld(), AuthoritativeFlightNodes[i], AuthoritativeFlightNodes[i + 1], FColor::Yellow, false, 0.05f, 0, 1.8f);
				DrawDebugPoint(GetWorld(), AuthoritativeFlightNodes[i], 8.0f, FColor::Yellow, false, 0.05f);
			}
			DrawDebugPoint(GetWorld(), AuthoritativeFlightNodes.Last(), 10.0f, FColor::Red, false, 0.05f);
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

		const FString Telemetry = FString::Printf(
			TEXT("%s %s | State: %s | Seed: %d | Speed: %.0f | AirTime: %.2fs | Loc: (%.0f, %.0f, %.0f) | Progress: %.0f%% | Leg: %d | Catchup: %.2fx"),
			*RoleTag, *GetNameSafe(GetOwner()), *StateStr, LaunchSeed, Speed, TotalAirTime,
			CurrentLoc.X, CurrentLoc.Y, CurrentLoc.Z, ProgressPct, FlightLegIndex, ClientCatchupSpeedMultiplier
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
		UE_LOG(LogTemp, Log, TEXT("URocketProjectileComponent: Auto-resolved TargetPrimitive to [%s] on Actor [%s]"),
			*TargetPrimitive->GetName(), *GetOwner()->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("URocketProjectileComponent on [%s]: TargetPrimitive is null and no UPrimitiveComponent found!"),
			*GetOwner()->GetName());
	}
}

void URocketProjectileComponent::LogDebug(const FString& Message, bool bOnScreen) const
{
	if (!bEnableDebugLogs && !bOnScreen)
	{
		return;
	}

	const bool bIsServer = GetOwner() ? GetOwner()->HasAuthority() : false;
	const FString RoleTag = bIsServer ? TEXT("[SERVER]") : TEXT("[CLIENT]");
	const FString ActorName = GetOwner() ? GetOwner()->GetName() : TEXT("None");
	const FString FullMessage = FString::Printf(TEXT("%s [%s] %s"), *RoleTag, *ActorName, *Message);

	if (bEnableDebugLogs)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *FullMessage);
	}

	if ((bOnScreen || bShowOnScreenDebug) && GEngine)
	{
		const FColor DisplayColor = bIsServer ? FColor::Green : FColor::Cyan;
		GEngine->AddOnScreenDebugMessage(-1, 3.5f, DisplayColor, FullMessage);
	}
}

void URocketProjectileComponent::PrintRocketDebugInfo() const
{
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

	UE_LOG(LogTemp, Warning, TEXT("%s"), *Info);
	if (GEngine)
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
	float InFuelDuration)
{
	for (int32 i = 0; i < InRockets.Num(); ++i)
	{
		if (URocketProjectileComponent* Rocket = InRockets[i])
		{
			const FVector Dir = InDirections.IsValidIndex(i) ? InDirections[i] : FVector::ForwardVector;
			Rocket->LaunchRocket(Dir, false, 0, InFuelDuration);
		}
	}
}

void URocketProjectileComponent::ConfigureRocket(
	float InFuelDuration,
	float InSpeed,
	uint8 InSeed,
	FVector InLaunchDirection)
{
	if (InFuelDuration > 0.0f)
	{
		MaxFuelDuration = InFuelDuration;
		FlightSettings.MaxFuelDuration = InFuelDuration;
		FlightNetState.FuelDuration = MaxFuelDuration;
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
		Multicast_ConfigureRocket(MaxFuelDuration, BaseSpeed, static_cast<uint8>(LaunchSeed & 0xFF));
	}

	LogDebug(FString::Printf(TEXT("ConfigureRocket: MaxFuel=%.1f, Speed=%.1f, Seed=%d (bLaunchOnInitialized=%d)"),
		MaxFuelDuration, BaseSpeed, LaunchSeed, bLaunchOnInitialized ? 1 : 0));

	if (bLaunchOnInitialized)
	{
		const FVector Dir = InLaunchDirection.IsNearlyZero() ? (GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector) : InLaunchDirection;
		LaunchRocket(Dir, bLaunchWithHighAccuracy, static_cast<uint8>(LaunchSeed & 0xFF), MaxFuelDuration, BaseSpeed);
	}
}

void URocketProjectileComponent::Multicast_ConfigureRocket_Implementation(
	float InFuelDuration,
	float InSpeed,
	uint8 InSeed)
{
	if (InFuelDuration > 0.0f)
	{
		MaxFuelDuration = InFuelDuration;
		FlightSettings.MaxFuelDuration = InFuelDuration;
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
	LogDebug(FString::Printf(TEXT("Multicast_ConfigureRocket on client: MaxFuel=%.1f, Speed=%.1f, Seed=%d"),
		MaxFuelDuration, BaseSpeed, LaunchSeed));
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
	float InFuelDuration,
	float InSpeed)
{
	// Server already launched locally on the frame it was called; only clients need to apply client-side launch!
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return;
	}

	// All-In-One Instant Launch: High-accuracy initial location and parameters
	ApplyClientLaunch(InServerLaunchTime, InLaunchDirection, true, InLaunchLocation, InSeed, InFuelDuration, InSpeed);
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

void URocketProjectileComponent::Multicast_SendFlightPlan_Implementation(const TArray<FVector_NetQuantize>& InFlightNodes)
{
	// Server already generated and activated the flight plan locally before broadcasting; clients now receive and activate it
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return;
	}

	SetAuthoritativeFlightPlan(InFlightNodes);
}

void URocketProjectileComponent::GenerateServerFlightPlan(TArray<FVector_NetQuantize>& OutNodes)
{
	OutNodes.Empty();
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

	// Node 0: Initial liftoff location rounded to integer
	OutNodes.Add(FVector_NetQuantize(LiftoffCenter));

	// 3. Multi-leg line traces (around 8 to 16 nodes, e.g. 12 legs = 13 nodes)
	const int32 NumLegs = 12;
	const float MaxLegDistance = 2500.0f;
	FRandomStream PlanStream(LaunchSeed > 0 ? LaunchSeed : 1);

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(RocketServerFlightPlanTrace), false, GetOwner());
	FVector CurrentLoc = LiftoffCenter;

	for (int32 LegIdx = 0; LegIdx < NumLegs; ++LegIdx)
	{
		const FVector TraceEnd = CurrentLoc + (CurrentHeading * MaxLegDistance);
		FHitResult Hit;
		const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, CurrentLoc, TraceEnd, ECC_Visibility, TraceParams);

		if (bHit)
		{
			// Store hit location rounded to whole integer
			const FVector HitPoint = Hit.ImpactPoint;
			OutNodes.Add(FVector_NetQuantize(HitPoint));

			// Calculate reflection vector
			FVector Reflected = CurrentHeading - 2.0f * (CurrentHeading | Hit.Normal) * Hit.Normal;

			// Chaotic deflection cone
			const float DeflectionDeg = FMath::Max(FallingSettings.RicochetRandomDeflection, RicochetRandomDeflection);
			if (DeflectionDeg > 0.0f)
			{
				Reflected = PlanStream.VRandCone(Reflected.GetSafeNormal(), FMath::DegreesToRadians(DeflectionDeg));
			}

			// Anti-stick guard to ensure the reflected trajectory points away from the surface
			const float NormalComp = Reflected | Hit.Normal;
			if (NormalComp < 0.25f)
			{
				Reflected = (Reflected - (NormalComp * Hit.Normal)) + (Hit.Normal * 0.35f);
				Reflected.Normalize();
			}

			// Standoff offset for next trace start
			const float Extent = GetProjectedExtentAlongDirection(Hit.Normal);
			CurrentLoc = HitPoint + (Hit.Normal * (Extent + 2.0f));
			CurrentHeading = Reflected.GetSafeNormal();
		}
		else
		{
			// No obstacle hit; store trace end location rounded to whole integer
			OutNodes.Add(FVector_NetQuantize(TraceEnd));
			CurrentLoc = TraceEnd;

			// Pick next airborne semi-random heading in cone
			CurrentHeading = PlanStream.VRandCone(CurrentHeading, FMath::DegreesToRadians(50.0f));
			if (CurrentHeading.Z < 0.05f)
			{
				CurrentHeading.Z = PlanStream.FRandRange(0.12f, 0.40f);
				CurrentHeading.Normalize();
			}
		}
	}

	LogDebug(FString::Printf(TEXT("GenerateServerFlightPlan complete: Generated %d nodes (Seed=%d)"), OutNodes.Num(), LaunchSeed), true);
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

	LogDebug(FString::Printf(TEXT("SetAuthoritativeFlightPlan: Stored %d rounded nodes, TotalPathDistance=%.1f cm"),
		AuthoritativeFlightNodes.Num(), RunningDist), true);

	for (int32 i = 0; i < AuthoritativeFlightNodes.Num(); ++i)
	{
		LogDebug(FString::Printf(TEXT("  FlightPlanNode[%d/%d]: Loc=%s, DistFromStart=%.0f cm"),
			i, AuthoritativeFlightNodes.Num(), *AuthoritativeFlightNodes[i].ToString(), CumulativeFlightPlanDistances[i]));
	}
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
	float InFuelDuration,
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

	if (InFuelDuration > 0.0f)
	{
		MaxFuelDuration = InFuelDuration;
		FlightSettings.MaxFuelDuration = InFuelDuration;
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

		// Suspend any PhysicsSimComponent while the rocket is in flight so it does not interfere or freeze the mesh
		for (UActorComponent* Comp : GetOwner()->GetComponents())
		{
			if (Comp && Comp->GetClass()->GetName().Contains(TEXT("PhysicsSim")))
			{
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

			const float LinSpeed = TargetPrimitive->GetPhysicsLinearVelocity().Size();
			const float AngSpeed = TargetPrimitive->GetPhysicsAngularVelocityInDegrees().Size();

			// If nearly motionless for 0.25s, settle the rocket authoritatively
			if (LinSpeed < 25.0f && AngSpeed < 25.0f)
			{
				PhysicsSettledTimeAccumulated += DeltaTime;
				if (PhysicsSettledTimeAccumulated >= 0.25f)
				{
					const FVector FinalLoc = TargetPrimitive->GetComponentLocation();
					const FRotator FinalRot = TargetPrimitive->GetComponentRotation();
					LogDebug(FString::Printf(TEXT("Physics body settled! LinSpeed=%.1f, AngSpeed=%.1f. Broadcasting Multicast_SettleRocket."), LinSpeed, AngSpeed));
					Multicast_SettleRocket(FinalLoc, FinalRot);
					return;
				}
			}
			else
			{
				PhysicsSettledTimeAccumulated = 0.0f;
			}

			// Fallback timeout: after 3.0s of physics tumble, force settle
			if (TotalAirTime >= MaxFuelDuration + 3.0f)
			{
				const FVector FinalLoc = TargetPrimitive->GetComponentLocation();
				const FRotator FinalRot = TargetPrimitive->GetComponentRotation();
				LogDebug(TEXT("Physics body reached fallback timeout. Forcing Multicast_SettleRocket."));
				Multicast_SettleRocket(FinalLoc, FinalRot);
				return;
			}
		}
		return;
	}

	if (!IsActive() || CurrentState == ERocketSimulationState::Idle || CurrentState == ERocketSimulationState::AtRest)
	{
		SetComponentTickEnabled(false);
		return;
	}

	TimeSinceLastBounce += DeltaTime;

	// In-flight debug heartbeat every 1 second
	DebugHeartbeatTimer += DeltaTime;
	if (DebugHeartbeatTimer >= 1.0f)
	{
		DebugHeartbeatTimer = 0.0f;
		const FString StateStr = UEnum::GetValueAsString(CurrentState);
		const FVector Loc = TargetPrimitive ? TargetPrimitive->GetComponentLocation() : FVector::ZeroVector;
		LogDebug(FString::Printf(TEXT("Heartbeat: State=%s, Flight=%.1f/%.1f s, Speed=%.1f, Loc=(X=%.0f, Y=%.0f, Z=%.0f), Bounces=%d"),
			*StateStr, ElapsedFlightTime, MaxFuelDuration, CurrentVelocity.Size(), Loc.X, Loc.Y, Loc.Z, CurrentBounceCount));
	}

	float StepDeltaTime = DeltaTime;

	// Client dead-reckoning catch-up calculated from InServerLaunchTime:
	// Client smoothly accelerates its 60Hz physics sub-stepping loop until its TotalAirTime matches ServerElapsedSinceLaunch!
	const bool bIsServer = GetOwner() ? GetOwner()->HasAuthority() : false;
	if (!bIsServer && LastReplicatedServerLaunchTime > 0.0f)
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
	else if (!bIsServer && ClientCatchupTimeRemaining > 0.0f)
	{
		StepDeltaTime = DeltaTime * ClientCatchupSpeedMultiplier;
		ClientCatchupTimeRemaining -= DeltaTime;
		if (ClientCatchupTimeRemaining <= 0.0f)
		{
			ClientCatchupSpeedMultiplier = 1.0f;
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

	// Fixed-interval deterministic physics sub-stepping (60 Hz)
	// Ensures identical trajectory, bounce reflection, and landing location regardless of client/server frame rate
	constexpr float FixedStep = 1.0f / 60.0f;
	SimulationTimeAccumulator += StepDeltaTime;

	int32 SubStepCount = 0;
	while (SimulationTimeAccumulator >= FixedStep && SubStepCount < 8)
	{
		PerformSimulationStep(FixedStep);
		SimulationTimeAccumulator -= FixedStep;
		SubStepCount++;

		if (CurrentState == ERocketSimulationState::AtRest || CurrentState == ERocketSimulationState::Idle)
		{
			SimulationTimeAccumulator = 0.0f;
			break;
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
	float InFuelDuration,
	float InSpeed)
{
	LaunchRocket(InDirection, true, InSeed, InFuelDuration, InSpeed);
}

void URocketProjectileComponent::LaunchRocket(
	FVector InDirection,
	bool bHighAccuracy,
	uint8 InSeed,
	float InFuelDuration,
	float InSpeed)
{
	const bool bUseAllInOne = bHighAccuracy || bLaunchWithHighAccuracy || (bLaunchOnInitialized && InSeed != 0 && InFuelDuration > 0.0f);

	if (InFuelDuration > 0.0f)
	{
		MaxFuelDuration = InFuelDuration;
	}
	if (InSpeed > 0.0f)
	{
		BaseSpeed = InSpeed;
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

			// Suspend any PhysicsSimComponent while the rocket is in flight so it does not interfere or freeze the mesh
			for (UActorComponent* Comp : OwnerActor->GetComponents())
			{
				if (Comp && Comp->GetClass()->GetName().Contains(TEXT("PhysicsSim")))
				{
					Comp->SetComponentTickEnabled(false);
				}
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("URocketProjectileComponent::LaunchRocket aborted: No TargetPrimitive found on [%s]! Please add a StaticMeshComponent."),
			*GetNameSafe(GetOwner()));
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
			// All-In-One Instant Launch (High Accuracy): Transmits Location, Direction, Seed, Fuel, Speed (~22 bytes)
			Multicast_LaunchRocketFull(
				ServerTime,
				FlightNetState.LaunchLocation,
				FlightNetState.LaunchDirection,
				FlightNetState.LaunchSeed,
				FlightNetState.FuelDuration,
				FlightNetState.Speed
			);
		}
		else
		{
			// Two-Stage Launch (Ultra-Compact): Transmits Time, Direction & Seed (~8 bytes, NO location snapping in mid-air)
			Multicast_LaunchRocket(ServerTime, FlightNetState.LaunchDirection, FlightNetState.LaunchSeed);
		}

		// 1. Pre-calculate 8-16 multi-bounce flight plan on server
		TArray<FVector_NetQuantize> ServerFlightPlan;
		GenerateServerFlightPlan(ServerFlightPlan);

		// 2. Server ALSO uses the exact rounded vector array for rocketing!
		SetAuthoritativeFlightPlan(ServerFlightPlan);

		// 3. Broadcast the rounded flight plan to clients as normal reliable data (NOT in MTU)
		Multicast_SendFlightPlan(ServerFlightPlan);
	}

	SetActive(true);
	SetComponentTickEnabled(true);

	LogDebug(FString::Printf(TEXT("LaunchRocket SUCCESS! Heading: %s, Speed: %.1f, Seed: %d, MaxFuel: %.1f s, Mode=%s"),
		*TipOverLaunchHeading.ToString(), CurrentVelocity.Size(), InSeed, MaxFuelDuration, bUseAllInOne ? TEXT("AllInOne_HighAccuracy") : TEXT("TwoStage_Compact")));

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
		TargetPrimitive->SetPhysicsLinearVelocity(FinalLinearVelocity);
		if (!FinalAngularVelocity.IsNearlyZero())
		{
			TargetPrimitive->SetPhysicsAngularVelocityInDegrees(FinalAngularVelocity);
		}

		// Keep tick enabled on both server and client to monitor settling and safety timeouts
		SetComponentTickEnabled(true);
		SetActive(true);
	}

	// 3. Keep Actor collision enabled but movement replication OFF during tumble to prevent teleport fighting
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->SetActorEnableCollision(true);
		OwnerActor->SetReplicateMovement(false);

		// Ensure PhysicsSimComponent remains suspended so it cannot freeze physics or zero velocities
		for (UActorComponent* Comp : OwnerActor->GetComponents())
		{
			if (Comp && Comp->GetClass()->GetName().Contains(TEXT("PhysicsSim")))
			{
				Comp->SetComponentTickEnabled(false);
			}
		}
	}

	LogDebug(FString::Printf(TEXT("ApplyPhysicsHandoff complete! IsServer: %d, LinearVel: %s, AngularVel: %s"),
		bIsServer ? 1 : 0, *FinalLinearVelocity.ToString(), *FinalAngularVelocity.ToString()));
}

void URocketProjectileComponent::Multicast_SettleRocket_Implementation(FVector InRestLocation, FRotator InRestRotation)
{
	AutoResolveTargetPrimitive();

	// 1. Update internal state
	CurrentVelocity = FVector::ZeroVector;
	SimulationTimeAccumulator = 0.0f;
	FlightNetState.bPhysicsSimulating = false;
	FlightNetState.RestLocation = InRestLocation;
	FlightNetState.RestRotation = InRestRotation;
	SetSimulationState(ERocketSimulationState::AtRest);
	SetComponentTickEnabled(false);

	// 2. Lock TargetPrimitive flush to rest transform and zero velocities
	if (TargetPrimitive)
	{
		TargetPrimitive->SetSimulatePhysics(false);
		TargetPrimitive->SetEnableGravity(false);
		TargetPrimitive->SetPhysicsLinearVelocity(FVector::ZeroVector);
		TargetPrimitive->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);

		// Place mesh flush at rest location if valid
		if (!InRestLocation.IsNearlyZero())
		{
			TargetPrimitive->SetWorldLocationAndRotation(InRestLocation, InRestRotation, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	// 3. Ensure Owner location and collision
	if (AActor* OwnerActor = GetOwner())
	{
		if (!InRestLocation.IsNearlyZero())
		{
			OwnerActor->SetActorLocationAndRotation(InRestLocation, InRestRotation, false, nullptr, ETeleportType::TeleportPhysics);
		}
		OwnerActor->SetActorEnableCollision(true);

		// Re-enable any PhysicsSimComponent that was suspended during rocket flight
		for (UActorComponent* Comp : OwnerActor->GetComponents())
		{
			if (Comp && Comp->GetClass()->GetName().Contains(TEXT("PhysicsSim")))
			{
				Comp->SetComponentTickEnabled(true);
			}
		}
	}

	// 4. Restore original settings (mobility, collision profile/responses/enabled, physics simulation, replicate movement)
	RestorePrimitiveSettings();

	LogDebug(FString::Printf(TEXT("Multicast_SettleRocket: Locked at Loc: %s, Rot: %s"),
		*InRestLocation.ToString(), *InRestRotation.ToString()), true);

	OnRocketSettled.Broadcast();
}

float URocketProjectileComponent::GetNormalizedRemainingFuel() const
{
	if (MaxFuelDuration <= 0.0f)
	{
		return 0.0f;
	}
	return FMath::Clamp(1.0f - (ElapsedFlightTime / MaxFuelDuration), 0.0f, 1.0f);
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
		ProcessSpentFallingStep(DeltaTime);
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

void URocketProjectileComponent::ProcessRocketStep(float DeltaTime)
{
	ElapsedFlightTime += DeltaTime;
	TotalAirTime += DeltaTime;

	// Check fuel expiration
	if (ElapsedFlightTime >= MaxFuelDuration)
	{
		LogDebug(FString::Printf(TEXT("Fuel Expired! Elapsed: %.2f s, Max: %.2f s. bUsePhysicsSimulation: %d"),
			ElapsedFlightTime, MaxFuelDuration, FallingSettings.bUsePhysicsSimulation ? 1 : 0));

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
		SetSimulationState(ERocketSimulationState::SpentFalling);
		return;
	}

	// Evaluate fuel curve
	float SpeedMultiplier = 1.0f;
	if (FuelThrustCurve)
	{
		const float NormalizedFuelTime = FMath::Clamp(ElapsedFlightTime / MaxFuelDuration, 0.0f, 1.0f);
		SpeedMultiplier = FuelThrustCurve->GetFloatValue(NormalizedFuelTime);
	}

	// Client dead-reckoning & catch-up: match server position along the authoritative flight plan
	if (GetOwner() && !GetOwner()->HasAuthority() && AuthoritativeFlightNodes.Num() >= 2 && LastReplicatedServerLaunchTime > 0.0f)
	{
		const float ClientServerTime = GetSynchronizedServerTime();
		const float PreFlightDuration = TotalTippingDuration + TotalDragDuration;
		const float ServerElapsedFlight = FMath::Max(0.0f, ClientServerTime - LastReplicatedServerLaunchTime - PreFlightDuration);

		const float ExpectedServerDist = ServerElapsedFlight * FlightSettings.BaseSpeed;
		const float ClientTraveledDist = (CumulativeFlightPlanDistances.IsValidIndex(CurrentFlightPlanNodeIndex) ? CumulativeFlightPlanDistances[CurrentFlightPlanNodeIndex] : 0.0f) + FlightDistanceTraveled;

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

	// Maintain forward momentum with slight gravity drop
	FVector CurrentHeading = CurrentVelocity.GetSafeNormal();
	if (CurrentHeading.IsNearlyZero())
	{
		CurrentHeading = TipOverLaunchHeading;
	}

	const float RemainingFuel = MaxFuelDuration - ElapsedFlightTime;
	const float EffectiveExhaustDuration = FMath::Min(LowFuelSettings.LowFuelPhaseDuration, MaxFuelDuration * 0.4f);

	if (LowFuelSettings.bEnableLowFuelSputter && RemainingFuel <= EffectiveExhaustDuration && RemainingFuel > 0.0f)
	{
		// State 4: Low Fuel / Sputter (almost empty thrust upward struggle)
		if (CurrentState != ERocketSimulationState::LowFuelSputter)
		{
			SetSimulationState(ERocketSimulationState::LowFuelSputter);
		}

		const float ExhaustAlpha = 1.0f - FMath::Clamp(RemainingFuel / EffectiveExhaustDuration, 0.0f, 1.0f);

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
			FlightDistanceTraveled += TargetSpeed * DeltaTime;
			const float NormalizedProgress = FlightBaselineLength > 0.0f ? (FlightDistanceTraveled / FlightBaselineLength) : 1.0f;

			FVector NextSplineLoc;
			FVector NextSplineTangent;
			EvaluateFlightSpline(NormalizedProgress, NextSplineLoc, NextSplineTangent);

			const FVector CurrentCenter = GetWorldCenterLocation();
			FVector MoveDelta = NextSplineLoc - CurrentCenter;

			CurrentVelocity = MoveDelta / FMath::Max(DeltaTime, 0.0001f);

			// Continuous travel history logging every 0.25s
			TravelHistoryLogTimer += DeltaTime;
			if (TravelHistoryLogTimer >= 0.25f)
			{
				TravelHistoryLogTimer = 0.0f;
				LogDebug(FString::Printf(TEXT("TravelHistory: Air=%.2fs, Prog=%.1f%%, Loc=%s, Vel=%s, Spd=%.0f, Leg=%d (Node %d/%d), Catchup=%.2fx"),
					TotalAirTime, NormalizedProgress * 100.0f, *CurrentCenter.ToString(), *CurrentVelocity.ToString(), CurrentVelocity.Size(), FlightLegIndex, CurrentFlightPlanNodeIndex, AuthoritativeFlightNodes.Num(), ClientCatchupSpeedMultiplier));
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
			FlightDistanceTraveled += TargetSpeed * DeltaTime;
			const float NormalizedProgress = FlightBaselineLength > 0.0f ? (FlightDistanceTraveled / FlightBaselineLength) : 1.0f;

			TravelHistoryLogTimer += DeltaTime;
			if (TravelHistoryLogTimer >= 0.25f)
			{
				TravelHistoryLogTimer = 0.0f;
				const FVector Center = GetWorldCenterLocation();
				LogDebug(FString::Printf(TEXT("TravelHistory: Air=%.2fs, Prog=%.1f%%, Loc=%s, Vel=%s, Spd=%.0f, Leg=%d (Node %d/%d), Catchup=%.2fx"),
					TotalAirTime, NormalizedProgress * 100.0f, *Center.ToString(), *CurrentVelocity.ToString(), CurrentVelocity.Size(), FlightLegIndex, CurrentFlightPlanNodeIndex, AuthoritativeFlightNodes.Num(), ClientCatchupSpeedMultiplier));
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

	// Ballistic drop under gravity
	CurrentVelocity.Z += FlightSettings.GravityZ * FallingSettings.SpentGravityScale * DeltaTime;

	// Air drag
	CurrentVelocity *= FMath::Clamp(1.0f - (0.4f * DeltaTime), 0.0f, 1.0f);

	const FVector CurrentCenter = GetWorldCenterLocation();

	// Line trace downward to measure height above the ground (Image 2)
	FHitResult GroundCheckHit;
	FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(RocketFallingGroundCheck), false, GetOwner());
	const float CheckDistance = FMath::Max(FallingSettings.GroundInterpDistance * 2.5f, 800.0f);
	const FVector TraceDown = CurrentCenter - FVector(0.0f, 0.0f, CheckDistance);
	const bool bFoundFloor = GetWorld() && GetWorld()->LineTraceSingleByChannel(GroundCheckHit, CurrentCenter, TraceDown, ECC_WorldStatic, GroundParams);

	if (bFoundFloor)
	{
		// Compute deterministic rest rotation from LaunchSeed and ground normal so server and client match identically
		if (!bTargetRestRotationInitialized)
		{
			const float SeedYaw = static_cast<float>((LaunchSeed * 67 + CurrentBounceCount * 13) % 360);
			DeterministicRestRotation = ComputeGroundRestRotation(GroundCheckHit.Normal, SeedYaw);
			bTargetRestRotationInitialized = true;
		}

		const float DistToGround = GroundCheckHit.Distance;
		if (DistToGround <= FallingSettings.GroundInterpDistance && TargetPrimitive)
		{
			// When nearing the ground, smoothly interpolate from tumble towards the deterministic landing rotation
			const float InterpAlpha = 1.0f - FMath::Clamp(DistToGround / FallingSettings.GroundInterpDistance, 0.0f, 1.0f);
			const FRotator CurrentRot = TargetPrimitive->GetComponentRotation();
			const FRotator SmoothRot = FMath::RInterpTo(CurrentRot, DeterministicRestRotation, DeltaTime, 6.0f + (InterpAlpha * 14.0f));
			SetTargetTransformAroundCenter(CurrentCenter, SmoothRot);
		}
		else if (TargetPrimitive && !CurrentVelocity.IsNearlyZero())
		{
			// High in the air: continuous visual tumble
			FallingTumbleAngle += FallingSettings.TumbleRate * DeltaTime;
			const FRotator TumbleDelta(FallingSettings.TumbleRate * DeltaTime, FallingSettings.TumbleRate * 1.33f * DeltaTime, 0.0f);
			const FRotator NewRot = (FRotationMatrix(TumbleDelta) * FRotationMatrix(TargetPrimitive->GetComponentRotation())).Rotator();
			SetTargetTransformAroundCenter(CurrentCenter, NewRot);
		}
	}
	else if (TargetPrimitive && !CurrentVelocity.IsNearlyZero())
	{
		// High in the air without ground in trace range: tumble
		FallingTumbleAngle += FallingSettings.TumbleRate * DeltaTime;
		const FRotator TumbleDelta(FallingSettings.TumbleRate * DeltaTime, FallingSettings.TumbleRate * 1.33f * DeltaTime, 0.0f);
		const FRotator NewRot = (FRotationMatrix(TumbleDelta) * FRotationMatrix(TargetPrimitive->GetComponentRotation())).Rotator();
		SetTargetTransformAroundCenter(CurrentCenter, NewRot);
	}

	// Move bottle with continuous physics sweep
	const FVector MoveDelta = CurrentVelocity * DeltaTime;
	FHitResult Hit;
	if (SweepAndMove(MoveDelta, Hit))
	{
		HandleRicochet(Hit);

		// If velocity drops below rest threshold against an upward surface, settle flush
		if (CurrentVelocity.Size() < AtRestSettings.RestVelocityThreshold && Hit.Normal.Z > 0.6f)
		{
			if (!bTargetRestRotationInitialized)
			{
				const float SeedYaw = static_cast<float>((LaunchSeed * 67 + CurrentBounceCount * 13) % 360);
				DeterministicRestRotation = ComputeGroundRestRotation(Hit.Normal, SeedYaw);
				bTargetRestRotationInitialized = true;
			}

			const float GroundExtent = GetProjectedExtentAlongDirection(Hit.Normal);
			const FVector RestCenter = Hit.ImpactPoint + (Hit.Normal * (GroundExtent + 0.2f));
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
			return;
		}
	}

	// Safety timeout: if falling for more than 2.5s without settling, trace straight down to find the floor and settle flush
	if (TotalAirTime >= MaxFuelDuration + 2.5f)
	{
		FHitResult FloorHit;
		FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(RocketFloorTrace), false, GetOwner());
		const FVector CenterLoc = GetWorldCenterLocation();
		const FVector TraceDownFallback = CenterLoc - FVector(0.0f, 0.0f, 3000.0f);
		if (GetWorld() && GetWorld()->LineTraceSingleByChannel(FloorHit, CenterLoc, TraceDownFallback, CollisionChannel, FloorParams))
		{
			if (!bTargetRestRotationInitialized)
			{
				const float SeedYaw = static_cast<float>((LaunchSeed * 67) % 360);
				DeterministicRestRotation = ComputeGroundRestRotation(FloorHit.Normal, SeedYaw);
				bTargetRestRotationInitialized = true;
			}

			const float GroundExtent = GetProjectedExtentAlongDirection(FloorHit.Normal);
			const FVector RestCenter = FloorHit.ImpactPoint + (FloorHit.Normal * (GroundExtent + 0.2f));
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
			return;
		}
	}
}

bool URocketProjectileComponent::SweepAndMove(const FVector& MoveDelta, FHitResult& OutHit)
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
	const float LeadingExtent = GetProjectedExtentAlongDirection(MoveDir);

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
		TargetPrimitive->GetComponentQuat(),
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
			const float ExtentAlongNormal = GetProjectedExtentAlongDirection(DepenDir);
			const FVector DepenCenter = CurrentCenter + (DepenDir * (OutHit.PenetrationDepth + ExtentAlongNormal + 1.5f));
			SetTargetTransformAroundCenter(DepenCenter, TargetPrimitive->GetComponentRotation());
			return true;
		}

		// Calculate safe non-penetrating center: push center back out along surface normal by the mesh extent along that normal
		const float ExtentAlongNormal = GetProjectedExtentAlongDirection(OutHit.Normal);
		const FVector SafeCenter = OutHit.ImpactPoint + (OutHit.Normal * (ExtentAlongNormal + 1.5f));

		SetTargetTransformAroundCenter(SafeCenter, TargetPrimitive->GetComponentRotation());
		return true;
	}
	else
	{
		const FVector EndCenter = CurrentCenter + MoveDelta;
		SetTargetTransformAroundCenter(EndCenter, TargetPrimitive->GetComponentRotation());
		return false;
	}
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
		const float ExtentAlongNormal = GetProjectedExtentAlongDirection(Hit.Normal);
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

	// Add chaotic random deflection cone
	const float DeflectionDeg = FMath::Max(FallingSettings.RicochetRandomDeflection, RicochetRandomDeflection);
	if (DeflectionDeg > 0.0f)
	{
		const float DeflectionRad = FMath::DegreesToRadians(DeflectionDeg);
		Reflected = RandomStream.VRandCone(Reflected.GetSafeNormal(), DeflectionRad);
	}

	// ANTI-STICK GUARD: Enforce that the reflected trajectory points decisively away from the wall
	const float NormalComponent = Reflected | Hit.Normal;
	if (NormalComponent < 0.25f)
	{
		Reflected = (Reflected - (NormalComponent * Hit.Normal)) + (Hit.Normal * 0.35f);
		Reflected.Normalize();
	}

	// Scale velocity by bounciness
	const float IncidentSpeed = Incident.Size();
	float NewSpeed = IncidentSpeed * Bounciness;

	if (CurrentState == ERocketSimulationState::SpentFalling)
	{
		// If user configured the bottle not to bounce after fuel is gone, immediately settle flush on surface
		const bool bCanBounce = FallingSettings.bBounceAfterFuelDepleted && bBounceAfterFuelDepleted;
		if (!bCanBounce)
		{
			if (!bTargetRestRotationInitialized)
			{
				const float SeedYaw = static_cast<float>((LaunchSeed * 67 + CurrentBounceCount * 13) % 360);
				DeterministicRestRotation = ComputeGroundRestRotation(Hit.Normal, SeedYaw);
				bTargetRestRotationInitialized = true;
			}

			const float ExtentAlongNormal = GetProjectedExtentAlongDirection(Hit.Normal);
			const FVector RestCenter = Hit.ImpactPoint + (Hit.Normal * (ExtentAlongNormal + 0.1f));
			SetTargetTransformAroundCenter(RestCenter, DeterministicRestRotation);
			CurrentVelocity = FVector::ZeroVector;
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

		// Lower bounce impulse according to velocity: as velocity drops, bounce impulse decays sharply
		const float SpeedDamping = FMath::Clamp(IncidentSpeed / 500.0f, 0.12f, 1.0f);
		const float EffectiveRestitution = FallingSettings.SpentBounciness * SpeedDamping;

		// Decompose velocity into normal bounce and tangential ground friction
		const FVector NormalVelocity = (Incident | Hit.Normal) * Hit.Normal;
		const FVector TangentVelocity = Incident - NormalVelocity;

		Reflected = (-NormalVelocity * EffectiveRestitution) + (TangentVelocity * FallingSettings.SpentFriction);

		// If velocity after bounce drops below rest threshold on an upward-facing surface, settle flush
		if (Reflected.Size() < AtRestSettings.RestVelocityThreshold && Hit.Normal.Z > 0.6f)
		{
			if (!bTargetRestRotationInitialized)
			{
				const float SeedYaw = static_cast<float>((LaunchSeed * 67 + CurrentBounceCount * 13) % 360);
				DeterministicRestRotation = ComputeGroundRestRotation(Hit.Normal, SeedYaw);
				bTargetRestRotationInitialized = true;
			}

			const float ExtentAlongNormal = GetProjectedExtentAlongDirection(Hit.Normal);
			const FVector RestCenter = Hit.ImpactPoint + (Hit.Normal * (ExtentAlongNormal + 0.1f));
			SetTargetTransformAroundCenter(RestCenter, DeterministicRestRotation);
			CurrentVelocity = FVector::ZeroVector;
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

		NewSpeed = Reflected.Size();
	}
	else if (CurrentState == ERocketSimulationState::Rocketing)
	{
		float CurveMultiplier = 1.0f;
		if (FuelThrustCurve)
		{
			const float NormalizedFuel = FMath::Clamp(ElapsedFlightTime / MaxFuelDuration, 0.0f, 1.0f);
			CurveMultiplier = FuelThrustCurve->GetFloatValue(NormalizedFuel);
		}
		NewSpeed = FMath::Max(NewSpeed, FlightSettings.BaseSpeed * CurveMultiplier * 0.85f);
	}

	CurrentVelocity = Reflected.GetSafeNormal() * NewSpeed;

	// Instantly place bottle at a safe standoff distance off the wall along normal to prevent start-penetration on next tick
	const float ExtentAlongNormal = GetProjectedExtentAlongDirection(Hit.Normal);
	const FVector SafeCenter = Hit.ImpactPoint + (Hit.Normal * (ExtentAlongNormal + 1.5f));

	// Re-initialize flight spline for the bounce leg!
	if (CurrentState == ERocketSimulationState::Rocketing)
	{
		FlightLegIndex++;
		CurrentFlightPlanNodeIndex++;

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

float URocketProjectileComponent::GetProjectedExtentAlongDirection(const FVector& Direction) const
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
	const FRotator CurrentRot = TargetPrimitive->GetComponentRotation();

	const FVector AxisX = CurrentRot.RotateVector(FVector::ForwardVector);
	const FVector AxisY = CurrentRot.RotateVector(FVector::RightVector);
	const FVector AxisZ = CurrentRot.RotateVector(FVector::UpVector);

	const float Extent =
		FMath::Abs(AxisX | NormalizedDir) * ScaledExtent.X +
		FMath::Abs(AxisY | NormalizedDir) * ScaledExtent.Y +
		FMath::Abs(AxisZ | NormalizedDir) * ScaledExtent.Z;

	return FMath::Max(Extent, SweepRadius);
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
		SetSimulationState(ERocketSimulationState::AtRest);
		SetComponentTickEnabled(false);
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

			if (!FlightNetState.LastBounceVelocity.IsNearlyZero())
			{
				CurrentVelocity = FlightNetState.LastBounceVelocity;
			}
			else if (CurrentVelocity.IsNearlyZero())
			{
				CurrentVelocity = FVector(0.0f, 0.0f, -50.0f);
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
