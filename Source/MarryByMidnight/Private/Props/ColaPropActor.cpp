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

void AColaPropActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AColaPropActor, AcceptedFailureType);
	DOREPLIFETIME(AColaPropActor, InteractionPressure);

	DOREPLIFETIME(AColaPropActor, ColaLevel);
	DOREPLIFETIME(AColaPropActor, FoamPressure);
}

void AColaPropActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (!RocketComp)
	{
		RocketComp = FindComponentByClass<URocketProjectileComponent>();
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
			const FVector RotVel = (CurRot - PrevRotation).Euler() / DeltaSeconds;
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
	if (DeltaSeconds <= 0.0001f)
	{
		return;
	}

	// Agitation from hand shaking ONLY applies while held by a player/character
	if (!bIsHeld && State != EPropState::Held)
	{
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
		const float SurgeMult = (ColaLevel > 1.0f) ? OverfillSurgeMultiplier : 1.0f;
		const float TargetFoam = FMath::Clamp(FoamPressure + DeltaJerk * 0.4f * SurgeMult, 0.0f, 10.0f);
		FoamPressure = FMath::FInterpTo(FoamPressure, TargetFoam, DeltaSeconds, 8.0f);
	}
	else
	{
		FoamPressure = FMath::FInterpTo(FoamPressure, 0.0f, DeltaSeconds, FoamDecayRate);
	}

	// Check stability and launch rocket if tolerance exceeded while held
	const float FoamRatio = FMath::GetMappedRangeValueClamped(FVector2D(0.0f, 10.0f), FVector2D(0.0f, 1.0f), FoamPressure);
	const float TotalVolume = ColaLevel + FoamRatio;

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

	const FVector UpVector = SkeletalMeshComponent ? SkeletalMeshComponent->GetUpVector() : GetActorUpVector();
	const FVector LaunchDir = FMath::VRandCone(UpVector, FMath::DegreesToRadians(25.0f));
	const float LaunchSpeed = FMath::Clamp(ColaLevel * 3500.0f, 1500.0f, 4500.0f);
	const float LaunchDistance = FMath::Clamp(ColaLevel * 6000.0f, 2000.0f, 8000.0f);
	const uint8 RandomSeed = static_cast<uint8>(FMath::RandRange(1, 254));

	RocketComp->LaunchRocket(LaunchDir, false, RandomSeed, LaunchDistance, LaunchSpeed);
}

void AColaPropActor::AddCap(int32 InProductID)
{
	if (!HasAuthority()) return;
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
	if (SkelComp && SkelComp->GetNumMaterials() > 1)
	{
		UMaterialInterface* CurrentMat = SkelComp->GetMaterial(1);
		if (!DynamicMaterialInstance || SkelComp->GetMaterial(1) != DynamicMaterialInstance)
		{
			if (CurrentMat)
			{
				DynamicMaterialInstance = SkelComp->CreateDynamicMaterialInstance(1, CurrentMat);
			}
		}
	}

	if (DynamicMaterialInstance)
	{
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
		DynamicMaterialInstance->SetScalarParameterValue(FName(TEXT("Liquid Level")), ColaLevel);
	}
}

void AColaPropActor::UpdateMorphTargets()
{
	if (!SkeletalMeshComponent)
	{
		return;
	}

	const float FoamRatio = FMath::GetMappedRangeValueClamped(FVector2D(0.0f, 10.0f), FVector2D(0.0f, 1.0f), FoamPressure);
	const float TotalVolume = ColaLevel + FoamRatio;

	const float TargetEmpty = FMath::Clamp(1.0f - TotalVolume, 0.0f, 1.0f);
	const float TargetOverfull = FMath::Clamp(TotalVolume - 1.0f, 0.0f, 1.0f);

	if (bIsFirstActiveFrame || WarmupFramesRemaining > 0)
	{
		// Hard-snap to target during warmup so there is no residual shape from the pool
		SkeletalMeshComponent->SetMorphTarget(FName(TEXT("EmptyLevel")), TargetEmpty);
		SkeletalMeshComponent->SetMorphTarget(FName(TEXT("OverfullLevel")), TargetOverfull);
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
		RocketComp->ResetRocketSimulation();
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

	// Ensure physics is engaged if not held so mid-air bottles fall under gravity
	if (State != EPropState::Held)
	{
		if (PropMesh)
		{
			PropMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			PropMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			PropMesh->SetNotifyRigidBodyCollision(true);
			PropMesh->SetSimulatePhysics(true);
		}
		if (PhysicsSimComponent)
		{
			PhysicsSimComponent->SetStateUnsettled(3.0f);
		}
	}
}

void AColaPropActor::HandleOnBeforeReturnToRest(AActivePropActor* Actor, int32 InISID, int32 InProductID, FTransform RestTransform)
{
	// Write back remaining portions based on final ColaLevel
	if (NumOfPortions > 0)
	{
		LocalUniqueData.EventData.ContentLevel = ColaLevel;
		RemainingPortions = GetRemainingPortions();
	}

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
	if (HasAuthority() && bIsActive) ApplyBottleImpact(NormalImpulse, 0);
	// Ignore visual agitation during pickup warmup; structural impact is handled above.
	if (!bIsActive || bIsFirstActiveFrame || WarmupFramesRemaining > 0 || HeldGracePeriod > 0.0f)
	{
		return;
	}

	const float ImpulseLen = NormalImpulse.Size();
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
	if (HasAuthority() && HitActor && RocketComp) ApplyBottleImpact(-HitResult.ImpactNormal * LinearVelocity.Size() * (PropMesh ? PropMesh->GetMass() : 1.0f), 0);
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
}
