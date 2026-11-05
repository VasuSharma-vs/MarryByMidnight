#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropManager.h"
#include "Components/RocketProjectileComponent.h"

UStaticMeshPhysicsSimulationComponent::UStaticMeshPhysicsSimulationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.05f; // Check settling velocity at 20Hz
	SetIsReplicatedByDefault(true);
}

void UStaticMeshPhysicsSimulationComponent::BeginPlay()
{
	Super::BeginPlay();

	EnsureTargetMeshResolved();

	if (TargetMesh)
	{
		// Store initial default collision profile on BeginPlay
		DefaultCollisionProfile = TargetMesh->GetCollisionProfileName();

		// Bind hit events so colliding with objects knocks this prop into Unsettled state
		if (bUnsettleOnHit)
		{
			TargetMesh->SetNotifyRigidBodyCollision(true);
			TargetMesh->OnComponentHit.AddDynamic(this, &UStaticMeshPhysicsSimulationComponent::OnTargetMeshHit);
		}
	}

	AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		ApplyState(InitialState);
	}
}

void UStaticMeshPhysicsSimulationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (TargetMesh)
	{
		TargetMesh->OnComponentHit.RemoveDynamic(this, &UStaticMeshPhysicsSimulationComponent::OnTargetMeshHit);
	}

	if (APropManager* Manager = APropManager::Get(this))
	{
		Manager->UnregisterSettlingComponent(this);
	}

	Super::EndPlay(EndPlayReason);
}

void UStaticMeshPhysicsSimulationComponent::OnTargetMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bUnsettleOnHit) return;
	if (IsHeldOrInMotion()) return;

	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority()) return;
	if (const AActivePropActor* Prop = Cast<AActivePropActor>(Owner))
	{
		if (!Prop->bIsActive || Prop->bIsHeld || Prop->bIsInMotion) return;
	}

	// Ignore passive static floor/world geometry unless hit by a Pawn, a moving physics body, or receiving an impact impulse
	const bool bHitByPawn = (OtherActor && OtherActor->IsA<APawn>());
	const bool bHitByMovingPhysics = (OtherComp && OtherComp->IsSimulatingPhysics() && OtherComp->GetComponentVelocity().SizeSquared() > FMath::Square(VelocityTolerance));
	const bool bHasImpulse = (NormalImpulse.SizeSquared() >= FMath::Square(HitImpulseThreshold));

	if (!bHitByPawn && !bHitByMovingPhysics && !bHasImpulse)
	{
		return;
	}

	const float UnsettledDuration = (DefaultUnsettledDelay > 0.0f) ? DefaultUnsettledDelay : 3.0f;

	// 1. If currently in Unsettled state: Retrigger the delay timer back to 3.0s on repeated hits
	if (CurrentState == EPhysicsSimulationState::Unsettled)
	{
		RetriggerUnsettledTimer(UnsettledDuration);
		return;
	}

	// 2. If in Settling or AtRest: Transition to Unsettled with fresh 3.0s delay
	if (CurrentState == EPhysicsSimulationState::Settling || CurrentState == EPhysicsSimulationState::AtRest)
	{
		SetStateUnsettled(UnsettledDuration);
	}
}

float UStaticMeshPhysicsSimulationComponent::GetLinearSpeed() const
{
	return TargetMesh ? TargetMesh->GetComponentVelocity().Size() : 0.0f;
}

float UStaticMeshPhysicsSimulationComponent::GetAngularSpeed() const
{
	if (!TargetMesh) return 0.0f;
	return TargetMesh->GetPhysicsAngularVelocityInDegrees().Size();
}

