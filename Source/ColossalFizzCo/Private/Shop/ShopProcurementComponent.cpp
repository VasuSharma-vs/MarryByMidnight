#include "Shop/ShopProcurementComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractableProp.h"
#include "Shop/ShopInteractionGeometry.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "CollisionShape.h"
#include "CollisionQueryParams.h"

UShopProcurementComponent::UShopProcurementComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	Catalog = TSoftObjectPtr<UShopSupplyCatalog>(FSoftObjectPath(TEXT("/Game/NormalColaPrototype/DA_NormalColaSupplyCatalog.DA_NormalColaSupplyCatalog")));
}

bool UShopProcurementComponent::InitializeCatalog()
{
	if (bCatalogInitialized) return true;
	auto* Manager=Cast<APropManager>(GetOwner()); const auto* Data=Catalog.LoadSynchronous();
	if (!Manager || !Manager->HasAuthority() || GetNetMode()==NM_Client || !Data || Data->FinishedBottleProducts.Num()!=5) return false;
	auto* Empty=Data->EmptyBottleProduct.LoadSynchronous();
	if (!Empty || Empty->NumOfPortions<=0 || Empty->DefaultContentLevel!=0) return false;
	TSet<int32> Contents; TArray<UInteractablePropDataAsset*> Bottles;
	for (const auto& Ref:Data->FinishedBottleProducts)
	{
		auto* Product=Ref.LoadSynchronous();
		if (!Product || Product->DefaultContentID<=0 || Product->NumOfPortions!=Empty->NumOfPortions
			|| Product->BottleMaterialType!=Empty->BottleMaterialType || Contents.Contains(Product->DefaultContentID)) return false;
		Contents.Add(Product->DefaultContentID); Bottles.Add(Product);
	}
	for (auto* Product:Bottles)
	{
		const auto Definition=FPropProductDefinition::FromDataAsset(Product,0,Product->DefaultContentID);
		FPropContentDefinition Content; Content.ContentID=Product->DefaultContentID; Content.DisplayName=Product->ItemDisplayName;
		Content.BasePortionPrice=Product->Price; Content.IdealServingTemperature=Product->ConsumableTemperature;
		Content.DensityGPerMl=FMath::IsFinite(Product->LiquidDensityGPerMl) ? FMath::Max(.001f,Product->LiquidDensityGPerMl) : 1.04f;
		Content.ExpiryProfile=Definition.ExpiryProfile; Content.TasteProfile=Definition.TasteProfile; Content.ExpiredTasteProfile=Definition.ExpiredTasteProfile;
		Content.StimulusProfile=Definition.StimulusProfile; Content.HazardType=EColaHazardType::FoamRocket;
		if (Product->MaterialOverrides.Num()>1) Content.LiquidMaterial=Product->MaterialOverrides[1];
		if (!Manager->RegisterContentDefinition(Content)) return false;
	}
	const int32 EmptyPID=Manager->RegisterProductFromDataAsset(Empty);
	if (EmptyPID<=0) return false;
	TMap<int32,int32> Mapping;
	for (auto* Product:Bottles)
	{
		const int32 PID=Manager->RegisterProductFromDataAsset(Product); if (PID<=0) return false;
		Mapping.Add(Product->DefaultContentID,PID);
	}
	TArray<int32> PIDs; Mapping.GenerateValueArray(PIDs); PIDs.Add(EmptyPID);
	for (int32 PID:PIDs)
	{
		FPropProductDefinition Product=*Manager->FindProductDefinition(PID);
		Product.EmptyBottleProductID=EmptyPID; Product.FilledProductIDByContentID=Mapping; Product.SupportedContentIDs=Contents.Array();
		if (!Manager->RegisterProductDefinition(Product)) return false;
	}
	RuntimeCatalog=Catalog.Get();
	bCatalogInitialized=true; return true;
}

bool UShopProcurementComponent::IsNormalContent(int32 ID) const
{
	if (ID<=0) return false;
	const auto* Data=Catalog.Get(); if (!Data) return false;
	for (const auto& Ref:Data->FinishedBottleProducts) if (const auto* Product=Ref.Get()) if (Product->DefaultContentID==ID) return true;
	return false;
}

bool UShopProcurementComponent::IsCylinderProduct(int32 PID) const
{
	const auto* Manager=Cast<APropManager>(GetOwner()); const auto* Data=Catalog.Get();
	const auto* Product=Manager ? Manager->FindProductDefinition(PID) : nullptr;
	if (!Data || !Product || !Product->SourceDataAsset) return false;
	for (const auto& Offer:Data->Offers)
		if (Offer.Kind==EShopSupplyKind::ColaCylinder && Offer.Product.Get()==Product->SourceDataAsset.Get()) return true;
	return false;
}

bool UShopProcurementComponent::Quote(FName OfferID, int32 Quantity, int64& TotalCents) const
{
	TotalCents = 0;
	if (Quantity < 1 || Quantity > FMath::Clamp(MaxQuantityPerPurchase,1,64)) return false;
	const auto* Data = Catalog.LoadSynchronous();
	const auto* Offer = Data ? Data->FindOffer(OfferID) : nullptr;
	if (!Offer || Offer->Product.IsNull() || Offer->UnitCostCents <= 0 || Offer->UnitCostCents > MAX_int64 / Quantity) return false;
	TotalCents = Offer->UnitCostCents * Quantity;
	return true;
}

