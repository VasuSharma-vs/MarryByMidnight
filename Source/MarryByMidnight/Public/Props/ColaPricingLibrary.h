#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ColaPricingLibrary.generated.h"

USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FColaPriceQuote
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, SaveGame) bool bValid = false;
	UPROPERTY(BlueprintReadOnly, SaveGame) bool bExpired = false;
	UPROPERTY(BlueprintReadOnly, SaveGame) double Portions = 0;
	/** Normal, full-bottle base price. Neither overfill nor discounts increase/decrease this penalty basis. */
	UPROPERTY(BlueprintReadOnly, SaveGame) int64 OriginalBaseCents = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame) double ContentDiscount = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame) int64 ContentDiscountCents = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame) int64 TemperatureDiscountCents = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame) double TemperatureDiscount = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame) int64 SaleCents = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame) int64 CompensationCents = 0;
};

/** One pricing implementation for scanners, AI and UI. Quotes use cents; only the aggregate bill rounds to dollars. */
UCLASS()
class MARRYBYMIDNIGHT_API UColaPricingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, Category="Shop|Pricing")
	static FColaPriceQuote QuoteCola(double ContentLevel, int32 NominalPortions, double PricePerPortion,
		double Temperature, double OptimumTemperature, double TemperaturePenaltyRange, bool bExpired);
	UFUNCTION(BlueprintPure, Category="Shop|Bottles")
	static bool CanReuseBottle(double Strength, bool bClean, double ContentLevel);
	/** Nonnegative cents -> nearest whole dollar, half-dollar ties up; -1 on invalid/overflow. */
	static int64 RoundBillToWholeDollar(int64 DetailedCents);
};
