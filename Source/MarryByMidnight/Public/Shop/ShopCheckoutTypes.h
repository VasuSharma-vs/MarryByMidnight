#pragma once
#include "CoreMinimal.h"
#include "Props/ColaPricingLibrary.h"
#include "ShopCheckoutTypes.generated.h"

UENUM(BlueprintType)
enum class EShopCheckoutResult : uint8
{
	Success, AlreadyCompleted, NotAuthority, InvalidRequest, Unavailable, InvalidProduct,
	ExpiredProduct, Unpaid, IncorrectChange, QuoteChanged, WalletFailure, NotFound, OperationConflict
};

/** Derived scanner view. Reading it never changes representation, stock, or wallet state. */
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FShopProductScanData
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Shop|Scanner") int32 ISID = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shop|Scanner") int32 ProductID = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shop|Scanner") int32 ContentID = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shop|Scanner") FText DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "Shop|Scanner") float ContentLevel = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shop|Scanner") int32 NominalPortions = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shop|Scanner") float Temperature = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shop|Scanner") float OptimumTemperature = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shop|Scanner") float Strength = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shop|Scanner") bool bClean = false;
	UPROPERTY(BlueprintReadOnly, Category = "Shop|Scanner") float CapSealed = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shop|Scanner") double ContainerCreationTime = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shop|Scanner") double ContentCreationTime = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shop|Scanner") double ContentAgeSeconds = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shop|Scanner") FColaPriceQuote Price;
	UPROPERTY() uint32 EventSequence = 0;
};

USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FShopStockEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop") int32 ISID = 0;
	/** Invalid GUID means shop-owned; a valid GUID is the persistent customer identity. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop") FGuid CustomerOwner;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop") int64 OriginalPurchaseCents = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop") FTransform StockTransform = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FCheckoutItemQuote
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Checkout") int32 ISID = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Checkout") int64 SaleCents = 0;
	UPROPERTY() uint32 EventSequence = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Checkout") int32 ProductID = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Checkout") int32 ContentID = 0;
};

USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FShopCheckoutView
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Checkout") FGuid CheckoutID;
	UPROPERTY(BlueprintReadOnly, Category = "Checkout") FGuid CustomerID;
	UPROPERTY(BlueprintReadOnly, Category = "Checkout") TArray<FCheckoutItemQuote> Items;
	UPROPERTY(BlueprintReadOnly, Category = "Checkout") int64 TotalCents = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Checkout") int64 TenderCents = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Checkout") int64 ChangeCents = 0;
};

UENUM(BlueprintType)
enum class EShopScannerPhase : uint8
{
	Scanning, Checkout, Completed
};

USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FShopScannerState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") EShopScannerPhase Phase = EShopScannerPhase::Scanning;
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") FShopCheckoutView Checkout;
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") FShopProductScanData LastScan;
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") EShopCheckoutResult LastResult = EShopCheckoutResult::Success;
};

USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FCompletedCheckoutReceipt
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") FGuid CheckoutID;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") FGuid CustomerID;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") TArray<int32> ItemISIDs;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") int64 TotalCents = 0;
};

USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FShopCheckoutSaveData
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) TArray<FShopStockEntry> Stock;
	UPROPERTY(SaveGame) TArray<FCompletedCheckoutReceipt> Completed;
};
