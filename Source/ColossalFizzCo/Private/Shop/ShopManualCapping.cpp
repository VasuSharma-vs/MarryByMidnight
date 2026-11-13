#include "Shop/ShopProductionStationComponent.h"
#include "Shop/ShopInstanceVolumeQuery.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "Props/InteractablePropManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Props/ColaPropActor.h"
#include "Shop/ShopCheckoutComponent.h"

bool UShopProductionStationComponent::OperatorInRange(const AActor* Player) const
{
	const auto* Pawn=Cast<APawn>(Player);
	const auto* Actions=IsValid(Pawn) ? Pawn->FindComponentByClass<UShopPlayerInteractionComponent>() : nullptr;
	return IsValid(Pawn) && IsValid(Pawn->GetController()) && Pawn->GetController()->IsPlayerController()
		&& Pawn->GetWorld()==GetWorld() && GetOwner() && Actions && Actions->IsStationReachable(GetOwner());
}

float UShopProductionStationComponent::RequiredHoldingForce(int32 BottleID) const
{
	const auto* Manager=APropManager::Get(this);
	const auto* Unique=Manager ? Manager->FindUniqueData(BottleID) : nullptr;
	const auto* Content=Unique ? Manager->FindContentDefinition(Unique->EventData.ContentID) : nullptr;
	return Unique ? Unique->ContinuousData.SealedPressure+Unique->EventData.ContentLevel*(Content ? Content->PressureStrengthMultiplier : 1.f) : 0.f;
}

bool UShopProductionStationComponent::HasSufficientCapHold(int32 BottleID) const
{
	return bRequiresCooperativeCap && View.ManualBottleISID==BottleID && OperatorInRange(View.CapHolder)
		&& View.HoldingForce>=RequiredHoldingForce(BottleID);
}

void UShopProductionStationComponent::ResetManualCapping()
{
	if (auto* Manager=APropManager::Get(this)) if (auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(View.ManualBottleISID)))
		if (Bottle->CapHoldingStation.Get()==this) Bottle->CapHoldingStation.Reset();
	View.ManualBottleISID=0; View.CapHolder=nullptr; View.CapRotator=nullptr;
	View.HoldingForce=0; View.HoldingEfficiency=1; View.RotationProgress=0;
	LastHoldingPress=LastRotationTurn=-1; LiquidReleaseEpisode.Invalidate();
}

