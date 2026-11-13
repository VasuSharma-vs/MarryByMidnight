#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"
#include "Props/ColaLiquidMotion.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColaLiquidMotionTest,"ColossalFizzCo.Props.LiquidMotion",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FColaLiquidMotionTest::RunTest(const FString& Parameters)
{
	const float Dt=1.f/120;
	FColaLiquidMotion Liquid;
	FTransform Transform;
	Liquid.Advance(Dt,Transform,FVector::ZeroVector,FVector::ZeroVector,.5f,35,150);
	// Accelerate right: the surface rises on the left, then continues oscillating after stopping.
	for (int32 I=1;I<=45;++I) Liquid.Advance(Dt,Transform,FVector(I*12,0,0),FVector::ZeroVector,.5f,35,150);
	TestTrue(TEXT("Acceleration generates opposing liquid inertia"),Liquid.SurfaceNormal().X>0.01);
	const float MovingEnergy=Liquid.WaveEnergy;
	Liquid.Advance(Dt,Transform,FVector::ZeroVector,FVector::ZeroVector,.5f,35,150);
	TestTrue(TEXT("Abrupt stop does not erase retained motion"),!Liquid.IsSettled() && Liquid.WaveEnergy>0);
	bool bOscillated=false; const double InitialTilt=Liquid.Tilt.X;
	for (int32 I=0;I<1200;++I)
	{
		Liquid.Advance(Dt,Transform,FVector::ZeroVector,FVector::ZeroVector,.5f,35,150);
		bOscillated |= InitialTilt*Liquid.Tilt.X<0;
		if (!TestTrue(TEXT("Surface always remains in world-up hemisphere"),Liquid.SurfaceNormal().Z>.7)) return false;
	}
	TestTrue(TEXT("Liquid oscillates through equilibrium"),bOscillated);
	TestTrue(TEXT("Stationary liquid settles and permits pooling"),Liquid.IsSettled() && Liquid.WaveEnergy<MovingEnergy);
	Liquid.Reset(); Liquid.Advance(Dt,Transform,FVector(0,0,-2400),FVector::ZeroVector,.5f,35,150);
	Liquid.Advance(Dt,Transform,FVector::ZeroVector,FVector(24,-18,0),.5f,35,150);
	TestTrue(TEXT("Hard ground impact and tumble cannot flip the liquid"),Liquid.SurfaceNormal().Z>.7 && Liquid.WaveEnergy>0);
	for (int32 I=0;I<360;++I) Liquid.Advance(Dt,Transform,FVector::ZeroVector,FVector(0,0,5),.5f,35,150);
	TestTrue(TEXT("Sustained spinning gradually entrains the fluid"),Liquid.FluidSpin>4);
	Liquid.Advance(Dt,Transform,FVector::ZeroVector,FVector::ZeroVector,.5f,35,150);
	TestTrue(TEXT("Vortex retains angular momentum after bottle stops"),Liquid.FluidSpin>3);
	for (int32 I=0;I<1500;++I) Liquid.Advance(Dt,Transform,FVector::ZeroVector,FVector::ZeroVector,.5f,35,150);
	TestTrue(TEXT("Vortex decays without continuous rotation"),Liquid.IsSettled());
	const auto Angular=FColaLiquidMotion::AngularVelocityBetween(FRotator(0,179,0).Quaternion(),FRotator(0,-179,0).Quaternion(),.1f);
	TestTrue(TEXT("Quaternion sampling crosses the yaw wrap by two degrees"),FMath::IsNearlyEqual(Angular.Size(),FMath::DegreesToRadians(20.),.001));
	for (float Fill : {0.f,.01f,.5f,.99f,1.f})
	{
		Liquid.Reset(); Liquid.Advance(Dt,Transform,FVector::ZeroVector,FVector::ZeroVector,Fill,35,150);
		for(int32 I=0;I<60;++I) Liquid.Advance(Dt,Transform,FVector(I*40,0,0),FVector(0,0,4),Fill,35,150);
		TestTrue(TEXT("All fill levels have finite bounded motion"),!Liquid.SurfaceNormal().ContainsNaN() && Liquid.SurfaceNormal().Z>.7);
		if (Fill==0 || Fill==1) TestTrue(TEXT("No exposed free surface at empty/full endpoints"),Liquid.IsSettled());
	}
	// Compare frame rates using the same continuous acceleration/stop trajectory.
	auto Simulate=[](float Step)
	{
		FColaLiquidMotion State;
		for(int32 I=0;I<FMath::RoundToInt(2/Step);++I) { const float T=I*Step; const float V=T<.5f ? 600*T : (T<1 ? 300.f : 0.f); State.Advance(Step,FTransform::Identity,FVector(V,0,0),FVector::ZeroVector,.5f,35,150); }
		return State;
	};
	const auto Fine=Simulate(1.f/120), Coarse=Simulate(1.f/30);
	TestTrue(TEXT("Sloshing remains stable across 30 and 120 fps"),(Fine.Tilt-Coarse.Tilt).Size()<.05);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColaLiquidVolumeTest,"ColossalFizzCo.Props.LiquidVolume",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FColaLiquidVolumeTest::RunTest(const FString& Parameters)
{
	const double Started=FPlatformTime::Seconds();
	float WorstError=0;
	for (const FVector N : {FVector(0,0,1),FVector(0,0,-1),FVector(1,0,0),FVector(1,0,1).GetSafeNormal(),FVector(.2,.8,.3).GetSafeNormal()})
		for (float Fill : {.02f,.1f,.25f,.5f,.8f,.98f})
		{
			const float Offset=FColaLiquidMotion::CylinderPlaneOffset(Fill,N,75,35);
			int32 Below=0;
			// Independent uniform volume quadrature, not the analytic CDF used by the solver.
			for(int32 Z=0;Z<32;++Z) for(int32 I=0;I<384;++I)
			{
				const double Radius=35*FMath::Sqrt((I+.5)/384.), Angle=I*2.399963229728653;
				const FVector P(Radius*FMath::Cos(Angle),Radius*FMath::Sin(Angle),150*((Z+.5)/32.-.5));
				Below+=FVector::DotProduct(P,N)<=Offset ? 1 : 0;
			}
			const float Error=FMath::Abs(float(Below)/(32*384)-Fill); WorstError=FMath::Max(WorstError,Error);
			if (!TestTrue(TEXT("Tilted cylindrical proxy preserves fill volume within 2 percent"),Error<.02f)) return false;
		}
	AddInfo(FString::Printf(TEXT("Independent volume validation: worst fraction error %.5f; elapsed %.2f ms"),WorstError,(FPlatformTime::Seconds()-Started)*1000));
	return true;
}
#endif
