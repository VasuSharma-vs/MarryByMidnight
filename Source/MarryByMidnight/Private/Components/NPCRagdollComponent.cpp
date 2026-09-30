#include "Components/NPCRagdollComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Async/Async.h"
#include "PhysicsEngine/BodyInstance.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"

UNPCRagdollComponent::UNPCRagdollComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UNPCRagdollComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UNPCRagdollComponent, CurrentState);
}

void UNPCRagdollComponent::OnRep_CurrentState(ERagdollState PreviousState)
{
	ExitState(PreviousState);
	EnterState(CurrentState);
	OnRagdollStateChanged.Broadcast(PreviousState, CurrentState);
}

void UNPCRagdollComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!TargetMesh.IsValid())
	{
		if (ACharacter* CharOwner = Cast<ACharacter>(GetOwner()))
		{
			TargetMesh = CharOwner->GetMesh();
		}
		else if (AActor* OwnerActor = GetOwner())
		{
			TargetMesh = OwnerActor->FindComponentByClass<USkeletalMeshComponent>();
		}
	}

	if (TargetMesh.IsValid())
	{
		SaveOriginalTransform();
		TargetMesh->SetNotifyRigidBodyCollision(true);
		TargetMesh->OnComponentHit.AddDynamic(this, &UNPCRagdollComponent::HandleMeshHit);
	}
}

void UNPCRagdollComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(UnsettledTimerHandle);
		World->GetTimerManager().ClearTimer(SettlingCheckTimerHandle);
	}

	if (TargetMesh.IsValid())
	{
		TargetMesh->OnComponentHit.RemoveDynamic(this, &UNPCRagdollComponent::HandleMeshHit);
	}

	Super::EndPlay(EndPlayReason);
}

void UNPCRagdollComponent::SetTargetSkeletalMesh(USkeletalMeshComponent* InMesh)
{
	if (TargetMesh.IsValid())
	{
		TargetMesh->OnComponentHit.RemoveDynamic(this, &UNPCRagdollComponent::HandleMeshHit);
	}

	TargetMesh = InMesh;

	if (TargetMesh.IsValid())
	{
		SaveOriginalTransform();
		TargetMesh->SetNotifyRigidBodyCollision(true);
		TargetMesh->OnComponentHit.AddDynamic(this, &UNPCRagdollComponent::HandleMeshHit);
	}
}

void UNPCRagdollComponent::SetRagdollState(ERagdollState NewState)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;
	if (CurrentState == NewState) return;

	const ERagdollState OldState = CurrentState;
	ExitState(OldState);
	CurrentState = NewState;
	EnterState(NewState);

	OnRagdollStateChanged.Broadcast(OldState, NewState);
}

void UNPCRagdollComponent::SaveOriginalTransform()
{
	if (TargetMesh.IsValid())
	{
		SavedOriginalRelativeLocation = TargetMesh->GetRelativeLocation();
		SavedOriginalRelativeRotation = TargetMesh->GetRelativeRotation();
	}
}

void UNPCRagdollComponent::RestoreOriginalTransform()
{
	if (TargetMesh.IsValid())
	{
		TargetMesh->SetRelativeLocationAndRotation(SavedOriginalRelativeLocation, SavedOriginalRelativeRotation);
	}
}

void UNPCRagdollComponent::StartRagdoll()
{
	SetRagdollState(ERagdollState::Unsettled);
}

void UNPCRagdollComponent::StopRagdoll()
{
	SetRagdollState(ERagdollState::Animation);
}

void UNPCRagdollComponent::TriggerHitReaction()
{
	SetRagdollState(ERagdollState::Unsettled);
}

void UNPCRagdollComponent::ExitState(ERagdollState OldState)
{
	// Save baseline transform whenever transitioning away from Animation
	if (OldState == ERagdollState::Animation)
	{
		SaveOriginalTransform();
	}
	else if (OldState == ERagdollState::Unsettled)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(UnsettledTimerHandle);
		}
	}
	else if (OldState == ERagdollState::Settling)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(SettlingCheckTimerHandle);
		}
		bIsAsyncCheckPending = false;
	}
}

