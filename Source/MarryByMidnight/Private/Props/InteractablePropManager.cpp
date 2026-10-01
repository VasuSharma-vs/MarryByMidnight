#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropDataAsset.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Async/Async.h"
#include "TimerManager.h"

AInteractablePropManager::AInteractablePropManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f; // Run every frame to distribute 1 prop update per frame
	bReplicates = true;
}

AInteractablePropManager* AInteractablePropManager::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = WorldContextObject->GetWorld();
	if (!World) return nullptr;

	for (TActorIterator<AInteractablePropManager> It(World); It; ++It)
	{
		return *It;
	}

	// Auto-spawn manager if not placed in world on authority
	if (World->GetAuthGameMode() || World->GetNetMode() != NM_Client)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AInteractablePropManager>(SpawnParams);
	}

	return nullptr;
}

void AInteractablePropManager::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (World && HasAuthority())
	{
		World->GetTimerManager().SetTimer(SettlingTimerHandle, this, &AInteractablePropManager::CheckSettlingQueue, SettlingCheckInterval, true);
	}
}

void AInteractablePropManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(SettlingTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void AInteractablePropManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		UpdateDistributedProps(DeltaSeconds);
		UpdateDistributedISM(DeltaSeconds);
	}
}

void AInteractablePropManager::RegisterProp(AInteractableProp* Prop)
{
	if (Prop && !RegisteredProps.Contains(Prop))
	{
		RegisteredProps.Add(Prop);
	}
}

void AInteractablePropManager::UnregisterProp(AInteractableProp* Prop)
{
	RegisteredProps.Remove(Prop);
}

void AInteractablePropManager::RegisterISMComponent(UInteractableInstancedStaticMeshComponent* ISMComp)
{
	if (ISMComp && !RegisteredISMComponents.Contains(ISMComp))
	{
		RegisteredISMComponents.Add(ISMComp);
	}
}

void AInteractablePropManager::UnregisterISMComponent(UInteractableInstancedStaticMeshComponent* ISMComp)
{
	RegisteredISMComponents.Remove(ISMComp);
}

void AInteractablePropManager::RegisterSettlingComponent(UStaticMeshPhysicsSimulationComponent* SimComp)
{
	if (SimComp && !ActiveSettlingComponents.Contains(SimComp))
	{
		ActiveSettlingComponents.Add(SimComp);
	}
}

void AInteractablePropManager::UnregisterSettlingComponent(UStaticMeshPhysicsSimulationComponent* SimComp)
{
	ActiveSettlingComponents.Remove(SimComp);
}

void AInteractablePropManager::UpdateDistributedProps(float DeltaSeconds)
{
	// Clean stale weak pointers
	RegisteredProps.RemoveAll([](const TWeakObjectPtr<AInteractableProp>& Ptr) { return !Ptr.IsValid(); });

	if (RegisteredProps.Num() == 0) return;

	// Loop one (or PropsUpdatedPerFrame) object per frame
	const int32 CountToUpdate = FMath::Min(PropsUpdatedPerFrame, RegisteredProps.Num());
	for (int32 i = 0; i < CountToUpdate; ++i)
	{
		if (CurrentPropUpdateIndex >= RegisteredProps.Num())
		{
			CurrentPropUpdateIndex = 0;
		}

		if (RegisteredProps.IsValidIndex(CurrentPropUpdateIndex))
		{
			if (AInteractableProp* Prop = RegisteredProps[CurrentPropUpdateIndex].Get())
			{
				if (Prop->bEnableTemperature)
				{
					// Update temperature using elapsed cycle time
					const float EffectiveDelta = DeltaSeconds * static_cast<float>(RegisteredProps.Num());
					Prop->UpdateTemperature(AmbientTemperature, EffectiveDelta);
				}
			}
		}

		CurrentPropUpdateIndex = (CurrentPropUpdateIndex + 1) % RegisteredProps.Num();
	}
}

