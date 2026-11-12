#include "Props/ColaPropActor.h"
#include "Components/RocketProjectileComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Props/InteractablePropManager.h"

AColaPropActor::AColaPropActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;

	RocketComp = nullptr;

	ColaLevel = 1.0f;
	StabilityTolerance = 2.0f;
	BasePrice = 10.0f;
	FoamPressure = 0.0f;
	AgitationJerkThreshold = 1.5f;
	OverfillSurgeMultiplier = 3.0f;
	FoamDecayRate = 1.2f;
	DropImpactThreshold = 2500.0f;
	bIsFirstActiveFrame = true;
}

void AColaPropActor::OnApplySharedDefinitions(const FPropProductDefinition* ProductDef, const FPropContentDefinition* ContentDef)
{
	if (ProductDef)
	{
		BottleMass=FMath::IsFinite(ProductDef->BottleMass) ? FMath::Max(.001f,ProductDef->BottleMass) : FMath::Max(.001f,DefaultBottleMassKg);
		PortionVolumeMl=FMath::IsFinite(ProductDef->PortionVolumeMl) ? FMath::Max(.001f,ProductDef->PortionVolumeMl) : 20.f;
		ContentDensityGPerMl=ContentDef && FMath::IsFinite(ContentDef->DensityGPerMl) ? FMath::Max(.001f,ContentDef->DensityGPerMl) : 1.04f;
		FPropProductDefinition BottleDefinition = *ProductDef;
		ContentMass=GetContentID()>0 ? FMath::Max(0.f,GetContentLevel())*ProductDef->ConsumableProfile.NumOfPortions*PortionVolumeMl*ContentDensityGPerMl/1000.f : 0;
		BottleDefinition.GameplayMassKg = BottleMass+ContentMass;
		Super::OnApplySharedDefinitions(&BottleDefinition, ContentDef);
	}
	else Super::OnApplySharedDefinitions(ProductDef, ContentDef);
	if (ContentDef && ContentDef->LiquidMaterial && PropMesh && (!SkeletalMeshComponent || !SkeletalMeshComponent->GetSkeletalMeshAsset()))
		PropMesh->SetMaterial(1,ContentDef->LiquidMaterial);
}

void AColaPropActor::UpdateBottleMass()
{
	if (const auto* Content=ResolveContentDefinition()) ContentDensityGPerMl=Content->DensityGPerMl;
	ContentMass=GetContentID()>0 ? FMath::Max(0.f,GetContentLevel())*GetNumPortions()*PortionVolumeMl*ContentDensityGPerMl/1000.f : 0;
	const float Total=FMath::Max(.001f,BottleMass+ContentMass);
	// Update the existing body, without recreating it or adding an impulse. Chaos
	// retains its linear/angular velocity; peers derive the same mass from content.
	if (PropMesh && !FMath::IsNearlyEqual(PropMesh->BodyInstance.GetMassOverride(),Total,.000001f))
		PropMesh->SetMassOverrideInKg(NAME_None,Total,true);
}

void AColaPropActor::ApplyUnsettlingImpulse(const FVector& Impulse)
{
	// This is momentum (kg cm/s), not a velocity assignment. Large hits still move
	// a bottle; a walking bump can no longer launch every mass at the same speed.
	if (PropMesh && PropMesh->IsSimulatingPhysics() && !Impulse.ContainsNaN())
		PropMesh->AddImpulse(Impulse, NAME_None, false);
}

FVector AColaPropActor::NormalizePhysicalContactImpulse(const FVector& Impulse) const
{
	const float Mass = PropMesh ? PropMesh->BodyInstance.GetBodyMass() : ImpactReferenceMassKg;
	return Impulse * (FMath::Max(.1f, ImpactReferenceMassKg) / FMath::Max(.1f, Mass));
}

void AColaPropActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AColaPropActor, AcceptedFailureType);
	DOREPLIFETIME(AColaPropActor, InteractionPressure);

	DOREPLIFETIME(AColaPropActor, ColaLevel);
	DOREPLIFETIME(AColaPropActor, FoamPressure);
	DOREPLIFETIME(AColaPropActor, RocketLaunchConfiguration);
}

void AColaPropActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (!RocketComp)
	{
		RocketComp = FindComponentByClass<URocketProjectileComponent>();
	}
	if (!RocketComp)
	{
		RocketComp=NewObject<URocketProjectileComponent>(this,TEXT("NativeBottleRocket"));
		AddInstanceComponent(RocketComp); RocketComp->bLaunchOnInitialized=false;
		RocketComp->SetNetAddressable(); RocketComp->SetIsReplicated(true); RocketComp->RegisterComponent();
	}

	// Bind ASM lifecycle events
	OnPropBecomeActive.AddUniqueDynamic(this, &AColaPropActor::HandleOnBecomeActive);
	OnBeforeReturnToRestPosition.AddUniqueDynamic(this, &AColaPropActor::HandleOnBeforeReturnToRest);

	// Bind drop impact hit on PropMesh
	if (PropMesh)
	{
		PropMesh->SetNotifyRigidBodyCollision(true);
		PropMesh->OnComponentHit.AddUniqueDynamic(this, &AColaPropActor::HandleBottleComponentHit);
	}

	// Bind rocket projectile delegates
	if (RocketComp)
	{
		RocketComp->OnMovementStep.RemoveAll(this);
		RocketComp->OnMovementStep.AddUObject(this,&AColaPropActor::UpdateOriginalRocketDischarge);
		RocketComp->SetAllDebugEnabled(false);
		RocketComp->OnRocketHitTarget.AddUniqueDynamic(this, &AColaPropActor::HandleRocketHitTarget);
		RocketComp->OnRocketFuelDepleted.AddUniqueDynamic(this, &AColaPropActor::HandleRocketFuelDepleted);
		RocketComp->OnRocketSettled.AddUniqueDynamic(this, &AColaPropActor::HandleRocketSettled);
	}
}

void AColaPropActor::BeginPlay()
{
	Super::BeginPlay();

	if (RocketComp)
	{
		RocketComp->SetAllDebugEnabled(false);
	}

	USceneComponent* ActiveComp = SkeletalMeshComponent ? Cast<USceneComponent>(SkeletalMeshComponent) : Cast<USceneComponent>(PropMesh);
	if (ActiveComp)
	{
		PrevLocation = ActiveComp->GetComponentLocation();
		PrevRotation = ActiveComp->GetComponentRotation();
	}
	else
	{
		PrevLocation = GetActorLocation();
		PrevRotation = GetActorRotation();
	}

	PrevVelocity = FVector::ZeroVector;
	PrevGForce = 0.0f;
	WarmupFramesRemaining = 20;
	HeldGracePeriod = 0.5f;
	bIsFirstActiveFrame = true;
	bWasHeldLastFrame = bIsHeld;
}

void AColaPropActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Guard against ticking inactive pooled actors parked at Z = -100,000
	if (!bIsActive)
	{
		return;
	}
	if (HasAuthority() && UsesPlasticPressure()) UpdatePlasticPressure(DeltaSeconds);

	USceneComponent* MeshComp = SkeletalMeshComponent ? Cast<USceneComponent>(SkeletalMeshComponent) : Cast<USceneComponent>(PropMesh);
	const FVector CurLoc = MeshComp ? MeshComp->GetComponentLocation() : GetActorLocation();
	const FRotator CurRot = MeshComp ? MeshComp->GetComponentRotation() : GetActorRotation();

	// If the component is still parked near the pool reset position (Z < -50,000), ignore completely
	if (CurLoc.Z < -50000.0f)
	{
		PrevLocation = CurLoc;
		PrevRotation = CurRot;
		LinearVelocity = FVector::ZeroVector;
		AngularVelocity = FVector::ZeroVector;
		PrevVelocity = FVector::ZeroVector;
		PrevGForce = 0.0f;
		return;
	}

	// Detect when newly held
	if (bIsHeld && !bWasHeldLastFrame)
	{
		WarmupFramesRemaining = 20;
		HeldGracePeriod = 0.5f;
		PrevLocation = CurLoc;
		PrevRotation = CurRot;
		PrevVelocity = FVector::ZeroVector;
		LinearVelocity = FVector::ZeroVector;
		AngularVelocity = FVector::ZeroVector;
		PrevGForce = 0.0f;
	}
	bWasHeldLastFrame = bIsHeld;

	// Check if the bottle is held but still interpolating towards the player's hand (not yet attached)
	const bool bIsAttachedToParent = (GetAttachParentActor() != nullptr || (GetRootComponent() && GetRootComponent()->GetAttachParent() != nullptr));
	const bool bIsHeldInTransit = (bIsHeld || State == EPropState::Held) && !bIsAttachedToParent;
	if (bIsHeldInTransit || bIsInterpolatingToRest) LiquidMotion.bHasHistory=false;
	else AdvanceLiquidVisuals(DeltaSeconds, MeshComp ? MeshComp->GetComponentTransform() : GetActorTransform());

	// Warmup frames on promotion / activation / pickup:
	// While warming up, in held grace period, or interpolating to rest / transit:
	// Keep latching PrevLocation = CurLoc so no velocity is accumulated from movement to target!
	if (bIsFirstActiveFrame || WarmupFramesRemaining > 0 || HeldGracePeriod > 0.0f || bIsHeldInTransit || bIsInterpolatingToRest)
	{
		if (WarmupFramesRemaining > 0)
		{
			--WarmupFramesRemaining;
		}
		if (HeldGracePeriod > 0.0f)
		{
			HeldGracePeriod -= DeltaSeconds;
		}
		bIsFirstActiveFrame = false;

		PrevLocation = CurLoc;
		PrevRotation = CurRot;
		PrevVelocity = FVector::ZeroVector;
		LinearVelocity = FVector::ZeroVector;
		AngularVelocity = FVector::ZeroVector;
		PrevGForce = 0.0f;

		UpdateMorphTargets();
		UpdateDynamicMaterial(false);
		return;
	}

	// Detect any sudden teleport / position jump (> 200 cm in a single frame)
	// and re-latch position without creating a velocity spike
	const float DistMovedSq = FVector::DistSquared(CurLoc, PrevLocation);
	if (DistMovedSq > FMath::Square(200.0f) || PrevLocation.Z < -50000.0f)
	{
		PrevLocation = CurLoc;
		PrevRotation = CurRot;
		PrevVelocity = FVector::ZeroVector;
		LinearVelocity = FVector::ZeroVector;
		AngularVelocity = FVector::ZeroVector;
		PrevGForce = 0.0f;
		UpdateDynamicMaterial(false);
		return;
	}

	if (DeltaSeconds > 0.0001f)
	{
		// If simulating physics, read velocity directly from the physics body to avoid finite-difference spikes
		if (PropMesh && PropMesh->IsSimulatingPhysics())
		{
			LinearVelocity = PropMesh->GetComponentVelocity().GetClampedToMaxSize(3000.0f);
			AngularVelocity = PropMesh->GetPhysicsAngularVelocityInDegrees().GetClampedToMaxSize(720.0f);
		}
		else
		{
			const FVector CurVel = (CurLoc - PrevLocation) / DeltaSeconds;
			const FVector RotVel = FColaLiquidMotion::AngularVelocityBetween(PrevRotation.Quaternion(),CurRot.Quaternion(),DeltaSeconds)*(180.f/PI);
			LinearVelocity = CurVel.GetClampedToMaxSize(3000.0f);
			AngularVelocity = RotVel.GetClampedToMaxSize(720.0f);
		}

		// Server authoritative foam & agitation updates
		if (HasAuthority())
		{
			UpdateFoamPressure(DeltaSeconds);
		}

		// Update dynamic material parameters safely
		UpdateDynamicMaterial(true);
	}

	UpdateMorphTargets();

	PrevLocation = CurLoc;
	PrevRotation = CurRot;
	// Physical settling can finish before cosmetic inertia; retry only while an ASM is active.
	if (HasAuthority() && State==EPropState::AtRest && CanDemoteToISM() && PropManagerReference && ISID>0)
		PropManagerReference->SetInstanceToRest(ISID);
}

