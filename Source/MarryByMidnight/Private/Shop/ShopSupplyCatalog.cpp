#include "Shop/ShopSupplyCatalog.h"

const FShopSupplyOffer* UShopSupplyCatalog::FindOffer(FName OfferID) const
{
	if (OfferID.IsNone()) return nullptr;
	const FShopSupplyOffer* Found = nullptr;
	for (const auto& Offer : Offers)
	{
		if (Offer.OfferID != OfferID) continue;
		if (Found) return nullptr; // Ambiguous authored IDs cannot select a price/product safely.
		Found = &Offer;
	}
	return Found;
}
