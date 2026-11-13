#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Misc/ScopeExit.h"
#include "Shop/ShopProcurementComponent.h"
#include "Props/InteractablePropDataAsset.h"

namespace
{
	bool StockMayChange(const APropManager* Manager, int32 ISID)
	{
		FShopStockEntry Entry;
		return !Manager->ShopCheckout || !Manager->ShopCheckout->GetStockEntry(ISID,Entry) || Manager->ShopCheckout->IsAvailableShopStock(ISID);
	}
}

bool APropManager::EnsureDefaultPhysicalCap(int32 BottleID)
{
	if (!HasAuthority() || GetNetMode()==NM_Client) return false;
	const auto* Record=FindISIDData(BottleID);
	const auto* Def=Record ? FindProductDefinition(Record->ProductID) : nullptr;
	if (!Def) return false;
	if (Def->bIsBottleCap || Def->BottleMaterialType!=EBottleMaterialType::Plastic
		|| Def->DefaultUniqueData.EventData.CapSealed<=0 || !Def->SourceDataAsset || !ShopSupplies) return true;
	const auto* Bottle=FindUniqueData(BottleID);
	if (!Bottle) return false;
	if (Bottle->EventData.AttachedCapISID)
	{
		const auto* Cap=FindUniqueData(Bottle->EventData.AttachedCapISID);
		return Cap && Cap->EventData.AttachedBottleISID==BottleID;
	}
	// Opening/ejecting a cap clears the seal. Never create another free cap for it.
	if (Bottle->EventData.CapSealed<=0) return true;
	const auto* Catalog=ShopSupplies->Catalog.LoadSynchronous();
	const FSoftObjectPath Source(Def->SourceDataAsset.Get());
	if (!Catalog || !Catalog->FinishedBottleProducts.ContainsByPredicate([&](const auto& Product){return Product.ToSoftObjectPath()==Source;})) return true;
	const auto Type=Def->DefaultCapType;
	const FTransform Transform=Def->CapAttachmentTransform*GetTransform(BottleID);
	const double BottleCreationTime=Bottle->EventData.CreationTime;
	const auto* Offer=Catalog->Offers.FindByPredicate([&](const auto& Item)
	{
		const auto* Product=Item.Kind==EShopSupplyKind::Cap ? Item.Product.LoadSynchronous() : nullptr;
		return Product && Product->bIsBottleCap && Product->DefaultCapType==Type
			&& IsBottleCapTypeSupportedInMask(Def->SupportedCapMask,static_cast<uint8>(Type));
	});
	if (!Offer) return false;
	const int32 PID=RegisterProductFromDataAsset(Offer->Product.LoadSynchronous());
	const int32 CapID=PID>0 ? GenerateProductInstance(PID,Transform) : 0;
	if (!CapID) return false;
	// Registration/generation may reallocate canonical maps; do not retain their pointers.
	Bottle=FindUniqueData(BottleID);
	if (!Bottle || Bottle->EventData.AttachedCapISID || Bottle->EventData.CapSealed<=0)
	{
		ReleaseInstanceToEmpty(CapID); return false;
	}
	FPropEventSyncDelta B,C;
	B.ISID=BottleID; B.ExtendedDirtyMask=EPropExtendedEventMask::AttachedCap; B.AttachedCapISID=CapID;
	B.SetFlag(EPropEventDirtyMask::CapType); B.CapType=static_cast<uint8>(Type);
	C.ISID=CapID; C.ExtendedDirtyMask=EPropExtendedEventMask::AttachedBottle; C.AttachedBottleISID=BottleID;
	C.SetFlag(EPropEventDirtyMask::Grabbable); C.bIsGrabbable=false;
	C.SetFlag(EPropEventDirtyMask::CreationTime); C.CreationTime=BottleCreationTime;
	if (!SubmitAtomicInstanceEventUpdates({B,C})) { ReleaseInstanceToEmpty(CapID); return false; }
	SynchronizeCapAssembly(BottleID);
	return true;
}

