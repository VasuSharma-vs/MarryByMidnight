#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ShopSupplyCatalog.generated.h"

class UInteractablePropDataAsset;

UENUM(BlueprintType)
enum class EShopSupplyKind : uint8 { EmptyBottle, Cap, ColaCylinder };

UENUM(BlueprintType)
enum class ENormalColaFlavor : uint8 { Lemonade, Mint, Sweet, Spicy, ZeroCalorie };

USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FShopSupplyOffer
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName OfferID;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EShopSupplyKind Kind = EShopSupplyKind::EmptyBottle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) ENormalColaFlavor Flavor = ENormalColaFlavor::Lemonade;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UInteractablePropDataAsset> Product;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) int64 UnitCostCents = 100;
};

/** Authored procurement offers. Prices and deliverable products never come from player requests. */
UCLASS(BlueprintType)
class MARRYBYMIDNIGHT_API UShopSupplyCatalog : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FShopSupplyOffer> Offers;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UInteractablePropDataAsset> EmptyBottleProduct;
	/** Exactly the five authored Normal Cola products, in ENormalColaFlavor order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<TSoftObjectPtr<UInteractablePropDataAsset>> FinishedBottleProducts;
	const FShopSupplyOffer* FindOffer(FName OfferID) const;
};