void AInteractablePropManager::UpdateDistributedISM(float DeltaSeconds)
{
	// Clean stale weak pointers
	RegisteredISMComponents.RemoveAll([](const TWeakObjectPtr<UInteractableInstancedStaticMeshComponent>& Ptr) { return !Ptr.IsValid(); });

	if (RegisteredISMComponents.Num() == 0) return;

	if (CurrentISMUpdateIndex >= RegisteredISMComponents.Num())
	{
		CurrentISMUpdateIndex = 0;
	}

	if (RegisteredISMComponents.IsValidIndex(CurrentISMUpdateIndex))
	{
		if (UInteractableInstancedStaticMeshComponent* ISM = RegisteredISMComponents[CurrentISMUpdateIndex].Get())
		{
			const int32 NumInstances = ISM->GetInstanceCount();
			if (NumInstances > 0)
			{
				if (CurrentISMInstanceIndex >= NumInstances)
				{
					CurrentISMInstanceIndex = 0;
				}

				const float EffectiveDelta = DeltaSeconds * static_cast<float>(NumInstances);
				ISM->UpdateInstanceTemperature(CurrentISMInstanceIndex, AmbientTemperature, EffectiveDelta);

				CurrentISMInstanceIndex++;
				if (CurrentISMInstanceIndex >= NumInstances)
				{
					CurrentISMInstanceIndex = 0;
					CurrentISMUpdateIndex = (CurrentISMUpdateIndex + 1) % RegisteredISMComponents.Num();
				}
			}
			else
			{
				CurrentISMUpdateIndex = (CurrentISMUpdateIndex + 1) % RegisteredISMComponents.Num();
			}
		}
		else
		{
			CurrentISMUpdateIndex = (CurrentISMUpdateIndex + 1) % RegisteredISMComponents.Num();
		}
	}
}

void AInteractablePropManager::CheckSettlingQueue()
{
	// Clean stale pointers
	ActiveSettlingComponents.RemoveAll([](const TWeakObjectPtr<UStaticMeshPhysicsSimulationComponent>& Ptr) { return !Ptr.IsValid(); });

	if (ActiveSettlingComponents.Num() == 0) return;

	if (bUseAsyncSettlingCheck)
	{
		ProcessSettlingQueueAsync();
	}
	else
	{
		// Synchronous batched check
		TArray<UStaticMeshPhysicsSimulationComponent*> RestedComponents;
		for (auto It = ActiveSettlingComponents.CreateIterator(); It; ++It)
		{
			if (UStaticMeshPhysicsSimulationComponent* SimComp = It->Get())
			{
				if (SimComp->CurrentState == EPhysicsSimulationState::Settling)
				{
					const float Speed = SimComp->GetLinearSpeed();
					if (Speed <= SimComp->VelocityTolerance)
					{
						RestedComponents.Add(SimComp);
						It.RemoveCurrent();
					}
				}
				else
				{
					It.RemoveCurrent();
				}
			}
		}

		for (UStaticMeshPhysicsSimulationComponent* SimComp : RestedComponents)
		{
			SimComp->SetStateAtRest();
			OnSettlingObjectRested.Broadcast(SimComp, SimComp->GetOwner());
		}
	}
}

void AInteractablePropManager::ProcessSettlingQueueAsync()
{
	struct FSettlingData
	{
		TWeakObjectPtr<UStaticMeshPhysicsSimulationComponent> Comp;
		FVector Velocity;
		float Tolerance;
	};

	TArray<FSettlingData> Snapshots;
	Snapshots.Reserve(ActiveSettlingComponents.Num());

	for (const auto& WeakComp : ActiveSettlingComponents)
	{
		if (UStaticMeshPhysicsSimulationComponent* SimComp = WeakComp.Get())
		{
			if (SimComp->CurrentState == EPhysicsSimulationState::Settling)
			{
				UPrimitiveComponent* Prim = SimComp->TargetMesh;
				const FVector Vel = Prim ? Prim->GetComponentVelocity() : FVector::ZeroVector;
				Snapshots.Add({ WeakComp, Vel, SimComp->VelocityTolerance });
			}
		}
	}

	if (Snapshots.Num() == 0) return;

	TWeakObjectPtr<AInteractablePropManager> WeakThis(this);

	// Offload velocity calculations onto async background task
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Snapshots, WeakThis]()
	{
		TArray<TWeakObjectPtr<UStaticMeshPhysicsSimulationComponent>> SettledComps;
		for (const FSettlingData& Item : Snapshots)
		{
			if (Item.Velocity.Size() <= Item.Tolerance)
			{
				SettledComps.Add(Item.Comp);
			}
		}

		if (SettledComps.Num() > 0)
		{
			// Dispatch back to GameThread to set AtRest and remove from array
			AsyncTask(ENamedThreads::GameThread, [SettledComps, WeakThis]()
			{
				if (!WeakThis.IsValid()) return;
				AInteractablePropManager* Manager = WeakThis.Get();

				for (const auto& WeakComp : SettledComps)
				{
					if (UStaticMeshPhysicsSimulationComponent* SimComp = WeakComp.Get())
					{
						if (SimComp->CurrentState == EPhysicsSimulationState::Settling)
						{
							SimComp->SetStateAtRest();
							Manager->ActiveSettlingComponents.Remove(WeakComp);
							Manager->OnSettlingObjectRested.Broadcast(SimComp, SimComp->GetOwner());
						}
					}
				}
			});
		}
	});
}

