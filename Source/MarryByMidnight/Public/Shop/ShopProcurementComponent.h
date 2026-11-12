#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Shop/ShopSupplyCatalog.h"
#include "ShopProcurementComponent.generated.h"

UENUM(BlueprintType)
enum class EShopPurchaseResult : uint8
{
	Success, AlreadyPurchased, NotAuthority, InvalidOffer, InvalidQuantity,
	InsufficientFunds, OperationConflict, DeliveryFailed
};

/** Trusted server service on Prop Manager. An owned-player adapter must validate station access first. */
UCLASS(ClassGroup=(Shop), meta=(BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UShopProcurementComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UShopProcurementComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Supplies") TSoftObjectPtr<UShopSupplyCatalog> Catalog;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Supplies", meta=(ClampMin="1",ClampMax="64")) int32 MaxQuantityPerPurchase = 32;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Supplies|Delivery") bool bUseDeliveryArea=false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Supplies|Delivery", meta=(ClampMin="1",ClampMax="8")) int32 DeliveryColumns=4;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Supplies|Delivery", meta=(ClampMin="1",ClampMax="16")) int32 DeliveryRows=16;
	bool Quote(FName OfferID, int32 Quantity, int64& TotalCents) const;
	bool InitializeCatalog();
	bool IsNormalContent(int32 ContentID) const;
	bool IsCylinderProduct(int32 ProductID) const;
	bool IsNormalPrototypeActive() const { return bCatalogInitialized; }
	EShopPurchaseResult Purchase(FGuid OperationID, FName OfferID, int32 Quantity, const FTransform& DeliveryTransform, TArray<int32>& DeliveredISIDs);
private:
	/** A soft asset path does not retain the initialized catalog across garbage collection. */
	UPROPERTY(Transient) TObjectPtr<UShopSupplyCatalog> RuntimeCatalog;
	bool bCatalogInitialized=false;
};