void AColaPropActor::AdvanceLiquidVisuals(float DeltaSeconds, const FTransform& Transform)
{
	if (DeltaSeconds <= SMALL_NUMBER) return;
	const bool bPhysics=PropMesh && PropMesh->IsSimulatingPhysics();
	const FVector Velocity=bPhysics ? PropMesh->GetPhysicsLinearVelocity()
		: (LiquidMotion.bHasHistory ? (Transform.GetLocation()-LiquidMotion.PreviousTransform.GetLocation())/DeltaSeconds : FVector::ZeroVector);
	const FVector Angular=bPhysics ? PropMesh->GetPhysicsAngularVelocityInRadians()
		: (LiquidMotion.bHasHistory ? FColaLiquidMotion::AngularVelocityBetween(LiquidMotion.PreviousTransform.GetRotation(),Transform.GetRotation(),DeltaSeconds) : FVector::ZeroVector);
	LiquidMotion.Advance(DeltaSeconds,Transform,Velocity,Angular,FMath::Clamp(GetContentLevel(),0.f,1.f),LiquidRadiusCm,LiquidHeightCm,LiquidResponseGain);
}

void AColaPropActor::OnRep_ColaLevel()
{
	UpdateMorphTargets();
	UpdateDynamicMaterial(false);
}

void AColaPropActor::OnRep_FoamPressure()
{
	UpdateMorphTargets();
}

bool AColaPropActor::OnPlayerGrab_Implementation(USceneComponent* AttachToParent, FName SocketName)
{
	WarmupFramesRemaining = 15;
	HeldGracePeriod = 0.5f;
	PrevVelocity = FVector::ZeroVector;
	LinearVelocity = FVector::ZeroVector;
	AngularVelocity = FVector::ZeroVector;
	PrevGForce = 0.0f;
	bWasHeldLastFrame = (State == EPropState::Held || bIsHeld);
	return Super::OnPlayerGrab_Implementation(AttachToParent, SocketName);
}

