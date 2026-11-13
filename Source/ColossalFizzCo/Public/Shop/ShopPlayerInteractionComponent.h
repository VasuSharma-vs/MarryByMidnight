#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Shop/ShopCheckoutTypes.h"
#include "Shop/ShopProductionStationComponent.h"
#include "ShopPlayerInteractionComponent.generated.h"

class UShopScannerComponent;
class UShopATMComponent;
class UShopTerminalWidget;

UENUM(BlueprintType)
enum class EShopScannerAction : uint8 { Save, RemoveItem, Clear, LockBill, Complete, CashCompensation, ProvideReplacement };

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnShopPlayerActionResult, int64, Sequence, EShopCheckoutResult, Result);

/** Owned-player RPC boundary. Shared stations never accept client-specified prices, totals or customer IDs. */
UCLASS(ClassGroup = (Shop), meta = (BlueprintSpawnableComponent))
class COLOSSALFIZZCO_API UShopPlayerInteractionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UShopPlayerInteractionComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop") float MaxInteractionDistance = 350;
	UPROPERTY(BlueprintAssignable, Category = "Shop") FOnShopPlayerActionResult OnActionResult;
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void RequestScannerAction(UShopScannerComponent* Scanner, FGuid ExpectedSession, EShopScannerAction Action, int32 ItemISID, FGuid CaseID);
	UFUNCTION(BlueprintCallable, Category = "Shop") void RequestATMWithdrawal(UShopATMComponent* ATM, const TArray<int32>& Denominations);
	UFUNCTION(BlueprintCallable, Category = "Shop") void OpenTerminal(AActor* Station);
	/** Local E interaction: trace from the real camera using the station reach budget. */
	UFUNCTION(BlueprintCallable, Category="Shop") bool TryOpenTerminalFromView();
	/** Shared query used by the input path and collision/reach regression tests. */
	AActor* FindTerminalInView(const FVector& ViewLocation, const FVector& ViewDirection) const;
	UFUNCTION(BlueprintPure, Category="Shop") bool IsTerminalOpen() const;
	UFUNCTION(BlueprintCallable, Category="Shop") void RequestSupplyPurchase(UShopProductionStationComponent* Station, FGuid SessionID, FName OfferID, int32 Quantity=1);
	UFUNCTION(BlueprintCallable, Category="Shop") void RequestProductionAction(UShopProductionStationComponent* Station, FGuid SessionID, EShopProductionAction Action, int32 BottleISID, int32 OtherISID=0);
	EShopCheckoutResult ExecuteSupplyPurchase(UShopProductionStationComponent* Station, FGuid SessionID, FName OfferID, int32 Quantity, FGuid OperationID, int64 Sequence);
	EShopCheckoutResult ExecuteProductionAction(UShopProductionStationComponent* Station, FGuid SessionID, EShopProductionAction Action, int32 BottleISID, int32 OtherISID, int64 Sequence);
	bool IsStationReachable(const AActor* Station) const { return CanReach(Station); }
	EShopCheckoutResult ExecuteATMWithdrawal(UShopATMComponent* ATM, const TArray<int32>& Denominations, FGuid OperationID, int64 Sequence);
	/** Server/test entry uses the same access, ordering, session and case checks as the owned RPC. */
	EShopCheckoutResult ExecuteScannerAction(UShopScannerComponent* Scanner, FGuid ExpectedSession, EShopScannerAction Action, int32 ItemISID, FGuid CaseID, int64 Sequence);
private:
	int64 LocalSequence = 0;
	int64 LastServerSequence = 0;
	UFUNCTION(Server, Reliable) void Server_ScannerAction(UShopScannerComponent* Scanner, FGuid ExpectedSession, EShopScannerAction Action, int32 ItemISID, FGuid CaseID, int64 Sequence);
	UFUNCTION(Client, Reliable) void Client_ActionResult(int64 Sequence, EShopCheckoutResult Result);
	UFUNCTION(Server, Reliable) void Server_ATMWithdrawal(UShopATMComponent* ATM, const TArray<int32>& Denominations, FGuid OperationID, int64 Sequence);
	UFUNCTION(Server, Reliable) void Server_SupplyPurchase(UShopProductionStationComponent* Station, FGuid SessionID, FName OfferID, int32 Quantity, FGuid OperationID, int64 Sequence);
	UFUNCTION(Server, Reliable) void Server_ProductionAction(UShopProductionStationComponent* Station, FGuid SessionID, EShopProductionAction Action, int32 BottleISID, int32 OtherISID, int64 Sequence);
	UFUNCTION(Client, Reliable) void Client_OpenTerminal(AActor* Station);
	UPROPERTY(Transient) TObjectPtr<UShopTerminalWidget> TerminalWidget;
	bool CanReach(const AActor* Station) const;
};