bool AInteractablePropManager::CanInteractWithMesh(UPrimitiveComponent* TargetComponent, int32 InstanceIndex, AActor* InstigatorActor, bool& bOutIsGrabbable, FInteractablePropData& OutData)
{
	bOutIsGrabbable = false;
	if (!TargetComponent) return false;

	// Case 1: Custom Instanced Static Mesh Component
	if (UInteractableInstancedStaticMeshComponent* ISM = Cast<UInteractableInstancedStaticMeshComponent>(TargetComponent))
	{
		if (ISM->GetPropDataForInstance(InstanceIndex, OutData))
		{
			bOutIsGrabbable = OutData.bIsGrabbable;
			return true;
		}
		return false;
	}

	// Case 2: Individual AInteractableProp
	if (AInteractableProp* Prop = Cast<AInteractableProp>(TargetComponent->GetOwner()))
	{
		OutData.ItemDisplayName = Prop->ItemDisplayName;
		OutData.ConsumableType = Prop->ConsumableType;
		OutData.ConsumableTemperature = Prop->ConsumableTemperature;
		OutData.CurrentTemperature = Prop->CurrentTemperature;
		OutData.bEnableTemperature = Prop->bEnableTemperature;
		OutData.TemperatureTolerance = Prop->TemperatureTolerance;
		OutData.TemperatureDopamineBonus = Prop->TemperatureDopamineBonus;
		OutData.BodyTemperatureEffect = Prop->BodyTemperatureEffect;
		OutData.ThermalExchangeRate = Prop->ThermalExchangeRate;
		OutData.Price = Prop->Price;
		OutData.MaxPortions = Prop->MaxPortions;
		OutData.RemainingPortions = Prop->RemainingPortions;
		OutData.QuantityLevel = Prop->QuantityLevel;
		OutData.bIsGrabbable = Prop->bIsGrabbable;
		OutData.SimulationState = Prop->PhysicsSimComponent ? Prop->PhysicsSimComponent->CurrentState : EPhysicsSimulationState::AtRest;
		OutData.VelocityTolerance = Prop->PhysicsSimComponent ? Prop->PhysicsSimComponent->VelocityTolerance : 10.0f;
		OutData.bHasExpiry = Prop->bHasExpiry;
		OutData.ExpiryLifetimeMinutes = Prop->ExpiryLifetimeMinutes;
		OutData.AgeMinutes = Prop->AgeMinutes;
		OutData.SicknessLevel = Prop->SicknessLevel;
		OutData.ExpiredDopaminePenalty = Prop->ExpiredDopaminePenalty;
		OutData.StimuliPerPortion = Prop->StimuliPerPortion;
		OutData.TasteProfile = Prop->TasteProfile;

		bOutIsGrabbable = Prop->bIsGrabbable;
		return true;
	}

	return false;
}

bool AInteractablePropManager::InteractWithMesh(UPrimitiveComponent* TargetComponent, int32 InstanceIndex, AActor* InstigatorActor, FName ActionName, int32 PortionsToConsume, FInteractablePropData& OutUpdatedData)
{
	if (!TargetComponent || !InstigatorActor) return false;

	// Case 1: Custom Instanced Static Mesh Component
	if (UInteractableInstancedStaticMeshComponent* ISM = Cast<UInteractableInstancedStaticMeshComponent>(TargetComponent))
	{
		if (ActionName == FName("Consume") || ActionName == FName("Drink") || ActionName == FName("Eat"))
		{
			int32 Consumed = 0;
			const bool bSuccess = ISM->ConsumeInstancePortions(InstanceIndex, PortionsToConsume, InstigatorActor, Consumed);
			if (bSuccess)
			{
				ISM->GetPropDataForInstance(InstanceIndex, OutUpdatedData);
			}
			return bSuccess;
		}

		if (ActionName == FName("Grab") || ActionName == FName("PickUp"))
		{
			ISM->SetInstanceGrabbable(InstanceIndex, false);
			ISM->GetPropDataForInstance(InstanceIndex, OutUpdatedData);
			return true;
		}

		return ISM->GetPropDataForInstance(InstanceIndex, OutUpdatedData);
	}

	// Case 2: Individual AInteractableProp
	if (AInteractableProp* Prop = Cast<AInteractableProp>(TargetComponent->GetOwner()))
	{
		const float Ratio = (Prop->MaxPortions > 0) ? static_cast<float>(PortionsToConsume) / static_cast<float>(Prop->MaxPortions) : 1.0f;
		const bool bSuccess = Prop->ConsumePortion(InstigatorActor, Ratio);
		CanInteractWithMesh(TargetComponent, InstanceIndex, InstigatorActor, OutUpdatedData.bIsGrabbable, OutUpdatedData);
		return bSuccess;
	}

	return false;
}
