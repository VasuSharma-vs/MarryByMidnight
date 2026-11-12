#include "Components/PropActivationVolumeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropManager.h"
#include "Props/PropInstanceVolumeQuery.h"
#include "Engine/World.h"

UPropActivationVolumeComponent::UPropActivationVolumeComponent()
{
	PrimaryComponentTick.bCanEverTick=true;
	PrimaryComponentTick.TickInterval=.1f;
	bAutoActivate=true;
	InitBoxExtent(FVector(250));
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetHiddenInGame(true);
	ShapeColor=FColor(0,200,100);
}

bool UPropActivationVolumeComponent::IsPolicyEnabled() const
{
	return bKeepPropsActive && IsActive() && IsRegistered() && HasBegunPlay()
		&& IsValid(GetOwner()) && GetOwner()->HasAuthority() && GetNetMode()!=NM_Client
		&& GetWorld() && !GetWorld()->bIsTearingDown
		&& !GetComponentTransform().ContainsNaN() && !GetScaledBoxExtent().ContainsNaN()
		&& GetScaledBoxExtent().GetMin()>0;
}

bool UPropActivationVolumeComponent::ContainsActiveInstance(const APropManager* Manager,int32 ISID) const
{
	if (!IsPolicyEnabled() || !Manager || Manager->GetWorld()!=GetWorld()) return false;
	const UPrimitiveComponent* Mesh=Manager->GetOriginalFragmentComponent(ISID);
	if (!Mesh)
	{
		const auto* Actor=Manager->GetActiveActorFromISID(ISID);
		if (!IsValid(Actor) || !Actor->bIsActive || Actor->ISID!=ISID || Actor==GetOwner()) return false;
		Mesh=Actor->PropMesh;
	}
	return IsValid(Mesh) && Mesh->GetOwner()!=GetOwner()
		&& Mesh->OverlapComponent(GetComponentLocation(),GetComponentQuat(),FCollisionShape::MakeBox(GetScaledBoxExtent()));
}

void UPropActivationVolumeComponent::BeginPlay()
{
	Super::BeginPlay();
	const bool bServer=GetOwner() && GetOwner()->HasAuthority() && GetNetMode()!=NM_Client;
	SetComponentTickInterval(FMath::IsFinite(RefreshInterval) ? FMath::Clamp(RefreshInterval,.02f,5.f) : .1f);
	SetComponentTickEnabled(bServer && IsActive());
	if (bServer) RefreshProps();
}

void UPropActivationVolumeComponent::Activate(bool bReset)
{
	Super::Activate(bReset);
	if (HasBegunPlay())
	{
		SetComponentTickEnabled(GetOwner() && GetOwner()->HasAuthority() && GetNetMode()!=NM_Client);
		RefreshProps();
	}
}

void UPropActivationVolumeComponent::Deactivate()
{
	Super::Deactivate();
	ReleaseTrackedProps();
}

void UPropActivationVolumeComponent::SetKeepingPropsActive(bool bEnabled)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return;
	bKeepPropsActive=bEnabled;
	if (HasBegunPlay()) RefreshProps();
}

void UPropActivationVolumeComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
	RefreshProps();
}

void UPropActivationVolumeComponent::RefreshProps()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || !HasBegunPlay()) return;
	if (!RegisteredManager.IsValid())
	{
		RegisteredManager=APropManager::Get(this);
		if (RegisteredManager.IsValid()) RegisteredManager->RegisterActivationVolume(this);
	}
	APropManager* Manager=RegisteredManager.Get();
	if (!Manager) return;
	if (!IsPolicyEnabled()) { ReleaseTrackedProps(); return; }

	TSet<int32> Inside=PropInstanceVolumeQuery::Query(this,Manager);
	// Held props may temporarily change collision response. Retain their identity
	// using a direct geometry test instead of treating that as a volume exit.
	for (int32 ID:TrackedISIDs) if (ContainsActiveInstance(Manager,ID)) Inside.Add(ID);
	TSet<int32> Previous=MoveTemp(TrackedISIDs);
	TrackedISIDs=MoveTemp(Inside);
	TArray<int32> Pending;
	for (int32 ID:TrackedISIDs)
	{
		if (Manager->GetRepresentation(ID)==EPropRepresentation::ISM) Pending.Add(ID);
		else if (auto* Actor=Manager->GetActiveActorFromISID(ID))
		{
			if (!Actor->bIsHeld && !Actor->bIsInMotion && Actor->PhysicsSimComponent
				&& !Actor->PhysicsSimComponent->IsHeldOrInMotion())
			{
				if (Actor->State!=EPropState::Unsettled || Actor->PhysicsSimComponent->CurrentState!=EPhysicsSimulationState::Unsettled)
					Manager->SetInstanceToUnsettled(ID);
				Actor->PhysicsSimComponent->ClearUnsettledTimer();
			}
		}
	}
	Pending.Sort();
	const int32 Attempts=FMath::Min(Pending.Num(),FMath::Clamp(MaxPromotionsPerRefresh,1,128));
	for (int32 I=0;I<Attempts;++I)
	{
		PromotionCursor%=Pending.Num();
		Manager->SetInstanceToUnsettled(Pending[PromotionCursor++]);
	}
	for (int32 ID:Previous)
		if (!TrackedISIDs.Contains(ID) && !Manager->IsInstanceKeptActive(ID)
			&& Manager->GetRepresentation(ID)==EPropRepresentation::ASM) Manager->SetInstanceToSettling(ID);
}

void UPropActivationVolumeComponent::ReleaseTrackedProps()
{
	TSet<int32> Previous=MoveTemp(TrackedISIDs);
	if (APropManager* Manager=RegisteredManager.Get())
		if (Manager->HasAuthority() && GetWorld() && !GetWorld()->bIsTearingDown)
			for (int32 ID:Previous)
				if (!Manager->IsInstanceKeptActive(ID) && Manager->GetRepresentation(ID)==EPropRepresentation::ASM)
					Manager->SetInstanceToSettling(ID);
}

void UPropActivationVolumeComponent::UnregisterPolicy()
{
	if (RegisteredManager.IsValid()) RegisteredManager->UnregisterActivationVolume(this);
	ReleaseTrackedProps();
	RegisteredManager.Reset();
}

void UPropActivationVolumeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterPolicy();
	Super::EndPlay(EndPlayReason);
}

void UPropActivationVolumeComponent::OnUnregister()
{
	UnregisterPolicy();
	Super::OnUnregister();
}