bool UStaticMeshPhysicsSimulationComponent::CheckSettled(float CustomLinearTolerance, float CustomAngularTolerance) const
{
	if (AActor* OwnerActor = GetOwner())
	{
		if (URocketProjectileComponent* RocketComp = OwnerActor->FindComponentByClass<URocketProjectileComponent>())
		{
			if (RocketComp->IsActive() &&
				RocketComp->GetSimulationState() != ERocketSimulationState::AtRest &&
				RocketComp->GetSimulationState() != ERocketSimulationState::Idle)
			{
				return false;
			}
		}
	}

	if (!TargetMesh)
	{
		return true;
	}
	if (!TargetMesh->IsSimulatingPhysics())
	{
		return true;
	}
	const float LinTol = (CustomLinearTolerance > 0.0f) ? CustomLinearTolerance : ActiveVelocityTolerance;
	const float AngTol = (CustomAngularTolerance > 0.0f) ? CustomAngularTolerance : AngularVelocityTolerance;
	const bool bStationary = (GetLinearSpeed() <= LinTol) && (GetAngularSpeed() <= AngTol);
	if (!bStationary)
	{
		return false;
	}

	// Verify the object is in contact with or immediately above a physical surface (not hanging in mid-air!)
	if (UWorld* World = GetWorld())
	{
		FHitResult GroundHit;
		FCollisionQueryParams QP(SCENE_QUERY_STAT(StaticMeshPhysicsCheckSettledSurface), false, GetOwner());
		const FVector BoundsOrigin = TargetMesh->Bounds.Origin;
		const float BoxExtentZ = TargetMesh->Bounds.BoxExtent.Z;
		const FVector TraceStart = BoundsOrigin;
		const FVector TraceEnd = BoundsOrigin - FVector(0.0f, 0.0f, BoxExtentZ + 35.0f);

		const bool bHasSurface = World->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_WorldStatic, QP)
			|| World->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_WorldDynamic, QP);

		if (!bHasSurface)
		{
			// Body is floating or falling in mid-air - keep physics active and awake!
			TargetMesh->WakeAllRigidBodies();
			return false;
		}
	}

	return true;
}

void UStaticMeshPhysicsSimulationComponent::EnsureTargetMeshResolved()
{
	if (!TargetMesh)
	{
		AActor* Owner = GetOwner();
		if (Owner)
		{
			TargetMesh = Owner->FindComponentByClass<UStaticMeshComponent>();
			if (!TargetMesh)
			{
				TargetMesh = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
			}
		}
	}
}

void UStaticMeshPhysicsSimulationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	if (const AActivePropActor* Prop = Cast<AActivePropActor>(Owner))
	{
		if (!Prop->bIsActive || Prop->bIsHeld || Prop->bIsInMotion)
		{
			return;
		}
	}

	if (URocketProjectileComponent* RocketComp = Owner->FindComponentByClass<URocketProjectileComponent>())
	{
		if (RocketComp->IsActive() &&
			RocketComp->GetSimulationState() != ERocketSimulationState::AtRest &&
			RocketComp->GetSimulationState() != ERocketSimulationState::Idle)
		{
			return;
		}
	}

	if (bIsInMotion || CurrentState == EPhysicsSimulationState::AtRest || CurrentState == EPhysicsSimulationState::Held)
	{
		return;
	}

	EnsureTargetMeshResolved();
	if (!TargetMesh)
	{
		return;
	}

	StateElapsedTime += DeltaTime;
	const bool bIsCurrentlyStationary = CheckSettled();
	if (bIsCurrentlyStationary)
	{
		StationaryTimeAccumulated += DeltaTime;
	}
	else
	{
		StationaryTimeAccumulated = 0.0f;
	}

	if (CurrentState == EPhysicsSimulationState::InMotion)
	{
		// InMotion: when bIsInMotion is false, physics motion transitions to Unsettled state
		SetStateUnsettled();
		return;
	}
	else if (CurrentState == EPhysicsSimulationState::Unsettled)
	{
		const float EffectiveDelay = (ActiveUnsettledDelay > 0.0f) ? ActiveUnsettledDelay : (DefaultUnsettledDelay > 0.0f ? DefaultUnsettledDelay : 3.0f);
		// Unsettled: transitions to Settling only when the delay duration (default 3.0s) has completed and timer is done
		if (StateElapsedTime >= EffectiveDelay && !IsUnsettledTimerActive())
		{
			SetStateSettling();
		}
	}
	else if (CurrentState == EPhysicsSimulationState::Settling)
	{
		// Settling: checks rotation and location velocity against tolerance; moves to AtRest when within tolerance
		if (bIsCurrentlyStationary)
		{
			SetStateAtRest();
		}
	}
}

void UStaticMeshPhysicsSimulationComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UStaticMeshPhysicsSimulationComponent, CurrentState);
	DOREPLIFETIME(UStaticMeshPhysicsSimulationComponent, bIsGrabbable);
	DOREPLIFETIME(UStaticMeshPhysicsSimulationComponent, bIsInMotion);
}

void UStaticMeshPhysicsSimulationComponent::OnRep_IsInMotion()
{
	if (bIsInMotion && TargetMesh)
	{
		TargetMesh->SetSimulatePhysics(true);
		TargetMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
	}
}