void AColaPropActor::UpdateFoamPressure(float DeltaSeconds)
{
	if (!HasAuthority()) return;
	if (DeltaSeconds <= 0.0001f)
	{
		return;
	}

	// Agitation from hand shaking ONLY applies while held by a player/character
	if (!bIsHeld && State != EPropState::Held)
	{
		if (UsesPlasticPressure()) return;
		if (FoamPressure > 0.0f)
		{
			FoamPressure = FMath::FInterpTo(FoamPressure, 0.0f, DeltaSeconds, FoamDecayRate);
		}
		return;
	}

	// Must be attached to player/character socket to calculate hand agitation
	const bool bIsAttachedToParent = (GetAttachParentActor() != nullptr || (GetRootComponent() && GetRootComponent()->GetAttachParent() != nullptr));
	if (!bIsAttachedToParent)
	{
		if (FoamPressure > 0.0f)
		{
			FoamPressure = FMath::FInterpTo(FoamPressure, 0.0f, DeltaSeconds, FoamDecayRate);
		}
		return;
	}

	// Skip agitation while warming up, in held grace period, or interpolating to rest
	if (WarmupFramesRemaining > 0 || HeldGracePeriod > 0.0f || bIsInterpolatingToRest)
	{
		return;
	}

	USceneComponent* MeshComp = SkeletalMeshComponent ? Cast<USceneComponent>(SkeletalMeshComponent) : Cast<USceneComponent>(PropMesh);
	const FVector CurLoc = MeshComp ? MeshComp->GetComponentLocation() : GetActorLocation();

	// Guard against teleport / reset position
	if (CurLoc.Z < -50000.0f || PrevLocation.Z < -50000.0f || FVector::DistSquared(CurLoc, PrevLocation) > FMath::Square(200.0f))
	{
		PrevLocation = CurLoc;
		PrevVelocity = FVector::ZeroVector;
		PrevGForce = 0.0f;
		return;
	}

	const FVector RawVel = (CurLoc - PrevLocation) / DeltaSeconds;
	// Clamp velocity to realistic hand shaking speeds (max 600 cm/s = 6 m/s) to filter out single-frame jumps
	const FVector CurVel = RawVel.GetClampedToMaxSize(600.0f);
	const FVector CurAccel = ((CurVel - PrevVelocity) / DeltaSeconds).GetClampedToMaxSize(5000.0f); // max ~5G
	const float CurGForce = CurAccel.Size() / 980.0f;
	const float DeltaJerk = FMath::Clamp(FMath::Abs(CurGForce - PrevGForce), 0.0f, 10.0f);

	PrevVelocity = CurVel;
	PrevGForce = CurGForce;

	if (DeltaJerk > AgitationJerkThreshold)
	{
		if (UsesPlasticPressure()) { AddAgitationPressure((DeltaJerk-AgitationJerkThreshold)*FMath::Max(0.f,ShakePressureGain)*DeltaSeconds); return; }
		const float SurgeMult = (ColaLevel > 1.0f) ? OverfillSurgeMultiplier : 1.0f;
		const float TargetFoam = FMath::Clamp(FoamPressure + DeltaJerk * 0.4f * SurgeMult, 0.0f, 10.0f);
		FoamPressure = FMath::FInterpTo(FoamPressure, TargetFoam, DeltaSeconds, 8.0f);
	}
	else
	{
		if (UsesPlasticPressure()) return;
		FoamPressure = FMath::FInterpTo(FoamPressure, 0.0f, DeltaSeconds, FoamDecayRate);
	}

	// Check stability and launch rocket if tolerance exceeded while held
	const float FoamRatio = FMath::GetMappedRangeValueClamped(FVector2D(0.0f,10.0f),FVector2D(0.0f,1.0f),FoamPressure);
	const float TotalVolume=GetContentLevel()+FoamRatio;

	if (TotalVolume >= StabilityTolerance)
	{
		LaunchBottle();
	}
}

void AColaPropActor::FillBottle(float FillStrength)
{
	FillBottleAtRate(FillStrength, 1.0f);
}

void AColaPropActor::LaunchBottle()
{
	if (!HasAuthority()) return;
	if (UsesPlasticPressure()) { StartPlasticPressureRelease(); return; }
	if (!RocketComp)
	{
		RocketComp = FindComponentByClass<URocketProjectileComponent>();
	}

	if (!RocketComp)
	{
		return;
	}

	const ERocketSimulationState SimState = RocketComp->GetSimulationState();
	if (SimState != ERocketSimulationState::Idle && SimState != ERocketSimulationState::AtRest)
	{
		return;
	}

	// Eject cap with vertical physics impulse
	RemoveCap(1.0f, ECapRemovalOperation::EjectPhysics);
	FoamPressure = 0.0f;
	LaunchOriginalBottleRocket();
}