void UNPCRagdollComponent::EnterState(ERagdollState NewState)
{
	switch (NewState)
	{
	case ERagdollState::Unsettled:
	{
		// 1. Halt and disable locomotion
		if (ACharacter* CharOwner = Cast<ACharacter>(GetOwner()))
		{
			if (UCharacterMovementComponent* MoveComp = CharOwner->GetCharacterMovement())
			{
				MoveComp->StopMovementImmediately();
				MoveComp->DisableMovement();
			}

			if (UCapsuleComponent* Capsule = CharOwner->GetCapsuleComponent())
			{
				Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}

		// 2. Start simulating ragdoll physics and wake bodies
		if (TargetMesh.IsValid())
		{
			TargetMesh->SetCollisionProfileName(TEXT("Ragdoll"));
			TargetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			TargetMesh->SetSimulatePhysics(true);
			TargetMesh->WakeAllRigidBodies();
		}

		// 3. Start delay timer to transition to Settling
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				UnsettledTimerHandle,
				[this]() { SetRagdollState(ERagdollState::Settling); },
				FMath::Max(0.1f, UnsettledDelay),
				false
			);
		}
		break;
	}

	case ERagdollState::Settling:
	{
		// Continuously check bone velocities in async worker thread
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				SettlingCheckTimerHandle,
				this,
				&UNPCRagdollComponent::PerformAsyncSettlingCheck,
				FMath::Max(0.05f, SettlingCheckInterval),
				true
			);
		}
		break;
	}

	case ERagdollState::AtRest:
	{
		// Pause ragdoll simulation while keeping current position intact
		if (TargetMesh.IsValid())
		{
			TargetMesh->PutAllRigidBodiesToSleep();
		}
		break;
	}

	case ERagdollState::Keep:
	{
		// Continuously keep ragdoll simulating
		if (TargetMesh.IsValid())
		{
			TargetMesh->SetSimulatePhysics(true);
			TargetMesh->WakeAllRigidBodies();
		}
		break;
	}

	case ERagdollState::Animation:
	{
		AActor* OwnerActor = GetOwner();
		if (TargetMesh.IsValid())
		{
			FVector RagdollPos = TargetMesh->GetComponentLocation();

			if (PelvisBoneName != NAME_None && TargetMesh->DoesSocketExist(PelvisBoneName))
			{
				RagdollPos = TargetMesh->GetSocketLocation(PelvisBoneName);
			}

			if (OwnerActor)
			{
				// Ground line trace to place actor cleanly on floor
				FHitResult GroundHit;
				FCollisionQueryParams Params(TEXT("RagdollFloorTrace"), false, OwnerActor);
				const FVector TraceStart = RagdollPos + FVector(0.0f, 0.0f, 50.0f);
				const FVector TraceEnd = RagdollPos - FVector(0.0f, 0.0f, 350.0f);
				if (GetWorld() && (GetWorld()->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_WorldStatic, Params)
					|| GetWorld()->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_Visibility, Params)))
				{
					RagdollPos = GroundHit.Location;
				}

				// Project RagdollPos to NavMesh so the character lands on a valid walkable navigation surface
				UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
				if (NavSys)
				{
					FNavLocation NavLoc;
					if (NavSys->ProjectPointToNavigation(RagdollPos, NavLoc, FVector(300.0f, 300.0f, 500.0f)))
					{
						RagdollPos = NavLoc.Location;
					}
				}

				if (ACharacter* CharOwner = Cast<ACharacter>(OwnerActor))
				{
					UCapsuleComponent* Capsule = CharOwner->GetCapsuleComponent();
					if (Capsule)
					{
						Capsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
						Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
						RagdollPos.Z += Capsule->GetScaledCapsuleHalfHeight() + 2.0f;
					}

					CharOwner->TeleportTo(RagdollPos, OwnerActor->GetActorRotation());
					if (Capsule)
					{
						Capsule->UpdateComponentToWorld();
					}

					// Turn off physics simulation on target mesh BEFORE re-attaching!
					TargetMesh->SetSimulatePhysics(false);
					TargetMesh->SetCollisionProfileName(TEXT("CharacterMesh"));
					TargetMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
					if (Capsule)
					{
						TargetMesh->AttachToComponent(Capsule, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
					}
					RestoreOriginalTransform();

					if (UCharacterMovementComponent* MoveComp = CharOwner->GetCharacterMovement())
					{
						MoveComp->StopMovementImmediately();
						MoveComp->SetMovementMode(MOVE_Walking);
						MoveComp->Velocity = FVector::ZeroVector;
						MoveComp->ClearAccumulatedForces();
						MoveComp->UpdateComponentVelocity();
					}
				}
				else
				{
					OwnerActor->TeleportTo(RagdollPos, OwnerActor->GetActorRotation());
					TargetMesh->SetSimulatePhysics(false);
					TargetMesh->SetCollisionProfileName(TEXT("CharacterMesh"));
					RestoreOriginalTransform();
				}
			}
			else
			{
				TargetMesh->SetSimulatePhysics(false);
				TargetMesh->SetCollisionProfileName(TEXT("CharacterMesh"));
				RestoreOriginalTransform();
			}
		}
		else if (ACharacter* CharOwner = Cast<ACharacter>(OwnerActor))
		{
			if (UCapsuleComponent* Capsule = CharOwner->GetCapsuleComponent())
			{
				Capsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
				Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Capsule->UpdateComponentToWorld();
			}
			if (UCharacterMovementComponent* MoveComp = CharOwner->GetCharacterMovement())
			{
				MoveComp->StopMovementImmediately();
				MoveComp->SetMovementMode(MOVE_Walking);
				MoveComp->Velocity = FVector::ZeroVector;
				MoveComp->ClearAccumulatedForces();
				MoveComp->UpdateComponentVelocity();
			}
		}
		break;
	}
	}
}

