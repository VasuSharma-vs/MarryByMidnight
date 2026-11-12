#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ShopInteractionLibrary.generated.h"

UCLASS()
class MARRYBYMIDNIGHT_API UShopInteractionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "Shop") static void OpenShopTerminal(AActor* Player, AActor* Station);
	/** Returns true only when the local player's aimed shop terminal opened. */
	UFUNCTION(BlueprintCallable, Category="Shop") static bool TryOpenShopTerminalFromView(AActor* Player);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop") static bool RegisterCustomerAtCounter(AActor* Customer, AActor* Counter);
};
