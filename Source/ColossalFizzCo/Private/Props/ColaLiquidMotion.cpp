#include "Props/ColaLiquidMotion.h"

namespace
{
	double DiskCDF(double U)
	{
		if (U <= -1) return 0;
		if (U >= 1) return 1;
		return .5 + (FMath::Asin(U) + U*FMath::Sqrt(FMath::Max(0.,1-U*U))) / UE_DOUBLE_PI;
	}
	// Integral of the projected circular cross-section CDF, including its tails.
	double IntegratedDiskCDF(double T, double B)
	{
		if (T <= -B) return 0;
		if (T >= B) return T;
		const double U = T/B, Root = FMath::Sqrt(FMath::Max(0.,1-U*U));
		return B*(.5*U + (U*FMath::Asin(U) + Root - Root*Root*Root/3.) / UE_DOUBLE_PI);
	}
}

void FColaLiquidMotion::Reset() { *this = FColaLiquidMotion(); }

FVector FColaLiquidMotion::AngularVelocityBetween(const FQuat& Previous, const FQuat& Current, float Dt)
{
	if (Dt <= SMALL_NUMBER || !FMath::IsFinite(Dt)) return FVector::ZeroVector;
	FQuat Delta = (Current*Previous.Inverse()).GetNormalized();
	if (Delta.W < 0) Delta = Delta * -1.; // shortest arc; no +/-180 degree Euler discontinuity
	FVector Axis; double Angle;
	Delta.ToAxisAndAngle(Axis, Angle);
	return Axis.GetSafeNormal()*Angle/Dt;
}

void FColaLiquidMotion::Advance(float Dt, const FTransform& Transform, const FVector& Velocity,
	const FVector& AngularVelocityRadians, float Fill, float Radius, float Height, float ResponseGain)
{
	if (!FMath::IsFinite(Dt) || Dt <= SMALL_NUMBER || Velocity.ContainsNaN() || AngularVelocityRadians.ContainsNaN() || Transform.ContainsNaN()) return;
	const bool bDiscontinuity = !bHasHistory || Dt > .25f || FVector::DistSquared(Transform.GetLocation(),PreviousTransform.GetLocation()) > FMath::Square(500.);
	if (bDiscontinuity)
	{
		PreviousVelocity=Velocity; PreviousAngularVelocity=AngularVelocityRadians; PreviousTransform=Transform; bHasHistory=true;
		FilteredAcceleration=FVector::ZeroVector;
	}
	const float StepTime=FMath::Min(Dt,.1f), Gravity=980.f;
	FVector Acceleration=bDiscontinuity ? FVector::ZeroVector : ((Velocity-PreviousVelocity)/Dt).GetClampedToMaxSize(Gravity*6);
	const FVector Alpha=bDiscontinuity ? FVector::ZeroVector : ((AngularVelocityRadians-PreviousAngularVelocity)/Dt).GetClampedToMaxSize(60);
	const FVector Lever=Transform.GetRotation().GetUpVector()*FMath::Max(1.f,Height)*.2;
	Acceleration += FVector::CrossProduct(Alpha,Lever)*.25;
	FilteredAcceleration=FMath::Lerp(FilteredAcceleration,Acceleration.GetClampedToMaxSize(Gravity*6),1-FMath::Exp(-StepTime/.055f));
	const float F=FMath::Clamp(Fill,0.f,1.f), Headroom=4*F*(1-F);
	const float Depth=FMath::Max(1.f,Height*F), K=1.841f/FMath::Max(5.f,Radius);
	const float Frequency=FMath::Clamp(FMath::Sqrt(Gravity*K*FMath::Tanh(K*Depth)),3.f,12.f);
	const FVector2D Target=FVector2D(FilteredAcceleration.X,FilteredAcceleration.Y)/Gravity*(.4f*FMath::Sqrt(Headroom)*FMath::Clamp(ResponseGain,0.f,4.f));
	const FVector2D BoundedTarget=Target.GetClampedToMaxSize(.65);
	const int32 Steps=FMath::Clamp(FMath::CeilToInt(StepTime*120),1,12);
	const float H=StepTime/Steps;
	for (int32 I=0;I<Steps;++I)
	{
		TiltVelocity += ((BoundedTarget-Tilt)*FMath::Square(Frequency)-TiltVelocity*(2*.24f*Frequency))*H;
		Tilt += TiltVelocity*H;
		if (Tilt.SizeSquared() > .85*.85)
		{
			Tilt= Tilt.GetSafeNormal()*.85;
			const double Outward=FVector2D::DotProduct(TiltVelocity,Tilt.GetSafeNormal());
			if (Outward>0) TiltVelocity-=Tilt.GetSafeNormal()*Outward;
		}
	}
	const float MotionEnergy=FMath::Clamp(float(Tilt.Size()*2+TiltVelocity.Size()/Frequency+FMath::Abs(FilteredAcceleration.Z)/Gravity*.1+Alpha.Size()*.008),0.f,1.f)*Headroom;
	WaveEnergy=FMath::Lerp(WaveEnergy,MotionEnergy,1-FMath::Exp(-StepTime/(MotionEnergy>WaveEnergy ? .06f : .65f)));
	const float TargetSpin=FMath::Clamp(float(FVector::DotProduct(AngularVelocityRadians,SurfaceNormal())),-12.f,12.f);
	FluidSpin=FMath::Lerp(FluidSpin,TargetSpin,1-FMath::Exp(-StepTime/(FMath::Abs(TargetSpin)>FMath::Abs(FluidSpin) ? 1.f : 1.4f)));
	if (F <= .0001f || F >= .9999f) { Tilt=FVector2D::ZeroVector; TiltVelocity=FVector2D::ZeroVector; WaveEnergy=0; FluidSpin=0; }
	PreviousVelocity=Velocity; PreviousAngularVelocity=AngularVelocityRadians; PreviousTransform=Transform;
}

