#pragma once

#include "CoreMinimal.h"

/** Cosmetic only: motion history belongs to an active representation, never to the saved product. */
struct MARRYBYMIDNIGHT_API FColaLiquidMotion
{
	FVector2D Tilt = FVector2D::ZeroVector;
	FVector2D TiltVelocity = FVector2D::ZeroVector;
	FVector FilteredAcceleration = FVector::ZeroVector;
	FVector PreviousVelocity = FVector::ZeroVector;
	FVector PreviousAngularVelocity = FVector::ZeroVector;
	FTransform PreviousTransform = FTransform::Identity;
	float WaveEnergy = 0;
	float FluidSpin = 0; // radians/second around world gravity
	bool bHasHistory = false;

	void Reset();
	void Advance(float DeltaSeconds, const FTransform& Transform, const FVector& Velocity,
		const FVector& AngularVelocityRadians, float Fill, float RadiusCm, float HeightCm, float ResponseGain = 1.f);
	FVector SurfaceNormal() const;
	bool IsSettled() const;
	static FVector AngularVelocityBetween(const FQuat& Previous, const FQuat& Current, float DeltaSeconds);
	/** Exact volume fraction for a cylindrical proxy clipped by a plane perpendicular to LocalNormal. */
	static float CylinderVolumeFraction(float Offset, const FVector& LocalNormal, float HalfHeight, float Radius);
	static float CylinderPlaneOffset(float Fill, const FVector& LocalNormal, float HalfHeight, float Radius);
};