bool APropManager::AttachPhysicalCap(int32 BottleID, int32 CapID)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || BottleID==CapID || !StockMayChange(this,BottleID) || !StockMayChange(this,CapID)) return false;
	const auto* BottleRecord=FindISIDData(BottleID); const auto* CapRecord=FindISIDData(CapID);
	const auto* Bottle=FindUniqueData(BottleID); const auto* Cap=FindUniqueData(CapID);
	const auto* BottleDef=BottleRecord ? FindProductDefinition(BottleRecord->ProductID) : nullptr;
	const auto* CapDef=CapRecord ? FindProductDefinition(CapRecord->ProductID) : nullptr;
	if (!Bottle || !Cap || !BottleDef || !CapDef || !CapDef->bIsBottleCap || BottleDef->bIsBottleCap
		|| BottleDef->ConsumableProfile.NumOfPortions<=0 || Bottle->EventData.AttachedCapISID || Cap->EventData.AttachedBottleISID
		|| Bottle->EventData.CapSealed>0 || CapRecord->State==EPropState::Held || Cap->EventData.Strength<=0
		|| !IsBottleCapTypeSupportedInMask(BottleDef->SupportedCapMask,static_cast<uint8>(CapDef->DefaultCapType))
		|| FVector::DistSquared(GetTransform(BottleID).GetLocation(),GetTransform(CapID).GetLocation())>FMath::Square(150.f)) return false;
	const uint8 Type=static_cast<uint8>(CapDef->DefaultCapType);
	AActivePropActor* BottleActor=nullptr; AActivePropActor* CapActor=nullptr;
	if (!PromoteInstanceToASM(BottleID,EPropActivationReason::Interaction,false,BottleActor) || !BottleActor
		|| !PromoteInstanceToASM(CapID,EPropActivationReason::Interaction,false,CapActor) || !CapActor) return false;
	FPropEventSyncDelta B,C;
	B.ISID=BottleID; B.ExtendedDirtyMask=EPropExtendedEventMask::AttachedCap; B.AttachedCapISID=CapID;
	B.SetFlag(EPropEventDirtyMask::CapType); B.CapType=Type;
	C.ISID=CapID; C.ExtendedDirtyMask=EPropExtendedEventMask::AttachedBottle; C.AttachedBottleISID=BottleID;
	C.SetFlag(EPropEventDirtyMask::Grabbable); C.bIsGrabbable=false;
	if (!SubmitAtomicInstanceEventUpdates({B,C})) return false;
	SynchronizeCapAssembly(BottleID);
	return CapActor->GetAttachParentActor()==BottleActor;
}

bool APropManager::ClosePhysicalCap(int32 BottleID, float Torque)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !FMath::IsFinite(Torque) || Torque<0 || Torque>1 || !StockMayChange(this,BottleID)) return false;
	const auto* Record=FindISIDData(BottleID); const auto* Bottle=FindUniqueData(BottleID);
	const auto* Def=Record ? FindProductDefinition(Record->ProductID) : nullptr;
	const auto* Cap=Bottle ? FindUniqueData(Bottle->EventData.AttachedCapISID) : nullptr;
	if (!Bottle || !Def || !Cap || Cap->EventData.AttachedBottleISID!=BottleID || Cap->EventData.Strength<=0) return false;
	FPropEventSyncDelta D; D.ISID=BottleID; D.SetFlag(EPropEventDirtyMask::CapSealed);
	const bool SweetSpot=Torque>=Def->TorqueSweetSpotMin && Torque<=Def->TorqueSweetSpotMax;
	D.CapSealed=SweetSpot ? 1.f : FMath::Min(.99f,Torque/FMath::Max(.01f,Def->TorqueSweetSpotMin));
	if (Torque>Def->TorqueSweetSpotMax)
	{
		D.SetFlag(EPropEventDirtyMask::Strength);
		D.Strength=FMath::Max(0.f,Bottle->EventData.Strength-(Torque-Def->TorqueSweetSpotMax)*FMath::Max(0.f,Def->StrengthDamageRate));
	}
	return SubmitInstanceEventUpdate(BottleID,D);
}

bool APropManager::DetachPhysicalCap(int32 BottleID, FVector Velocity)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || Velocity.ContainsNaN() || !StockMayChange(this,BottleID)) return false;
	const auto* Bottle=FindUniqueData(BottleID);
	const int32 CapID=Bottle ? Bottle->EventData.AttachedCapISID : 0;
	const auto* Cap=FindUniqueData(CapID);
	if (!Bottle || !Cap || Cap->EventData.AttachedBottleISID!=BottleID) return false;
	FTransform Transform=GetTransform(CapID);
	if (const auto* Actor=GetActiveActorFromISID(CapID)) Transform=Actor->GetActorTransform();
	FPropEventSyncDelta B,C;
	B.ISID=BottleID; B.ExtendedDirtyMask=EPropExtendedEventMask::AttachedCap; B.AttachedCapISID=0;
	B.SetFlag(EPropEventDirtyMask::CapSealed); B.SetFlag(EPropEventDirtyMask::CapType); B.CapSealed=0; B.CapType=0;
	C.ISID=CapID; C.ExtendedDirtyMask=EPropExtendedEventMask::AttachedBottle; C.AttachedBottleISID=0;
	C.SetFlag(EPropEventDirtyMask::Grabbable); C.bIsGrabbable=true;
	if (!SubmitAtomicInstanceEventUpdates({B,C})) return false;
	if (auto* Actor=GetActiveActorFromISID(CapID))
	{
		Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		ReleaseGrab(CapID,Actor->InteractionOwner.Get(),Velocity);
	}
	else
	{
		InitializeInstanceTransform(CapID,Transform);
		if (!Velocity.IsNearlyZero()) ApplyImpulse(CapID,Velocity,true);
	}
	return true;
}

