#include "Tests/NormalColaPrototypeTestActor.h"
#include "Props/InteractablePropManager.h"
#include "Props/ColaPropActor.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Components/StaticMeshComponent.h"

void ANormalColaPrototypeTestActor::TickPressureFlight()
{
	if (TestSupplyISIDs.Num()!=3) return;
	const int32 BottleID=TestSupplyISIDs[0], CapID=TestSupplyISIDs[1];
	auto* Bottle=Cast<AColaPropActor>(Manager->GetActiveActorFromISID(BottleID));
	if (Stage>=300 && !bPressureObservationStarted)
	{
		LocalSawPressureFlight=false; LocalPeakPressureHeight=0; bPressureObservationStarted=true;
	}
	if (Bottle && Stage>=300)
	{
		const bool Powered=Bottle->RocketComp && Bottle->RocketComp->IsPoweredFlight();
		LocalSawPressureFlight|=Powered;
		LocalPeakPressureHeight=FMath::Max(LocalPeakPressureHeight,float(Bottle->GetActorLocation().Z-PressureLaunchOrigin.Z));
		LocalPressureFlight=FString::Printf(TEXT("ISID=%d powered=%d physics=%d state=%d pos=%s velocity=%s pressure=%.3f level=%.3f seal=%.3f cap=%d peak=%.1f"),
			BottleID,Powered,Bottle->PropMesh->IsSimulatingPhysics(),int32(Bottle->RocketComp->GetSimulationState()),
			*Bottle->GetActorLocation().ToCompactString(),*Bottle->PropMesh->GetPhysicsLinearVelocity().ToCompactString(),
			Bottle->GetBottlePressure(),Bottle->GetContentLevel(),Bottle->GetCapSealed(),Bottle->LocalUniqueData.EventData.AttachedCapISID,LocalPeakPressureHeight);
	}
	if (!HasAuthority() || Stage==400) return;
	if (Stage==200 && Command==3)
	{
		const auto* Data=Manager->FindUniqueData(BottleID);
		if (!Data || Data->EventData.CapSealed<1 || Data->EventData.ContentLevel<=0 || Data->EventData.AttachedCapISID!=CapID)
		{ Fail(TEXT("Pressure flight requires the actual manufactured capped bottle")); return; }
		if (!Bottle) Bottle=Cast<AColaPropActor>(Manager->ConvertISMToASMFromISID(BottleID));
		if (!Bottle || !Bottle->RocketComp) { Fail(TEXT("Manufactured bottle cannot enter pressure validation")); return; }
		// Test setup only: original flight starts upright; reaction-thrust mode
		// starts inverted so its physical nozzle points down. Use real impacts.
		PressureLaunchOrigin=FVector(-2000,-3000,260);
		Bottle->PropMesh->SetSimulatePhysics(false);
		const FRotator LaunchRotation=Bottle->bUseOriginalRocketFlight ? FRotator::ZeroRotator : FRotator(180,0,0);
		Manager->InitializeInstanceTransform(BottleID,FTransform(LaunchRotation,PressureLaunchOrigin,FVector::OneVector));
		PressureImpactCount=0; PressureStartedAt=GetWorld()->GetTimeSeconds(); Stage=310; ForceNetUpdate(); return;
	}
	const auto* Record=Manager->FindISIDData(BottleID);
	const bool SettledISM=Stage==330 && !Bottle && Record && Record->State==EPropState::AtRest;
	if (!Bottle && !SettledISM) { Fail(TEXT("Pressure bottle disappeared before physics settlement")); return; }
	if (Stage==310)
	{
		Bottle->ApplyBottleImpact(FVector(0,0,Bottle->DropImpactThreshold*1.8f),170000+(++PressureImpactCount));
		if (Bottle->RocketComp->IsPoweredFlight())
		{
			const auto* Cap=Manager->FindUniqueData(CapID);
			const bool PhysicsMode=Bottle->RocketComp->GetReplicatedFlightState().bPressurePhysicsFlight;
			const bool CorrectMovement=PhysicsMode ? (Bottle->PropMesh->IsSimulatingPhysics() && Bottle->IsReplicatingMovement()
				&& Bottle->RocketComp->GetPressureThrustDirection().Z>.9) : (!Bottle->PropMesh->IsSimulatingPhysics() && Bottle->bIsInMotion);
			if (!Cap || Cap->EventData.AttachedBottleISID!=0 || Bottle->GetCapSealed()>0 || !CorrectMovement)
			{ Fail(TEXT("Impact launch did not preserve cap identity and the selected rocket-component mode")); return; }
			PressureStartedAt=GetWorld()->GetTimeSeconds(); Stage=320; ForceNetUpdate();
			UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_PRESSURE_FLIGHT_STARTED bottle=%d cap=%d impacts=%d"),BottleID,CapID,PressureImpactCount);
		}
		else if (PressureImpactCount>=20) Fail(TEXT("Repeated real impact calls did not launch sealed plastic bottle"));
		return;
	}
	if (GetWorld()->GetTimeSeconds()-PressureStartedAt>45) { Fail(TEXT("Physical pressure flight did not settle within 45 seconds")); return; }
	if (Stage==320 && Bottle && !Bottle->RocketComp->IsPoweredFlight())
	{
		if (!LocalSawPressureFlight || LocalPeakPressureHeight<50 || Bottle->GetContentLevel()>=.99f)
		{ Fail(TEXT("Pressure flight did not produce powered ascent with actual liquid loss")); return; }
		Stage=330; ForceNetUpdate();
	}
	if (Stage==330 && (SettledISM || (Bottle && Bottle->RocketComp->GetSimulationState()==ERocketSimulationState::AtRest)))
	{
		if (Manager->SharedWallet->GetWalletState().BalanceCents!=ExpectedBankCents || Manager->ShopCheckout->RecentReceipts.Num()!=SalesCompleted)
		{ Fail(TEXT("Pressure flight changed unrelated shop accounting")); return; }
		Stage=400; ForceNetUpdate();
		const auto* Data=Manager->FindUniqueData(BottleID);
		UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_PRESSURE_FLIGHT_PASSED bottle=%d peak=%.1f level=%.3f"),BottleID,LocalPeakPressureHeight,Data ? Data->EventData.ContentLevel : -1.f);
	}
}