EShopPurchaseResult UShopProcurementComponent::Purchase(FGuid ID, FName OfferID, int32 Quantity, const FTransform& Delivery, TArray<int32>& Delivered)
{
	Delivered.Reset();
	auto* Manager = Cast<APropManager>(GetOwner());
	if (!Manager || !Manager->HasAuthority() || Manager->GetNetMode() == NM_Client) return EShopPurchaseResult::NotAuthority;
	if (!ID.IsValid() || Delivery.ContainsNaN() || !Delivery.IsValid() || !Manager->SharedWallet) return EShopPurchaseResult::DeliveryFailed;
	if (Quantity < 1 || Quantity > FMath::Clamp(MaxQuantityPerPurchase,1,64)) return EShopPurchaseResult::InvalidQuantity;
	int64 Total = 0;
	if (!Quote(OfferID, Quantity, Total)) return EShopPurchaseResult::InvalidOffer;
	const FName Reason(*FString::Printf(TEXT("Supply:%s:%d"), *OfferID.ToString(), Quantity));
	FWalletReceipt Previous;
	if (Manager->SharedWallet->FindReceipt(ID, Previous))
		return Previous.Reason == Reason && Previous.DeltaCents < 0 ? EShopPurchaseResult::AlreadyPurchased : EShopPurchaseResult::OperationConflict;
	const FShopSupplyOffer Offer = *Catalog.LoadSynchronous()->FindOffer(OfferID);
	if (!Catalog.Get()->FinishedBottleProducts.IsEmpty() && !InitializeCatalog()) return EShopPurchaseResult::InvalidOffer;
	UInteractablePropDataAsset* Product = Offer.Product.LoadSynchronous();
	if (!Product || !Product->StaticMesh || !FMath::IsFinite(Product->NormalCapacity)) return EShopPurchaseResult::InvalidOffer;
	const auto Result = Manager->SharedWallet->CommitWithResources(ID, -Total, Reason, [&]()
	{
		const int32 PID = Manager->RegisterProductFromDataAsset(Product);
		if (PID <= 0) return false;
		FVector CellSize=ShopInteractionGeometry::MeshGridSpacing(Product->StaticMesh,Delivery.GetScale3D());
		TArray<FTransform> Placements;
		if (bUseDeliveryArea)
		{
			// One shared footprint keeps mixed orders separated, including after save/load.
			for (const auto& Entry:Catalog.Get()->Offers) if (const auto* Other=Entry.Product.LoadSynchronous())
				CellSize=CellSize.ComponentMax(ShopInteractionGeometry::MeshGridSpacing(Other->StaticMesh,Delivery.GetScale3D()));
			TArray<FBox> Occupied;
			for (const auto& Pair:Manager->ISIDData)
				if (const auto* Def=Manager->FindProductDefinition(Pair.Value.ProductID)) if (Def->StaticMesh)
					Occupied.Add(Def->StaticMesh->GetBoundingBox().TransformBy(Manager->GetTransform(Pair.Key)));
			const int32 Columns=FMath::Clamp(DeliveryColumns,1,8), Rows=FMath::Clamp(DeliveryRows,1,16);
			const int32 Capacity=FMath::Min(64,Columns*Rows);
			for (int32 Slot=0;Slot<Capacity && Placements.Num()<Quantity;++Slot)
			{
				const auto Transform=ShopInteractionGeometry::GridItemTransform(Delivery,CellSize,Slot,Capacity,FVector::OneVector,Columns,Rows);
				const FBox Bounds=Product->StaticMesh->GetBoundingBox().TransformBy(Transform);
				const FBox Interior=Bounds.ExpandBy(-.5f);
				if (Occupied.ContainsByPredicate([&](const auto& Other){return Interior.Intersect(Other);})) continue;
				const FVector Extent=Interior.GetExtent().ComponentMax(FVector(.1));
				if (GetWorld()->OverlapBlockingTestByChannel(Interior.GetCenter(),FQuat::Identity,ECC_PhysicsBody,
					FCollisionShape::MakeBox(Extent),FCollisionQueryParams(SCENE_QUERY_STAT(SupplyDeliverySpace),false))) continue;
				Placements.Add(Transform); Occupied.Add(Bounds);
			}
			if (Placements.Num()!=Quantity) return false;
		}
		for (int32 Index=0; Index<Quantity; ++Index)
		{
			const FTransform Transform=bUseDeliveryArea ? Placements[Index] : ShopInteractionGeometry::GridItemTransform(Delivery,CellSize,Index,Quantity);
			const int32 ISID = Manager->GenerateProductInstance(PID,Transform);
			if (ISID <= 0)
			{
				for (int32 Created : Delivered) Manager->ReleaseInstanceToEmpty(Created);
				Delivered.Reset();
				return false;
			}
			Delivered.Add(ISID);
			AActivePropActor* Actor=nullptr;
			if (!Manager->PromoteInstanceToASM(ISID,EPropActivationReason::Drop,true,Actor) || !Actor)
			{
				for (int32 Created:Delivered) Manager->ReleaseInstanceToEmpty(Created);
				Delivered.Reset(); return false;
			}
		}
		return true;
	});
	switch (Result)
	{
	case EWalletOperationResult::Applied: return EShopPurchaseResult::Success;
	case EWalletOperationResult::AlreadyApplied: return EShopPurchaseResult::AlreadyPurchased;
	case EWalletOperationResult::InsufficientFunds: return EShopPurchaseResult::InsufficientFunds;
	case EWalletOperationResult::OperationConflict: return EShopPurchaseResult::OperationConflict;
	default: return EShopPurchaseResult::DeliveryFailed;
	}
}