void UStaticMeshPhysicsSimulationComponent::OnRep_CurrentState()
{
	if (AActivePropActor* Prop = Cast<AActivePropActor>(GetOwner()))
	{
		if (!Prop->bIsActive || Prop->ISID <= 0)
		{
			return;
		}
		if (!Prop->GetActorEnableCollision() || Prop->IsHidden() || (Prop->PropMesh && !Prop->PropMesh->GetStaticMesh()) || Prop->GetActorLocation().Z < -50000.0f || Prop->GetActorScale3D().GetMax() < 0.005f)
		{
			Prop->OnRep_IsActive();
		}
	}
	ApplyState(CurrentState);
}

void UStaticMeshPhysicsSimulationComponent::OnRep_IsGrabbable()
{
	SetActorGrabbableValue(bIsGrabbable);
}

FBoolProperty* UStaticMeshPhysicsSimulationComponent::FindActorGrabbableProperty() const
{
	AActor* Owner = GetOwner();
	if (!Owner) return nullptr;

	UClass* OwnerClass = Owner->GetClass();
	static const TArray<FName> PotentialNames = {
		FName(TEXT("bIsGrabbable")),
		FName(TEXT("bIsGrabable")),
		FName(TEXT("IsGrabbable")),
		FName(TEXT("IsGrabable")),
		FName(TEXT("is_grabbable")),
		FName(TEXT("is_grabable")),
		FName(TEXT("is grabable")),
		FName(TEXT("is grabbable"))
	};

	for (const FName& PropName : PotentialNames)
	{
		if (FProperty* Prop = OwnerClass->FindPropertyByName(PropName))
		{
			if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
			{
				return BoolProp;
			}
		}
	}

	return nullptr;
}

bool UStaticMeshPhysicsSimulationComponent::GetActorGrabbableValue(bool& bOutHasProperty) const
{
	bOutHasProperty = false;
	AActor* Owner = GetOwner();
	if (!Owner) return false;

	if (AActivePropActor* Prop = Cast<AActivePropActor>(Owner))
	{
		bOutHasProperty = true;
		return Prop->bIsGrabbable;
	}

	if (FBoolProperty* BoolProp = FindActorGrabbableProperty())
	{
		bOutHasProperty = true;
		return BoolProp->GetPropertyValue_InContainer(Owner);
	}

	return false;
}

void UStaticMeshPhysicsSimulationComponent::SetActorGrabbableValue(bool bNewGrabbable)
{
	bIsGrabbable = bNewGrabbable;

	AActor* Owner = GetOwner();
	if (Owner)
	{
		// 1. Direct typed access if owner is AActivePropActor
		if (AActivePropActor* Prop = Cast<AActivePropActor>(Owner))
		{
			Prop->SetGrabbable(bNewGrabbable);
			OnGrabbableStateChanged.Broadcast(bNewGrabbable);
			return;
		}

		// 2. Dynamic reflection access for Blueprint actors (e.g. PSO_BaseMesh)
		if (FBoolProperty* BoolProp = FindActorGrabbableProperty())
		{
			BoolProp->SetPropertyValue_InContainer(Owner, bNewGrabbable);
			OnGrabbableStateChanged.Broadcast(bNewGrabbable);
			return;
		}

		// 3. If actor does not have grabbable variable, skip without error
	}

	OnGrabbableStateChanged.Broadcast(bNewGrabbable);
}

bool UStaticMeshPhysicsSimulationComponent::IsGrabbable() const
{
	bool bHasProperty = false;
	const bool bActorValue = GetActorGrabbableValue(bHasProperty);
	if (bHasProperty)
	{
		return bActorValue;
	}

	return CurrentState != EPhysicsSimulationState::Held;
}

void UStaticMeshPhysicsSimulationComponent::SetPhysicsState(EPhysicsSimulationState NewState, float CustomDelay, float CustomTolerance)
{
	AActor* Owner = GetOwner();
	if (!Owner) return;

	ApplyState(NewState, CustomDelay, CustomTolerance);
	if (!Owner->HasAuthority() && Owner->GetNetConnection() != nullptr && !Owner->IsA<AActivePropActor>())
	{
		Server_SetPhysicsState(NewState, CustomDelay, CustomTolerance);
	}
}

void UStaticMeshPhysicsSimulationComponent::Server_SetPhysicsState_Implementation(EPhysicsSimulationState NewState, float CustomDelay, float CustomTolerance)
{
	ApplyState(NewState, CustomDelay, CustomTolerance);
}

