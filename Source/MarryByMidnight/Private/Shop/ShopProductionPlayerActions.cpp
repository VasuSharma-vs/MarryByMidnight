#include "Shop/ShopPlayerInteractionComponent.h"

void UShopPlayerInteractionComponent::RequestSupplyPurchase(UShopProductionStationComponent* Station, FGuid Session, FName Offer, int32 Quantity)
{
	if (LocalSequence==MAX_int64) return;
	const int64 Sequence=++LocalSequence; const FGuid ID=FGuid::NewGuid();
	if (GetOwner() && GetOwner()->HasAuthority() && GetNetMode()!=NM_Client) OnActionResult.Broadcast(Sequence,ExecuteSupplyPurchase(Station,Session,Offer,Quantity,ID,Sequence));
	else Server_SupplyPurchase(Station,Session,Offer,Quantity,ID,Sequence);
}
void UShopPlayerInteractionComponent::Server_SupplyPurchase_Implementation(UShopProductionStationComponent* Station, FGuid Session, FName Offer, int32 Quantity, FGuid ID, int64 Sequence)
{
	Client_ActionResult(Sequence,ExecuteSupplyPurchase(Station,Session,Offer,Quantity,ID,Sequence));
}
EShopCheckoutResult UShopPlayerInteractionComponent::ExecuteSupplyPurchase(UShopProductionStationComponent* Station, FGuid Session, FName Offer, int32 Quantity, FGuid ID, int64 Sequence)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return EShopCheckoutResult::NotAuthority;
	if (Sequence<=0 || Sequence<=LastServerSequence) return EShopCheckoutResult::OperationConflict;
	LastServerSequence=Sequence;
	if (!IsValid(Station) || Station->GetWorld()!=GetWorld() || !CanReach(Station->GetOwner())) return EShopCheckoutResult::Unavailable;
	return Station->Purchase(Session,ID,Offer,Quantity);
}
void UShopPlayerInteractionComponent::RequestProductionAction(UShopProductionStationComponent* Station, FGuid Session, EShopProductionAction Action, int32 Bottle, int32 Other)
{
	if (LocalSequence==MAX_int64) return;
	const int64 Sequence=++LocalSequence;
	if (GetOwner() && GetOwner()->HasAuthority() && GetNetMode()!=NM_Client) OnActionResult.Broadcast(Sequence,ExecuteProductionAction(Station,Session,Action,Bottle,Other,Sequence));
	else Server_ProductionAction(Station,Session,Action,Bottle,Other,Sequence);
}
void UShopPlayerInteractionComponent::Server_ProductionAction_Implementation(UShopProductionStationComponent* Station, FGuid Session, EShopProductionAction Action, int32 Bottle, int32 Other, int64 Sequence)
{
	Client_ActionResult(Sequence,ExecuteProductionAction(Station,Session,Action,Bottle,Other,Sequence));
}
EShopCheckoutResult UShopPlayerInteractionComponent::ExecuteProductionAction(UShopProductionStationComponent* Station, FGuid Session, EShopProductionAction Action, int32 Bottle, int32 Other, int64 Sequence)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return EShopCheckoutResult::NotAuthority;
	if (Sequence<=0 || Sequence<=LastServerSequence) return EShopCheckoutResult::OperationConflict;
	LastServerSequence=Sequence;
	if (!IsValid(Station) || Station->GetWorld()!=GetWorld() || !CanReach(Station->GetOwner())) return EShopCheckoutResult::Unavailable;
	return Station->ExecuteForPlayer(Session,Action,Bottle,Other,GetOwner());
}
