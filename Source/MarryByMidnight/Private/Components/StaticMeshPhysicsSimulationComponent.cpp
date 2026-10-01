#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Props/InteractableProp.h"

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
	}

	AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		ApplyState(InitialState);
	}
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

	// Settling State: check velocity until it reaches below tolerance level
	if (CurrentState == EPhysicsSimulationState::Settling)
	{
		EnsureTargetMeshResolved();
		if (TargetMesh)
		{
			const float LinearSpeed = TargetMesh->GetComponentVelocity().Size();
			if (LinearSpeed <= ActiveVelocityTolerance)
			{
				SetStateAtRest();
			}
		}
	}
}

void UStaticMeshPhysicsSimulationComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UStaticMeshPhysicsSimulationComponent, CurrentState);
	DOREPLIFETIME(UStaticMeshPhysicsSimulationComponent, bIsGrabbable);
}

void UStaticMeshPhysicsSimulationComponent::OnRep_CurrentState()
{
	ApplyState(CurrentState);
}

void UStaticMeshPhysicsSimulationComponent::OnRep_IsGrabbable()
{
	AActor* Owner = GetOwner();
	if (Owner)
	{
		if (AInteractableProp* Prop = Cast<AInteractableProp>(Owner))
		{
			Prop->SetGrabbable(bIsGrabbable);
		}
	}
	OnGrabbableStateChanged.Broadcast(bIsGrabbable);
}

void UStaticMeshPhysicsSimulationComponent::SetPhysicsState(EPhysicsSimulationState NewState, float CustomDelay, float CustomTolerance)
{
	AActor* Owner = GetOwner();
	if (!Owner) return;

	if (Owner->HasAuthority())
	{
		ApplyState(NewState, CustomDelay, CustomTolerance);
	}
	else
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

void UStaticMeshPhysicsSimulationComponent::SetStateAtRest()
{
	SetPhysicsState(EPhysicsSimulationState::AtRest);
}

void UStaticMeshPhysicsSimulationComponent::SetStateInMotion()
{
	SetPhysicsState(EPhysicsSimulationState::InMotion);
}

void UStaticMeshPhysicsSimulationComponent::SetStateHeld()
{
	SetPhysicsState(EPhysicsSimulationState::Held);
}

void UStaticMeshPhysicsSimulationComponent::SetStateSettling(float InToleranceLevel)
{
	SetPhysicsState(EPhysicsSimulationState::Settling, -1.0f, InToleranceLevel);
}

void UStaticMeshPhysicsSimulationComponent::SetStateUnsettled(float UnsettledDelay)
{
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
	const EPhysicsSimulationState PrevState = CurrentState;
	CurrentState = NewState;

	EnsureTargetMeshResolved();

	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();

	if (World)
	{
		World->GetTimerManager().ClearTimer(UnsettledTimerHandle);
	}

	switch (NewState)
	{
	case EPhysicsSimulationState::AtRest:
		// at rest -> set simulate physics to false, set collision profile to default profile, set grabable to false, set replicate to true
		if (TargetMesh)
		{
			TargetMesh->SetSimulatePhysics(false);
			if (!DefaultCollisionProfile.IsNone())
			{
				TargetMesh->SetCollisionProfileName(DefaultCollisionProfile);
			}
		}
		SetGrabbableInternal(false);
		if (Owner)
		{
			Owner->SetReplicates(true);
			Owner->SetReplicateMovement(true);
		}
		break;

	case EPhysicsSimulationState::InMotion:
		// In motion -> set simulate physics to true, set collision profile to default profile, set grabable to false, set replicate to true
		if (TargetMesh)
		{
			TargetMesh->SetSimulatePhysics(true);
			if (!DefaultCollisionProfile.IsNone())
			{
				TargetMesh->SetCollisionProfileName(DefaultCollisionProfile);
			}
		}
		SetGrabbableInternal(false);
		if (Owner)
		{
			Owner->SetReplicates(true);
			Owner->SetReplicateMovement(true);
		}
		break;

	case EPhysicsSimulationState::Held:
		// held -> set simulate physics to false, set collision profile to IgnoreOnlyPawn, set grabable to false, set replicate to false
		if (TargetMesh)
		{
			TargetMesh->SetSimulatePhysics(false);
			TargetMesh->SetCollisionProfileName(FName(TEXT("IgnoreOnlyPawn")));
			TargetMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		}
		SetGrabbableInternal(false);
		if (Owner)
		{
			Owner->SetReplicateMovement(false);
			Owner->SetReplicates(false);
		}
		break;

	case EPhysicsSimulationState::Settling:
		// Settling -> check velocity of object when reach nearly input parameter of tolerance level to set object state at rest
		ActiveVelocityTolerance = (CustomTolerance > 0.0f) ? CustomTolerance : VelocityTolerance;
		if (TargetMesh)
		{
			TargetMesh->SetSimulatePhysics(true);
			if (!DefaultCollisionProfile.IsNone())
			{
				TargetMesh->SetCollisionProfileName(DefaultCollisionProfile);
			}
		}
		SetGrabbableInternal(false);
		if (Owner)
		{
			Owner->SetReplicates(true);
			Owner->SetReplicateMovement(true);
		}
		break;

	case EPhysicsSimulationState::Unsettled:
		// Unsettled -> set simulate physics to true, set collision profile to default profile, set grabable to true, set replicate to true
		// then after input parameter delay set unsettled to settling state (default: 3.0s if -1)
		if (TargetMesh)
		{
			TargetMesh->SetSimulatePhysics(true);
			if (!DefaultCollisionProfile.IsNone())
			{
				TargetMesh->SetCollisionProfileName(DefaultCollisionProfile);
			}
		}
		SetGrabbableInternal(true);
		if (Owner)
		{
			Owner->SetReplicates(true);
			Owner->SetReplicateMovement(true);
		}

		if (World && Owner && Owner->HasAuthority())
		{
			const float Delay = (CustomDelay >= 0.0f) ? CustomDelay : DefaultUnsettledDelay;
			World->GetTimerManager().SetTimer(UnsettledTimerHandle, this, &UStaticMeshPhysicsSimulationComponent::TransitionFromUnsettledToSettling, Delay, false);
		}
		break;
	}

	OnPhysicsSimulationStateChanged.Broadcast(CurrentState, PrevState);
}

void UStaticMeshPhysicsSimulationComponent::SetGrabbableInternal(bool bNewGrabbable)
{
	bIsGrabbable = bNewGrabbable;

	AActor* Owner = GetOwner();
	if (Owner)
	{
		if (AInteractableProp* Prop = Cast<AInteractableProp>(Owner))
		{
			Prop->SetGrabbable(bNewGrabbable);
		}
	}

	OnGrabbableStateChanged.Broadcast(bNewGrabbable);
}

void UStaticMeshPhysicsSimulationComponent::TransitionFromUnsettledToSettling()
{
	SetStateSettling();
}
