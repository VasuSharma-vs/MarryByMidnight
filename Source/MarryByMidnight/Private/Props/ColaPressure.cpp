#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Components/StaticMeshComponent.h"
#include "Shop/ShopProductionStationComponent.h"

bool APropManager::SetBottleAgitationPressure(int32 InISID,float Pressure,bool bPublish)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !FMath::IsFinite(Pressure) || Pressure<0) return false;
	auto* Unique=FindUniqueData(InISID); const auto* Record=FindISIDData(InISID);
	const auto* Def=Record ? FindProductDefinition(Record->ProductID) : nullptr;
	if (!Unique || !Def || Def->BottleMaterialType!=EBottleMaterialType::Plastic) return false;
	const float NewPressure=FMath::Min(Pressure,100.f);
	const bool Changed=Unique->ContinuousData.SealedPressure!=NewPressure;
	Unique->ContinuousData.SealedPressure=NewPressure;
	if (Changed) MarkInstanceDirty(InISID,EPropDirtyFlags::RuntimeData);
	// LastUpdatedTime is also the thermal integration clock. Pressure must not consume
	// elapsed heating/cooling time. Only update this channel in the active canonical cache.
	if (auto* Active=GetActiveActorFromISID(InISID)) Active->LocalUniqueData.ContinuousData.SealedPressure=NewPressure;
	if (bPublish)
	{
		FPropContinuousSyncItem Item; Item.ISID=InISID;
		Item.Temperature=Unique->ContinuousData.Temperature; Item.SealedPressure=Unique->ContinuousData.SealedPressure;
		const double SnapshotTime=FMath::Max3(WorldElapsedSeconds,Unique->ContinuousData.LastUpdatedTime,Record->LastDiscreteEventTime);
		Multicast_SyncContinuousBatch(SnapshotTime,{Item});
	}
	return true;
}

bool AColaPropActor::UsesPlasticPressure() const
{
	const auto* Def=ResolveProductDefinition();
	return Def && Def->BottleMaterialType==EBottleMaterialType::Plastic;
}

float AColaPropActor::GetBottlePressure() const
{
	if (GetContentLevel()<=UE_SMALL_NUMBER) return 0;
	const auto* Content=ResolveContentDefinition();
	return FMath::Max(0.f,LocalUniqueData.ContinuousData.SealedPressure)
		+GetContentLevel()*FMath::Max(0.f,Content ? Content->PressureStrengthMultiplier : 1.f);
}

float AColaPropActor::GetBottlePressureTolerance() const
{
	const auto* Def=ResolveProductDefinition();
	return Def ? FMath::Max(UE_SMALL_NUMBER,Def->PressureFailureThreshold*GetStrengthRatio()) : TNumericLimits<float>::Max();
}

void AColaPropActor::AddAgitationPressure(float Amount)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !UsesPlasticPressure() || !FMath::IsFinite(Amount) || Amount<=0 || IsEmpty() || !PropManagerReference) return;
	const auto* Def=ResolveProductDefinition();
	const float Pressure=LocalUniqueData.ContinuousData.SealedPressure+Amount/FMath::Max(.01f,Def->PressureResistanceMultiplier);
	if (!PropManagerReference->SetBottleAgitationPressure(ISID,Pressure)) return;
	LastPublishedPressure=LocalUniqueData.ContinuousData.SealedPressure;
	InteractionPressure=LocalUniqueData.ContinuousData.SealedPressure;
	FoamPressure=FMath::Clamp(InteractionPressure*5.f,0.f,10.f);
	const bool HeldClosed=CapHoldingStation.IsValid() && CapHoldingStation->HasSufficientCapHold(ISID);
	if (GetCapSealed()>=1 && !HeldClosed && GetBottlePressure()>GetBottlePressureTolerance()) StartPlasticPressureRelease();
}

bool AColaPropActor::StartPlasticPressureRelease()
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !UsesPlasticPressure() || IsEmpty() || !PropManagerReference || !RocketComp
		|| GetBottlePressure()<=GetBottlePressureTolerance() || GetCapSealed()<1
		|| (RocketComp->GetSimulationState()!=ERocketSimulationState::Idle && RocketComp->GetSimulationState()!=ERocketSimulationState::AtRest)
		|| !PropMesh || GetRootComponent()!=PropMesh || !PropMesh->GetStaticMesh()) return false;
	if (bIsHeld && !PropManagerReference->ReleaseGrab(ISID,InteractionOwner.Get())) return false;
	const int32 AttachedCap=LocalUniqueData.EventData.AttachedCapISID;
	if (AttachedCap>0)
	{
		if (!PropManagerReference->DetachPhysicalCap(ISID,GetActorUpVector()*1500.f)) return false;
	}
	else RemoveCap(1.f,ECapRemovalOperation::EjectPhysics);
	if (GetCapSealed()>0) return false;
	const bool bLaunched=bUseOriginalRocketFlight ? LaunchOriginalBottleRocket(true) : RocketComp->StartPressurePhysicsFlight();
	if (!bLaunched) return false;
	PressureReleaseEpisode=FGuid::NewGuid();
	RocketInitialContentLevel=GetContentLevel(); RocketInitialPressure=LocalUniqueData.ContinuousData.SealedPressure;
	ForceNetUpdate(); return true;
}

