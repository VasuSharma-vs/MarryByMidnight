#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NormalColaPrototypeTestActor.generated.h"
class ACharacter;
class UShopScannerComponent;
class APropManager;

/** Opt-in instrumentation for the isolated validation map, never placed in the playable map. */
UCLASS(NotBlueprintable)
class COLOSSALFIZZCO_API ANormalColaPrototypeTestActor:public AActor
{
	GENERATED_BODY()
public:
	ANormalColaPrototypeTestActor();
	virtual void BeginPlay() override;
	virtual void Tick(float Dt) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(EditAnywhere,Category="Test") int32 Command=0;
	/** Disable simulated cap inputs so actual G/R keys can be accepted on both peers. */
	UPROPERTY(Replicated,EditAnywhere,Category="Test") bool bManualCappingInput=false;
	UPROPERTY(Replicated,VisibleAnywhere,Category="Test") int32 Stage=0;
	UPROPERTY(Replicated,VisibleAnywhere,Category="Test") int32 SalesCompleted=0;
	UPROPERTY(Replicated,VisibleAnywhere,Category="Test") int64 ExpectedCashCents=0;
	UPROPERTY(Replicated,VisibleAnywhere,Category="Test") FString LastFailure;
	UPROPERTY(Replicated,VisibleAnywhere,Category="Test") FGuid TestSession;
	UPROPERTY(Replicated,VisibleAnywhere,Category="Test") FGuid TestCustomerID;
	UPROPERTY(Replicated,VisibleAnywhere,Category="Test") TArray<int32> TestItems;
	UPROPERTY(Replicated,VisibleAnywhere,Category="Test") TArray<int32> TestSupplyISIDs;
	UPROPERTY(Replicated,VisibleAnywhere,Category="Test") int64 ExpectedBankCents=0;
	UPROPERTY(VisibleAnywhere,Category="Test") int32 LocalPlayerCount=0;
	UPROPERTY(VisibleAnywhere,Category="Test") int32 LocalReceiptCount=0;
	UPROPERTY(VisibleAnywhere,Category="Test") int64 LocalBankCents=0;
	UPROPERTY(VisibleAnywhere,Category="Test") int64 LocalCashCents=0;
	UPROPERTY(VisibleAnywhere,Category="Test") int32 LocalPresentPurchases=0;
	UPROPERTY(VisibleAnywhere,Category="Test") int32 LocalRequestsSent=0;
	UPROPERTY(VisibleAnywhere,Category="Test") TArray<FString> LocalVisitStates;
	UPROPERTY(VisibleAnywhere,Category="Test") FString LocalNavigationState;
	UPROPERTY(VisibleAnywhere,Category="Test") FString LocalNavigationProbe;
	UPROPERTY(VisibleAnywhere,Category="Test") FString LocalManufacturedBottle;
	UPROPERTY(VisibleAnywhere,Category="Test") FString LocalPressureFlight;
	UPROPERTY(VisibleAnywhere,Category="Test") bool LocalSawPressureFlight=false;
	UPROPERTY(VisibleAnywhere,Category="Test") float LocalPeakPressureHeight=0;
	UPROPERTY(Replicated,VisibleAnywhere,Category="Test") FVector PressureLaunchOrigin=FVector::ZeroVector;
private:
	TWeakObjectPtr<APropManager> Manager;
	TWeakObjectPtr<UShopScannerComponent> Scanner;
	TWeakObjectPtr<ACharacter> Customer;
	FGuid CheckoutID;
	double StartedAt=0;
	int32 LastSentStage=0;
	TSet<int32> KnownInstances;
	double ProductionStartedAt=0;
	double PressureStartedAt=0;
	int32 PressureImpactCount=0;
	bool bPressureObservationStarted=false;
	bool bCommandLinePressureTest=false;
	int32 LoggedStage=0;
	int32 ObservedStage=0;
	double ObservedStageAt=0;
	double NextProgressLogAt=0;
	void TickReinvestment();
	void TickPressureFlight();
	void Fail(const FString& Message);
};
