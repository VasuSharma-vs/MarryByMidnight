#pragma once
#include "CoreMinimal.h"
#include "Props/ColaPricingLibrary.h"
#include "Shop/ShopCashTypes.h"
#include "ShopCheckoutTypes.generated.h"

UENUM(BlueprintType)
enum class EShopCheckoutResult : uint8
{
	Success, AlreadyCompleted, NotAuthority, InvalidRequest, Unavailable, InvalidProduct,
	ExpiredProduct, Unpaid, IncorrectChange, QuoteChanged, WalletFailure, NotFound, OperationConflict,
	CompensationPending, InvalidReplacement, AlreadyResolved, CashUnavailable
};

/** Derived scanner view. Reading it never changes representation, stock, or wallet state. */
USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FShopProductScanData
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop|Scanner") int32 ISID = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop|Scanner") int32 ProductID = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop|Scanner") int32 ContentID = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop|Scanner") FText DisplayName;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop|Scanner") float ContentLevel = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop|Scanner") int32 NominalPortions = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop|Scanner") float Temperature = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop|Scanner") float OptimumTemperature = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop|Scanner") float Strength = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop|Scanner") bool bClean = false;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop|Scanner") float CapSealed = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop|Scanner") double ContainerCreationTime = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop|Scanner") double ContentCreationTime = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop|Scanner") double ContentAgeSeconds = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop|Scanner") FColaPriceQuote Price;
	UPROPERTY(SaveGame) uint32 EventSequence = 0;
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FShopStockEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop") int32 ISID = 0;
	/** Invalid GUID means shop-owned; a valid GUID is the persistent customer identity. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop") FGuid CustomerOwner;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop") int64 OriginalPurchaseCents = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop") FTransform StockTransform = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FCheckoutItemQuote
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") int32 ISID = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") int64 SaleCents = 0;
	UPROPERTY(SaveGame) uint32 EventSequence = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") int32 ProductID = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") int32 ContentID = 0;
	/** Immutable once the original invoice is locked. Individual identities survive display grouping. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") FShopProductScanData Snapshot;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") bool bSaved = false;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") bool bInScannerVolume = false;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") bool bAvailable = true;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") bool bExpiredAtSale = false;
	static FCheckoutItemQuote FromScan(const FShopProductScanData& Scan)
	{
		FCheckoutItemQuote Q;
		Q.ISID = Scan.ISID; Q.ProductID = Scan.ProductID; Q.ContentID = Scan.ContentID;
		Q.SaleCents = Scan.Price.SaleCents; Q.EventSequence = Scan.EventSequence; Q.Snapshot = Scan;
		return Q;
	}
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FShopGroupedEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") int32 ProductID = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") int32 ContentID = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") FText DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") TArray<int32> BottleISIDs;
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") int32 ExpiredCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") int64 OriginalSubtotalCents = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") int64 ContentDiscountCents = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") int64 TemperatureDiscountCents = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") int64 FinalSubtotalCents = 0;
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FShopCheckoutView
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") FGuid CheckoutID;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") FGuid CustomerID;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") TArray<FCheckoutItemQuote> Items;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") int64 TotalCents = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") int64 DetailedTotalCents = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") int64 RoundingAdjustmentCents = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") bool bInvoiceLocked = false;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") int64 TenderCents = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") int64 ChangeCents = 0;
};

UENUM(BlueprintType)
enum class EShopScannerPhase : uint8
{
	Scanning, Checkout, Completed
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FShopScannerState
{
	GENERATED_BODY()
	/** Rotates on cart reset/cancellation so a delayed input cannot act on the next customer. */
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") FGuid SessionID;
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") EShopScannerPhase Phase = EShopScannerPhase::Scanning;
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") FShopCheckoutView Checkout;
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") FShopProductScanData LastScan;
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") EShopCheckoutResult LastResult = EShopCheckoutResult::Success;
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") TArray<FShopGroupedEntry> Groups;
	UPROPERTY(BlueprintReadOnly, Category = "Scanner") TArray<FShopProductScanData> ReplacementCandidates;
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FCompletedCheckoutReceipt
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop") TArray<int32> TenderCoinISIDs;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Shop") TArray<int32> ChangeCoinISIDs;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") FGuid CheckoutID;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") FGuid CustomerID;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") TArray<int32> ItemISIDs;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") int64 TotalCents = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") int64 DetailedTotalCents = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") int64 RoundingAdjustmentCents = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") TArray<FCheckoutItemQuote> Items;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") double CompletedTime = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Checkout") int64 Sequence = 0;
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FShopSettlementSummary
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Checkout") FGuid CheckoutID;
	UPROPERTY(BlueprintReadOnly, Category = "Checkout") int64 AdjustedRevenueCents = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Checkout") int64 OutstandingDeductionCents = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Checkout") int32 UnselectedCompensations = 0;
};

UENUM(BlueprintType)
enum class EShopCompensationChoice : uint8 { Unselected, Replacement, AdditionalDeduction };

UENUM(BlueprintType)
enum class EShopAdjustmentKind : uint8 { AutomaticExpiryPenalty, CashCompensation, FreeReplacement };

/** One durable case per expired bottle on a completed invoice. Invoice items are never edited. */
USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FShopExpiryCase
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") FGuid CaseID;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") FGuid CheckoutID;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") FGuid CustomerID;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") int32 OriginalISID = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") int32 ContentID = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") int32 NominalPortions = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") int64 OriginalBaseCents = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") int64 AutomaticPaidCents = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") int64 CompensationPaidCents = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") EShopCompensationChoice Choice = EShopCompensationChoice::Unselected;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") int32 ReplacementISID = 0;
	bool IsResolved() const
	{
		return AutomaticPaidCents == OriginalBaseCents &&
			((Choice == EShopCompensationChoice::Replacement && ReplacementISID > 0) ||
			 (Choice == EShopCompensationChoice::AdditionalDeduction && CompensationPaidCents == OriginalBaseCents));
	}
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FShopPostSaleAdjustment
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") FGuid AdjustmentID;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") FGuid CaseID;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") FGuid CheckoutID;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") EShopAdjustmentKind Kind = EShopAdjustmentKind::AutomaticExpiryPenalty;
	/** Positive means money deducted from the original sale's revenue. Replacement has zero cash amount. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") int64 DeductedCents = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") int32 ReplacementISID = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Compensation") double Time = 0;
};

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FShopCheckoutSaveData
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) TArray<FShopStockEntry> Stock;
	UPROPERTY(SaveGame) TArray<FCompletedCheckoutReceipt> Completed;
	UPROPERTY(SaveGame) TArray<FShopExpiryCase> ExpiryCases;
	UPROPERTY(SaveGame) TArray<FShopPostSaleAdjustment> Adjustments;
	UPROPERTY(SaveGame) FShopCashSaveData Cash;
};
