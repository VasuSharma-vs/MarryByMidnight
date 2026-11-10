#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Shop/ShopCheckoutTypes.h"
#include "ShopScannerComponent.generated.h"

class APropManager;
class UShopCheckoutComponent;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnShopScannerStateChanged, FShopScannerState, State);

/** Server cart/checkout adapter for a physical scanner. Player RPCs stay on the owning character. */
UCLASS(ClassGroup = (Shop), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UShopScannerComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UShopScannerComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scanner", meta = (ClampMin = "1", ClampMax = "64")) int32 MaxCartItems = 32;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scanner", meta = (ClampMin = "1")) float MaxScanDistance = 300;
	UPROPERTY(BlueprintAssignable, Category = "Scanner") FOnShopScannerStateChanged OnScannerStateChanged;

	UFUNCTION(BlueprintPure, Category = "Scanner") FShopScannerState GetScannerState() const { return ScannerState; }
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Scanner") EShopCheckoutResult ScanProduct(int32 ISID);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Scanner") EShopCheckoutResult ScanOverlap(UPrimitiveComponent* ProductComponent, int32 BodyIndex);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Scanner") EShopCheckoutResult RemoveProduct(int32 ISID);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Scanner") EShopCheckoutResult AssignCustomer(FGuid CustomerID);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Scanner") EShopCheckoutResult BeginCheckout();
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Scanner") EShopCheckoutResult ReceiveTender(FGuid CashOperationID, int64 AmountCents);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Scanner") EShopCheckoutResult ReturnChange(FGuid CashOperationID, int64 AmountCents);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Scanner") EShopCheckoutResult CompleteCheckout();
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Scanner") EShopCheckoutResult ClearCart();

private:
	UPROPERTY(ReplicatedUsing = OnRep_ScannerState) FShopScannerState ScannerState;
	UPROPERTY(Transient) TWeakObjectPtr<APropManager> Manager;
	UPROPERTY(Transient) TWeakObjectPtr<UShopCheckoutComponent> BoundCheckout;
	bool bNotifying = false;
	bool IsServer() const;
	APropManager* ResolveManager();
	UShopCheckoutComponent* ResolveCheckout();
	EShopCheckoutResult SetResult(EShopCheckoutResult Result);
	void PublishState();
	EShopCheckoutResult ApplyCash(FGuid OperationID, int64 Amount, bool bChange);
	UFUNCTION() void SynchronizeCheckout();
	UFUNCTION() void OnRep_ScannerState();
};