void AColaPropActor::UpdateOriginalRocketDischarge(float DeltaSeconds)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !bUseOriginalRocketFlight || !PressureReleaseEpisode.IsValid()
		|| !PropManagerReference || !RocketComp || RocketComp->GetReplicatedFlightState().bPhysicsSimulating) return;
	const float Progress=FMath::Clamp(RocketComp->GetTotalFlightDistanceTraveled()/FMath::Max(1.f,RocketLaunchConfiguration.Distance),0.f,1.f);
	const float Target=RocketInitialContentLevel*(1-Progress);
	const double Portions=FMath::Max(0.f,GetContentLevel()-Target)*GetNumPortions();
	if (Portions>0) PropManagerReference->ReleaseBottleLiquidPortions(ISID,Portions,PressureReleaseEpisode);
	PropManagerReference->SetBottleAgitationPressure(ISID,FMath::Min(LocalUniqueData.ContinuousData.SealedPressure,RocketInitialPressure*(1-Progress)),false);
}

void AColaPropActor::UpdatePlasticPressure(float DeltaSeconds)
{
	if (!HasAuthority() || GetNetMode()==NM_Client || !UsesPlasticPressure() || !PropManagerReference
		|| !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0) return;
	PressurePublishElapsed+=DeltaSeconds;
	float Pressure=LocalUniqueData.ContinuousData.SealedPressure;
	const bool Jet=PressureReleaseEpisode.IsValid() && RocketComp && RocketComp->IsPoweredFlight();
	const bool HeldClosed=CapHoldingStation.IsValid() && CapHoldingStation->HasSufficientCapHold(ISID);
	if (Jet)
	{
		if (bUseOriginalRocketFlight)
		{
			// Distance-driven discharge runs at each projectile movement sub-step.
			// A fixed per-second drain can stop a fast flight with half a bottle left.
			UpdateOriginalRocketDischarge(DeltaSeconds);
			Pressure=LocalUniqueData.ContinuousData.SealedPressure;
		}
		else
		{
		if (GetContentLevel()>UE_SMALL_NUMBER && Pressure>UE_KINDA_SMALL_NUMBER)
		{
			const auto* Def=ResolveProductDefinition();
			const double Portions=FMath::Max(0.f,JetLiquidLevelPerSecond)*DeltaSeconds*Def->ConsumableProfile.NumOfPortions;
			if (Portions>0 && PropManagerReference->ReleaseBottleLiquidPortions(ISID,Portions,PressureReleaseEpisode)
				&& RocketComp->IsPressureThrustActive())
				RocketComp->ApplyPressureThrust(GetBottlePressure()*FMath::Max(0.f,JetAccelerationPerPressure));
			Pressure=FMath::Max(0.f,Pressure-FMath::Max(0.f,JetPressureVentRate)*DeltaSeconds);
		}
		}
		if (GetContentLevel()<=UE_SMALL_NUMBER || Pressure<=UE_KINDA_SMALL_NUMBER) RocketComp->FinishPressureThrust();
	}
	else
	{
		const float Decay=GetCapSealed()>=1 || HeldClosed ? PressureDecayRate : AtmosphereVentRate;
		Pressure=IsEmpty() ? 0.f : FMath::Max(0.f,Pressure-FMath::Max(0.f,Decay)*DeltaSeconds);
	}
	const bool Publish=PressurePublishElapsed>=.2f && Pressure!=LastPublishedPressure;
	if (Pressure!=LocalUniqueData.ContinuousData.SealedPressure || Publish)
		PropManagerReference->SetBottleAgitationPressure(ISID,Pressure,Publish);
	if (Publish) { PressurePublishElapsed=0; LastPublishedPressure=Pressure; }
	InteractionPressure=Pressure; FoamPressure=FMath::Clamp(Pressure*5.f,0.f,10.f);
	if (!Jet && GetCapSealed()>=1 && GetBottlePressure()>GetBottlePressureTolerance()) StartPlasticPressureRelease();
}