bool UStaticMeshPhysicsSimulationComponent::Server_SetPhysicsState_Validate(EPhysicsSimulationState NewState, float CustomDelay, float CustomTolerance)
{
	return true;
}

void UStaticMeshPhysicsSimulationComponent::ClearUnsettledTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(UnsettledTimerHandle);
	}
}

bool UStaticMeshPhysicsSimulationComponent::IsHeldOrInMotion() const
{
	if (bIsInMotion)
	{
		return true;
	}
	if (CurrentState == EPhysicsSimulationState::Held || CurrentState == EPhysicsSimulationState::InMotion)
	{
		return true;
	}
	if (AActor* OwnerActor = GetOwner())
	{
		if (const AActivePropActor* PropOwner = Cast<AActivePropActor>(OwnerActor))
		{
			if (PropOwner->bIsHeld || PropOwner->bIsInMotion || PropOwner->State == EPropState::Held || PropOwner->State == EPropState::InMotion)
			{
				return true;
			}
		}
		if (URocketProjectileComponent* RocketComp = OwnerActor->FindComponentByClass<URocketProjectileComponent>())
		{
			if (RocketComp->IsActive() &&
				RocketComp->GetSimulationState() != ERocketSimulationState::AtRest &&
				RocketComp->GetSimulationState() != ERocketSimulationState::Idle)
			{
				return true;
			}
		}
	}
	return false;
}

void UStaticMeshPhysicsSimulationComponent::SetStateAtRest()
{
	if (IsHeldOrInMotion())
	{
		return;
	}
	SetPhysicsState(EPhysicsSimulationState::AtRest);
}

void UStaticMeshPhysicsSimulationComponent::SetStateInMotion()
{
	ClearUnsettledTimer();
	SetPhysicsState(EPhysicsSimulationState::InMotion);
}

void UStaticMeshPhysicsSimulationComponent::SetInMotion(bool bNewInMotion)
{
	bIsInMotion = bNewInMotion;
	if (bNewInMotion)
	{
		SetStateInMotion();
	}
	else
	{
		if (CurrentState == EPhysicsSimulationState::InMotion)
		{
			SetStateUnsettled();
		}
	}
}

void UStaticMeshPhysicsSimulationComponent::SetStateHeld()
{
	ClearUnsettledTimer();
	SetPhysicsState(EPhysicsSimulationState::Held);
}

void UStaticMeshPhysicsSimulationComponent::SetHeld(bool bNewHeld)
{
	if (bNewHeld)
	{
		bIsInMotion = false;
		SetStateHeld();
	}
	else
	{
		if (CurrentState == EPhysicsSimulationState::Held)
		{
			SetStateSettling();
		}
	}
}

void UStaticMeshPhysicsSimulationComponent::SetStateSettling(float InToleranceLevel)
{
	if (bIsInMotion)
	{
		return;
	}
	if (const AActivePropActor* PropOwner = Cast<AActivePropActor>(GetOwner()))
	{
		if (PropOwner->bIsHeld || PropOwner->bIsInMotion)
		{
			return;
		}
	}
	SetPhysicsState(EPhysicsSimulationState::Settling, -1.0f, InToleranceLevel);
}

void UStaticMeshPhysicsSimulationComponent::SetStateUnsettled(float UnsettledDelay)
{
	if (bIsInMotion)
	{
		return;
	}
	if (const AActivePropActor* PropOwner = Cast<AActivePropActor>(GetOwner()))
	{
		if (PropOwner->bIsHeld || PropOwner->bIsInMotion)
		{
			return;
		}
	}
	SetPhysicsState(EPhysicsSimulationState::Unsettled, UnsettledDelay);
}

void UStaticMeshPhysicsSimulationComponent::RetriggerUnsettledTimer(float NewDelay)
{
	// Calling SetStateUnsettled resets any active countdown and starts a fresh timer (e.g. 3.0s)
	SetStateUnsettled(NewDelay);
}

float UStaticMeshPhysicsSimulationComponent::GetRemainingUnsettledTime() const
{
	UWorld* World = GetWorld();
	if (World && World->GetTimerManager().IsTimerActive(UnsettledTimerHandle))
	{
		return World->GetTimerManager().GetTimerRemaining(UnsettledTimerHandle);
	}
	return 0.0f;
}

bool UStaticMeshPhysicsSimulationComponent::IsUnsettledTimerActive() const
{
	UWorld* World = GetWorld();
	return World ? World->GetTimerManager().IsTimerActive(UnsettledTimerHandle) : false;
}

