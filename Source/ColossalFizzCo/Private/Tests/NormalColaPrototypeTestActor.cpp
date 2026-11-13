#include "Tests/NormalColaPrototypeTestActor.h"
#include "Shop/ShopCustomerFlow.h"
#include "Shop/ShopCustomerVisitComponent.h"
#include "Shop/ShopCustomerCashComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCashRegisterComponent.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavMesh/RecastNavMesh.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"
#include "Containers/Ticker.h"
#include "Engine/NetDriver.h"
#include "Components/RocketProjectileComponent.h"

ANormalColaPrototypeTestActor::ANormalColaPrototypeTestActor()
{
	bReplicates=true; bAlwaysRelevant=true; PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.2f;
}
void ANormalColaPrototypeTestActor::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	bCommandLinePressureTest=FParse::Param(FCommandLine::Get(),TEXT("NormalColaPressureTest"));
	if (HasAuthority() && (bCommandLinePressureTest || FParse::Param(FCommandLine::Get(),TEXT("NormalColaReinvestmentTest")))) Command=2;
	int32 ExitAfter=0;
	if (FParse::Value(FCommandLine::Get(),TEXT("NormalColaTestExitAfter="),ExitAfter))
	{
		// A world timer disappears if a client loses its server and browses to another map.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) { FPlatformMisc::RequestExit(false); return false; }),float(FMath::Clamp(ExitAfter,30,600)));
	}
	UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_TEST_BOOT authority=%d command=%d world=%s"),HasAuthority(),Command,*GetWorld()->GetName());
#if DO_ENABLE_NET_TEST
	if (const auto* Driver=GetWorld()->GetNetDriver())
		UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_NET_PROFILE authority=%d lag_ms=%d loss_pct=%d"),HasAuthority(),Driver->PacketSimulationSettings.PktLag,Driver->PacketSimulationSettings.PktLoss);