bool AColaPropActor::LaunchOriginalBottleRocket(bool bHighAccuracy)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !RocketComp || !PropMesh || GetRootComponent()!=PropMesh
		|| (RocketComp->GetSimulationState()!=ERocketSimulationState::Idle && RocketComp->GetSimulationState()!=ERocketSimulationState::AtRest)) return false;
	// Move the canonical ASM root; a visual child alone would leave collision and the ISID pose behind.
	RocketComp->TargetPrimitive=PropMesh;

	// Erratic flight trajectory configuration
	RocketComp->FlightSettings.bEnableErraticPath = true;
	RocketComp->FlightSettings.ZigZagMode = ERocketZigZagMode::Random3D;
	RocketComp->FlightSettings.bEnable3DZigZag = true;
	RocketComp->FlightSettings.bUseRandomWaypointSpacing = true;
	RocketComp->FlightSettings.MinWaypointSpacing = 350.0f;
	RocketComp->FlightSettings.MaxWaypointSpacing = 650.0f;
	RocketComp->FlightSettings.MinPathDeviation = 35.0f;
	RocketComp->FlightSettings.MaxPathDeviation = 90.0f;
	RocketComp->SetMaxFlightBounces(8);
	RocketComp->SetFixedFallingBounces(4);

	const int32 Seed=RocketRandomSeed>0 ? RocketRandomSeed : FMath::RandRange(1,MAX_int32);
	FRandomStream Random(Seed);
	auto Range=[&](float A,float B,float Fallback)
	{
		A=FMath::IsFinite(A) ? FMath::Max(1.f,A) : Fallback;
		B=FMath::IsFinite(B) ? FMath::Max(1.f,B) : Fallback;
		return Random.FRandRange(FMath::Min(A,B),FMath::Max(A,B));
	};
	RocketLaunchConfiguration.Seed=Seed;
	RocketLaunchConfiguration.Distance=Range(RocketMinDistance,RocketMaxDistance,5000.f);
	RocketLaunchConfiguration.Speed=Range(RocketMinSpeed,RocketMaxSpeed,3000.f);
	RocketLaunchConfiguration.Acceleration=FMath::IsFinite(RocketAcceleration) ? FMath::Clamp(RocketAcceleration,0.f,50000.f) : 1600.f;
	const float Variation=FMath::IsFinite(RocketDirectionVariation) ? FMath::Clamp(RocketDirectionVariation,0.f,89.f) : 25.f;
	RocketLaunchConfiguration.Direction=Random.VRandCone(GetActorUpVector(),FMath::DegreesToRadians(Variation));
	if (UsesPlasticPressure()) RocketComp->ConfigureBottleProjectile(RocketLaunchConfiguration.Acceleration);
	const uint8 PathSeed=static_cast<uint8>(Random.RandRange(1,254));
	RocketComp->LaunchRocket(RocketLaunchConfiguration.Direction,bHighAccuracy,PathSeed,RocketLaunchConfiguration.Distance,RocketLaunchConfiguration.Speed);
	return RocketComp->IsPoweredFlight();
}

void AColaPropActor::AddCap(int32 InProductID)
{
	if (!HasAuthority()) return;
	if (LocalUniqueData.EventData.AttachedCapISID>0) { if (PropManagerReference) PropManagerReference->SynchronizeCapAssembly(ISID); return; }
	if (CapMeshComponent && CapMeshComponent->IsAttachedTo(GetRootComponent()))
	{
		return;
	}

	if (!CapMeshComponent)
	{
		CapMeshComponent = NewObject<UStaticMeshComponent>(this, TEXT("BottleCap"));
		CapMeshComponent->RegisterComponent();
	}

	USceneComponent* AttachTarget = SkeletalMeshComponent ? Cast<USceneComponent>(SkeletalMeshComponent) : Cast<USceneComponent>(PropMesh);
	if (AttachTarget && CapMeshComponent)
	{
		CapMeshComponent->AttachToComponent(AttachTarget, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		CapMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 170.0f));
		CapMeshComponent->SetRelativeRotation(FRotator::ZeroRotator);
		CapMeshComponent->SetSimulatePhysics(false);
		CapMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CapMeshComponent->SetVisibility(true);
	}
}

void AColaPropActor::RemoveCap(float RemovingStrength, ECapRemovalOperation Operation)
{
	if (!HasAuthority()) return;
	if (LocalUniqueData.EventData.AttachedCapISID>0 && PropManagerReference)
	{
		PropManagerReference->DetachPhysicalCap(ISID,Operation==ECapRemovalOperation::EjectPhysics ? GetActorUpVector()*FMath::Max(1.f,RemovingStrength)*1500.f : FVector::ZeroVector);
		return;
	}
	LocalUniqueData.EventData.CapSealed = 0; LocalUniqueData.EventData.CapType = 0;
	EmitPropEventUpdate(EPropEventDirtyMask::CapSealed | EPropEventDirtyMask::CapType);
	if (!CapMeshComponent || !CapMeshComponent->IsAttachedTo(GetRootComponent()))
	{
		return;
	}

	CapMeshComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

	if (Operation == ECapRemovalOperation::EjectPhysics)
	{
		CapMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		CapMeshComponent->SetSimulatePhysics(true);

		const FVector UpVec = CapMeshComponent->GetUpVector();
		const float Strength = FMath::Max(1.0f, RemovingStrength) * 1500.0f;
		CapMeshComponent->AddImpulse(UpVec * Strength, NAME_None, true);

		// Clean up detached cap after 5 seconds
		TWeakObjectPtr<UStaticMeshComponent> Debris = CapMeshComponent;
		CapMeshComponent = nullptr;
		GetWorldTimerManager().SetTimer(CapDebrisTimer, [Debris]()
		{
			if (Debris.IsValid()) Debris->DestroyComponent();
		}, 5.0f, false);
	}
	else
	{
		CapMeshComponent->DestroyComponent();
		CapMeshComponent = nullptr;
	}
}

