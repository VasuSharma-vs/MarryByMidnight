#include "Props/InteractablePropManager.h"

bool APropManager::GetProductScanData(int32 InISID, FShopProductScanData& OutData) const
{
	OutData = FShopProductScanData();
	const FISIDRuntimeIndex* Record = FindISIDData(InISID);
	const FPropInstanceUniqueData* Unique = FindUniqueData(InISID);
	const FPropProductDefinition* Product = Record ? FindProductDefinition(Record->ProductID) : nullptr;
	const FPropContentDefinition* Content = Unique ? FindContentDefinition(Unique->EventData.ContentID) : nullptr;
	if (!Record || !Unique || !Product || !Content || Unique->EventData.ContentLevel < 0
		|| !FMath::IsFinite(WorldElapsedSeconds) || WorldElapsedSeconds < 0
		|| !FMath::IsFinite(Unique->EventData.CreationTime) || Unique->EventData.CreationTime < 0
		|| !FMath::IsFinite(Unique->EventData.ContentCreationTime) || Unique->EventData.ContentCreationTime < 0 || Unique->EventData.ContentCreationTime > WorldElapsedSeconds
		|| !FMath::IsFinite(Unique->EventData.Strength) || Unique->EventData.Strength < 0 || Unique->EventData.Strength > 1
		|| !FMath::IsFinite(Unique->EventData.CapSealed) || Unique->EventData.CapSealed < 0 || Unique->EventData.CapSealed > 1
		|| (Content->ExpiryProfile.bHasExpiry && (!FMath::IsFinite(Content->ExpiryProfile.ExpiryLifetimeMinutes) || Content->ExpiryProfile.ExpiryLifetimeMinutes <= 0))) return false;

	const double Age = WorldElapsedSeconds - Unique->EventData.ContentCreationTime;
	const bool Expired = Content->ExpiryProfile.bHasExpiry && Age >= Content->ExpiryProfile.ExpiryLifetimeMinutes * 60.0;
	const FColaPriceQuote Price = UColaPricingLibrary::QuoteCola(Unique->EventData.ContentLevel, Product->ConsumableProfile.NumOfPortions,
		Content->BasePortionPrice, Unique->ContinuousData.Temperature, Content->IdealServingTemperature, Content->TemperaturePenaltyRange, Expired);
	if (!Price.bValid) return false;

	OutData.ISID = InISID;
	OutData.ProductID = Record->ProductID;
	OutData.ContentID = Unique->EventData.ContentID;
	OutData.DisplayName = Content->DisplayName.IsEmpty() ? Product->DisplayName : Content->DisplayName;
	OutData.ContentLevel = Unique->EventData.ContentLevel;
	OutData.NominalPortions = Product->ConsumableProfile.NumOfPortions;
	OutData.Temperature = Unique->ContinuousData.Temperature;
	OutData.OptimumTemperature = Content->IdealServingTemperature;
	OutData.Strength = Unique->EventData.Strength;
	OutData.bClean = Unique->EventData.bClean;
	OutData.CapSealed = Unique->EventData.CapSealed;
	OutData.ContainerCreationTime = Unique->EventData.CreationTime;
	OutData.ContentCreationTime = Unique->EventData.ContentCreationTime;
	OutData.ContentAgeSeconds = Age;
	OutData.EventSequence = Unique->EventData.EventSequence;
	OutData.Price = Price;
	return true;
}
