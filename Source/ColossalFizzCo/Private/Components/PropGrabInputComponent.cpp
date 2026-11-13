#include "Components/PropGrabInputComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropManager.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

EPropGrabIntent FPropGrabGesture::Process(EPropGrabInputPhase Phase, double Now, uint64 HeldToken, double HoldSeconds, double DoubleTapSeconds)
{
	if (!FMath::IsFinite(Now) || !FMath::IsFinite(HoldSeconds) || !FMath::IsFinite(DoubleTapSeconds)
		|| HoldSeconds <= 0 || DoubleTapSeconds <= 0) return EPropGrabIntent::None;
	if (Phase == EPropGrabInputPhase::Started)
	{
		bActive = true; bPerformed = false; StartedAt = Now; StartedToken = HeldToken;
		return EPropGrabIntent::None;
	}
	if (!bActive) return EPropGrabIntent::None;
	const bool bEnding = Phase != EPropGrabInputPhase::Ongoing;
	EPropGrabIntent Intent = EPropGrabIntent::None;
	if (!bPerformed && Now >= StartedAt + HoldSeconds)
	{
		bPerformed = true;
		LastTapToken = 0;
		// A pickup during this press must not become placement of the newly held object.
		if (HeldToken == StartedToken) Intent = StartedToken ? EPropGrabIntent::Place : EPropGrabIntent::Grab;
	}
	if (bEnding)
	{
		bActive = false;
		if (!bPerformed && StartedToken && HeldToken == StartedToken)
		{
			if (LastTapToken == HeldToken && Now >= LastTapAt && Now - LastTapAt <= DoubleTapSeconds)
			{
				Intent = EPropGrabIntent::Drop;
				LastTapToken = 0;
			}
			else { LastTapToken = HeldToken; LastTapAt = Now; }
		}
	}
	return Intent;
}

UPropGrabInputComponent::UPropGrabInputComponent() { PrimaryComponentTick.bCanEverTick = false; }

void UPropGrabInputComponent::BeginPlay()
{
	Super::BeginPlay();
	// CharacterMovement's default 750,000 push force overwhelms even giant props.
	// Apply on every peer through the existing player component; NPC movement is untouched.
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->InitialPushForceFactor = .02f;
			Movement->PushForceFactor = .5f;
			Movement->bPushForceScaledToMass = true;
			Movement->bTouchForceScaledToMass = true;
			Movement->TouchForceFactor = .001f;
			Movement->MinTouchForce = 0.f;
			Movement->MaxTouchForce = .005f;
			Movement->RepulsionForce = .0001f;
		}
	}
	if (!GetWorld() || !GetWorld()->GetWorldSettings()->ActorHasTag(TEXT("NormalColaPrototype"))) return;
	// The prototype has no combat attack window. Its legacy idle hand overlaps must not apply punches.
	TArray<UPrimitiveComponent*> Parts; GetOwner()->GetComponents(Parts);
	for (auto* Part:Parts) if (Part->GetName().Contains(TEXT("Hand")) && Part->GetName().Contains(TEXT("Collision"))) Part->SetGenerateOverlapEvents(false);
}

void UPropGrabInputComponent::ProcessGrabInput(EPropGrabInputPhase Phase, AActor* HeldActor, UPrimitiveComponent* HeldComponent)
{
	uint64 Token = 0;
	if (IsValid(HeldComponent)) Token = (uint64(HeldComponent->GetUniqueID()) << 32) | 1;
	else if (IsValid(HeldActor))
	{
		const auto* Prop = Cast<AActivePropActor>(HeldActor);
		Token = (uint64(HeldActor->GetUniqueID()) << 32) | uint32(Prop ? Prop->ISID : 1);
	}
	LastIntent = GetWorld() ? Gesture.Process(Phase, GetWorld()->GetTimeSeconds(), Token, HoldSeconds, DoubleTapSeconds) : EPropGrabIntent::None;
}

bool UPropGrabInputComponent::CanGrabTarget(AActor* Target) const
{
	if (!IsValid(Target) || Target->GetWorld() != GetWorld() || !IsValid(GetOwner())) return false;
	if (const auto* Prop = Cast<AActivePropActor>(Target))
	{
		const auto* Manager = APropManager::Get(this);
		return Manager && Prop->ISID > 0 && Manager->GetActiveActorFromISID(Prop->ISID) == Prop && Manager->ValidateGrab(Prop->ISID, GetOwner());
	}
	return true; // Other actor types keep their established interface validation.
}

bool UPropGrabInputComponent::OwnsHeldTarget(AActor* Target) const
{
	if (!IsValid(Target) || Target->GetWorld() != GetWorld() || !IsValid(GetOwner())) return false;
	if (const auto* Prop = Cast<AActivePropActor>(Target))
	{
		const auto* Manager = APropManager::Get(this);
		const auto* Record = Manager ? Manager->FindISIDData(Prop->ISID) : nullptr;
		return Record && Record->ActiveActor == Prop && Record->State == EPropState::Held && Record->RuntimeData.InteractionOwner.Get() == GetOwner();
	}
	for (AActor* Parent = Target->GetAttachParentActor(); IsValid(Parent); Parent = Parent->GetAttachParentActor())
		if (Parent == GetOwner()) return true;
	return false;
}
