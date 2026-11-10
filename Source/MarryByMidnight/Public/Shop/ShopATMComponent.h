#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Shop/ShopCheckoutTypes.h"
#include "ShopATMComponent.generated.h"
class UBoxComponent;
class USceneComponent;

UCLASS(ClassGroup = (Shop), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UShopATMComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UShopATMComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATM") TObjectPtr<UBoxComponent> DepositVolume;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ATM") TObjectPtr<USceneComponent> CoinOutput;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "ATM") EShopCheckoutResult Withdraw(FGuid OperationID, const TArray<int32>& Denominations);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "ATM") void CollectDeposits();
private:
	FTimerHandle DepositTimer;
};
