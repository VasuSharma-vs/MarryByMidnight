#include "Props/ColaPricingLibrary.h"

FColaPriceQuote UColaPricingLibrary::QuoteCola(double ContentLevel, int32 NominalPortions, double PricePerPortion,
	double Temperature, double OptimumTemperature, double TemperaturePenaltyRange, bool bExpired)
{
	FColaPriceQuote Quote;
	if (!FMath::IsFinite(ContentLevel) || ContentLevel < 0 || NominalPortions <= 0 || !FMath::IsFinite(PricePerPortion) || PricePerPortion < 0
		|| !FMath::IsFinite(Temperature) || !FMath::IsFinite(OptimumTemperature) || !FMath::IsFinite(TemperaturePenaltyRange) || TemperaturePenaltyRange <= 0) return Quote;
	Quote.Portions = ContentLevel * NominalPortions;
	const double BaseCents = Quote.Portions * PricePerPortion * 100.0;
	const double CompensationCents = FMath::Max(1.0, ContentLevel) * NominalPortions * PricePerPortion * 100.0;
	// Keep all monetary integers in the exact range of double precision and avoid integer overflow.
	if (!FMath::IsFinite(BaseCents) || !FMath::IsFinite(CompensationCents) || BaseCents > 9007199254740991.0 || CompensationCents > 9007199254740991.0) return FColaPriceQuote();
	Quote.bValid = true;
	Quote.bExpired = bExpired;
	Quote.TemperatureDiscount = FMath::Clamp(FMath::Abs(Temperature - OptimumTemperature) / TemperaturePenaltyRange, 0.0, 1.0) * 0.20;
	if (bExpired) Quote.CompensationCents = FMath::RoundToInt64(CompensationCents);
	else Quote.SaleCents = FMath::RoundToInt64(BaseCents * (1.0 - Quote.TemperatureDiscount));
	return Quote;
}

bool UColaPricingLibrary::CanReuseBottle(double Strength, bool bClean, double ContentLevel)
{
	return FMath::IsFinite(Strength) && Strength > 0.70 && Strength <= 1.0 && bClean && FMath::IsFinite(ContentLevel) && ContentLevel == 0.0;
}