float AColaPropActor::CalculateUnderfillPenalty(float TargetFill) const
{
	const FColaPriceQuote Quote = GetSaleQuote();
	const FPropContentDefinition* Content = ResolveContentDefinition();
	const float FullPrice = Content ? Content->BasePortionPrice * GetNumPortions() : BasePrice;
	return FMath::Max(0.0f, FullPrice - static_cast<float>(Quote.SaleCents / 100.0));
}

float AColaPropActor::CalculateSalePayout(float TargetFill) const
{
	return static_cast<float>(GetSaleQuote().SaleCents / 100.0);
}

void AColaPropActor::UpdateDynamicMaterial(bool bUpdateVelocity)
{
	USkeletalMeshComponent* SkelComp = SkeletalMeshComponent;
	UMeshComponent* LiquidMesh=SkelComp && SkelComp->GetSkeletalMeshAsset() ? static_cast<UMeshComponent*>(SkelComp) : static_cast<UMeshComponent*>(PropMesh.Get());
	if (LiquidMesh && LiquidMesh->GetNumMaterials() > 1)
	{
		UMaterialInterface* CurrentMat = LiquidMesh->GetMaterial(1);
		if (!DynamicMaterialInstance || LiquidMesh->GetMaterial(1) != DynamicMaterialInstance)
		{
			if (CurrentMat)
			{
				DynamicMaterialInstance = LiquidMesh->CreateDynamicMaterialInstance(1, CurrentMat);
				if (DynamicMaterialInstance)
				{
					DynamicMaterialInstance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Liquid Container Height")),LiquidHeightCm);
					DynamicMaterialInstance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Liquid Container Radius")),LiquidRadiusCm);
					float SloshStrength=.01f;
					DynamicMaterialInstance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("SloshStrength")),SloshStrength);
					LiquidResponseGain=FMath::Clamp(SloshStrength/.01f,0.f,4.f);
					LiquidHeightCm=FMath::Max(1.f,LiquidHeightCm); LiquidRadiusCm=FMath::Max(1.f,LiquidRadiusCm);
					LastLiquidFill=-1;
				}
			}
		}
	}

	if (DynamicMaterialInstance)
	{
		const FVector NormalWS=LiquidMotion.SurfaceNormal();
		const FTransform VisualTransform=LiquidMesh ? LiquidMesh->GetComponentTransform() : GetActorTransform();
		const FVector NormalLocal=VisualTransform.InverseTransformVectorNoScale(NormalWS).GetSafeNormal();
		const float Fill=FMath::Clamp(GetContentLevel(),0.f,1.f);
		if (!NormalLocal.Equals(LastLiquidLocalNormal,.001) || !FMath::IsNearlyEqual(Fill,LastLiquidFill,.0001f))
		{
			LiquidPlaneOffset=FColaLiquidMotion::CylinderPlaneOffset(Fill,NormalLocal,LiquidHeightCm*.5f,LiquidRadiusCm);
			LastLiquidLocalNormal=NormalLocal; LastLiquidFill=Fill;
		}
		DynamicMaterialInstance->SetScalarParameterValue(TEXT("Native Liquid Motion"),1);
		DynamicMaterialInstance->SetScalarParameterValue(TEXT("Use Instance Liquid Data"),0);
		DynamicMaterialInstance->SetScalarParameterValue(TEXT("Liquid Cached Fill"),Fill);
		DynamicMaterialInstance->SetVectorParameterValue(TEXT("Liquid Surface Normal WS"),FLinearColor(NormalWS));
		DynamicMaterialInstance->SetVectorParameterValue(TEXT("Liquid Motion State"),FLinearColor(LiquidMotion.WaveEnergy,LiquidMotion.FluidSpin,float(ISID%997)*.173f,0));
		DynamicMaterialInstance->SetScalarParameterValue(TEXT("Liquid Plane Offset"),LiquidPlaneOffset);
		if (bUpdateVelocity)
		{
			DynamicMaterialInstance->SetVectorParameterValue(FName(TEXT("Object Velocity")), FLinearColor(LinearVelocity));
			DynamicMaterialInstance->SetVectorParameterValue(FName(TEXT("Object Angular Velocity")), FLinearColor(AngularVelocity));
		}
		else
		{
			DynamicMaterialInstance->SetVectorParameterValue(FName(TEXT("Object Velocity")), FLinearColor::Black);
			DynamicMaterialInstance->SetVectorParameterValue(FName(TEXT("Object Angular Velocity")), FLinearColor::Black);
		}

		DynamicMaterialInstance->SetScalarParameterValue(FName(TEXT("Use Dynamic Values")), 1.0f);
		ColaLevel=GetContentLevel(); // Compatibility mirror must never drive canonical content.
		DynamicMaterialInstance->SetScalarParameterValue(FName(TEXT("Liquid Level")), Fill);
	}
}

