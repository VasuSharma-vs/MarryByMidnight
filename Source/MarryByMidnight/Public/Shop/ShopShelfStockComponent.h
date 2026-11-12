#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShopShelfStockComponent.generated.h"
class UBoxComponent;

/** Manufactures become sale stock only after being placed on a shelf with a compatible closed cap. */
UCLASS(ClassGroup=(Shop),meta=(BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UShopShelfStockComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UShopShelfStockComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Stock") TObjectPtr<UBoxComponent> StockVolume;
	void RefreshStock();
private:
	FTimerHandle RefreshTimer;
};