EShopCheckoutResult UShopProductionStationComponent::ManualCapAction(EShopProductionAction Action,int32 BottleID,AActor* Player)
{
	if (!OperatorInRange(Player)) return EShopCheckoutResult::Unavailable;
	if (Action==EShopProductionAction::LeaveCapRole)
	{
		if (View.CapHolder==Player) { View.CapHolder=nullptr; View.HoldingForce=0; }
		if (View.CapRotator==Player) View.CapRotator=nullptr;
		GetOwner()->ForceNetUpdate(); return EShopCheckoutResult::Success;
	}
	auto* Manager=APropManager::Get(this);
	const auto* Unique=Manager ? Manager->FindUniqueData(BottleID) : nullptr;
	const auto* Record=Manager ? Manager->FindISIDData(BottleID) : nullptr;
	const auto* Def=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
	FShopStockEntry Stock;
	if (Manager && Manager->ShopCheckout && Manager->ShopCheckout->GetStockEntry(BottleID,Stock)
		&& !Manager->ShopCheckout->IsAvailableShopStock(BottleID)) return EShopCheckoutResult::Unavailable;
	if (!Unique || !Def || Def->BottleMaterialType!=EBottleMaterialType::Plastic || !Unique->EventData.AttachedCapISID
		|| Unique->EventData.CapSealed>=1 || !ShopInstanceVolumeQuery::Query(BottleInput).Contains(BottleID)
		|| (View.ManualBottleISID && View.ManualBottleISID!=BottleID)) return EShopCheckoutResult::Unavailable;
	const bool ClaimHolding=Action==EShopProductionAction::HoldCapRole || (Action==EShopProductionAction::PressCap && !IsValid(View.CapHolder));
	const bool ClaimRotating=Action==EShopProductionAction::RotateCapRole || (Action==EShopProductionAction::TurnCap && !IsValid(View.CapRotator));
	if (ClaimHolding || ClaimRotating)
	{
		auto& Role=ClaimHolding ? View.CapHolder : View.CapRotator;
		const auto& Other=ClaimHolding ? View.CapRotator : View.CapHolder;
		if ((IsValid(Role) && Role!=Player) || Other==Player) return EShopCheckoutResult::OperationConflict;
		if (!View.ManualBottleISID) { View.ManualBottleISID=BottleID; LiquidReleaseEpisode=FGuid::NewGuid(); }
		if (auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(BottleID))) Bottle->CapHoldingStation=this;
		Role=Player;
		if (Action==EShopProductionAction::HoldCapRole || Action==EShopProductionAction::RotateCapRole)
		{ GetOwner()->ForceNetUpdate(); return EShopCheckoutResult::Success; }
		// The first real G/R contribution can claim its available role in this
		// same request. No unheld interval is required between selection and G.
	}
	const double Now=GetWorld()->GetTimeSeconds();
	if (Action==EShopProductionAction::PressCap)
	{
		if (View.CapHolder!=Player || (LastHoldingPress>=0 && Now-LastHoldingPress<FMath::Max(.02f,MinimumInputInterval))) return EShopCheckoutResult::OperationConflict;
		LastHoldingPress=Now;
		View.HoldingForce=FMath::Clamp(View.HoldingForce+FMath::Max(0.f,HoldingForcePerPress)*View.HoldingEfficiency,0.f,FMath::Max(0.f,MaximumHoldingForce)*View.HoldingEfficiency);
	}
	else if (Action==EShopProductionAction::TurnCap)
	{
		if (View.CapRotator!=Player || !HasSufficientCapHold(BottleID) || (LastRotationTurn>=0 && Now-LastRotationTurn<FMath::Max(.02f,MinimumInputInterval))) return EShopCheckoutResult::OperationConflict;
		LastRotationTurn=Now;
		View.RotationProgress=FMath::Clamp(View.RotationProgress+FMath::Clamp(RotationPerTurn,.01f,1.f),0.f,1.f);
		if (View.RotationProgress>=1)
		{
			if (!Manager->ClosePhysicalCap(BottleID,FMath::Lerp(Def->TorqueSweetSpotMin,Def->TorqueSweetSpotMax,.5f))) return EShopCheckoutResult::Unavailable;
			ResetManualCapping();
		}
	}
	else return EShopCheckoutResult::InvalidRequest;
	GetOwner()->ForceNetUpdate(); return EShopCheckoutResult::Success;
}

void UShopProductionStationComponent::UpdateManualCapping(float DeltaSeconds)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<0 || !View.ManualBottleISID) return;
	auto* Manager=APropManager::Get(this);
	const auto* Unique=Manager ? Manager->FindUniqueData(View.ManualBottleISID) : nullptr;
	const auto* Record=Manager ? Manager->FindISIDData(View.ManualBottleISID) : nullptr;
	const auto* Product=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
	FShopStockEntry Stock;
	const bool Locked=Manager && Manager->ShopCheckout && Manager->ShopCheckout->GetStockEntry(View.ManualBottleISID,Stock)
		&& !Manager->ShopCheckout->IsAvailableShopStock(View.ManualBottleISID);
	if (Locked || !Unique || !Product || !Unique->EventData.AttachedCapISID || Unique->EventData.CapSealed>=1
		|| !ShopInstanceVolumeQuery::Query(BottleInput).Contains(View.ManualBottleISID)) { ResetManualCapping(); return; }
	if (!OperatorInRange(View.CapHolder)) { View.CapHolder=nullptr; View.HoldingForce=0; }
	if (!OperatorInRange(View.CapRotator)) View.CapRotator=nullptr;
	View.HoldingEfficiency=FMath::Max(0.f,View.HoldingEfficiency-FMath::Max(0.f,HoldingEfficiencyDecayPerSecond)*DeltaSeconds);
	View.HoldingForce=FMath::Clamp(View.HoldingForce-FMath::Max(0.f,HoldingForceDecayPerSecond)*DeltaSeconds,0.f,FMath::Max(0.f,MaximumHoldingForce)*View.HoldingEfficiency);
	if (!HasSufficientCapHold(View.ManualBottleISID) && Unique->EventData.ContentLevel>0 && LeakageLevelPerSecond>0)
		Manager->ReleaseBottleLiquidPortions(View.ManualBottleISID,double(LeakageLevelPerSecond)*DeltaSeconds*Product->ConsumableProfile.NumOfPortions,LiquidReleaseEpisode);
	GetOwner()->ForceNetUpdate();
}