void AColaPropActor::UpdateMorphTargets()
{
	if (!SkeletalMeshComponent)
	{
		return;
	}

	const float FoamRatio=UsesPlasticPressure() ? 0.f : FMath::GetMappedRangeValueClamped(FVector2D(0.f,10.f),FVector2D(0.f,1.f),FoamPressure);
	const float TotalVolume=GetContentLevel()+FoamRatio;
	// Resting carbonation should not keep every stocked bottle in an active ASM.
	// Expansion measures actual pressure above its normal baseline toward tolerance.
	const float RestPressure=FMath::Max(0.f,GetBottlePressure()-LocalUniqueData.ContinuousData.SealedPressure);
	const float Ratio=UsesPlasticPressure() ? LocalUniqueData.ContinuousData.SealedPressure/FMath::Max(UE_SMALL_NUMBER,GetBottlePressureTolerance()-RestPressure) : 0;
	const float Expansion=FMath::SmoothStep(.15f,1.f,Ratio)*FMath::Clamp(PressureMorphScale,0.f,1.f);
	const float Step=GetWorld() ? FMath::Clamp(GetWorld()->GetDeltaSeconds(),0.f,.1f) : 0;
	SmoothedPressureExpansion=bMorphInitialized ? FMath::FInterpTo(SmoothedPressureExpansion,Expansion,Step,PressureMorphInterpolationSpeed) : Expansion;

	const float TargetEmpty = FMath::Clamp(1.0f - TotalVolume, 0.0f, 1.0f);
	const float TargetOverfull = FMath::Max(FMath::Clamp(TotalVolume-1.f,0.f,1.f),SmoothedPressureExpansion);

	if (!bMorphInitialized)
	{
		// Hard-snap to target during warmup so there is no residual shape from the pool
		SkeletalMeshComponent->SetMorphTarget(FName(TEXT("EmptyLevel")), TargetEmpty);
		SkeletalMeshComponent->SetMorphTarget(FName(TEXT("OverfullLevel")), TargetOverfull);
		bMorphInitialized=true;
	}
	else
	{
		// Smoothly interpolate during active gameplay to eliminate any sudden jucking/popping
		const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
		const float CurEmpty = SkeletalMeshComponent->GetMorphTarget(FName(TEXT("EmptyLevel")));
		const float CurOverfull = SkeletalMeshComponent->GetMorphTarget(FName(TEXT("OverfullLevel")));
		const float NewEmpty = FMath::FInterpTo(CurEmpty, TargetEmpty, DeltaTime, 12.0f);
		const float NewOverfull = FMath::FInterpTo(CurOverfull, TargetOverfull, DeltaTime, 12.0f);

		SkeletalMeshComponent->SetMorphTarget(FName(TEXT("EmptyLevel")), NewEmpty);
		SkeletalMeshComponent->SetMorphTarget(FName(TEXT("OverfullLevel")), NewOverfull);
	}
}

void AColaPropActor::HandleOnBecomeActive(AActivePropActor* Actor, int32 InISID, int32 InProductID, FTransform ActiveTransform)
{
	if (!RocketComp)
	{
		RocketComp = FindComponentByClass<URocketProjectileComponent>();
	}

	if (RocketComp)
	{
		if (HasAuthority()) RocketComp->ResetRocketSimulation();
		else RocketComp->ReapplyReplicatedFlightState();
		RocketComp->bLaunchOnInitialized = false;
	}

	// Restore exact authoritative fill
	if (bHasValidUniqueData) ColaLevel = LocalUniqueData.EventData.ContentLevel;
	else if (NumOfPortions > 0)
	{
		ColaLevel = FMath::Clamp(static_cast<float>(RemainingPortions) / static_cast<float>(NumOfPortions), 0.0f, 2.0f);
	}
	else
	{
		ColaLevel = 1.0f;
	}

	if (GetCapSealed() > 0)
	{
		AddCap(InProductID);
	}

	// Zero out foam, velocity and agitation state completely upon promotion
	FoamPressure = 0.0f;
	WarmupFramesRemaining = 20;
	HeldGracePeriod = 0.5f;
	bIsFirstActiveFrame = true;
	PrevLocation = ActiveTransform.GetLocation();
	PrevRotation = ActiveTransform.GetRotation().Rotator();
	PrevVelocity = FVector::ZeroVector;
	LinearVelocity = FVector::ZeroVector;
	AngularVelocity = FVector::ZeroVector;
	PrevGForce = 0.0f;
	bWasHeldLastFrame = (State == EPropState::Held || bIsHeld);

	// Invalidate dynamic material pointer so it rebinds cleanly to current mesh
	DynamicMaterialInstance = nullptr;
	UpdateDynamicMaterial(false);
	UpdateMorphTargets();

	// Base activation enables actor collision before applying the requested physics state.
	// This notification runs earlier; starting Chaos here would simulate a collision-disabled body.
}