void UNPCRagdollComponent::PerformAsyncSettlingCheck()
{
	if (bIsAsyncCheckPending || !TargetMesh.IsValid() || CurrentState != ERagdollState::Settling)
	{
		return;
	}

	bIsAsyncCheckPending = true;

	// Gather bone velocities on Game Thread
	TArray<FVector> Velocities;
	Velocities.Reserve(TargetMesh->Bodies.Num());

	for (FBodyInstance* BI : TargetMesh->Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			Velocities.Add(BI->GetUnrealWorldVelocity());
		}
	}

	const float ThresholdSq = FMath::Square(SettlingVelocityThreshold);

	// Evaluate all bone velocities in an async worker thread
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakThis = TWeakObjectPtr<UNPCRagdollComponent>(this), Velocities, ThresholdSq]()
	{
		bool bAllNearlyZero = true;
		for (const FVector& V : Velocities)
		{
			if (V.SizeSquared() > ThresholdSq)
			{
				bAllNearlyZero = false;
				break;
			}
		}

		// Return result to Game Thread
		AsyncTask(ENamedThreads::GameThread, [WeakThis, bAllNearlyZero]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->bIsAsyncCheckPending = false;
				if (bAllNearlyZero && WeakThis->CurrentState == ERagdollState::Settling)
				{
					WeakThis->SetRagdollState(ERagdollState::AtRest);
				}
			}
		});
	});
}

void UNPCRagdollComponent::HandleMeshHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (!bHitTriggersUnsettled) return;
	if (!OtherActor || OtherActor == GetOwner()) return;

	// Do NOT start ragdoll from hits while in Animation state
	if (CurrentState == ERagdollState::Animation && !bHitTriggersFromAnimation)
	{
		return;
	}

	// If impulse is reported and below threshold, ignore weak scrapes
	if (MinHitImpulseThreshold > 0.0f && !NormalImpulse.IsNearlyZero() && NormalImpulse.Size() < MinHitImpulseThreshold)
	{
		return;
	}

	// Any qualifying hit sets ragdoll state to Unsettled
	SetRagdollState(ERagdollState::Unsettled);
}