void UStaticMeshPhysicsSimulationComponent::ApplyState(EPhysicsSimulationState NewState, float CustomDelay, float CustomTolerance)
{
	AActor* Owner = GetOwner();
	AActivePropActor* ActivePropOwner = Cast<AActivePropActor>(Owner);

	// Guard against invalid state transitions:
	// 1. A Held prop cannot transition into Settling, AtRest, or Unsettled while still held by owner
	if (ActivePropOwner && (ActivePropOwner->State == EPropState::Held || ActivePropOwner->bIsHeld))
	{
		if (NewState == EPhysicsSimulationState::Settling || NewState == EPhysicsSimulationState::AtRest || NewState == EPhysicsSimulationState::Unsettled)
		{
			return;
		}
	}

	// 2. An InMotion prop cannot transition into Settling, AtRest, or Unsettled while bIsInMotion is true
	if (bIsInMotion || (ActivePropOwner && ActivePropOwner->bIsInMotion))
	{
		if (NewState == EPhysicsSimulationState::Settling || NewState == EPhysicsSimulationState::AtRest || NewState == EPhysicsSimulationState::Unsettled)
		{
			return;
		}
	}

	// 3. An InMotion prop cannot snap directly to AtRest without settling first while still in motion
	if (CurrentState == EPhysicsSimulationState::InMotion && NewState == EPhysicsSimulationState::AtRest && (bIsInMotion || (ActivePropOwner && ActivePropOwner->bIsInMotion)))
	{
		return;
	}

	const EPhysicsSimulationState PrevState = CurrentState;
	CurrentState = NewState;
	StateElapsedTime = 0.0f;
	if (NewState != EPhysicsSimulationState::Settling)
	{
		StationaryTimeAccumulated = 0.0f;
	}

	EnsureTargetMeshResolved();

	UWorld* World = GetWorld();

	if (World)
	{
		World->GetTimerManager().ClearTimer(UnsettledTimerHandle);
	}

	// Unregister from central settling queue if leaving Settling state
	if (PrevState == EPhysicsSimulationState::Settling && NewState != EPhysicsSimulationState::Settling)
	{
		if (APropManager* Manager = APropManager::Get(this))
		{
			Manager->UnregisterSettlingComponent(this);
		}
	}

	const bool bCanModifyReplication = (Owner && Owner->HasAuthority() && World && !World->IsNetMode(NM_Client));
	const bool bAllowPhysicsOnOwner = (!ActivePropOwner || ActivePropOwner->bIsActive);
	const bool bReplicateMovementWhenFree = (ActivePropOwner == nullptr);

	switch (NewState)
	{
	case EPhysicsSimulationState::AtRest:
		// at rest -> set simulate physics to false, set collision profile to default profile, set grabable to true, set replicate to true
		if (TargetMesh)
		{
			TargetMesh->SetSimulatePhysics(false);
			if (!DefaultCollisionProfile.IsNone() && TargetMesh->GetCollisionProfileName() != DefaultCollisionProfile)
			{
				TargetMesh->SetCollisionProfileName(DefaultCollisionProfile);
			}
		}
		SetGrabbableInternal(true);
		if (bCanModifyReplication)
		{
			Owner->SetReplicates(true);
			Owner->SetReplicateMovement(bReplicateMovementWhenFree);
		}
		break;

	case EPhysicsSimulationState::InMotion:
		// In motion -> set simulate physics to true, set collision profile to default profile, set grabable to true, set replicate to true
		if (TargetMesh && bAllowPhysicsOnOwner)
		{
			if (Owner && !Owner->GetActorEnableCollision())
			{
				Owner->SetActorEnableCollision(true);
			}
			if (!DefaultCollisionProfile.IsNone() && TargetMesh->GetCollisionProfileName() != DefaultCollisionProfile)
			{
				TargetMesh->SetCollisionProfileName(DefaultCollisionProfile);
			}
			if (TargetMesh->GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics)
			{
				TargetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}
			TargetMesh->BodyInstance.SetMaxDepenetrationVelocity(80.0f);
			if (!TargetMesh->IsSimulatingPhysics())
			{
				TargetMesh->SetSimulatePhysics(true);
			}
		}
		SetGrabbableInternal(true);
		if (bCanModifyReplication)
		{
			Owner->SetReplicates(true);
			Owner->SetReplicateMovement(bReplicateMovementWhenFree);
		}
		break;

	case EPhysicsSimulationState::Held:
		// Section 27: Held ASM remains network-relevant and replicated while gameplay-relevant
		if (TargetMesh)
		{
			TargetMesh->SetSimulatePhysics(false);
			TargetMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
			TargetMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			TargetMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
		}
		SetGrabbableInternal(false);
		if (bCanModifyReplication)
		{
			Owner->SetReplicates(true);
			Owner->SetReplicateMovement(true);
		}
		break;

	case EPhysicsSimulationState::Settling:
		// Settling -> check velocity of object when reach nearly input parameter of tolerance level to set object state at rest
		ActiveVelocityTolerance = (CustomTolerance > 0.0f) ? CustomTolerance : VelocityTolerance;
		if (TargetMesh && bAllowPhysicsOnOwner)
		{
			if (Owner && !Owner->GetActorEnableCollision())
			{
				Owner->SetActorEnableCollision(true);
			}
			if (!DefaultCollisionProfile.IsNone() && TargetMesh->GetCollisionProfileName() != DefaultCollisionProfile)
			{
				TargetMesh->SetCollisionProfileName(DefaultCollisionProfile);
			}
			if (TargetMesh->GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics)
			{
				TargetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}
			TargetMesh->BodyInstance.SetMaxDepenetrationVelocity(80.0f);
			if (!TargetMesh->IsSimulatingPhysics())
			{
				TargetMesh->SetSimulatePhysics(true);
			}
		}
		SetGrabbableInternal(true);
		if (bCanModifyReplication)
		{
			Owner->SetReplicates(true);
			Owner->SetReplicateMovement(bReplicateMovementWhenFree);
		}

		// Register with central Prop Manager settling queue if enabled
		if (bCheckInstancedStaticMeshes && Owner && Owner->HasAuthority())
		{
			if (APropManager* Manager = APropManager::Get(this))
			{
				Manager->RegisterSettlingComponent(this);
			}
		}
		break;

	case EPhysicsSimulationState::Unsettled:
		// Unsettled -> set simulate physics to true, set collision profile to default profile, set grabable to true, set replicate to true
		// then after input parameter delay set unsettled to settling state (default: 3.0s if -1)
		if (TargetMesh && bAllowPhysicsOnOwner)
		{
			if (Owner && !Owner->GetActorEnableCollision())
			{
				Owner->SetActorEnableCollision(true);
			}
			if (!DefaultCollisionProfile.IsNone() && TargetMesh->GetCollisionProfileName() != DefaultCollisionProfile)
			{
				TargetMesh->SetCollisionProfileName(DefaultCollisionProfile);
			}
			if (TargetMesh->GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics)
			{
				TargetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}
			TargetMesh->BodyInstance.SetMaxDepenetrationVelocity(80.0f);
			if (!TargetMesh->IsSimulatingPhysics())
			{
				TargetMesh->SetSimulatePhysics(true);
			}
		}
		SetGrabbableInternal(true);
		if (bCanModifyReplication)
		{
			Owner->SetReplicates(true);
			Owner->SetReplicateMovement(bReplicateMovementWhenFree);
		}

		if (World && Owner && Owner->HasAuthority())
		{
			float Delay = (CustomDelay > 0.0f) ? CustomDelay : DefaultUnsettledDelay;
			if (Delay <= 0.0f)
			{
				Delay = 3.0f;
			}
			ActiveUnsettledDelay = Delay;
			World->GetTimerManager().SetTimer(UnsettledTimerHandle, this, &UStaticMeshPhysicsSimulationComponent::TransitionFromUnsettledToSettling, Delay, false);
		}
		break;
	}

	OnPhysicsSimulationStateChanged.Broadcast(CurrentState, PrevState);

	// If we just transitioned into Settling and the object is already completely stationary, settle to AtRest immediately
	if (NewState == EPhysicsSimulationState::Settling && Owner && Owner->HasAuthority())
	{
		if (CheckSettled() && (PrevState == EPhysicsSimulationState::Unsettled || PrevState == EPhysicsSimulationState::InMotion))
		{
			SetStateAtRest();
		}
	}
}

void UStaticMeshPhysicsSimulationComponent::SetGrabbableInternal(bool bNewGrabbable)
{
	SetActorGrabbableValue(bNewGrabbable);
}

void UStaticMeshPhysicsSimulationComponent::TransitionFromUnsettledToSettling()
{
	if (IsHeldOrInMotion())
	{
		ClearUnsettledTimer();
		return;
	}
	SetStateSettling();
}
