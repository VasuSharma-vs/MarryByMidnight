#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ColaPricingLibrary.generated.h"

USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FColaPriceQuote
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bValid = false;
	UPROPERTY(BlueprintReadOnly) bool bExpired = false;
	UPROPERTY(BlueprintReadOnly) double Portions = 0;
	UPROPERTY(BlueprintReadOnly) double TemperatureDiscount = 0;
	UPROPERTY(BlueprintReadOnly) int64 SaleCents = 0;
	UPROPERTY(BlueprintReadOnly) int64 CompensationCents = 0;
};

/** One pricing implementation for scanners, AI and UI. Currency rounds once at quote boundaries. */
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
};
