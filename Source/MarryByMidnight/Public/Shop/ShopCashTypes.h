#pragma once

#include "CoreMinimal.h"
#include "ShopCashTypes.generated.h"

class UInteractablePropDataAsset;

USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FShopCurrencyRecipe
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cash") int32 Dollars = 1;
	/** Defer loading until use; coin actor interfaces can refer back to player/shop classes during native CDO construction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cash") TSoftObjectPtr<UInteractablePropDataAsset> Product;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cash", meta = (ClampMin = "0.001")) float MeshScale = .04f;
};

UENUM(BlueprintType)
enum class EShopCoinCustody : uint8 { World, Drawer, Tender, Change };

USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FShopCashCoin
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Cash") int32 ISID = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Cash") int32 ProductID = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Cash") int32 Dollars = 0;
	/** Invalid means team-owned. Customer cash becomes team cash only on committed sale. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Cash") FGuid CustomerOwner;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Cash") FGuid SourceOperationID;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Cash") EShopCoinCustody Custody = EShopCoinCustody::World;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Cash") FGuid RegisterID;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Cash") FGuid CheckoutID;
};

UENUM(BlueprintType)
enum class EShopCashOperationKind : uint8 { ATMWithdrawal, ATMDeposit, CustomerTender, CheckoutTransfer };

USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FShopCashOperation
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FGuid OperationID;
	UPROPERTY(SaveGame) EShopCashOperationKind Kind = EShopCashOperationKind::ATMWithdrawal;
	UPROPERTY(SaveGame) FGuid CustomerID;
	UPROPERTY(SaveGame) TArray<int32> CoinISIDs;
	UPROPERTY(SaveGame) TArray<int32> CoinDollars;
	UPROPERTY(SaveGame) TArray<int32> CoinProductIDs;
	UPROPERTY(SaveGame) int64 AmountCents = 0;
	UPROPERTY(SaveGame) int64 Sequence = 0;
	/** CheckoutTransfer lists customer tender first, then team-owned change. */
	UPROPERTY(SaveGame) int32 TenderCoinCount = 0;
};

USTRUCT()
struct MARRYBYMIDNIGHT_API FShopCashSaveData
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) TArray<FShopCashCoin> Coins;
	UPROPERTY(SaveGame) TArray<FShopCashOperation> Operations;
};

struct FShopPhysicalCashAllocation
{
	FGuid RegisterID;
	TArray<int32> Tender;
	TArray<int32> Change;
};