void AColaPropActor::HandleOnBeforeReturnToRest(AActivePropActor* Actor, int32 InISID, int32 InProductID, FTransform RestTransform)
{
	// Persist canonical contents, never the legacy visual mirror (which may be stale).
	ColaLevel=GetContentLevel();
	RemainingPortions = GetRemainingPortions();

	FoamPressure = 0.0f;
	WarmupFramesRemaining = 0;
	HeldGracePeriod = 0.0f;
	LinearVelocity = FVector::ZeroVector;
	AngularVelocity = FVector::ZeroVector;
	PrevVelocity = FVector::ZeroVector;
	PrevGForce = 0.0f;
	DynamicMaterialInstance = nullptr;

	if (SkeletalMeshComponent)
	{
		SkeletalMeshComponent->SetMorphTarget(FName(TEXT("EmptyLevel")), 0.0f);
		SkeletalMeshComponent->SetMorphTarget(FName(TEXT("OverfullLevel")), 0.0f);
	}
}

void AColaPropActor::HandleBottleComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bIsActive || bIsFirstActiveFrame || WarmupFramesRemaining > 0 || HeldGracePeriod > 0.0f)
	{
		return;
	}
	// Attached-body contacts with its own carrier are transport artifacts, not external damage.
	// Genuine promotion impacts use ApplyInitialImpactFromPromotion and intentionally bypass warmup.
	AActor* Carrier = InteractionOwner.Get();
	if (bIsHeld && IsValid(Carrier) && IsValid(OtherActor)
		&& (OtherActor == Carrier || OtherActor->IsAttachedTo(Carrier) || Carrier->IsAttachedTo(OtherActor))) return;
	if (HasAuthority() && bDebugBottleImpacts && NormalImpulse.Size()>DropImpactThreshold)
		UE_LOG(LogTemp,Display,TEXT("BOTTLE_IMPACT_TRACE id=%d other=%s component=%s impulse=%.3f mass=%.3f held=%d"),
			ISID,*GetNameSafe(OtherActor),*GetNameSafe(OtherComp),NormalImpulse.Size(),PropMesh ? PropMesh->BodyInstance.GetBodyMass() : 0.f,bIsHeld ? 1 : 0);
	const FVector CalibratedImpulse = NormalizePhysicalContactImpulse(NormalImpulse);
	if (HasAuthority()) ApplyBottleImpact(CalibratedImpulse, 0);
	if (UsesPlasticPressure()) return;

	const float ImpulseLen = CalibratedImpulse.Size();
	if (ImpulseLen > DropImpactThreshold && HasAuthority())
	{
		const float OverfillMult = (ColaLevel > 1.0f) ? OverfillSurgeMultiplier : 1.0f;
		const float ExcessImpulse = ImpulseLen - DropImpactThreshold;
		const float Surge = ExcessImpulse * 0.0004f * OverfillMult;
		FoamPressure = FMath::Clamp(FoamPressure + Surge, 0.0f, 10.0f);
		UpdateMorphTargets();

		const float FoamRatio = FMath::GetMappedRangeValueClamped(FVector2D(0.0f, 10.0f), FVector2D(0.0f, 1.0f), FoamPressure);
		if ((ColaLevel + FoamRatio) >= StabilityTolerance)
		{
			LaunchBottle();
		}
	}
}

void AColaPropActor::HandleRocketHitTarget(AActor* HitActor, const FHitResult& HitResult)
{
	if (HasAuthority() && HitActor && RocketComp) ApplyBottleImpact(NormalizePhysicalContactImpulse(-HitResult.ImpactNormal * LinearVelocity.Size() * (PropMesh ? PropMesh->BodyInstance.GetBodyMass() : 1.0f)), 0);
}

void AColaPropActor::HandleRocketFuelDepleted()
{
	if (PhysicsSimComponent)
	{
		PhysicsSimComponent->RetriggerUnsettledTimer(1.0f);
	}
}

void AColaPropActor::HandleRocketSettled()
{
	SetInMotion(false);
	if (PhysicsSimComponent) PhysicsSimComponent->SetStateUnsettled(0.1f);
	if (PropMesh && bIsActive && PropMesh->IsSimulatingPhysics())
	{
		const auto* Def=ResolveProductDefinition();
		PropMesh->SetEnableGravity(!Def || Def->PhysicsProfile.bEnableGravity);
	}
}
