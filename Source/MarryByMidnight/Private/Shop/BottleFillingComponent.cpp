#include "Shop/BottleFillingComponent.h"
#include "Props/InteractablePropManager.h"
#include "Net/UnrealNetwork.h"

UBottleFillingComponent::UBottleFillingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UBottleFillingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBottleFillingComponent, bIsFilling);
	DOREPLIFETIME(UBottleFillingComponent, SourceISID);
	DOREPLIFETIME(UBottleFillingComponent, TargetISID);
}

bool UBottleFillingComponent::ConnectionsInRange() const
{
	if (!Manager.IsValid() || !GetOwner() || !Manager->FindISIDData(SourceISID) || !Manager->FindISIDData(TargetISID)) return false;
	const FVector Origin = GetOwner()->GetActorLocation();
	return FVector::DistSquared(Origin, Manager->GetTransform(SourceISID).GetLocation()) <= FMath::Square(MaxConnectionDistance)
		&& FVector::DistSquared(Origin, Manager->GetTransform(TargetISID).GetLocation()) <= FMath::Square(MaxConnectionDistance);
}

bool UBottleFillingComponent::StartFilling(int32 Source, int32 Target)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bIsFilling || !FMath::IsFinite(PortionsPerSecond) || PortionsPerSecond <= 0) return false;
	Manager = APropManager::Get(this); SourceISID = Source; TargetISID = Target;
	if (!ConnectionsInRange()) return false;
	SessionID = Manager->BeginLiquidTransfer(Source, Target);
	if (!SessionID.IsValid()) return false;
	bIsFilling = true; AccumulatedSeconds = 0; SetComponentTickEnabled(true); GetOwner()->ForceNetUpdate(); return true;
}

void UBottleFillingComponent::StopFilling()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (Manager.IsValid()) Manager->EndLiquidTransfer(SessionID);
	SessionID.Invalidate(); bIsFilling = false; AccumulatedSeconds = 0;
	SetComponentTickEnabled(false); GetOwner()->ForceNetUpdate();
}

void UBottleFillingComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Dt, TickType, Function);
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bIsFilling) return;
	if (!ConnectionsInRange() || !FMath::IsFinite(Dt) || Dt < 0 || !FMath::IsFinite(PortionsPerSecond) || PortionsPerSecond <= 0) { StopFilling(); return; }
	AccumulatedSeconds += Dt;
	// Fixed steps retain elapsed time across frames and bound work during a frame hitch.
	constexpr float Step = 0.05f;
	for (int32 I = 0; bIsFilling && I < 8 && AccumulatedSeconds >= Step; ++I)
	{
		AccumulatedSeconds -= Step;
		if (Manager->TransferLiquid(SessionID, PortionsPerSecond * Step) <= 0) StopFilling();
	}
}

void UBottleFillingComponent::EndPlay(const EEndPlayReason::Type Reason) { StopFilling(); Super::EndPlay(Reason); }