void APropManager::SynchronizeCapAssembly(int32 ISID)
{
	const auto* Unique=FindUniqueData(ISID);
	if (!Unique) return;
	const int32 BottleID=Unique->EventData.AttachedBottleISID ? Unique->EventData.AttachedBottleISID : ISID;
	const auto* Bottle=FindUniqueData(BottleID);
	if (!Bottle || !Bottle->EventData.AttachedCapISID || SynchronizingCapBottles.Contains(BottleID)) return;
	const int32 CapID=Bottle->EventData.AttachedCapISID;
	const auto* Cap=FindUniqueData(CapID); const auto* BottleRecord=FindISIDData(BottleID);
	const auto* Def=BottleRecord ? FindProductDefinition(BottleRecord->ProductID) : nullptr;
	auto* Parent=GetActiveActorFromISID(BottleID);
	if (!Cap || Cap->EventData.AttachedBottleISID!=BottleID || !Def || !Parent) return;
	const FTransform Local=Def->CapAttachmentTransform;
	SynchronizingCapBottles.Add(BottleID);
	ON_SCOPE_EXIT { SynchronizingCapBottles.Remove(BottleID); };
	auto* CapActor=GetActiveActorFromISID(CapID);
	if (!CapActor && HasAuthority()) PromoteInstanceToASM(CapID,EPropActivationReason::Interaction,false,CapActor);
	if (!CapActor) return; // Client conversion batch may arrive after the parent; cap activation retries binding.
	if (CapActor->GetAttachParentActor()!=Parent || !CapActor->bIsHeld)
	{
		if (HasAuthority()) { SetGrabber(CapID,Parent); SetPropState(CapID,EPropState::Held); }
		CapActor->OnPlayerGrab(Parent->GetRootComponent(),NAME_None);
		CapActor->SetActorRelativeTransform(Local,false,nullptr,ETeleportType::TeleportPhysics);
	}
	if (CapActor->PropMesh) CapActor->PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

bool APropManager::PrepareCapForBottleRest(int32 BottleID)
{
	const auto* Bottle=FindUniqueData(BottleID);
	const int32 CapID=Bottle ? Bottle->EventData.AttachedCapISID : 0;
	if (!CapID) return true;
	const auto* Cap=FindUniqueData(CapID); auto* Record=FindISIDData(CapID);
	if (!Cap || Cap->EventData.AttachedBottleISID!=BottleID || !Record) return false;
	auto* Actor=GetActiveActorFromISID(CapID);
	if (!Actor) return Record->Representation==EPropRepresentation::ISM;
	if (SynchronizingCapBottles.Contains(BottleID)) return false;
	SynchronizingCapBottles.Add(BottleID);
	ON_SCOPE_EXIT { SynchronizingCapBottles.Remove(BottleID); };
	const FTransform WorldTransform=Actor->GetActorTransform();
	Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Actor->bIsHeld=false; Actor->InteractionOwner=nullptr; Actor->InteractionState=EPropInteractionLockState::Free;
	if (Actor->PhysicsSimComponent) Actor->PhysicsSimComponent->SetHeld(false);
	if (Actor->PropMesh) Actor->PropMesh->SetSimulatePhysics(false);
	Record->State=EPropState::AtRest; Record->RuntimeData.InteractionOwner.Reset(); Record->RuntimeData.CurrentInteractor.Reset();
	Record->RuntimeData.InteractionState=EPropInteractionLockState::Free; Record->RuntimeData.bInteractionLock=false;
	Actor->State=EPropState::AtRest; Record->ActiveTransform=WorldTransform;
	return ConvertASMToISM(CapID,EPropState::AtRest,true,true);
}
