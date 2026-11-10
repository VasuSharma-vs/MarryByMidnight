#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Shop/ShopCheckoutTypes.h"
#include "ShopCashRegisterComponent.generated.h"

class UBoxComponent;
class UShopScannerComponent;

/** An empty, finite register. Only real coins inside its payment trays count. */
UCLASS(ClassGroup = (Shop), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UShopCashRegisterComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UShopCashRegisterComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cash") TObjectPtr<UShopScannerComponent> Scanner;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cash") TObjectPtr<UBoxComponent> TenderVolume;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cash") TObjectPtr<UBoxComponent> ChangeVolume;
	/** Saved in the station asset/placement, never generated anew during play. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cash") FGuid RegisterID;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Cash") EShopCheckoutResult RefreshPayment();
	static TSet<int32> QueryCoinsInVolume(const UBoxComponent* Volume);
private:
	FTimerHandle PollTimer;
	FGuid ActiveCheckoutID;
};
