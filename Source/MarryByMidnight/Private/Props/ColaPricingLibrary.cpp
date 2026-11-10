#include "Props/ColaPricingLibrary.h"

FColaPriceQuote UColaPricingLibrary::QuoteCola(double ContentLevel, int32 NominalPortions, double PricePerPortion,
	double Temperature, double OptimumTemperature, double TemperaturePenaltyRange, bool bExpired)
{
	FColaPriceQuote Quote;
	if (!FMath::IsFinite(ContentLevel) || ContentLevel < 0 || NominalPortions <= 0 || !FMath::IsFinite(PricePerPortion) || PricePerPortion < 0
		|| !FMath::IsFinite(Temperature) || !FMath::IsFinite(OptimumTemperature) || !FMath::IsFinite(TemperaturePenaltyRange) || TemperaturePenaltyRange <= 0) return Quote;
	Quote.Portions = ContentLevel * NominalPortions;
	const double FullBaseCents = static_cast<double>(NominalPortions) * PricePerPortion * 100.0;
	const double ContentMultiplier = FMath::Clamp(ContentLevel, 0.0, 1.0);
	const double BaseCents = FullBaseCents * ContentMultiplier;
	// Keep all monetary integers in the exact range of double precision and avoid integer overflow.
	if (!FMath::IsFinite(BaseCents) || !FMath::IsFinite(FullBaseCents) || FullBaseCents > 9007199254740991.0) return FColaPriceQuote();
	Quote.bValid = true;
	Quote.bExpired = bExpired;
	Quote.OriginalBaseCents = FMath::RoundToInt64(FullBaseCents);
	Quote.ContentDiscount = 1.0 - ContentMultiplier;
	Quote.ContentDiscountCents = FMath::RoundToInt64(FullBaseCents - BaseCents);
	Quote.TemperatureDiscount = FMath::Clamp(FMath::Abs(Temperature - OptimumTemperature) / TemperaturePenaltyRange, 0.0, 1.0) * 0.20;
	// Expiry is a post-sale adjustment. It must not silently alter or reject the original invoice.
	Quote.SaleCents = FMath::RoundToInt64(BaseCents * (1.0 - Quote.TemperatureDiscount));
	Quote.TemperatureDiscountCents = Quote.OriginalBaseCents - Quote.ContentDiscountCents - Quote.SaleCents;
	if (bExpired) Quote.CompensationCents = Quote.OriginalBaseCents;
	return Quote;
}

int64 UColaPricingLibrary::RoundBillToWholeDollar(int64 DetailedCents)
{
	if (DetailedCents < 0) return -1;
	const int64 Whole = DetailedCents / 100;
	const int64 RoundedWhole = Whole + (DetailedCents % 100 >= 50 ? 1 : 0);
	return RoundedWhole <= MAX_int64 / 100 ? RoundedWhole * 100 : -1;
}

bool UColaPricingLibrary::CanReuseBottle(double Strength, bool bClean, double ContentLevel)
{
	return FMath::IsFinite(Strength) && Strength > 0.70 && Strength <= 1.0 && bClean && FMath::IsFinite(ContentLevel) && ContentLevel == 0.0;
}