FVector FColaLiquidMotion::SurfaceNormal() const { return FVector(Tilt.X,Tilt.Y,1).GetSafeNormal(); }
bool FColaLiquidMotion::IsSettled() const { return Tilt.SizeSquared()<.000025 && TiltVelocity.SizeSquared()<.0001 && WaveEnergy<.006f && FMath::Abs(FluidSpin)<.03f; }

float FColaLiquidMotion::CylinderVolumeFraction(float Offset, const FVector& Normal, float HalfHeight, float Radius)
{
	const FVector N=Normal.GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
	const double A=FMath::Abs(N.Z)*FMath::Max(.001f,HalfHeight), B=FMath::Sqrt(N.X*N.X+N.Y*N.Y)*FMath::Max(.001f,Radius);
	if (B<.00001) return FMath::Clamp(.5+Offset/(2*FMath::Max(A,.00001)),0.,1.);
	if (A<.00001) return DiskCDF(Offset/B);
	return FMath::Clamp((IntegratedDiskCDF(Offset+A,B)-IntegratedDiskCDF(Offset-A,B))/(2*A),0.,1.);
}

float FColaLiquidMotion::CylinderPlaneOffset(float Fill, const FVector& Normal, float HalfHeight, float Radius)
{
	const FVector N=Normal.GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
	const float Extent=FMath::Abs(N.Z)*HalfHeight+FMath::Sqrt(N.X*N.X+N.Y*N.Y)*Radius;
	if (Fill<=0) return -Extent;
	if (Fill>=1) return Extent;
	float Low=-Extent, High=Extent;
	for (int32 I=0;I<14;++I) { const float Mid=(Low+High)*.5f; if (CylinderVolumeFraction(Mid,N,HalfHeight,Radius)<Fill) Low=Mid; else High=Mid; }
	return (Low+High)*.5f;
}