#endif
#endif
}
void ANormalColaPrototypeTestActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANormalColaPrototypeTestActor,Stage); DOREPLIFETIME(ANormalColaPrototypeTestActor,SalesCompleted);
	DOREPLIFETIME(ANormalColaPrototypeTestActor,ExpectedCashCents); DOREPLIFETIME(ANormalColaPrototypeTestActor,LastFailure);
	DOREPLIFETIME(ANormalColaPrototypeTestActor,TestSession); DOREPLIFETIME(ANormalColaPrototypeTestActor,TestCustomerID); DOREPLIFETIME(ANormalColaPrototypeTestActor,TestItems);
	DOREPLIFETIME(ANormalColaPrototypeTestActor,TestSupplyISIDs); DOREPLIFETIME(ANormalColaPrototypeTestActor,ExpectedBankCents);
	DOREPLIFETIME(ANormalColaPrototypeTestActor,PressureLaunchOrigin);
	DOREPLIFETIME(ANormalColaPrototypeTestActor,bManualCappingInput);
}
void ANormalColaPrototypeTestActor::Fail(const FString& Message)
{
	LastFailure=Message; Stage=-1; ForceNetUpdate(); UE_LOG(LogTemp,Error,TEXT("NORMAL_COLA_PLAYTEST_FAILED: %s"),*Message);
}
void ANormalColaPrototypeTestActor::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!Manager.IsValid()) Manager=APropManager::Get(this);
	if (!Manager.IsValid() || !Manager->SharedWallet || !Manager->ShopCheckout) return;
	if (!Scanner.IsValid()) for (TActorIterator<AShopCustomerFlow> It(GetWorld()); It; ++It) { Scanner=It->GetScanner(); break; }
	LocalBankCents=Manager->SharedWallet->GetWalletState().BalanceCents;
	LocalCashCents=Manager->ShopCheckout->TeamCashCents; LocalReceiptCount=Manager->ShopCheckout->RecentReceipts.Num();
	if (Stage!=ObservedStage) { ObservedStage=Stage; ObservedStageAt=GetWorld()->GetTimeSeconds(); }
	const bool PressureSnapshotReady=Stage!=400 || LocalPressureFlight.Contains(FString::Printf(TEXT("state=%d "),int32(ERocketSimulationState::AtRest)));
	if ((Stage==200 || Stage==400) && PressureSnapshotReady && LoggedStage!=Stage && GetWorld()->GetTimeSeconds()-ObservedStageAt>.75
		&& LocalBankCents==ExpectedBankCents && LocalCashCents==0 && LocalReceiptCount>=2)
	{
		LoggedStage=Stage;
		UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_PEER_ACCEPTANCE authority=%d stage=%d bank=%lld cash=%lld receipts=%d bottle=[%s] pressure=[%s] peak=%.2f"),
			HasAuthority(),Stage,LocalBankCents,LocalCashCents,LocalReceiptCount,*LocalManufacturedBottle,*LocalPressureFlight,LocalPeakPressureHeight);
	}
	if (HasAuthority() && bCommandLinePressureTest && Stage==200 && LoggedStage==200 && GetWorld()->GetTimeSeconds()-ObservedStageAt>1.5) Command=3;
	LocalPresentPurchases=0; for (int32 ID:TestItems) if (Manager->FindISIDData(ID)) ++LocalPresentPurchases;
	LocalPlayerCount=GetWorld()->GetGameState() ? GetWorld()->GetGameState()->PlayerArray.Num() : 0;
	LocalVisitStates.Empty();
	if (auto* Nav=UNavigationSystemV1::GetCurrent(GetWorld()))
	{
		const auto& Agents=Nav->GetSupportedAgents();
		const auto* Data=Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate));
		LocalNavigationState=FString::Printf(TEXT("agents=%d radius=%.1f height=%.1f data=%s building=%d bounds=%d activeTiles=%d worldTime=%.1f"),Agents.Num(),
			Agents.IsEmpty() ? -1.f : Agents[0].AgentRadius,Agents.IsEmpty() ? -1.f : Agents[0].AgentHeight,
			*GetNameSafe(Data),UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()),Nav->GetNavigationBounds().Num(),Data ? Data->GetNumActiveTiles() : 0,GetWorld()->GetTimeSeconds());
		if (HasAuthority() && Command==4 && LocalNavigationProbe.IsEmpty())
		{
			LocalNavigationProbe=FString::Printf(TEXT("mode=%d initialized=%d gameWorld=%d; "),int32(Nav->GetRunMode()),Nav->IsInitialized(),GetWorld()->IsGameWorld());
			for (const auto& Bounds:Nav->GetNavigationBounds()) LocalNavigationProbe+=TEXT("bounds=")+Bounds.AreaBox.ToString()+TEXT("; ");
			FHitResult Floor; const FVector Entrance(0,-2700,2);
			GetWorld()->LineTraceSingleByChannel(Floor,Entrance+FVector(0,0,1400),Entrance-FVector(0,0,200),ECC_WorldStatic);
			LocalNavigationProbe+=FString::Printf(TEXT("entranceFloor=%s component=%s point=%s; grid="),*GetNameSafe(Floor.GetActor()),*GetNameSafe(Floor.GetComponent()),*Floor.ImpactPoint.ToCompactString());
			for (int32 X=-3000;X<=3000;X+=500) for (int32 Y=-3500;Y<=2000;Y+=500)
			{
				FNavLocation Point;
				if (Nav->ProjectPointToNavigation(FVector(X,Y,2),Point,FVector(50,50,150)))
					LocalNavigationProbe+=Point.Location.ToCompactString()+TEXT(" ");
			}
			UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_NAVIGATION_PROBE %s"),*LocalNavigationProbe);
		}
	}
	else LocalNavigationState=TEXT("No local navigation system");
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It) if (const auto* Visit=It->FindComponentByClass<UShopCustomerVisitComponent>())
		LocalVisitStates.Add(GetNameSafe(*It)+TEXT(": ")+Visit->Status);
	if (HasAuthority() && Command && GetWorld()->GetTimeSeconds()>=NextProgressLogAt)
	{
		NextProgressLogAt=GetWorld()->GetTimeSeconds()+10;
		FString Arrival;
		for (TActorIterator<AShopCustomerFlow> It(GetWorld());It;++It) { Arrival=It->ArrivalStatus; break; }
		int32 Bottles=0,Sealed=0,Registered=0;
		for (const auto& Pair:Manager->ISIDData)
		{
			const auto* Data=Manager->FindUniqueData(Pair.Key);
			if (!Data || !Manager->ShopSupplies || !Manager->ShopSupplies->IsNormalContent(Data->EventData.ContentID)) continue;
			++Bottles; Sealed+=Data->EventData.CapSealed>=1;
			FShopStockEntry Entry; Registered+=Manager->ShopCheckout->GetStockEntry(Pair.Key,Entry);
		}
		UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_TEST_PROGRESS stage=%d players=%d bank=%lld cash=%lld receipts=%d bottles=%d sealed=%d registered=%d available=%d arrival=[%s] visits=[%s] navigation=[%s]"),
			Stage,LocalPlayerCount,LocalBankCents,LocalCashCents,LocalReceiptCount,Bottles,Sealed,Registered,Manager->ShopCheckout->GetAvailableColaStockISIDs().Num(),
			*Arrival,*FString::Join(LocalVisitStates,TEXT(" | ")),*LocalNavigationState);
	}
	if (!Scanner.IsValid() || Stage<0) return;
	if (Stage>=300 || (Stage==200 && Command==3)) { TickPressureFlight(); return; }
	if (Stage>=100) { TickReinvestment(); return; }
	if (!HasAuthority())
	{
		const auto* PC=Cast<APlayerController>(GetOwner()); APawn* Pawn=PC ? PC->GetPawn().Get() : nullptr;
		auto* Actions=Pawn ? Pawn->FindComponentByClass<UShopPlayerInteractionComponent>() : nullptr;
		const auto View=Scanner->GetScannerState();
		if (!PC || !PC->IsLocalController() || !Actions || LastSentStage==Stage || View.SessionID!=TestSession) return;
		if ((Stage==10 && View.Phase==EShopScannerPhase::Scanning) || (Stage==20 && View.Phase==EShopScannerPhase::Checkout))
		{
			Actions->RequestScannerAction(Scanner.Get(),TestSession,Stage==10 ? EShopScannerAction::LockBill : EShopScannerAction::Complete,0,FGuid());
			LastSentStage=Stage; ++LocalRequestsSent;
		}
		return;
	}
	if (Stage==0 && (Command==1 || Command==2)) { Stage=1; StartedAt=GetWorld()->GetTimeSeconds(); }
	if (Stage>0 && Stage<100 && GetWorld()->GetTimeSeconds()-StartedAt>180) { Fail(TEXT("Customer/remote-checkout/departure loop exceeded 180 seconds")); return; }
	if (Stage==1)
	{
		for (auto It=GetWorld()->GetPlayerControllerIterator();It;++It)
			if (It->Get() && It->Get()->IsLocalController()) It->Get()->SetControlRotation(FRotator(0,90,0));
		APlayerController* Cashier=nullptr;
		for (auto It=GetWorld()->GetPlayerControllerIterator(); It; ++It) if (It->Get() && !It->Get()->IsLocalController() && It->Get()->GetPawn()) { Cashier=It->Get(); break; }
		if (!Cashier) return; // This test deliberately requires a real remote player.
		for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
		{
			const auto* Visit=It->FindComponentByClass<UShopCustomerVisitComponent>(); const auto* Cash=It->FindComponentByClass<UShopCustomerCashComponent>();
			if (!Visit || !Cash || Visit->Phase!=EShopVisitPhase::WaitingForCashier) continue;
			Customer=*It; TestCustomerID=Cash->CustomerID; TestItems=Visit->GetCollectedISIDs();
			Cashier->GetPawn()->SetActorLocation(Scanner->GetOwner()->GetActorLocation()+FVector(-110,-160,100),false,nullptr,ETeleportType::TeleportPhysics);
			for (int32 I=0; I<TestItems.Num(); ++I)
			{
				Cashier->GetPawn()->SetActorLocation(Manager->GetTransform(TestItems[I]).GetLocation()+FVector(0,-140,100),false,nullptr,ETeleportType::TeleportPhysics);
				if (!Manager->ValidateGrab(TestItems[I],Cashier->GetPawn())) { Fail(TEXT("Cashier could not reach a customer bottle")); return; }
				auto* Prop=Manager->GetActiveActorFromISID(TestItems[I]);
				if (!Prop) Prop=Manager->ConvertISMToASMFromISID(TestItems[I]);
				if (!Prop) { Fail(TEXT("Customer bottle could not be promoted for scanning")); return; }
				if (!Prop->AttachForInteraction(Cashier->GetPawn()->GetRootComponent())) { Fail(TEXT("Cashier could not take a customer bottle")); return; }
				FTransform Transform=Prop->GetActorTransform(); Transform.SetLocation(Scanner->ScanVolume->GetComponentLocation()+FVector((I%2)*100-50,(I/2)*100-50,-100));
				Cashier->GetPawn()->SetActorLocation(Transform.GetLocation()+FVector(0,-160,100),false,nullptr,ETeleportType::TeleportPhysics);
				Manager->UpdateHeldTransform(TestItems[I],Transform); Prop->DetachForInteraction(); Prop->PropMesh->SetSimulatePhysics(false);
			}
			Cashier->GetPawn()->SetActorLocation(Scanner->GetOwner()->GetActorLocation()+FVector(350,-700,100),false,nullptr,ETeleportType::TeleportPhysics);
			Scanner->RefreshLiveProducts();
			if (Scanner->GetScannerState().Checkout.Items.Num()!=TestItems.Num()) { Fail(TEXT("Physical scanner did not detect every customer bottle")); return; }
			TestSession=Scanner->GetScannerState().SessionID; SetOwner(Cashier); Stage=10; LastSentStage=0; ForceNetUpdate(); break;
		}
	}
	else if (Stage==10 && Scanner->GetScannerState().Phase==EShopScannerPhase::Checkout)
	{
		CheckoutID=Scanner->GetScannerState().Checkout.CheckoutID;
		if (auto* Register=Scanner->GetOwner()->FindComponentByClass<UShopCashRegisterComponent>()) Register->RefreshPayment();
		const auto Bill=Scanner->GetScannerState().Checkout;
		if (Bill.TenderCents>Bill.TotalCents && Bill.CustomerID==TestCustomerID)
		{
			auto* Register=Scanner->GetOwner()->FindComponentByClass<UShopCashRegisterComponent>();
			const int64 Dollars=(Bill.TenderCents-Bill.TotalCents)/100;
			if (Register && Register->ChangeVolume && Dollars<=100)
			{
				TArray<TArray<int32>> Plan; Plan.SetNum(int32(Dollars)+1); TArray<bool> Reachable; Reachable.Init(false,int32(Dollars)+1); Reachable[0]=true;
				for (int32 ID:UShopCashRegisterComponent::QueryCoinsInVolume(Register->TenderVolume))
				{
					FShopCashCoin Coin; if (!Manager->ShopCheckout->GetCoin(ID,Coin) || Coin.CustomerOwner.IsValid() || Coin.CheckoutID.IsValid()) continue;
					for (int32 Value=int32(Dollars); Value>=Coin.Dollars; --Value) if (!Reachable[Value] && Reachable[Value-Coin.Dollars])
					{ Reachable[Value]=true; Plan[Value]=Plan[Value-Coin.Dollars]; Plan[Value].Add(ID); }
				}
				if (Reachable.Last()) for (int32 ID:Plan.Last()) Manager->InitializeInstanceTransform(ID,FTransform(Register->ChangeVolume->GetComponentLocation()));
				Register->RefreshPayment();
			}
		}
		const auto Paid=Scanner->GetScannerState().Checkout;
		if (Paid.TenderCents>=Paid.TotalCents && Paid.ChangeCents==Paid.TenderCents-Paid.TotalCents && Paid.CustomerID==TestCustomerID) { Stage=20; ForceNetUpdate(); }
	}
	else if (Stage==20)
	{
		FCompletedCheckoutReceipt Receipt;
		if (Manager->ShopCheckout->GetCompletedReceipt(CheckoutID,Receipt))
		{
			ExpectedCashCents+=Receipt.TotalCents; ++SalesCompleted;
			if (Receipt.CustomerID!=TestCustomerID || Manager->SharedWallet->GetWalletState().BalanceCents!=0 || Manager->ShopCheckout->GetTeamCashCents()!=ExpectedCashCents)
			{ Fail(TEXT("Completed sale violated customer or cash/bank accounting")); return; }
			Stage=30; ForceNetUpdate();
		}
	}
	else if (Stage==30 && Customer.IsValid())
	{
		const auto* Cash=Customer->FindComponentByClass<UShopCustomerCashComponent>();
		if (Cash && Cash->CustomerID!=TestCustomerID)
		{
			for (int32 ID:TestItems) if (Manager->FindISIDData(ID)) { Fail(TEXT("Departed customer purchase was not retired")); return; }
			FCompletedCheckoutReceipt Receipt;
			if (!Manager->ShopCheckout->GetCompletedReceipt(CheckoutID,Receipt)) { Fail(TEXT("Departure deleted completed receipt")); return; }
			Stage=SalesCompleted>=2 ? 100 : 1; CheckoutID.Invalidate(); ForceNetUpdate();
			if (Stage==100) UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_REMOTE_LOOP_PASSED sales=%d cash=%lld bank=0"),SalesCompleted,ExpectedCashCents);
		}
	}
}
