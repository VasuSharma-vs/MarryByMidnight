#include "Components/InteractablePropSenseSightComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

UInteractablePropSenseSightComponent::UInteractablePropSenseSightComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UInteractablePropSenseSightComponent::BeginPlay()
{
	Super::BeginPlay();

	// Ensure ticking is explicitly active
	PrimaryComponentTick.SetTickFunctionEnable(true);
	SetComponentTickEnabled(true);

	// Resolve PropManager and sibling BatchUpdateComponent
	APropManager* Manager = Cast<APropManager>(GetOwner());
	if (!Manager)
	{
		Manager = APropManager::Get(this);
	}
	if (Manager)
	{
		PropManagerRef = Manager;
		BatchUpdateComponentRef = Manager->BatchUpdateComponent;
	}

	// Hook into the batch evaluation native delegate
	if (BatchUpdateComponentRef.IsValid())
	{
		BatchUpdateComponentRef->OnBatchEvaluatePropsNative.AddUObject(this, &UInteractablePropSenseSightComponent::HandleBatchEvaluated);
	}
}

void UInteractablePropSenseSightComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BatchUpdateComponentRef.IsValid())
	{
		BatchUpdateComponentRef->OnBatchEvaluatePropsNative.RemoveAll(this);
	}

	OneTimeObservers.Empty();
	ContinuousObservers.Empty();

	Super::EndPlay(EndPlayReason);
}

void UInteractablePropSenseSightComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UWorld* World = GetWorld();
	if (!World) return;

	// 1. Draw debug for continuous observers
	for (FContinuousObserverData& Config : ContinuousObservers)
	{
		const bool bShouldDrawCone = (bDrawDebugCone || Config.bDrawDebugCone);
		const bool bShouldDrawShapes = (bDrawDebugVisibleObjects || Config.bDrawDebugVisibleObjects);

		if (bShouldDrawCone || bShouldDrawShapes)
		{
			if (bShouldDrawShapes)
			{
				EvaluateAllPropsForContinuous(Config);
			}
			DrawDebugForObserver(World, Config);
		}
	}

	// 2. Draw debug for one-time observers
	for (const FOneTimeObserverData& Config : OneTimeObservers)
	{
		if (bDrawDebugCone || bDrawDebugVisibleObjects || Config.bDrawDebugCone || Config.bDrawDebugVisibleObjects)
		{
			DrawDebugForObserver(World, Config);
		}
	}
}

// -----------------------------------------------------------------------------
// Section 1: Unique Observer Color Assignment
// -----------------------------------------------------------------------------

FColor UInteractablePropSenseSightComponent::AllocateUniqueObserverColor()
{
	static const FColor Palette[] = {
		FColor(0, 255, 255),    // Cyan
		FColor(255, 215, 0),    // Gold
		FColor(50, 205, 50),    // Lime Green
		FColor(255, 105, 180),  // Hot Pink
		FColor(255, 69, 0),     // Red-Orange
		FColor(138, 43, 226),   // Blue Violet
		FColor(0, 191, 255),    // Deep Sky Blue
		FColor(255, 140, 0),    // Dark Orange
		FColor(173, 255, 47),   // Green Yellow
		FColor(238, 130, 238),  // Violet
		FColor(0, 250, 154),    // Medium Spring Green
		FColor(255, 20, 147)    // Deep Pink
	};
	constexpr int32 PaletteSize = UE_ARRAY_COUNT(Palette);
	const FColor Color = Palette[NextColorIndex % PaletteSize];
	NextColorIndex++;
	return Color;
}

// -----------------------------------------------------------------------------
// Section 9: Distance-Based Sorting of Observation Results
// -----------------------------------------------------------------------------

void UInteractablePropSenseSightComponent::SyncDetectedItemsFromPerPID(FContinuousObserverData& Obs)
{
	Obs.DetectedItems.Reset();
	Obs.ISIDToIndexMap.Reset();

	int32 Total = 0;
	for (const auto& Pair : Obs.PerPIDDetectedItems)
	{
		Total += Pair.Value.Num();
	}
	Obs.DetectedItems.Reserve(Total);
	Obs.ISIDToIndexMap.Reserve(Total);

	for (const auto& Pair : Obs.PerPIDDetectedItems)
	{
		Obs.DetectedItems.Append(Pair.Value);
	}

	if (Obs.DetectedItems.Num() > 1)
	{
		Obs.DetectedItems.Sort([](const FPropSightDetectedItem& A, const FPropSightDetectedItem& B)
		{
			return A.Distance < B.Distance;
		});
	}

	for (int32 Idx = 0; Idx < Obs.DetectedItems.Num(); ++Idx)
	{
		Obs.ISIDToIndexMap.Add(Obs.DetectedItems[Idx].ISID, Idx);
	}

	if (Obs.DetectedItems.Num() > 0)
	{
		const FPropSightDetectedItem& Closest = Obs.DetectedItems[0];
		Obs.BestDistanceSq = FMath::Square(Closest.Distance);
		Obs.BestClosestProp.ISID = Closest.ISID;
		Obs.BestClosestProp.ProductID = Closest.ProductID;
		Obs.BestClosestProp.WorldLocation = Closest.Location;
		Obs.BestClosestProp.Distance = Closest.Distance;
		Obs.BestClosestProp.Representation = Closest.Representation;
		Obs.BestClosestProp.State = Closest.State;
		Obs.BestClosestProp.ActiveActor = Closest.ActiveActor;
	}
	else
	{
		Obs.BestClosestProp = FPropSightPerceptionResult();
		Obs.BestDistanceSq = MAX_FLT;
	}

	Obs.bNeedsSort = false;
}

void UInteractablePropSenseSightComponent::SyncDetectedItemsFromPerPID(FOneTimeObserverData& Obs)
{
	Obs.DetectedItems.Reset();
	Obs.ISIDToIndexMap.Reset();

	int32 Total = 0;
	for (const auto& Pair : Obs.PerPIDDetectedItems)
	{
		Total += Pair.Value.Num();
	}
	Obs.DetectedItems.Reserve(Total);
	Obs.ISIDToIndexMap.Reserve(Total);

	for (const auto& Pair : Obs.PerPIDDetectedItems)
	{
		Obs.DetectedItems.Append(Pair.Value);
	}

	if (Obs.DetectedItems.Num() > 1)
	{
		Obs.DetectedItems.Sort([](const FPropSightDetectedItem& A, const FPropSightDetectedItem& B)
		{
			return A.Distance < B.Distance;
		});
	}

	for (int32 Idx = 0; Idx < Obs.DetectedItems.Num(); ++Idx)
	{
		Obs.ISIDToIndexMap.Add(Obs.DetectedItems[Idx].ISID, Idx);
	}

	if (Obs.DetectedItems.Num() > 0)
	{
		const FPropSightDetectedItem& Closest = Obs.DetectedItems[0];
		Obs.BestDistanceSq = FMath::Square(Closest.Distance);
		Obs.BestClosestProp.ISID = Closest.ISID;
		Obs.BestClosestProp.ProductID = Closest.ProductID;
		Obs.BestClosestProp.WorldLocation = Closest.Location;
		Obs.BestClosestProp.Distance = Closest.Distance;
		Obs.BestClosestProp.Representation = Closest.Representation;
		Obs.BestClosestProp.State = Closest.State;
		Obs.BestClosestProp.ActiveActor = Closest.ActiveActor;
	}
	else
	{
		Obs.BestClosestProp = FPropSightPerceptionResult();
		Obs.BestDistanceSq = MAX_FLT;
	}

	Obs.bNeedsSort = false;
}

bool UInteractablePropSenseSightComponent::ProcessItemForObserver(
	FContinuousObserverData& Obs,
	const FPropBatchEvaluationItem& Item,
	bool bVisible,
	float DistSq,
	const FVector& TargetLoc)
{
	const bool bHasLimits = (Obs.PIDInstanceLimits.Num() > 0);

	if (bVisible)
	{
		const float Dist = FMath::Sqrt(DistSq);

		if (bHasLimits)
		{
			const int32* LimitPtr = Obs.PIDInstanceLimits.Find(Item.ProductID);
			if (!LimitPtr || *LimitPtr <= 0)
			{
				if (Obs.ISIDToIndexMap.Contains(Item.ISID))
				{
					for (auto& Pair : Obs.PerPIDDetectedItems)
					{
						for (int32 Idx = Pair.Value.Num() - 1; Idx >= 0; --Idx)
						{
							if (Pair.Value[Idx].ISID == Item.ISID)
							{
								Pair.Value.RemoveAt(Idx);
								break;
							}
						}
					}
					SyncDetectedItemsFromPerPID(Obs);
					return true;
				}
				return false;
			}

			const int32 MaxCount = *LimitPtr;
			TArray<FPropSightDetectedItem>& PIDList = Obs.PerPIDDetectedItems.FindOrAdd(Item.ProductID);

			int32 ExistingIdx = INDEX_NONE;
			for (int32 Idx = 0; Idx < PIDList.Num(); ++Idx)
			{
				if (PIDList[Idx].ISID == Item.ISID)
				{
					ExistingIdx = Idx;
					break;
				}
			}

			if (ExistingIdx != INDEX_NONE)
			{
				PIDList[ExistingIdx].Location = TargetLoc;
				PIDList[ExistingIdx].Distance = Dist;
				PIDList[ExistingIdx].Representation = Item.Representation;
				PIDList[ExistingIdx].State = Item.State;
				PIDList[ExistingIdx].ActiveActor = Item.ActiveActor;

				PIDList.Sort([](const FPropSightDetectedItem& A, const FPropSightDetectedItem& B)
				{
					return A.Distance < B.Distance;
				});

				SyncDetectedItemsFromPerPID(Obs);
				return true;
			}
			else
			{
				FPropSightDetectedItem NewDetected;
				NewDetected.ISID = Item.ISID;
				NewDetected.ProductID = Item.ProductID;
				NewDetected.Location = TargetLoc;
				NewDetected.Distance = Dist;
				NewDetected.Representation = Item.Representation;
				NewDetected.State = Item.State;
				NewDetected.ActiveActor = Item.ActiveActor;

				if (PIDList.Num() < MaxCount)
				{
					int32 InsertIdx = 0;
					while (InsertIdx < PIDList.Num() && PIDList[InsertIdx].Distance <= Dist)
					{
						++InsertIdx;
					}
					PIDList.Insert(NewDetected, InsertIdx);

					SyncDetectedItemsFromPerPID(Obs);
					return true;
				}
				else
				{
					if (PIDList.Num() > 0 && Dist < PIDList.Last().Distance)
					{
						PIDList.Pop();

						int32 InsertIdx = 0;
						while (InsertIdx < PIDList.Num() && PIDList[InsertIdx].Distance <= Dist)
						{
							++InsertIdx;
						}
						PIDList.Insert(NewDetected, InsertIdx);

						SyncDetectedItemsFromPerPID(Obs);
						return true;
					}
					return false;
				}
			}
		}
		else
		{
			FPropSightDetectedItem Detected;
			Detected.ISID = Item.ISID;
			Detected.ProductID = Item.ProductID;
			Detected.Location = TargetLoc;
			Detected.Distance = Dist;
			Detected.Representation = Item.Representation;
			Detected.State = Item.State;
			Detected.ActiveActor = Item.ActiveActor;

			AddOrUpdateDetectedItem(Obs.DetectedItems, Obs.ISIDToIndexMap, Detected);
			Obs.bNeedsSort = true;
			return true;
		}
	}
	else
	{
		if (bHasLimits)
		{
			if (Obs.ISIDToIndexMap.Contains(Item.ISID))
			{
				for (auto& Pair : Obs.PerPIDDetectedItems)
				{
					for (int32 Idx = Pair.Value.Num() - 1; Idx >= 0; --Idx)
					{
						if (Pair.Value[Idx].ISID == Item.ISID)
						{
							Pair.Value.RemoveAt(Idx);
							break;
						}
					}
				}
				SyncDetectedItemsFromPerPID(Obs);
				return true;
			}
		}
		else
		{
			if (Obs.ISIDToIndexMap.Contains(Item.ISID))
			{
				RemoveDetectedItem(Obs.DetectedItems, Obs.ISIDToIndexMap, Item.ISID);
				Obs.bNeedsSort = true;
				return true;
			}
		}
	}

	return false;
}

bool UInteractablePropSenseSightComponent::ProcessItemForObserver(
	FOneTimeObserverData& Obs,
	const FPropBatchEvaluationItem& Item,
	bool bVisible,
	float DistSq,
	const FVector& TargetLoc)
{
	const bool bHasLimits = (Obs.PIDInstanceLimits.Num() > 0);

	if (bVisible)
	{
		const float Dist = FMath::Sqrt(DistSq);

		if (bHasLimits)
		{
			const int32* LimitPtr = Obs.PIDInstanceLimits.Find(Item.ProductID);
			if (!LimitPtr || *LimitPtr <= 0)
			{
				if (Obs.ISIDToIndexMap.Contains(Item.ISID))
				{
					for (auto& Pair : Obs.PerPIDDetectedItems)
					{
						for (int32 Idx = Pair.Value.Num() - 1; Idx >= 0; --Idx)
						{
							if (Pair.Value[Idx].ISID == Item.ISID)
							{
								Pair.Value.RemoveAt(Idx);
								break;
							}
						}
					}
					SyncDetectedItemsFromPerPID(Obs);
					return true;
				}
				return false;
			}

			const int32 MaxCount = *LimitPtr;
			TArray<FPropSightDetectedItem>& PIDList = Obs.PerPIDDetectedItems.FindOrAdd(Item.ProductID);

			int32 ExistingIdx = INDEX_NONE;
			for (int32 Idx = 0; Idx < PIDList.Num(); ++Idx)
			{
				if (PIDList[Idx].ISID == Item.ISID)
				{
					ExistingIdx = Idx;
					break;
				}
			}

			if (ExistingIdx != INDEX_NONE)
			{
				PIDList[ExistingIdx].Location = TargetLoc;
				PIDList[ExistingIdx].Distance = Dist;
				PIDList[ExistingIdx].Representation = Item.Representation;
				PIDList[ExistingIdx].State = Item.State;
				PIDList[ExistingIdx].ActiveActor = Item.ActiveActor;

				PIDList.Sort([](const FPropSightDetectedItem& A, const FPropSightDetectedItem& B)
				{
					return A.Distance < B.Distance;
				});

				SyncDetectedItemsFromPerPID(Obs);
				return true;
			}
			else
			{
				FPropSightDetectedItem NewDetected;
				NewDetected.ISID = Item.ISID;
				NewDetected.ProductID = Item.ProductID;
				NewDetected.Location = TargetLoc;
				NewDetected.Distance = Dist;
				NewDetected.Representation = Item.Representation;
				NewDetected.State = Item.State;
				NewDetected.ActiveActor = Item.ActiveActor;

				if (PIDList.Num() < MaxCount)
				{
					int32 InsertIdx = 0;
					while (InsertIdx < PIDList.Num() && PIDList[InsertIdx].Distance <= Dist)
					{
						++InsertIdx;
					}
					PIDList.Insert(NewDetected, InsertIdx);

					SyncDetectedItemsFromPerPID(Obs);
					return true;
				}
				else
				{
					if (PIDList.Num() > 0 && Dist < PIDList.Last().Distance)
					{
						PIDList.Pop();

						int32 InsertIdx = 0;
						while (InsertIdx < PIDList.Num() && PIDList[InsertIdx].Distance <= Dist)
						{
							++InsertIdx;
						}
						PIDList.Insert(NewDetected, InsertIdx);

						SyncDetectedItemsFromPerPID(Obs);
						return true;
					}
					return false;
				}
			}
		}
		else
		{
			FPropSightDetectedItem Detected;
			Detected.ISID = Item.ISID;
			Detected.ProductID = Item.ProductID;
			Detected.Location = TargetLoc;
			Detected.Distance = Dist;
			Detected.Representation = Item.Representation;
			Detected.State = Item.State;
			Detected.ActiveActor = Item.ActiveActor;

			AddOrUpdateDetectedItem(Obs.DetectedItems, Obs.ISIDToIndexMap, Detected);
			Obs.bNeedsSort = true;
			return true;
		}
	}
	else
	{
		if (bHasLimits)
		{
			if (Obs.ISIDToIndexMap.Contains(Item.ISID))
			{
				for (auto& Pair : Obs.PerPIDDetectedItems)
				{
					for (int32 Idx = Pair.Value.Num() - 1; Idx >= 0; --Idx)
					{
						if (Pair.Value[Idx].ISID == Item.ISID)
						{
							Pair.Value.RemoveAt(Idx);
							break;
						}
					}
				}
				SyncDetectedItemsFromPerPID(Obs);
				return true;
			}
		}
		else
		{
			if (Obs.ISIDToIndexMap.Contains(Item.ISID))
			{
				RemoveDetectedItem(Obs.DetectedItems, Obs.ISIDToIndexMap, Item.ISID);
				Obs.bNeedsSort = true;
				return true;
			}
		}
	}

	return false;
}

void UInteractablePropSenseSightComponent::EvaluatePropsForPID(FContinuousObserverData& Obs, int32 PID)
{
	APropManager* Manager = PropManagerRef.Get();
	if (!Manager)
	{
		Manager = APropManager::Get(this);
		PropManagerRef = Manager;
	}
	if (!Manager || PID <= 0) return;

	for (const TPair<int32, FISIDRuntimeIndex>& Pair : Manager->ISIDData)
	{
		const FISIDRuntimeIndex& Record = Pair.Value;
		if (Record.ISID <= 0 || Record.ProductID != PID) continue;

		FPropBatchEvaluationItem Item;
		Item.ISID = Record.ISID;
		Item.ProductID = Record.ProductID;
		Item.Representation = Record.Representation;
		Item.State = Record.State;
		Item.CurrentTemperature = Record.PackedRuntimeData.Temperature;
		Item.RemainingPortions = Record.PackedRuntimeData.RemainingPortions;
		Item.IISMComponentID = Record.IISMComponentID;
		Item.ActiveActor = Record.ActiveActor;
		Item.bIsGrabbable = Record.PackedRuntimeData.bIsGrabbable;
		if (Record.ActiveActor)
		{
			Item.bIsGrabbable = Record.ActiveActor->bIsGrabbable;
		}
		if (Record.State == EPropState::Held)
		{
			Item.bIsGrabbable = false;
		}

		Item.WorldTransform = Record.ActiveTransform;
		Item.WorldLocation = Record.ActiveTransform.GetLocation();

		if (Record.Representation == EPropRepresentation::ISM)
		{
			if (UInteractableISMComponent* Comp = Manager->FindIISMComponent(Record.IISMComponentID))
			{
				FTransform InstTransform;
				if (Comp->GetInstanceTransform(Record.ISMI, InstTransform, true))
				{
					Item.WorldTransform = InstTransform;
					Item.WorldLocation = InstTransform.GetLocation();
				}
			}
		}
		else if (Record.ActiveActor)
		{
			Item.WorldTransform = Record.ActiveActor->GetActorTransform();
			Item.WorldLocation = Record.ActiveActor->GetActorLocation();
		}

		float DistSq = 0.0f;
		FVector TargetLoc = FVector::ZeroVector;
		const bool bVisible = IsItemVisible(Item, Obs, DistSq, TargetLoc);
		ProcessItemForObserver(Obs, Item, bVisible, DistSq, TargetLoc);
	}
}

void UInteractablePropSenseSightComponent::EvaluatePropsForPID(FOneTimeObserverData& Obs, int32 PID)
{
	APropManager* Manager = PropManagerRef.Get();
	if (!Manager)
	{
		Manager = APropManager::Get(this);
		PropManagerRef = Manager;
	}
	if (!Manager || PID <= 0) return;

	for (const TPair<int32, FISIDRuntimeIndex>& Pair : Manager->ISIDData)
	{
		const FISIDRuntimeIndex& Record = Pair.Value;
		if (Record.ISID <= 0 || Record.ProductID != PID) continue;

		FPropBatchEvaluationItem Item;
		Item.ISID = Record.ISID;
		Item.ProductID = Record.ProductID;
		Item.Representation = Record.Representation;
		Item.State = Record.State;
		Item.CurrentTemperature = Record.PackedRuntimeData.Temperature;
		Item.RemainingPortions = Record.PackedRuntimeData.RemainingPortions;
		Item.IISMComponentID = Record.IISMComponentID;
		Item.ActiveActor = Record.ActiveActor;
		Item.bIsGrabbable = Record.PackedRuntimeData.bIsGrabbable;
		if (Record.ActiveActor)
		{
			Item.bIsGrabbable = Record.ActiveActor->bIsGrabbable;
		}
		if (Record.State == EPropState::Held)
		{
			Item.bIsGrabbable = false;
		}

		Item.WorldTransform = Record.ActiveTransform;
		Item.WorldLocation = Record.ActiveTransform.GetLocation();

		if (Record.Representation == EPropRepresentation::ISM)
		{
			if (UInteractableISMComponent* Comp = Manager->FindIISMComponent(Record.IISMComponentID))
			{
				FTransform InstTransform;
				if (Comp->GetInstanceTransform(Record.ISMI, InstTransform, true))
				{
					Item.WorldTransform = InstTransform;
					Item.WorldLocation = InstTransform.GetLocation();
				}
			}
		}
		else if (Record.ActiveActor)
		{
			Item.WorldTransform = Record.ActiveActor->GetActorTransform();
			Item.WorldLocation = Record.ActiveActor->GetActorLocation();
		}

		float DistSq = 0.0f;
		FVector TargetLoc = FVector::ZeroVector;
		const bool bVisible = IsItemVisible(Item, Obs, DistSq, TargetLoc);
		ProcessItemForObserver(Obs, Item, bVisible, DistSq, TargetLoc);
	}
}

void UInteractablePropSenseSightComponent::SortObserverResults(
	FContinuousObserverData& Obs,
	const FVector& ObserverLocation)
{
	const float MaxSightRadius = Obs.SightRadius;

	if (Obs.PIDInstanceLimits.Num() > 0)
	{
		for (auto& Pair : Obs.PerPIDDetectedItems)
		{
			// 1. Recalculate distance and prune items that moved outside sight radius
			for (int32 Idx = Pair.Value.Num() - 1; Idx >= 0; --Idx)
			{
				Pair.Value[Idx].Distance = FVector::Dist(ObserverLocation, Pair.Value[Idx].Location);
				if (Pair.Value[Idx].Distance > MaxSightRadius)
				{
					Pair.Value.RemoveAt(Idx);
				}
			}

			// 2. Sort remaining items in ascending order of distance
			if (Pair.Value.Num() > 1)
			{
				Pair.Value.Sort([](const FPropSightDetectedItem& A, const FPropSightDetectedItem& B)
				{
					return A.Distance < B.Distance;
				});
			}
		}

		SyncDetectedItemsFromPerPID(Obs);
		Obs.LastEvaluatedLocation = ObserverLocation;
		Obs.bNeedsSort = false;
		return;
	}

	// Case with no PID limits:
	// 1. Recalculate distance and prune items outside sight radius
	for (int32 Idx = Obs.DetectedItems.Num() - 1; Idx >= 0; --Idx)
	{
		Obs.DetectedItems[Idx].Distance = FVector::Dist(ObserverLocation, Obs.DetectedItems[Idx].Location);
		if (Obs.DetectedItems[Idx].Distance > MaxSightRadius)
		{
			Obs.DetectedItems.RemoveAt(Idx);
		}
	}

	const int32 NumItems = Obs.DetectedItems.Num();
	if (NumItems == 0)
	{
		Obs.BestClosestProp = FPropSightPerceptionResult();
		Obs.BestDistanceSq = MAX_FLT;
		Obs.ISIDToIndexMap.Reset();
		Obs.LastEvaluatedLocation = ObserverLocation;
		Obs.bNeedsSort = false;
		return;
	}

	// 2. Sort observer's result array in ascending order of distance
	// Index 0: Closest observed instance. Last index: Farthest observed instance.
	if (NumItems > 1)
	{
		Obs.DetectedItems.Sort([](const FPropSightDetectedItem& A, const FPropSightDetectedItem& B)
		{
			return A.Distance < B.Distance;
		});
	}

	// 3. Rebuild ISIDToIndexMap to reflect newly sorted indices
	Obs.ISIDToIndexMap.Reset();
	Obs.ISIDToIndexMap.Reserve(NumItems);
	for (int32 Idx = 0; Idx < NumItems; ++Idx)
	{
		Obs.ISIDToIndexMap.Add(Obs.DetectedItems[Idx].ISID, Idx);
	}

	// 4. Closest instance is always at index 0
	const FPropSightDetectedItem& Closest = Obs.DetectedItems[0];
	Obs.BestDistanceSq = FMath::Square(Closest.Distance);
	Obs.BestClosestProp.ISID = Closest.ISID;
	Obs.BestClosestProp.ProductID = Closest.ProductID;
	Obs.BestClosestProp.WorldLocation = Closest.Location;
	Obs.BestClosestProp.Distance = Closest.Distance;
	Obs.BestClosestProp.Representation = Closest.Representation;
	Obs.BestClosestProp.State = Closest.State;
	Obs.BestClosestProp.ActiveActor = Closest.ActiveActor;

	Obs.LastEvaluatedLocation = ObserverLocation;
	Obs.bNeedsSort = false;
}

void UInteractablePropSenseSightComponent::SortObserverResults(
	FOneTimeObserverData& Obs,
	const FVector& ObserverLocation)
{
	const float MaxSightRadius = Obs.SightRadius;

	if (Obs.PIDInstanceLimits.Num() > 0)
	{
		for (auto& Pair : Obs.PerPIDDetectedItems)
		{
			// 1. Recalculate distance and prune items that moved outside sight radius
			for (int32 Idx = Pair.Value.Num() - 1; Idx >= 0; --Idx)
			{
				Pair.Value[Idx].Distance = FVector::Dist(ObserverLocation, Pair.Value[Idx].Location);
				if (Pair.Value[Idx].Distance > MaxSightRadius)
				{
					Pair.Value.RemoveAt(Idx);
				}
			}

			// 2. Sort remaining items in ascending order of distance
			if (Pair.Value.Num() > 1)
			{
				Pair.Value.Sort([](const FPropSightDetectedItem& A, const FPropSightDetectedItem& B)
				{
					return A.Distance < B.Distance;
				});
			}
		}

		SyncDetectedItemsFromPerPID(Obs);
		Obs.LastEvaluatedLocation = ObserverLocation;
		Obs.bNeedsSort = false;
		return;
	}

	// Case with no PID limits:
	// 1. Recalculate distance and prune items outside sight radius
	for (int32 Idx = Obs.DetectedItems.Num() - 1; Idx >= 0; --Idx)
	{
		Obs.DetectedItems[Idx].Distance = FVector::Dist(ObserverLocation, Obs.DetectedItems[Idx].Location);
		if (Obs.DetectedItems[Idx].Distance > MaxSightRadius)
		{
			Obs.DetectedItems.RemoveAt(Idx);
		}
	}

	const int32 NumItems = Obs.DetectedItems.Num();
	if (NumItems == 0)
	{
		Obs.BestClosestProp = FPropSightPerceptionResult();
		Obs.BestDistanceSq = MAX_FLT;
		Obs.ISIDToIndexMap.Reset();
		Obs.LastEvaluatedLocation = ObserverLocation;
		Obs.bNeedsSort = false;
		return;
	}

	// 2. Sort in ascending order of distance
	if (NumItems > 1)
	{
		Obs.DetectedItems.Sort([](const FPropSightDetectedItem& A, const FPropSightDetectedItem& B)
		{
			return A.Distance < B.Distance;
		});
	}

	// 3. Rebuild ISIDToIndexMap to reflect newly sorted indices
	Obs.ISIDToIndexMap.Reset();
	Obs.ISIDToIndexMap.Reserve(NumItems);
	for (int32 Idx = 0; Idx < NumItems; ++Idx)
	{
		Obs.ISIDToIndexMap.Add(Obs.DetectedItems[Idx].ISID, Idx);
	}

	// 4. Closest instance is always at index 0
	const FPropSightDetectedItem& Closest = Obs.DetectedItems[0];
	Obs.BestDistanceSq = FMath::Square(Closest.Distance);
	Obs.BestClosestProp.ISID = Closest.ISID;
	Obs.BestClosestProp.ProductID = Closest.ProductID;
	Obs.BestClosestProp.WorldLocation = Closest.Location;
	Obs.BestClosestProp.Distance = Closest.Distance;
	Obs.BestClosestProp.Representation = Closest.Representation;
	Obs.BestClosestProp.State = Closest.State;
	Obs.BestClosestProp.ActiveActor = Closest.ActiveActor;

	Obs.LastEvaluatedLocation = ObserverLocation;
	Obs.bNeedsSort = false;
}

// -----------------------------------------------------------------------------
// Helpers: Add/Update & Remove Detected Items with synchronized IndexMap
// -----------------------------------------------------------------------------

void UInteractablePropSenseSightComponent::AddOrUpdateDetectedItem(
	TArray<FPropSightDetectedItem>& Items,
	TMap<int32, int32>& IndexMap,
	const FPropSightDetectedItem& NewItem)
{
	if (int32* FoundIdx = IndexMap.Find(NewItem.ISID))
	{
		if (Items.IsValidIndex(*FoundIdx))
		{
			Items[*FoundIdx] = NewItem;
			return;
		}
	}

	const int32 NewIndex = Items.Add(NewItem);
	IndexMap.Add(NewItem.ISID, NewIndex);
}

void UInteractablePropSenseSightComponent::RemoveDetectedItem(
	TArray<FPropSightDetectedItem>& Items,
	TMap<int32, int32>& IndexMap,
	int32 InISID)
{
	int32 FoundIdx = INDEX_NONE;
	if (IndexMap.RemoveAndCopyValue(InISID, FoundIdx))
	{
		if (Items.IsValidIndex(FoundIdx))
		{
			const int32 LastIdx = Items.Num() - 1;
			if (FoundIdx != LastIdx)
			{
				Items.RemoveAtSwap(FoundIdx);
				IndexMap[Items[FoundIdx].ISID] = FoundIdx;
			}
			else
			{
				Items.RemoveAt(FoundIdx);
			}
		}
	}
}

// -----------------------------------------------------------------------------
// Sight Transform Resolution
// -----------------------------------------------------------------------------

void UInteractablePropSenseSightComponent::ResolveSightTransform(
	const FContinuousObserverData& Config,
	FVector& OutLocation,
	FRotator& OutRotation) const
{
	// 1. If manual override is explicitly enabled, use override location & rotation
	if (Config.bHasManualTransformOverride)
	{
		OutLocation = Config.SightLocationOverride;
		OutRotation = Config.SightRotationOverride;
		return;
	}

	// 2. Resolve Observer Actor (or fallback to Component Owner)
	AActor* Observer = Config.ObserverActor.Get();
	if (!Observer)
	{
		Observer = GetOwner();
	}

	if (AController* C = Cast<AController>(Observer))
	{
		if (APawn* ControlledPawn = C->GetPawn())
		{
			Observer = ControlledPawn;
		}
	}

	if (Observer)
	{
		FVector EyesLoc = Observer->GetActorLocation();
		FRotator EyesRot = Observer->GetActorRotation();

		if (APawn* Pawn = Cast<APawn>(Observer))
		{
			EyesLoc = Pawn->GetPawnViewLocation();
			EyesRot = Pawn->GetViewRotation();
			if (EyesRot.IsNearlyZero() && Pawn->GetController())
			{
				EyesRot = Pawn->GetController()->GetControlRotation();
			}
			if (EyesRot.IsNearlyZero())
			{
				EyesRot = Pawn->GetActorRotation();
			}
		}
		else
		{
			Observer->GetActorEyesViewPoint(EyesLoc, EyesRot);
			if (EyesRot.IsNearlyZero())
			{
				EyesRot = Observer->GetActorRotation();
			}
		}

		OutLocation = EyesLoc;
		OutRotation = EyesRot;
		return;
	}

	// 3. Fallback to override transform if no valid actor exists
	OutLocation = Config.SightLocationOverride;
	OutRotation = Config.SightRotationOverride;
}

void UInteractablePropSenseSightComponent::ResolveSightTransform(
	const FOneTimeObserverData& Config,
	FVector& OutLocation,
	FRotator& OutRotation) const
{
	// 1. If manual override is explicitly enabled, use override location & rotation
	if (Config.bHasManualTransformOverride)
	{
		OutLocation = Config.SightLocationOverride;
		OutRotation = Config.SightRotationOverride;
		return;
	}

	// 2. Resolve Observer Actor (or fallback to Component Owner)
	AActor* Observer = Config.ObserverActor.Get();
	if (!Observer)
	{
		Observer = GetOwner();
	}

	if (AController* C = Cast<AController>(Observer))
	{
		if (APawn* ControlledPawn = C->GetPawn())
		{
			Observer = ControlledPawn;
		}
	}

	if (Observer)
	{
		FVector EyesLoc = Observer->GetActorLocation();
		FRotator EyesRot = Observer->GetActorRotation();

		if (APawn* Pawn = Cast<APawn>(Observer))
		{
			EyesLoc = Pawn->GetPawnViewLocation();
			EyesRot = Pawn->GetViewRotation();
			if (EyesRot.IsNearlyZero() && Pawn->GetController())
			{
				EyesRot = Pawn->GetController()->GetControlRotation();
			}
			if (EyesRot.IsNearlyZero())
			{
				EyesRot = Pawn->GetActorRotation();
			}
		}
		else
		{
			Observer->GetActorEyesViewPoint(EyesLoc, EyesRot);
			if (EyesRot.IsNearlyZero())
			{
				EyesRot = Observer->GetActorRotation();
			}
		}

		OutLocation = EyesLoc;
		OutRotation = EyesRot;
		return;
	}

	// 3. Fallback to override transform if no valid actor exists
	OutLocation = Config.SightLocationOverride;
	OutRotation = Config.SightRotationOverride;
}

// -----------------------------------------------------------------------------
// Line-of-Sight Raycast Validation
// -----------------------------------------------------------------------------

bool UInteractablePropSenseSightComponent::CheckMultiPointLineOfSight(
	const FVector& ViewOrigin,
	const FPropBatchEvaluationItem& Item,
	ECollisionChannel TraceChannel,
	AActor* ObserverActor) const
{
	UWorld* World = GetWorld();
	if (!World) return false;

	APropManager* Manager = PropManagerRef.Get();
	if (!Manager)
	{
		Manager = APropManager::Get(this);
	}
	UInteractableISMComponent* ExpectedISMComp = nullptr;
	if (Manager && Item.Representation == EPropRepresentation::ISM)
	{
		ExpectedISMComp = Manager->FindIISMComponent(Item.IISMComponentID);
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(PropSenseSightTrace), false);
	AActor* ResolvedObserverPawn = ObserverActor;

	if (ObserverActor)
	{
		Params.AddIgnoredActor(ObserverActor);

		TArray<AActor*> AttachedActors;
		ObserverActor->GetAttachedActors(AttachedActors, true, true);
		for (AActor* Attached : AttachedActors)
		{
			if (Attached)
			{
				Params.AddIgnoredActor(Attached);
			}
		}

		if (APawn* PawnActor = Cast<APawn>(ObserverActor))
		{
			ResolvedObserverPawn = PawnActor;
			if (AController* C = PawnActor->GetController())
			{
				Params.AddIgnoredActor(C);
			}
		}
		else if (AController* C = Cast<AController>(ObserverActor))
		{
			if (APawn* ControlledPawn = C->GetPawn())
			{
				ResolvedObserverPawn = ControlledPawn;
				Params.AddIgnoredActor(ControlledPawn);
				TArray<AActor*> PawnAttached;
				ControlledPawn->GetAttachedActors(PawnAttached, true, true);
				for (AActor* Attached : PawnAttached)
				{
					if (Attached)
					{
						Params.AddIgnoredActor(Attached);
					}
				}
			}
		}
	}
	if (Item.ActiveActor.IsValid())
	{
		Params.AddIgnoredActor(Item.ActiveActor.Get());
	}

	const FVector BaseElevated = Item.WorldLocation + FVector(0.0f, 0.0f, 15.0f);
	const FVector MidElevated = Item.WorldLocation + FVector(0.0f, 0.0f, 35.0f);
	const FVector TopElevated = Item.WorldLocation + FVector(0.0f, 0.0f, 55.0f);
	const FVector ViewDir = (Item.WorldLocation - ViewOrigin).GetSafeNormal();
	const FVector RightDir = (ViewDir ^ FVector::UpVector).GetSafeNormal() * 15.0f;

	TArray<FVector, TInlineAllocator<5>> SamplePoints;
	SamplePoints.Add(BaseElevated);
	SamplePoints.Add(MidElevated);
	SamplePoints.Add(TopElevated);
	SamplePoints.Add(MidElevated + RightDir);
	SamplePoints.Add(MidElevated - RightDir);

	for (const FVector& TargetPoint : SamplePoints)
	{
		const float FullDistance = FVector::Dist(ViewOrigin, TargetPoint);
		if (FullDistance <= 120.0f)
		{
			return true;
		}

		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(Hit, ViewOrigin, TargetPoint, TraceChannel, Params);

		if (!bHit)
		{
			return true;
		}

		if (ObserverActor && (Hit.GetActor() == ObserverActor || Hit.GetActor()->IsAttachedTo(ObserverActor)))
		{
			return true;
		}
		if (ResolvedObserverPawn && (Hit.GetActor() == ResolvedObserverPawn || Hit.GetActor()->IsAttachedTo(ResolvedObserverPawn)))
		{
			return true;
		}

		if (Item.ActiveActor.IsValid() && Hit.GetActor() == Item.ActiveActor.Get())
		{
			return true;
		}

		if ((Manager && Hit.GetActor() == Manager) || 
			(ExpectedISMComp && Hit.GetComponent() == ExpectedISMComp) ||
			(Hit.GetActor() && Hit.GetActor()->IsA<APropManager>()) ||
			(Hit.GetComponent() && Hit.GetComponent()->IsA<UInstancedStaticMeshComponent>()))
		{
			return true;
		}

		if (Hit.Distance >= (FullDistance - 45.0f))
		{
			return true;
		}
	}

	return false;
}

// -----------------------------------------------------------------------------
// Visibility Testing
// -----------------------------------------------------------------------------

bool UInteractablePropSenseSightComponent::IsItemVisible(
	const FPropBatchEvaluationItem& Item,
	const FContinuousObserverData& Obs,
	float& OutDistSq,
	FVector& OutTargetLoc) const
{
	if (Obs.IgnoredISIDs.Contains(Item.ISID))
	{
		return false;
	}

	if (Obs.bRequireGrabbable && !Item.bIsGrabbable)
	{
		return false;
	}

	if (Obs.bFilterByPID)
	{
		if (!Obs.TargetProductIDs.Contains(Item.ProductID))
		{
			return false;
		}
	}

	FVector ViewOrigin;
	FRotator ViewRotation;
	ResolveSightTransform(Obs, ViewOrigin, ViewRotation);

	const float DistSq = FVector::DistSquared(ViewOrigin, Item.WorldLocation);
	if (DistSq > FMath::Square(Obs.SightRadius))
	{
		return false;
	}

	const FVector DirectionToItem = (Item.WorldLocation - ViewOrigin).GetSafeNormal();
	const FVector ViewForward = ViewRotation.Vector();
	const float Dot = FVector::DotProduct(ViewForward, DirectionToItem);
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Obs.PeripheralVisionHalfAngle, 1.0f, 180.0f)));
	if (Dot < MinDot)
	{
		return false;
	}

	if (!Obs.bIgnoreBlocked)
	{
		if (!CheckMultiPointLineOfSight(ViewOrigin, Item, Obs.TraceChannel, Obs.ObserverActor.Get()))
		{
			return false;
		}
	}

	OutDistSq = DistSq;
	OutTargetLoc = Item.WorldLocation;
	return true;
}

bool UInteractablePropSenseSightComponent::IsItemVisible(
	const FPropBatchEvaluationItem& Item,
	const FOneTimeObserverData& Obs,
	float& OutDistSq,
	FVector& OutTargetLoc) const
{
	// Section 11.A: Ignored instances excluded from one-time observation results
	if (Obs.IgnoredISIDs.Contains(Item.ISID))
	{
		return false;
	}

	if (Obs.bRequireGrabbable && !Item.bIsGrabbable)
	{
		return false;
	}

	if (Obs.bFilterByPID)
	{
		if (!Obs.TargetProductIDs.Contains(Item.ProductID))
		{
			return false;
		}
	}

	FVector ViewOrigin;
	FRotator ViewRotation;
	ResolveSightTransform(Obs, ViewOrigin, ViewRotation);

	const float DistSq = FVector::DistSquared(ViewOrigin, Item.WorldLocation);
	if (DistSq > FMath::Square(Obs.SightRadius))
	{
		return false;
	}

	const FVector DirectionToItem = (Item.WorldLocation - ViewOrigin).GetSafeNormal();
	const FVector ViewForward = ViewRotation.Vector();
	const float Dot = FVector::DotProduct(ViewForward, DirectionToItem);
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Obs.PeripheralVisionHalfAngle, 1.0f, 180.0f)));
	if (Dot < MinDot)
	{
		return false;
	}

	if (!Obs.bIgnoreBlocked)
	{
		if (!CheckMultiPointLineOfSight(ViewOrigin, Item, Obs.TraceChannel, Obs.ObserverActor.Get()))
		{
			return false;
		}
	}

	OutDistSq = DistSq;
	OutTargetLoc = Item.WorldLocation;
	return true;
}

// -----------------------------------------------------------------------------
// Section 4: Dedicated Continuous Observation Function
// -----------------------------------------------------------------------------

void UInteractablePropSenseSightComponent::ProcessContinuousObservers(
	const TArray<FPropBatchEvaluationItem>& Batch,
	const FPropBatchMetadata& Metadata)
{
	// 1. For every instance received from PropBatchUpdateComponent:
	for (const FPropBatchEvaluationItem& Item : Batch)
	{
		for (FContinuousObserverData& Obs : ContinuousObservers)
		{
			if (!Obs.ObserverActor.IsValid())
			{
				continue;
			}

			float DistSq = 0.0f;
			FVector TargetLoc = FVector::ZeroVector;
			const bool bVisible = IsItemVisible(Item, Obs, DistSq, TargetLoc);
			ProcessItemForObserver(Obs, Item, bVisible, DistSq, TargetLoc);
		}
	}

	// 2. Section 9: Maintain accurate distance-based sorting for each continuous observer
	for (FContinuousObserverData& Obs : ContinuousObservers)
	{
		if (!Obs.ObserverActor.IsValid())
		{
			continue;
		}

		FVector CurrentOrigin;
		FRotator CurrentRot;
		ResolveSightTransform(Obs, CurrentOrigin, CurrentRot);

		// Account for observer movement using squared distance comparison (avoids sqrt)
		const bool bObserverMoved = (FVector::DistSquared(CurrentOrigin, Obs.LastEvaluatedLocation) > 25.0f);
		if (Obs.bNeedsSort || bObserverMoved)
		{
			SortObserverResults(Obs, CurrentOrigin);
		}
	}

	// 3. After relevant iteration cycle completes, send updated observation results to each continuous observer
	if (Metadata.bCycleCompleted)
	{
		// Section 12.D: Snapshot observer actors to guard against array mutation if a callback calls UnregisterObserver
		TArray<TWeakObjectPtr<AActor>> ObserversToNotify;
		ObserversToNotify.Reserve(ContinuousObservers.Num());
		for (const FContinuousObserverData& Obs : ContinuousObservers)
		{
			if (Obs.ObserverActor.IsValid())
			{
				ObserversToNotify.Add(Obs.ObserverActor);
			}
		}

		for (const TWeakObjectPtr<AActor>& WeakObs : ObserversToNotify)
		{
			AActor* Observer = WeakObs.Get();
			if (!Observer)
			{
				continue;
			}

			// Verify observer is STILL registered (not unregistered by an earlier callback in this cycle)
			FContinuousObserverData* ObsPtr = FindContinuousObserverMutable(Observer);
			if (!ObsPtr)
			{
				continue;
			}

			// Ensure results are accurately sorted with latest observer location
			FVector CurrentOrigin;
			FRotator CurrentRot;
			ResolveSightTransform(*ObsPtr, CurrentOrigin, CurrentRot);
			if (ObsPtr->bNeedsSort || FVector::DistSquared(CurrentOrigin, ObsPtr->LastEvaluatedLocation) > 1.0f)
			{
				SortObserverResults(*ObsPtr, CurrentOrigin);
			}

			if (OnPropSightPerceptionUpdated.IsBound())
			{
				FPropSightDetectedProps DetectedWrapper = BuildDetectedPropsWrapper(ObsPtr->DetectedItems);

				if (ObsPtr->bDrawDebugCone || ObsPtr->bDrawDebugVisibleObjects || DetectedWrapper.Props.Num() > 0)
				{
					FString ItemsSummary;
					APropManager* Mgr = PropManagerRef.Get();
					for (const FPropSightDetectedItem& Item : ObsPtr->DetectedItems)
					{
						FString PName = TEXT("Unknown");
						if (Mgr)
						{
							if (const FPropProductDefinition* Def = Mgr->FindProductDefinition(Item.ProductID))
							{
								PName = Def->DisplayName.ToString();
							}
						}
						ItemsSummary += FString::Printf(TEXT("[ISID:%d PID:%d (%s) Dist:%.0f] "), Item.ISID, Item.ProductID, *PName, Item.Distance);
					}

					UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] OnPropSightPerceptionUpdated: Observer '%s' (Color R=%d G=%d B=%d): Detected %d props (sorted closest to farthest): %s| Closest: ISID=%d PID=%d (Dist=%.0f)"),
						*Observer->GetName(),
						ObsPtr->DebugColor.R, ObsPtr->DebugColor.G, ObsPtr->DebugColor.B,
						ObsPtr->DetectedItems.Num(),
						*ItemsSummary,
						ObsPtr->BestClosestProp.ISID,
						ObsPtr->BestClosestProp.ProductID,
						ObsPtr->BestClosestProp.Distance);
				}

				FPropSightPerceptionResult ClosestProp = ObsPtr->BestClosestProp;

				OnPropSightPerceptionUpdated.Broadcast(
					Observer,
					DetectedWrapper.Props.Num() > 0,
					DetectedWrapper,
					ClosestProp
				);
			}
		}
	}

	// 4. Remove dead observers using reverse iteration
	for (int32 i = ContinuousObservers.Num() - 1; i >= 0; --i)
	{
		if (!ContinuousObservers[i].ObserverActor.IsValid())
		{
			ContinuousObservers.RemoveAt(i);
		}
	}
}

// -----------------------------------------------------------------------------
// Section 5: Dedicated One-Time Observation Function
// -----------------------------------------------------------------------------

void UInteractablePropSenseSightComponent::ProcessOneTimeObservers(
	const TArray<FPropBatchEvaluationItem>& Batch,
	const FPropBatchMetadata& Metadata)
{
	// Important: Use reverse iteration so removal does not cause adjacent observers to be skipped
	for (int32 i = OneTimeObservers.Num() - 1; i >= 0; --i)
	{
		FOneTimeObserverData& Obs = OneTimeObservers[i];

		if (!Obs.ObserverActor.IsValid() && Obs.SightLocationOverride.IsZero())
		{
			OneTimeObservers.RemoveAt(i);
			continue;
		}

		if (Metadata.bCycleCompleted)
		{
			Obs.bHasWrappedOnce = true;
		}

		for (const FPropBatchEvaluationItem& Item : Batch)
		{
			float DistSq = 0.0f;
			FVector TargetLoc = FVector::ZeroVector;
			const bool bVisible = IsItemVisible(Item, Obs, DistSq, TargetLoc);
			ProcessItemForObserver(Obs, Item, bVisible, DistSq, TargetLoc);
		}

		Obs.TotalItemsProcessed += Batch.Num();

		bool bReachedCompletion = false;
		if (Obs.RegisteredLoopSize <= 0)
		{
			bReachedCompletion = true;
		}
		else
		{
			const bool bProcessedFullCount = (Obs.TotalItemsProcessed >= Obs.RegisteredLoopSize);
			const bool bReachedRegisterIndexAfterWrap = (Obs.bHasWrappedOnce && Metadata.BatchEndIndex >= Obs.RegisterIndex);

			if (bProcessedFullCount || bReachedRegisterIndexAfterWrap)
			{
				bReachedCompletion = true;
			}
		}

		// One-Time Observer Completion and Removal
		if (bReachedCompletion)
		{
			FVector FinalOrigin;
			FRotator FinalRot;
			ResolveSightTransform(Obs, FinalOrigin, FinalRot);

			// Section 9: Ensure the result array is sorted before sending final results
			SortObserverResults(Obs, FinalOrigin);

			const bool bFoundAny = Obs.DetectedItems.Num() > 0;
			FPropSightDetectedProps DetectedWrapper = BuildDetectedPropsWrapper(Obs.DetectedItems);
			FPropSightPerceptionResult ClosestProp = Obs.BestClosestProp;
			FOnPropSightQueryCompleted CompletionCallback = Obs.OnQueryCompleted;

			// Section 12.D: Remove that observer's entry immediately BEFORE results have been dispatched
			// This guarantees re-entrant calls cannot process or unregister this entry twice
			OneTimeObservers.RemoveAt(i);

			// Dispatch collected observation results to Actor associated with that observer
			CompletionCallback.ExecuteIfBound(bFoundAny, DetectedWrapper, ClosestProp);
		}
	}
}

// -----------------------------------------------------------------------------
// Section 3: Batch Evaluation Hook
// -----------------------------------------------------------------------------

void UInteractablePropSenseSightComponent::HandleBatchEvaluated(
	const TArray<FPropBatchEvaluationItem>& Batch,
	const FPropBatchMetadata& Metadata)
{
	// 1. Process One-Time Observers
	ProcessOneTimeObservers(Batch, Metadata);

	// 2. Process Continuous Observers
	ProcessContinuousObservers(Batch, Metadata);
}

// -----------------------------------------------------------------------------
// One-Shot Sight Query API
// -----------------------------------------------------------------------------

int64 UInteractablePropSenseSightComponent::RequestClosestProductInSight(
	const TArray<int32>& TargetProductIDs,
	float SightRadius,
	float PeripheralVisionHalfAngle,
	AActor* ObserverActor,
	const FOnPropSightQueryCompleted& OnCompleted,
	bool bIgnoreBlocked,
	FVector SightLocationOverride,
	FRotator SightRotationOverride,
	ECollisionChannel TraceChannel,
	bool bInDrawDebugCone,
	bool bInDrawDebugVisibleObjects,
	bool bInRequireGrabbable)
{
	FOneTimeObserverData NewQuery;
	NewQuery.QueryID = NextQueryID++;
	NewQuery.ObserverActor = ObserverActor;
	NewQuery.TargetProductIDs = TargetProductIDs;
	NewQuery.bFilterByPID = (TargetProductIDs.Num() > 0);
	NewQuery.SightRadius = FMath::Max(50.0f, SightRadius);
	NewQuery.PeripheralVisionHalfAngle = FMath::Clamp(PeripheralVisionHalfAngle, 1.0f, 180.0f);
	NewQuery.bIgnoreBlocked = bIgnoreBlocked;
	NewQuery.bRequireGrabbable = bInRequireGrabbable;
	NewQuery.SightLocationOverride = SightLocationOverride;
	NewQuery.SightRotationOverride = SightRotationOverride;
	NewQuery.bHasManualTransformOverride = false;
	NewQuery.TraceChannel = TraceChannel;
	NewQuery.bDrawDebugCone = bInDrawDebugCone;
	NewQuery.bDrawDebugVisibleObjects = bInDrawDebugVisibleObjects;
	NewQuery.OnQueryCompleted = OnCompleted;
	NewQuery.DebugColor = AllocateUniqueObserverColor();

	// Section 11.A & 12.E: Inherit ignored ISIDs from persistent ignore list or continuous observer
	if (ObserverActor)
	{
		if (const TSet<int32>* PersistSet = PersistentIgnoredISIDs.Find(ObserverActor))
		{
			NewQuery.IgnoredISIDs.Append(*PersistSet);
		}
		if (const FContinuousObserverData* ExistingObs = FindContinuousObserver(ObserverActor))
		{
			NewQuery.IgnoredISIDs.Append(ExistingObs->IgnoredISIDs);
		}
	}

	// Section 3: Record current looping index and total number of product instances being processed
	int32 CurrentIndex = 0;
	if (BatchUpdateComponentRef.IsValid())
	{
		CurrentIndex = BatchUpdateComponentRef->GetNextBatchIndex();
	}
	int32 TotalLoopSize = 0;
	APropManager* Mgr = PropManagerRef.Get();
	if (!Mgr)
	{
		Mgr = APropManager::Get(this);
		PropManagerRef = Mgr;
	}
	if (Mgr)
	{
		TotalLoopSize = Mgr->AllActiveISIDs.Num();
	}

	NewQuery.RegisterIndex = CurrentIndex;
	NewQuery.RegisteredLoopSize = TotalLoopSize;
	NewQuery.TotalItemsProcessed = 0;
	NewQuery.bHasWrappedOnce = false;

	// Section 1: Observer Registration Logging
	FString TargetPIDsStr;
	for (int32 PID : TargetProductIDs)
	{
		FString ProdName = TEXT("Unknown");
		if (Mgr)
		{
			if (const FPropProductDefinition* Def = Mgr->FindProductDefinition(PID))
			{
				ProdName = Def->DisplayName.ToString();
			}
		}
		TargetPIDsStr += FString::Printf(TEXT("%d [%s], "), PID, *ProdName);
	}

	UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] ========================================================"));
	UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] One-Time Observer Registered (QueryID: %lld):"), NewQuery.QueryID);
	UE_LOG(LogTemp, Warning, TEXT("  -> Observer Actor : %s"), ObserverActor ? *ObserverActor->GetName() : TEXT("None"));
	UE_LOG(LogTemp, Warning, TEXT("  -> Target PIDs (%d): [%s]"), TargetProductIDs.Num(), *TargetPIDsStr);
	UE_LOG(LogTemp, Warning, TEXT("  -> Register Index : %d, Registered Loop Size: %d"), NewQuery.RegisterIndex, NewQuery.RegisteredLoopSize);
	UE_LOG(LogTemp, Warning, TEXT("  -> Assigned Color : R=%d, G=%d, B=%d"),
		NewQuery.DebugColor.R, NewQuery.DebugColor.G, NewQuery.DebugColor.B);
	UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] ========================================================"));

	if (TotalLoopSize == 0)
	{
		FPropSightDetectedProps EmptyProps;
		FPropSightPerceptionResult EmptyClosest;
		OnCompleted.ExecuteIfBound(false, EmptyProps, EmptyClosest);
		return NewQuery.QueryID;
	}

	EvaluateAllPropsForOneTime(NewQuery);

	OneTimeObservers.Add(NewQuery);
	return NewQuery.QueryID;
}

bool UInteractablePropSenseSightComponent::CancelSightQuery(int64 QueryID)
{
	for (int32 i = OneTimeObservers.Num() - 1; i >= 0; --i)
	{
		if (OneTimeObservers[i].QueryID == QueryID)
		{
			OneTimeObservers.RemoveAt(i);
			return true;
		}
	}
	return false;
}

int64 UInteractablePropSenseSightComponent::RequestClosestProductInSightWithLimits(
	const TMap<int32, int32>& InPIDInstanceLimits,
	float SightRadius,
	float PeripheralVisionHalfAngle,
	AActor* ObserverActor,
	const FOnPropSightQueryCompleted& OnCompleted,
	bool bIgnoreBlocked,
	FVector SightLocationOverride,
	FRotator SightRotationOverride,
	ECollisionChannel TraceChannel,
	bool bInDrawDebugCone,
	bool bInDrawDebugVisibleObjects,
	bool bInRequireGrabbable)
{
	FOneTimeObserverData NewQuery;
	NewQuery.QueryID = NextQueryID++;
	NewQuery.ObserverActor = ObserverActor;
	NewQuery.PIDInstanceLimits = InPIDInstanceLimits;
	InPIDInstanceLimits.GetKeys(NewQuery.TargetProductIDs);
	NewQuery.bFilterByPID = (InPIDInstanceLimits.Num() > 0);

	NewQuery.SightRadius = FMath::Max(50.0f, SightRadius);
	NewQuery.PeripheralVisionHalfAngle = FMath::Clamp(PeripheralVisionHalfAngle, 1.0f, 180.0f);
	NewQuery.bIgnoreBlocked = bIgnoreBlocked;
	NewQuery.bRequireGrabbable = bInRequireGrabbable;
	NewQuery.SightLocationOverride = SightLocationOverride;
	NewQuery.SightRotationOverride = SightRotationOverride;
	NewQuery.bHasManualTransformOverride = (ObserverActor == nullptr && (!SightLocationOverride.IsZero() || !SightRotationOverride.IsZero()));
	NewQuery.TraceChannel = TraceChannel;
	NewQuery.bDrawDebugCone = bInDrawDebugCone;
	NewQuery.bDrawDebugVisibleObjects = bInDrawDebugVisibleObjects;
	NewQuery.OnQueryCompleted = OnCompleted;
	NewQuery.DebugColor = AllocateUniqueObserverColor();

	if (ObserverActor)
	{
		if (const TSet<int32>* PersistSet = PersistentIgnoredISIDs.Find(ObserverActor))
		{
			NewQuery.IgnoredISIDs.Append(*PersistSet);
		}
		if (const FContinuousObserverData* ExistingObs = FindContinuousObserver(ObserverActor))
		{
			NewQuery.IgnoredISIDs.Append(ExistingObs->IgnoredISIDs);
		}
	}

	int32 CurrentIndex = 0;
	if (BatchUpdateComponentRef.IsValid())
	{
		CurrentIndex = BatchUpdateComponentRef->GetNextBatchIndex();
	}
	int32 TotalLoopSize = 0;
	APropManager* Mgr = PropManagerRef.Get();
	if (!Mgr)
	{
		Mgr = APropManager::Get(this);
		PropManagerRef = Mgr;
	}
	if (Mgr)
	{
		TotalLoopSize = Mgr->AllActiveISIDs.Num();
	}

	NewQuery.RegisterIndex = CurrentIndex;
	NewQuery.RegisteredLoopSize = TotalLoopSize;
	NewQuery.TotalItemsProcessed = 0;
	NewQuery.bHasWrappedOnce = false;

	if (TotalLoopSize == 0)
	{
		FPropSightDetectedProps EmptyProps;
		FPropSightPerceptionResult EmptyClosest;
		OnCompleted.ExecuteIfBound(false, EmptyProps, EmptyClosest);
		return NewQuery.QueryID;
	}

	EvaluateAllPropsForOneTime(NewQuery);
	OneTimeObservers.Add(NewQuery);
	return NewQuery.QueryID;
}

// -----------------------------------------------------------------------------
// Continuous Observer Registration API
// -----------------------------------------------------------------------------

void UInteractablePropSenseSightComponent::RegisterObserverForContinuousSight(
	AActor* ObserverActor,
	const TArray<int32>& TargetProductIDs,
	float SightRadius,
	float PeripheralVisionHalfAngle,
	bool bIgnoreBlocked,
	FVector SightLocationOverride,
	FRotator SightRotationOverride,
	ECollisionChannel TraceChannel,
	bool bInDrawDebugCone,
	bool bInDrawDebugVisibleObjects,
	bool bInRequireGrabbable)
{
	if (!ObserverActor)
	{
		ObserverActor = GetOwner();
		UE_LOG(LogTemp, Error, TEXT("[PropSenseSight] RegisterObserverForContinuousSight: ObserverActor is NULL! Defaulting to Owner (%s)."),
			ObserverActor ? *ObserverActor->GetName() : TEXT("None"));
	}
	if (!ObserverActor) return;

	UnregisterObserver(ObserverActor);

	FContinuousObserverData NewObserver;
	NewObserver.ObserverActor = ObserverActor;
	NewObserver.TargetProductIDs = TargetProductIDs;
	NewObserver.bFilterByPID = (TargetProductIDs.Num() > 0);
	NewObserver.SightRadius = FMath::Max(50.0f, SightRadius);
	NewObserver.PeripheralVisionHalfAngle = FMath::Clamp(PeripheralVisionHalfAngle, 1.0f, 180.0f);
	NewObserver.bIgnoreBlocked = bIgnoreBlocked;
	NewObserver.bRequireGrabbable = bInRequireGrabbable;
	NewObserver.SightLocationOverride = SightLocationOverride;
	NewObserver.SightRotationOverride = SightRotationOverride;
	NewObserver.bHasManualTransformOverride = false;
	NewObserver.TraceChannel = TraceChannel;
	NewObserver.bDrawDebugCone = bInDrawDebugCone;
	NewObserver.bDrawDebugVisibleObjects = bInDrawDebugVisibleObjects;
	NewObserver.DebugColor = AllocateUniqueObserverColor();

	// Section 12.E: Restore any persistent ignore list configured for this observer
	if (const TSet<int32>* PersistSet = PersistentIgnoredISIDs.Find(ObserverActor))
	{
		NewObserver.IgnoredISIDs.Append(*PersistSet);
	}

	// Section 1: Observer Registration Logging
	FString TargetPIDsStr;
	APropManager* Mgr = PropManagerRef.Get();
	if (!Mgr)
	{
		Mgr = APropManager::Get(this);
		PropManagerRef = Mgr;
	}
	for (int32 PID : TargetProductIDs)
	{
		FString ProdName = TEXT("Unknown");
		if (Mgr)
		{
			if (const FPropProductDefinition* Def = Mgr->FindProductDefinition(PID))
			{
				ProdName = Def->DisplayName.ToString();
			}
		}
		TargetPIDsStr += FString::Printf(TEXT("%d [%s], "), PID, *ProdName);
	}

	UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] ========================================================"));
	UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] Observer Registered for Continuous Sight:"));
	UE_LOG(LogTemp, Warning, TEXT("  -> Observer Actor : %s (Class: %s)"), *ObserverActor->GetName(), *ObserverActor->GetClass()->GetName());
	UE_LOG(LogTemp, Warning, TEXT("  -> Target PIDs (%d): [%s]"), TargetProductIDs.Num(), *TargetPIDsStr);
	UE_LOG(LogTemp, Warning, TEXT("  -> Sight Radius   : %.0f cm, HalfAngle: %.1f deg, IgnoreBlocked: %s"),
		NewObserver.SightRadius, NewObserver.PeripheralVisionHalfAngle, NewObserver.bIgnoreBlocked ? TEXT("True") : TEXT("False"));
	UE_LOG(LogTemp, Warning, TEXT("  -> Assigned Color : R=%d, G=%d, B=%d"),
		NewObserver.DebugColor.R, NewObserver.DebugColor.G, NewObserver.DebugColor.B);
	UE_LOG(LogTemp, Warning, TEXT("  -> Debug Visuals  : Cone=%s, VisibleObjects=%s"),
		bInDrawDebugCone ? TEXT("True") : TEXT("False"), bInDrawDebugVisibleObjects ? TEXT("True") : TEXT("False"));

	if (TargetProductIDs.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[PropSenseSight] *** WARNING: TargetProductIDs is EMPTY []! ***"));
		UE_LOG(LogTemp, Error, TEXT("[PropSenseSight] An empty TargetProductIDs array causes this observer to perceive ALL products in front of them!"));
	}
	UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] ========================================================"));

	EvaluateAllPropsForContinuous(NewObserver);

	ContinuousObservers.Add(NewObserver);

	if (OnPropSightPerceptionUpdated.IsBound())
	{
		FPropSightDetectedProps DetectedWrapper = BuildDetectedPropsWrapper(NewObserver.DetectedItems);
		OnPropSightPerceptionUpdated.Broadcast(
			ObserverActor,
			DetectedWrapper.Props.Num() > 0,
			DetectedWrapper,
			NewObserver.BestClosestProp
		);
	}
}

bool UInteractablePropSenseSightComponent::RegisterObserverSightSettings(
	AActor* ObserverActor,
	float SightRadius,
	float PeripheralVisionHalfAngle,
	bool bIgnoreBlocked,
	bool bRequireGrabbable,
	ECollisionChannel TraceChannel,
	FVector SightLocationOverride,
	FRotator SightRotationOverride)
{
	if (!ObserverActor)
	{
		ObserverActor = GetOwner();
		UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] RegisterObserverSightSettings: ObserverActor is NULL, defaulting to Owner (%s)."),
			ObserverActor ? *ObserverActor->GetName() : TEXT("None"));
	}
	if (!ObserverActor)
	{
		return false;
	}

	if (FContinuousObserverData* Existing = FindContinuousObserverMutable(ObserverActor))
	{
		// Group 1: Update sight parameters without resetting PID requirements or debug options
		Existing->SightRadius = FMath::Max(50.0f, SightRadius);
		Existing->PeripheralVisionHalfAngle = FMath::Clamp(PeripheralVisionHalfAngle, 1.0f, 180.0f);
		Existing->bIgnoreBlocked = bIgnoreBlocked;
		Existing->bRequireGrabbable = bRequireGrabbable;
		Existing->TraceChannel = TraceChannel;
		Existing->SightLocationOverride = SightLocationOverride;
		Existing->SightRotationOverride = SightRotationOverride;
		Existing->bHasManualTransformOverride = (ObserverActor == nullptr && (!SightLocationOverride.IsZero() || !SightRotationOverride.IsZero()));

		EvaluateAllPropsForContinuous(*Existing);
		return true;
	}

	// Not yet registered: create new registration with preserved settings
	FContinuousObserverData NewObserver;
	NewObserver.ObserverActor = ObserverActor;
	NewObserver.SightRadius = FMath::Max(50.0f, SightRadius);
	NewObserver.PeripheralVisionHalfAngle = FMath::Clamp(PeripheralVisionHalfAngle, 1.0f, 180.0f);
	NewObserver.bIgnoreBlocked = bIgnoreBlocked;
	NewObserver.bRequireGrabbable = bRequireGrabbable;
	NewObserver.TraceChannel = TraceChannel;
	NewObserver.SightLocationOverride = SightLocationOverride;
	NewObserver.SightRotationOverride = SightRotationOverride;
	NewObserver.bHasManualTransformOverride = (ObserverActor == nullptr && (!SightLocationOverride.IsZero() || !SightRotationOverride.IsZero()));
	NewObserver.DebugColor = AllocateUniqueObserverColor();

	// Restore persistent ignored ISIDs
	if (const TSet<int32>* PersistSet = PersistentIgnoredISIDs.Find(ObserverActor))
	{
		NewObserver.IgnoredISIDs.Append(*PersistSet);
	}

	// Restore persistent PID limits if previously configured
	if (const TMap<int32, int32>* PersistLimits = PersistentPIDInstanceLimits.Find(ObserverActor))
	{
		NewObserver.PIDInstanceLimits = *PersistLimits;
		NewObserver.bFilterByPID = (PersistLimits->Num() > 0);
		PersistLimits->GetKeys(NewObserver.TargetProductIDs);
	}
	else if (const TArray<int32>* PersistTargets = PersistentTargetProductIDs.Find(ObserverActor))
	{
		NewObserver.TargetProductIDs = *PersistTargets;
		if (const bool* bFilter = PersistentFilterByPID.Find(ObserverActor))
		{
			NewObserver.bFilterByPID = *bFilter;
		}
		else
		{
			NewObserver.bFilterByPID = (PersistTargets->Num() > 0);
		}
	}

	EvaluateAllPropsForContinuous(NewObserver);
	ContinuousObservers.Add(NewObserver);

	UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] Observer Sight Settings Registered for '%s': Radius=%.0f, HalfAngle=%.1f, IgnoreBlocked=%s"),
		*ObserverActor->GetName(), NewObserver.SightRadius, NewObserver.PeripheralVisionHalfAngle,
		NewObserver.bIgnoreBlocked ? TEXT("True (Skips raycasts)") : TEXT("False (Raycasts enforced)"));

	return true;
}

void UInteractablePropSenseSightComponent::RegisterObserverWithPIDLimits(
	AActor* ObserverActor,
	const TMap<int32, int32>& InPIDInstanceLimits,
	float SightRadius,
	float PeripheralVisionHalfAngle,
	bool bIgnoreBlocked,
	FVector SightLocationOverride,
	FRotator SightRotationOverride,
	ECollisionChannel TraceChannel,
	bool bInDrawDebugCone,
	bool bInDrawDebugVisibleObjects,
	bool bInRequireGrabbable)
{
	if (!ObserverActor)
	{
		ObserverActor = GetOwner();
		UE_LOG(LogTemp, Error, TEXT("[PropSenseSight] RegisterObserverWithPIDLimits: ObserverActor is NULL! Defaulting to Owner (%s)."),
			ObserverActor ? *ObserverActor->GetName() : TEXT("None"));
	}
	if (!ObserverActor) return;

	UnregisterContinuousObserver(ObserverActor);

	FContinuousObserverData NewObserver;
	NewObserver.ObserverActor = ObserverActor;
	NewObserver.PIDInstanceLimits = InPIDInstanceLimits;
	InPIDInstanceLimits.GetKeys(NewObserver.TargetProductIDs);
	NewObserver.bFilterByPID = (InPIDInstanceLimits.Num() > 0);

	NewObserver.SightRadius = FMath::Max(50.0f, SightRadius);
	NewObserver.PeripheralVisionHalfAngle = FMath::Clamp(PeripheralVisionHalfAngle, 1.0f, 180.0f);
	NewObserver.bIgnoreBlocked = bIgnoreBlocked;
	NewObserver.bRequireGrabbable = bInRequireGrabbable;
	NewObserver.SightLocationOverride = SightLocationOverride;
	NewObserver.SightRotationOverride = SightRotationOverride;
	NewObserver.bHasManualTransformOverride = (ObserverActor == nullptr && (!SightLocationOverride.IsZero() || !SightRotationOverride.IsZero()));
	NewObserver.TraceChannel = TraceChannel;
	NewObserver.bDrawDebugCone = bInDrawDebugCone;
	NewObserver.bDrawDebugVisibleObjects = bInDrawDebugVisibleObjects;
	NewObserver.DebugColor = AllocateUniqueObserverColor();

	// Store in persistent tables
	PersistentPIDInstanceLimits.Add(ObserverActor, InPIDInstanceLimits);
	PersistentTargetProductIDs.Add(ObserverActor, NewObserver.TargetProductIDs);
	PersistentFilterByPID.Add(ObserverActor, NewObserver.bFilterByPID);

	// Restore persistent ignored ISIDs
	if (const TSet<int32>* PersistSet = PersistentIgnoredISIDs.Find(ObserverActor))
	{
		NewObserver.IgnoredISIDs.Append(*PersistSet);
	}

	EvaluateAllPropsForContinuous(NewObserver);
	ContinuousObservers.Add(NewObserver);

	FString LimitsStr;
	APropManager* Mgr = PropManagerRef.Get();
	for (const auto& Pair : InPIDInstanceLimits)
	{
		FString PName = TEXT("Unknown");
		if (Mgr)
		{
			if (const FPropProductDefinition* Def = Mgr->FindProductDefinition(Pair.Key))
			{
				PName = Def->DisplayName.ToString();
			}
		}
		LimitsStr += FString::Printf(TEXT("PID %d (%s) -> Limit %d, "), Pair.Key, *PName, Pair.Value);
	}

	UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] Observer Registered with PID Limits: %s | Limits: [%s]"),
		*ObserverActor->GetName(), *LimitsStr);

	if (OnPropSightPerceptionUpdated.IsBound())
	{
		FPropSightDetectedProps DetectedWrapper = BuildDetectedPropsWrapper(NewObserver.DetectedItems);
		OnPropSightPerceptionUpdated.Broadcast(
			ObserverActor,
			DetectedWrapper.Props.Num() > 0,
			DetectedWrapper,
			NewObserver.BestClosestProp
		);
	}
}

namespace
{
	bool DoesObserverMatch(AActor* RegisteredActor, AActor* QueryActor)
	{
		if (!RegisteredActor || !QueryActor) return false;
		if (RegisteredActor == QueryActor) return true;

		AActor* RegPawn = RegisteredActor;
		if (AController* C = Cast<AController>(RegisteredActor))
		{
			if (APawn* P = C->GetPawn())
			{
				RegPawn = P;
			}
		}

		AActor* QueryPawn = QueryActor;
		if (AController* C = Cast<AController>(QueryActor))
		{
			if (APawn* P = C->GetPawn())
			{
				QueryPawn = P;
			}
		}

		if (RegPawn == QueryActor || RegisteredActor == QueryPawn || RegPawn == QueryPawn)
		{
			return true;
		}

		if (APawn* RegP = Cast<APawn>(RegisteredActor))
		{
			if (RegP->GetController() == QueryActor || RegP->GetController() == QueryPawn)
			{
				return true;
			}
		}

		if (APawn* QP = Cast<APawn>(QueryActor))
		{
			if (QP->GetController() == RegisteredActor || QP->GetController() == RegPawn)
			{
				return true;
			}
		}

		return false;
	}
}

// -----------------------------------------------------------------------------
// Section 12: Observer Unregistration Functions
// -----------------------------------------------------------------------------

bool UInteractablePropSenseSightComponent::UnregisterOneTimeObserver(AActor* ObserverActor)
{
	if (!ObserverActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] UnregisterOneTimeObserver: Called with NULL ObserverActor!"));
		return false;
	}

	bool bRemovedAny = false;
	for (int32 i = OneTimeObservers.Num() - 1; i >= 0; --i)
	{
		AActor* Act = OneTimeObservers[i].ObserverActor.Get();
		if (DoesObserverMatch(Act, ObserverActor))
		{
			const FColor ObsColor = OneTimeObservers[i].DebugColor;
			const int64 QID = OneTimeObservers[i].QueryID;

			UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] ========================================================"));
			UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] One-Time Observer Unregistered (QueryID: %lld):"), QID);
			UE_LOG(LogTemp, Warning, TEXT("  -> Observer Actor : %s"), *ObserverActor->GetName());
			UE_LOG(LogTemp, Warning, TEXT("  -> Assigned Color : R=%d, G=%d, B=%d"), ObsColor.R, ObsColor.G, ObsColor.B);
			UE_LOG(LogTemp, Warning, TEXT("  -> Pending query cancelled; callback unbound"));
			UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] ========================================================"));

			// Prevent any pending callbacks from being dispatched
			OneTimeObservers[i].OnQueryCompleted.Unbind();
			OneTimeObservers[i].DetectedItems.Empty();
			OneTimeObservers[i].ISIDToIndexMap.Empty();
			OneTimeObservers[i].PerPIDDetectedItems.Empty();

			OneTimeObservers.RemoveAt(i);
			bRemovedAny = true;
		}
	}

	return bRemovedAny;
}

bool UInteractablePropSenseSightComponent::UnregisterContinuousObserver(AActor* ObserverActor)
{
	if (!ObserverActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] UnregisterContinuousObserver: Called with NULL ObserverActor!"));
		return false;
	}

	bool bRemovedAny = false;
	for (int32 i = ContinuousObservers.Num() - 1; i >= 0; --i)
	{
		AActor* Act = ContinuousObservers[i].ObserverActor.Get();
		if (DoesObserverMatch(Act, ObserverActor))
		{
			const FColor ObsColor = ContinuousObservers[i].DebugColor;
			const int32 NumItems = ContinuousObservers[i].DetectedItems.Num();

			UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] ========================================================"));
			UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] Continuous Observer Unregistered: %s"), *ObserverActor->GetName());
			UE_LOG(LogTemp, Warning, TEXT("  -> Assigned Color : R=%d, G=%d, B=%d"), ObsColor.R, ObsColor.G, ObsColor.B);
			UE_LOG(LogTemp, Warning, TEXT("  -> Cleared %d detected items from observer results"), NumItems);
			UE_LOG(LogTemp, Warning, TEXT("  -> Remaining Continuous Observers: %d"), ContinuousObservers.Num() - 1);
			UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] ========================================================"));

			// Clear observation results maintained by that registration
			ContinuousObservers[i].DetectedItems.Empty();
			ContinuousObservers[i].ISIDToIndexMap.Empty();
			ContinuousObservers[i].PerPIDDetectedItems.Empty();

			ContinuousObservers.RemoveAt(i);
			bRemovedAny = true;
		}
	}

	return bRemovedAny;
}

bool UInteractablePropSenseSightComponent::UnregisterObserver(AActor* ObserverActor)
{
	if (!ObserverActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PropSenseSight] UnregisterObserver: Called with NULL ObserverActor!"));
		return false;
	}

	const bool bRemovedContinuous = UnregisterContinuousObserver(ObserverActor);
	const bool bRemovedOneTime = UnregisterOneTimeObserver(ObserverActor);

	return (bRemovedContinuous || bRemovedOneTime);
}

const FContinuousObserverData* UInteractablePropSenseSightComponent::FindContinuousObserver(AActor* ObserverActor) const
{
	if (!ObserverActor) return nullptr;

	for (const FContinuousObserverData& Obs : ContinuousObservers)
	{
		if (DoesObserverMatch(Obs.ObserverActor.Get(), ObserverActor))
		{
			return &Obs;
		}
	}
	return nullptr;
}

FContinuousObserverData* UInteractablePropSenseSightComponent::FindContinuousObserverMutable(AActor* ObserverActor)
{
	if (!ObserverActor) return nullptr;

	for (FContinuousObserverData& Obs : ContinuousObservers)
	{
		if (DoesObserverMatch(Obs.ObserverActor.Get(), ObserverActor))
		{
			return &Obs;
		}
	}
	return nullptr;
}

void UInteractablePropSenseSightComponent::UpdateObserverSightTransform(AActor* ObserverActor, FVector NewSightLocation, FRotator NewSightRotation)
{
	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		Found->SightLocationOverride = NewSightLocation;
		Found->SightRotationOverride = NewSightRotation;
		Found->bHasManualTransformOverride = true;
		EvaluateAllPropsForContinuous(*Found);
	}
}

void UInteractablePropSenseSightComponent::ClearObserverSightOverrides(AActor* ObserverActor)
{
	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		Found->SightLocationOverride = FVector::ZeroVector;
		Found->SightRotationOverride = FRotator::ZeroRotator;
		Found->bHasManualTransformOverride = false;
		EvaluateAllPropsForContinuous(*Found);
	}
}

void UInteractablePropSenseSightComponent::SetObserverRequireGrabbable(AActor* ObserverActor, bool bInRequireGrabbable)
{
	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		Found->bRequireGrabbable = bInRequireGrabbable;
		EvaluateAllPropsForContinuous(*Found);
	}
}

void UInteractablePropSenseSightComponent::ForceEvaluateObserver(AActor* ObserverActor)
{
	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		EvaluateAllPropsForContinuous(*Found);

		if (OnPropSightPerceptionUpdated.IsBound())
		{
			AActor* BroadcastActor = ObserverActor;
			if (!BroadcastActor)
			{
				BroadcastActor = Found->ObserverActor.Get();
			}
			FPropSightDetectedProps DetectedWrapper = BuildDetectedPropsWrapper(Found->DetectedItems);
			OnPropSightPerceptionUpdated.Broadcast(
				BroadcastActor,
				DetectedWrapper.Props.Num() > 0,
				DetectedWrapper,
				Found->BestClosestProp
			);
		}
	}
}

bool UInteractablePropSenseSightComponent::GetVisiblePropsForObserver(AActor* ObserverActor, TMap<int32, FVector>& OutVisibleProps) const
{
	OutVisibleProps.Empty();
	if (const FContinuousObserverData* Found = FindContinuousObserver(ObserverActor))
	{
		for (const FPropSightDetectedItem& Item : Found->DetectedItems)
		{
			OutVisibleProps.Add(Item.ISID, Item.Location);
		}
		return OutVisibleProps.Num() > 0;
	}
	return false;
}

bool UInteractablePropSenseSightComponent::GetVisibleProductIDsForObserver(AActor* ObserverActor, TArray<int32>& OutProductIDs) const
{
	OutProductIDs.Empty();
	if (const FContinuousObserverData* Found = FindContinuousObserver(ObserverActor))
	{
		TSet<int32> UniquePIDs;
		for (const FPropSightDetectedItem& Item : Found->DetectedItems)
		{
			if (Item.ProductID > 0)
			{
				UniquePIDs.Add(Item.ProductID);
			}
		}
		OutProductIDs = UniquePIDs.Array();
		OutProductIDs.Sort();
		return OutProductIDs.Num() > 0;
	}
	return false;
}

bool UInteractablePropSenseSightComponent::GetDetectedPropsListForObserver(AActor* ObserverActor, TArray<FPropSightPerceptionResult>& OutProps) const
{
	OutProps.Empty();
	if (const FContinuousObserverData* Found = FindContinuousObserver(ObserverActor))
	{
		OutProps.Reserve(Found->DetectedItems.Num());
		// Items are already sorted by ascending distance (closest at index 0, farthest at last index)
		for (const FPropSightDetectedItem& Item : Found->DetectedItems)
		{
			FPropSightPerceptionResult R;
			R.ISID = Item.ISID;
			R.ProductID = Item.ProductID;
			R.WorldLocation = Item.Location;
			R.Distance = Item.Distance;
			R.Representation = Item.Representation;
			R.State = Item.State;
			R.ActiveActor = Item.ActiveActor;
			OutProps.Add(R);
		}
		return OutProps.Num() > 0;
	}
	return false;
}

bool UInteractablePropSenseSightComponent::GetClosestVisiblePropForObserver(AActor* ObserverActor, FPropSightPerceptionResult& OutClosestProp) const
{
	if (const FContinuousObserverData* Found = FindContinuousObserver(ObserverActor))
	{
		if (Found->DetectedItems.Num() > 0)
		{
			// Index 0 is guaranteed to always be the closest observed instance
			const FPropSightDetectedItem& Closest = Found->DetectedItems[0];
			OutClosestProp.ISID = Closest.ISID;
			OutClosestProp.ProductID = Closest.ProductID;
			OutClosestProp.WorldLocation = Closest.Location;
			OutClosestProp.Distance = Closest.Distance;
			OutClosestProp.Representation = Closest.Representation;
			OutClosestProp.State = Closest.State;
			OutClosestProp.ActiveActor = Closest.ActiveActor;
			return true;
		}
	}
	return false;
}

// -----------------------------------------------------------------------------
// Section 10: Find Closest Detected Product by PID
// -----------------------------------------------------------------------------

bool UInteractablePropSenseSightComponent::FindClosestDetectedProductByPID(
	int32 PID,
	const TArray<FPropSightPerceptionResult>& DetectedPropsList,
	int32& OutClosestISID,
	FVector& OutClosestLocation,
	AActor* ObserverActor) const
{
	OutClosestISID = 0;
	OutClosestLocation = FVector::ZeroVector;

	if (PID <= 0 || DetectedPropsList.Num() == 0)
	{
		return false;
	}

	// 1. Resolve observer's current location as the distance calculation origin and check filters
	FVector OriginLocation = FVector::ZeroVector;
	bool bHasOrigin = false;
	TSet<int32> IgnoredISIDs;

	if (ObserverActor)
	{
		if (const FContinuousObserverData* Obs = FindContinuousObserver(ObserverActor))
		{
			// Section 11.B: If observer filters by PID and PID is not in allowlist, return false
			if (Obs->bFilterByPID && !Obs->TargetProductIDs.Contains(PID))
			{
				return false;
			}
			IgnoredISIDs = Obs->IgnoredISIDs;

			FRotator EyesRot;
			ResolveSightTransform(*Obs, OriginLocation, EyesRot);
			bHasOrigin = true;
		}
		else
		{
			if (const TSet<int32>* PersistSet = PersistentIgnoredISIDs.Find(ObserverActor))
			{
				IgnoredISIDs.Append(*PersistSet);
			}

			for (const FOneTimeObserverData& OneTime : OneTimeObservers)
			{
				if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
				{
					if (OneTime.bFilterByPID && !OneTime.TargetProductIDs.Contains(PID))
					{
						return false;
					}
					IgnoredISIDs.Append(OneTime.IgnoredISIDs);
					break;
				}
			}

			if (APawn* Pawn = Cast<APawn>(ObserverActor))
			{
				OriginLocation = Pawn->GetPawnViewLocation();
				bHasOrigin = true;
			}
			else
			{
				OriginLocation = ObserverActor->GetActorLocation();
				bHasOrigin = true;
			}
		}
	}
	else if (GetOwner())
	{
		OriginLocation = GetOwner()->GetActorLocation();
		bHasOrigin = !OriginLocation.IsZero();
	}

	APropManager* Mgr = PropManagerRef.Get();
	if (!Mgr)
	{
		Mgr = APropManager::Get(this);
	}

	// 2. Scan detected props list:
	// Works whether the array is sorted or unsorted.
	// Uses squared distance for comparisons to avoid unnecessary square-root calculations.
	float BestDistSq = MAX_FLT;
	int32 BestISID = 0;
	FVector BestLocation = FVector::ZeroVector;

	for (const FPropSightPerceptionResult& Item : DetectedPropsList)
	{
		if (Item.ISID <= 0)
		{
			continue;
		}

		// Section 11.A: Ignored instances must not be returned by FindClosestDetectedProductByPID
		if (IgnoredISIDs.Contains(Item.ISID))
		{
			continue;
		}

		// 3. Compare ISID against instances belonging to specified PID
		bool bMatchesPID = (Item.ProductID == PID);
		if (!bMatchesPID && Mgr)
		{
			if (const FISIDRuntimeIndex* Rec = Mgr->FindISIDData(Item.ISID))
			{
				bMatchesPID = (Rec->ProductID == PID);
			}
		}

		if (!bMatchesPID)
		{
			continue;
		}

		// 4. Calculate distance from observer's location to instance
		float DistSq = 0.0f;
		if (bHasOrigin)
		{
			DistSq = FVector::DistSquared(OriginLocation, Item.WorldLocation);
		}
		else if (Item.Distance > 0.0f)
		{
			DistSq = FMath::Square(Item.Distance);
		}

		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestISID = Item.ISID;
			BestLocation = Item.WorldLocation;
		}
	}

	// 5. Return closest matching instance
	if (BestISID > 0)
	{
		OutClosestISID = BestISID;
		OutClosestLocation = BestLocation;
		return true;
	}

	return false;
}

bool UInteractablePropSenseSightComponent::FindClosestDetectedProductInPropsByPID(
	int32 PID,
	const FPropSightDetectedProps& DetectedProps,
	int32& OutClosestISID,
	FVector& OutClosestLocation,
	AActor* ObserverActor) const
{
	return FindClosestDetectedProductByPID(PID, DetectedProps.DetectedPropsList, OutClosestISID, OutClosestLocation, ObserverActor);
}

bool UInteractablePropSenseSightComponent::FindClosestDetectedProductForObserver(
	int32 PID,
	AActor* ObserverActor,
	int32& OutClosestISID,
	FVector& OutClosestLocation) const
{
	OutClosestISID = 0;
	OutClosestLocation = FVector::ZeroVector;

	if (!ObserverActor || PID <= 0)
	{
		return false;
	}

	if (const FContinuousObserverData* Obs = FindContinuousObserver(ObserverActor))
	{
		// Section 11.B: If observer filters by PID and this PID is NOT allowed, return false immediately
		if (Obs->bFilterByPID && !Obs->TargetProductIDs.Contains(PID))
		{
			return false;
		}

		APropManager* Mgr = PropManagerRef.Get();
		if (!Mgr)
		{
			Mgr = APropManager::Get(this);
		}

		FVector CurrentOrigin;
		FRotator CurrentRot;
		ResolveSightTransform(*Obs, CurrentOrigin, CurrentRot);

		float BestDistSq = MAX_FLT;
		int32 BestISID = 0;
		FVector BestLocation = FVector::ZeroVector;

		for (const FPropSightDetectedItem& Item : Obs->DetectedItems)
		{
			// Section 11.A: Ignored instances must not be returned
			if (Obs->IgnoredISIDs.Contains(Item.ISID))
			{
				continue;
			}

			bool bMatchesPID = (Item.ProductID == PID);
			if (!bMatchesPID && Mgr)
			{
				if (const FISIDRuntimeIndex* Rec = Mgr->FindISIDData(Item.ISID))
				{
					bMatchesPID = (Rec->ProductID == PID);
				}
			}

			if (bMatchesPID)
			{
				const float DistSq = FVector::DistSquared(CurrentOrigin, Item.Location);
				if (DistSq <= FMath::Square(Obs->SightRadius) && DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					BestISID = Item.ISID;
					BestLocation = Item.Location;
				}
			}
		}

		if (BestISID > 0)
		{
			OutClosestISID = BestISID;
			OutClosestLocation = BestLocation;
			return true;
		}
	}

	for (const FOneTimeObserverData& OneTime : OneTimeObservers)
	{
		if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
		{
			if (OneTime.bFilterByPID && !OneTime.TargetProductIDs.Contains(PID))
			{
				return false;
			}

			APropManager* Mgr = PropManagerRef.Get();
			if (!Mgr)
			{
				Mgr = APropManager::Get(this);
			}

			FVector CurrentOrigin;
			FRotator CurrentRot;
			ResolveSightTransform(OneTime, CurrentOrigin, CurrentRot);

			float BestDistSq = MAX_FLT;
			int32 BestISID = 0;
			FVector BestLocation = FVector::ZeroVector;

			for (const FPropSightDetectedItem& Item : OneTime.DetectedItems)
			{
				if (OneTime.IgnoredISIDs.Contains(Item.ISID))
				{
					continue;
				}

				bool bMatchesPID = (Item.ProductID == PID);
				if (!bMatchesPID && Mgr)
				{
					if (const FISIDRuntimeIndex* Rec = Mgr->FindISIDData(Item.ISID))
					{
						bMatchesPID = (Rec->ProductID == PID);
					}
				}

				if (bMatchesPID)
				{
					const float DistSq = FVector::DistSquared(CurrentOrigin, Item.Location);
					if (DistSq <= FMath::Square(OneTime.SightRadius) && DistSq < BestDistSq)
					{
						BestDistSq = DistSq;
						BestISID = Item.ISID;
						BestLocation = Item.Location;
					}
				}
			}

			if (BestISID > 0)
			{
				OutClosestISID = BestISID;
				OutClosestLocation = BestLocation;
				return true;
			}
			break;
		}
	}

	return false;
}

// -----------------------------------------------------------------------------
// Section 11.A: ISID Ignore List Management
// -----------------------------------------------------------------------------

bool UInteractablePropSenseSightComponent::SetISIDToIgnore(AActor* ObserverActor, int32 InISID)
{
	if (!ObserverActor || InISID <= 0)
	{
		return false;
	}

	// Section 12.E: Record in persistent ignore table (survives unregistration/re-registration)
	PersistentIgnoredISIDs.FindOrAdd(ObserverActor).Add(InISID);

	bool bFoundAny = false;

	// 1. Continuous observers
	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		bFoundAny = true;
		Found->IgnoredISIDs.Add(InISID);

		// If an ISID is ignored after it has already been detected, remove it from results
		if (Found->ISIDToIndexMap.Contains(InISID))
		{
			for (auto& Pair : Found->PerPIDDetectedItems)
			{
				for (int32 Idx = Pair.Value.Num() - 1; Idx >= 0; --Idx)
				{
					if (Pair.Value[Idx].ISID == InISID)
					{
						Pair.Value.RemoveAt(Idx);
						break;
					}
				}
			}
			RemoveDetectedItem(Found->DetectedItems, Found->ISIDToIndexMap, InISID);

			FVector CurrentOrigin;
			FRotator CurrentRot;
			ResolveSightTransform(*Found, CurrentOrigin, CurrentRot);
			SortObserverResults(*Found, CurrentOrigin);
		}
	}

	// 2. Pending One-Time observers matching this observer
	for (FOneTimeObserverData& OneTime : OneTimeObservers)
	{
		if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
		{
			bFoundAny = true;
			OneTime.IgnoredISIDs.Add(InISID);

			if (OneTime.ISIDToIndexMap.Contains(InISID))
			{
				for (auto& Pair : OneTime.PerPIDDetectedItems)
				{
					for (int32 Idx = Pair.Value.Num() - 1; Idx >= 0; --Idx)
					{
						if (Pair.Value[Idx].ISID == InISID)
						{
							Pair.Value.RemoveAt(Idx);
							break;
						}
					}
				}
				RemoveDetectedItem(OneTime.DetectedItems, OneTime.ISIDToIndexMap, InISID);

				FVector CurrentOrigin;
				FRotator CurrentRot;
				ResolveSightTransform(OneTime, CurrentOrigin, CurrentRot);
				SortObserverResults(OneTime, CurrentOrigin);
			}
		}
	}

	return true;
}

bool UInteractablePropSenseSightComponent::UnsetISIDToIgnore(AActor* ObserverActor, int32 InISID)
{
	if (!ObserverActor || InISID <= 0)
	{
		return false;
	}

	bool bRemoved = false;
	if (TSet<int32>* PersistSet = PersistentIgnoredISIDs.Find(ObserverActor))
	{
		if (PersistSet->Remove(InISID) > 0)
		{
			bRemoved = true;
		}
	}

	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		if (Found->IgnoredISIDs.Remove(InISID) > 0)
		{
			bRemoved = true;
		}
	}

	for (FOneTimeObserverData& OneTime : OneTimeObservers)
	{
		if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
		{
			if (OneTime.IgnoredISIDs.Remove(InISID) > 0)
			{
				bRemoved = true;
			}
		}
	}

	return bRemoved;
}

bool UInteractablePropSenseSightComponent::RemoveAllISIDsToIgnore(AActor* ObserverActor)
{
	if (!ObserverActor)
	{
		return false;
	}

	bool bFoundAny = (PersistentIgnoredISIDs.Remove(ObserverActor) > 0);

	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		bFoundAny = true;
		Found->IgnoredISIDs.Empty();
	}

	for (FOneTimeObserverData& OneTime : OneTimeObservers)
	{
		if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
		{
			bFoundAny = true;
			OneTime.IgnoredISIDs.Empty();
		}
	}

	return bFoundAny;
}

void UInteractablePropSenseSightComponent::IgnoreISIDForObserver(AActor* ObserverActor, int32 InISID)
{
	SetISIDToIgnore(ObserverActor, InISID);
}

void UInteractablePropSenseSightComponent::IgnoreISIDsForObserver(AActor* ObserverActor, const TArray<int32>& InISIDs)
{
	if (!ObserverActor || InISIDs.Num() == 0) return;
	for (int32 ID : InISIDs)
	{
		SetISIDToIgnore(ObserverActor, ID);
	}
}

void UInteractablePropSenseSightComponent::UnignoreISIDForObserver(AActor* ObserverActor, int32 InISID)
{
	UnsetISIDToIgnore(ObserverActor, InISID);
}

void UInteractablePropSenseSightComponent::ClearIgnoredISIDsForObserver(AActor* ObserverActor)
{
	RemoveAllISIDsToIgnore(ObserverActor);
}

bool UInteractablePropSenseSightComponent::IsISIDIgnoredForObserver(AActor* ObserverActor, int32 InISID) const
{
	if (!ObserverActor || InISID <= 0) return false;

	if (const TSet<int32>* PersistSet = PersistentIgnoredISIDs.Find(ObserverActor))
	{
		if (PersistSet->Contains(InISID))
		{
			return true;
		}
	}

	if (const FContinuousObserverData* Found = FindContinuousObserver(ObserverActor))
	{
		return Found->IgnoredISIDs.Contains(InISID);
	}

	for (const FOneTimeObserverData& OneTime : OneTimeObservers)
	{
		if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
		{
			return OneTime.IgnoredISIDs.Contains(InISID);
		}
	}

	return false;
}

// -----------------------------------------------------------------------------
// Section 11.B: PID Observation Management
// -----------------------------------------------------------------------------

bool UInteractablePropSenseSightComponent::AddPIDToObservation(AActor* ObserverActor, int32 InPID)
{
	if (!ObserverActor || InPID <= 0)
	{
		return false;
	}

	PersistentTargetProductIDs.FindOrAdd(ObserverActor).AddUnique(InPID);
	PersistentFilterByPID.Add(ObserverActor, true);

	bool bFoundAny = false;

	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		bFoundAny = true;
		Found->bFilterByPID = true;
		Found->TargetProductIDs.AddUnique(InPID);
	}

	for (FOneTimeObserverData& OneTime : OneTimeObservers)
	{
		if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
		{
			bFoundAny = true;
			OneTime.bFilterByPID = true;
			OneTime.TargetProductIDs.AddUnique(InPID);
		}
	}

	return true;
}

bool UInteractablePropSenseSightComponent::RemovePIDFromObservation(AActor* ObserverActor, int32 InPID)
{
	if (!ObserverActor || InPID <= 0)
	{
		return false;
	}

	bool bFoundAny = false;
	if (TArray<int32>* PersistPIDs = PersistentTargetProductIDs.Find(ObserverActor))
	{
		if (PersistPIDs->Remove(InPID) > 0)
		{
			bFoundAny = true;
		}
	}
	if (TMap<int32, int32>* PersistLimits = PersistentPIDInstanceLimits.Find(ObserverActor))
	{
		if (PersistLimits->Remove(InPID) > 0)
		{
			bFoundAny = true;
		}
	}

	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		bFoundAny = true;
		Found->TargetProductIDs.Remove(InPID);
		Found->PIDInstanceLimits.Remove(InPID);
		Found->PerPIDDetectedItems.Remove(InPID);

		// Immediately purge any already detected results matching this PID
		bool bPurgedAny = false;
		for (int32 i = Found->DetectedItems.Num() - 1; i >= 0; --i)
		{
			if (Found->DetectedItems[i].ProductID == InPID)
			{
				RemoveDetectedItem(Found->DetectedItems, Found->ISIDToIndexMap, Found->DetectedItems[i].ISID);
				bPurgedAny = true;
			}
		}

		if (bPurgedAny)
		{
			FVector CurrentOrigin;
			FRotator CurrentRot;
			ResolveSightTransform(*Found, CurrentOrigin, CurrentRot);
			SortObserverResults(*Found, CurrentOrigin);
		}
	}

	for (FOneTimeObserverData& OneTime : OneTimeObservers)
	{
		if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
		{
			bFoundAny = true;
			OneTime.TargetProductIDs.Remove(InPID);
			OneTime.PIDInstanceLimits.Remove(InPID);
			OneTime.PerPIDDetectedItems.Remove(InPID);

			bool bPurgedAny = false;
			for (int32 i = OneTime.DetectedItems.Num() - 1; i >= 0; --i)
			{
				if (OneTime.DetectedItems[i].ProductID == InPID)
				{
					RemoveDetectedItem(OneTime.DetectedItems, OneTime.ISIDToIndexMap, OneTime.DetectedItems[i].ISID);
					bPurgedAny = true;
				}
			}

			if (bPurgedAny)
			{
				FVector CurrentOrigin;
				FRotator CurrentRot;
				ResolveSightTransform(OneTime, CurrentOrigin, CurrentRot);
				SortObserverResults(OneTime, CurrentOrigin);
			}
		}
	}

	return bFoundAny;
}

bool UInteractablePropSenseSightComponent::RemoveAllPIDsFromObservation(AActor* ObserverActor)
{
	if (!ObserverActor)
	{
		return false;
	}

	bool bFoundAny = false;
	if (TArray<int32>* PersistPIDs = PersistentTargetProductIDs.Find(ObserverActor))
	{
		PersistPIDs->Empty();
		bFoundAny = true;
	}
	if (TMap<int32, int32>* PersistLimits = PersistentPIDInstanceLimits.Find(ObserverActor))
	{
		PersistLimits->Empty();
		bFoundAny = true;
	}

	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		bFoundAny = true;
		Found->TargetProductIDs.Empty();
		Found->PIDInstanceLimits.Empty();
		Found->PerPIDDetectedItems.Empty();
		// If bFilterByPID is true, empty allowlist means observer perceives 0 products!
		if (Found->bFilterByPID)
		{
			Found->DetectedItems.Empty();
			Found->ISIDToIndexMap.Empty();
		}
	}

	for (FOneTimeObserverData& OneTime : OneTimeObservers)
	{
		if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
		{
			bFoundAny = true;
			OneTime.TargetProductIDs.Empty();
			OneTime.PIDInstanceLimits.Empty();
			OneTime.PerPIDDetectedItems.Empty();
			if (OneTime.bFilterByPID)
			{
				OneTime.DetectedItems.Empty();
				OneTime.ISIDToIndexMap.Empty();
			}
		}
	}

	return bFoundAny;
}

bool UInteractablePropSenseSightComponent::SetObserveAllPIDs(AActor* ObserverActor, bool bObserveAll)
{
	if (!ObserverActor)
	{
		return false;
	}

	PersistentFilterByPID.Add(ObserverActor, !bObserveAll);
	if (bObserveAll)
	{
		if (TArray<int32>* PersistPIDs = PersistentTargetProductIDs.Find(ObserverActor))
		{
			PersistPIDs->Empty();
		}
	}

	bool bFoundAny = false;

	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		bFoundAny = true;
		Found->bFilterByPID = !bObserveAll;
		if (bObserveAll)
		{
			Found->TargetProductIDs.Empty();
		}
		else if (Found->TargetProductIDs.IsEmpty())
		{
			Found->DetectedItems.Empty();
			Found->ISIDToIndexMap.Empty();
		}
	}

	for (FOneTimeObserverData& OneTime : OneTimeObservers)
	{
		if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
		{
			bFoundAny = true;
			OneTime.bFilterByPID = !bObserveAll;
			if (bObserveAll)
			{
				OneTime.TargetProductIDs.Empty();
			}
			else if (OneTime.TargetProductIDs.IsEmpty())
			{
				OneTime.DetectedItems.Empty();
				OneTime.ISIDToIndexMap.Empty();
			}
		}
	}

	return true;
}

void UInteractablePropSenseSightComponent::SetTargetProductIDsForObserver(AActor* ObserverActor, const TArray<int32>& NewProductIDs)
{
	if (!ObserverActor) return;

	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		Found->TargetProductIDs = NewProductIDs;
		Found->bFilterByPID = (NewProductIDs.Num() > 0);

		if (Found->bFilterByPID)
		{
			for (int32 i = Found->DetectedItems.Num() - 1; i >= 0; --i)
			{
				if (!NewProductIDs.Contains(Found->DetectedItems[i].ProductID))
				{
					RemoveDetectedItem(Found->DetectedItems, Found->ISIDToIndexMap, Found->DetectedItems[i].ISID);
				}
			}

			FVector CurrentOrigin;
			FRotator CurrentRot;
			ResolveSightTransform(*Found, CurrentOrigin, CurrentRot);
			SortObserverResults(*Found, CurrentOrigin);
		}
	}
}

void UInteractablePropSenseSightComponent::AddTargetProductIDForObserver(AActor* ObserverActor, int32 InProductID)
{
	AddPIDToObservation(ObserverActor, InProductID);
}

void UInteractablePropSenseSightComponent::RemoveTargetProductIDForObserver(AActor* ObserverActor, int32 InProductID)
{
	RemovePIDFromObservation(ObserverActor, InProductID);
}

bool UInteractablePropSenseSightComponent::GetTargetProductIDsForObserver(AActor* ObserverActor, TArray<int32>& OutProductIDs) const
{
	OutProductIDs.Empty();
	if (const FContinuousObserverData* Found = FindContinuousObserver(ObserverActor))
	{
		OutProductIDs = Found->TargetProductIDs;
		return true;
	}
	return false;
}

// -----------------------------------------------------------------------------
// Group 2: Observer PID Observation Management (Section 4 Group 2)
// -----------------------------------------------------------------------------

bool UInteractablePropSenseSightComponent::ManageObserverPIDObservation(
	AActor* ObserverActor,
	EObserverPIDOperation Operation,
	int32 PID,
	int32 InstanceLimit)
{
	if (!ObserverActor || PID <= 0)
	{
		return false;
	}

	switch (Operation)
	{
	case EObserverPIDOperation::RegisterPID:
	{
		const int32 ValidLimit = FMath::Max(1, InstanceLimit);
		PersistentPIDInstanceLimits.FindOrAdd(ObserverActor).Add(PID, ValidLimit);
		PersistentTargetProductIDs.FindOrAdd(ObserverActor).AddUnique(PID);
		PersistentFilterByPID.Add(ObserverActor, true);

		if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
		{
			Found->bFilterByPID = true;
			Found->TargetProductIDs.AddUnique(PID);
			Found->PIDInstanceLimits.Add(PID, ValidLimit);

			EvaluatePropsForPID(*Found, PID);
			SyncDetectedItemsFromPerPID(*Found);
		}

		for (FOneTimeObserverData& OneTime : OneTimeObservers)
		{
			if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
			{
				OneTime.bFilterByPID = true;
				OneTime.TargetProductIDs.AddUnique(PID);
				OneTime.PIDInstanceLimits.Add(PID, ValidLimit);
				EvaluatePropsForPID(OneTime, PID);
				SyncDetectedItemsFromPerPID(OneTime);
			}
		}
		return true;
	}

	case EObserverPIDOperation::UnregisterPID:
	{
		if (TMap<int32, int32>* PersistLimits = PersistentPIDInstanceLimits.Find(ObserverActor))
		{
			PersistLimits->Remove(PID);
		}
		if (TArray<int32>* PersistTargets = PersistentTargetProductIDs.Find(ObserverActor))
		{
			PersistTargets->Remove(PID);
		}

		bool bFoundAny = false;
		if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
		{
			bFoundAny = true;
			Found->PIDInstanceLimits.Remove(PID);
			Found->TargetProductIDs.Remove(PID);
			Found->PerPIDDetectedItems.Remove(PID);
			SyncDetectedItemsFromPerPID(*Found);
		}

		for (FOneTimeObserverData& OneTime : OneTimeObservers)
		{
			if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
			{
				bFoundAny = true;
				OneTime.PIDInstanceLimits.Remove(PID);
				OneTime.TargetProductIDs.Remove(PID);
				OneTime.PerPIDDetectedItems.Remove(PID);
				SyncDetectedItemsFromPerPID(OneTime);
			}
		}
		return bFoundAny;
	}

	case EObserverPIDOperation::SetInstanceLimit:
	{
		return SetPIDInstanceLimit(ObserverActor, PID, InstanceLimit);
	}

	default:
		break;
	}

	return false;
}

// -----------------------------------------------------------------------------
// Section 3: Functions to Update Instance Limits
// -----------------------------------------------------------------------------

bool UInteractablePropSenseSightComponent::IncreasePIDInstanceLimit(AActor* ObserverActor, int32 PID, int32 IncreaseBy)
{
	if (!ObserverActor || PID <= 0 || IncreaseBy <= 0)
	{
		return false;
	}

	bool bUpdatedAny = false;

	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		bUpdatedAny = true;
		const int32 CurrentLimit = Found->PIDInstanceLimits.FindRef(PID);
		const int32 NewLimit = CurrentLimit + IncreaseBy;
		Found->PIDInstanceLimits.Add(PID, NewLimit);
		Found->TargetProductIDs.AddUnique(PID);
		Found->bFilterByPID = true;

		PersistentPIDInstanceLimits.FindOrAdd(ObserverActor).Add(PID, NewLimit);
		PersistentTargetProductIDs.FindOrAdd(ObserverActor).AddUnique(PID);
		PersistentFilterByPID.Add(ObserverActor, true);

		EvaluatePropsForPID(*Found, PID);
		SyncDetectedItemsFromPerPID(*Found);
	}

	for (FOneTimeObserverData& OneTime : OneTimeObservers)
	{
		if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
		{
			bUpdatedAny = true;
			const int32 CurrentLimit = OneTime.PIDInstanceLimits.FindRef(PID);
			const int32 NewLimit = CurrentLimit + IncreaseBy;
			OneTime.PIDInstanceLimits.Add(PID, NewLimit);
			OneTime.TargetProductIDs.AddUnique(PID);
			OneTime.bFilterByPID = true;

			EvaluatePropsForPID(OneTime, PID);
			SyncDetectedItemsFromPerPID(OneTime);
		}
	}

	return bUpdatedAny;
}

bool UInteractablePropSenseSightComponent::DecreasePIDInstanceLimit(AActor* ObserverActor, int32 PID, int32 DecreaseBy)
{
	if (!ObserverActor || PID <= 0 || DecreaseBy <= 0)
	{
		return false;
	}

	bool bUpdatedAny = false;

	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		bUpdatedAny = true;
		const int32 CurrentLimit = Found->PIDInstanceLimits.FindRef(PID);
		const int32 NewLimit = FMath::Max(0, CurrentLimit - DecreaseBy);

		if (NewLimit <= 0)
		{
			Found->PIDInstanceLimits.Remove(PID);
			Found->PerPIDDetectedItems.Remove(PID);
			if (TMap<int32, int32>* PersistMap = PersistentPIDInstanceLimits.Find(ObserverActor))
			{
				PersistMap->Remove(PID);
			}
		}
		else
		{
			Found->PIDInstanceLimits.Add(PID, NewLimit);
			PersistentPIDInstanceLimits.FindOrAdd(ObserverActor).Add(PID, NewLimit);

			if (TArray<FPropSightDetectedItem>* ListPtr = Found->PerPIDDetectedItems.Find(PID))
			{
				if (ListPtr->Num() > NewLimit)
				{
					ListPtr->SetNum(NewLimit);
				}
			}
		}

		SyncDetectedItemsFromPerPID(*Found);
	}

	for (FOneTimeObserverData& OneTime : OneTimeObservers)
	{
		if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
		{
			bUpdatedAny = true;
			const int32 CurrentLimit = OneTime.PIDInstanceLimits.FindRef(PID);
			const int32 NewLimit = FMath::Max(0, CurrentLimit - DecreaseBy);

			if (NewLimit <= 0)
			{
				OneTime.PIDInstanceLimits.Remove(PID);
				OneTime.PerPIDDetectedItems.Remove(PID);
			}
			else
			{
				OneTime.PIDInstanceLimits.Add(PID, NewLimit);
				if (TArray<FPropSightDetectedItem>* ListPtr = OneTime.PerPIDDetectedItems.Find(PID))
				{
					if (ListPtr->Num() > NewLimit)
					{
						ListPtr->SetNum(NewLimit);
					}
				}
			}

			SyncDetectedItemsFromPerPID(OneTime);
		}
	}

	return bUpdatedAny;
}

bool UInteractablePropSenseSightComponent::SetPIDInstanceLimit(AActor* ObserverActor, int32 PID, int32 NewLimit)
{
	if (!ObserverActor || PID <= 0)
	{
		return false;
	}

	if (NewLimit <= 0)
	{
		return ManageObserverPIDObservation(ObserverActor, EObserverPIDOperation::UnregisterPID, PID);
	}

	bool bUpdatedAny = false;

	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		bUpdatedAny = true;
		Found->PIDInstanceLimits.Add(PID, NewLimit);
		Found->TargetProductIDs.AddUnique(PID);
		Found->bFilterByPID = true;

		PersistentPIDInstanceLimits.FindOrAdd(ObserverActor).Add(PID, NewLimit);
		PersistentTargetProductIDs.FindOrAdd(ObserverActor).AddUnique(PID);
		PersistentFilterByPID.Add(ObserverActor, true);

		TArray<FPropSightDetectedItem>& List = Found->PerPIDDetectedItems.FindOrAdd(PID);
		if (List.Num() > NewLimit)
		{
			List.SetNum(NewLimit);
		}
		else if (List.Num() < NewLimit)
		{
			EvaluatePropsForPID(*Found, PID);
		}

		SyncDetectedItemsFromPerPID(*Found);
	}

	for (FOneTimeObserverData& OneTime : OneTimeObservers)
	{
		if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
		{
			bUpdatedAny = true;
			OneTime.PIDInstanceLimits.Add(PID, NewLimit);
			OneTime.TargetProductIDs.AddUnique(PID);
			OneTime.bFilterByPID = true;

			TArray<FPropSightDetectedItem>& List = OneTime.PerPIDDetectedItems.FindOrAdd(PID);
			if (List.Num() > NewLimit)
			{
				List.SetNum(NewLimit);
			}
			else if (List.Num() < NewLimit)
			{
				EvaluatePropsForPID(OneTime, PID);
			}

			SyncDetectedItemsFromPerPID(OneTime);
		}
	}

	return bUpdatedAny;
}

// -----------------------------------------------------------------------------
// Group 4: PID Observation Queries (Section 4 Group 4)
// -----------------------------------------------------------------------------

bool UInteractablePropSenseSightComponent::GetObservedInstanceCountByPID(
	AActor* ObserverActor,
	int32 PID,
	int32& OutPID,
	int32& OutRequestedCount,
	int32& OutCurrentObservedCount) const
{
	OutPID = PID;
	OutRequestedCount = 0;
	OutCurrentObservedCount = 0;

	if (!ObserverActor || PID <= 0)
	{
		return false;
	}

	if (const FContinuousObserverData* Found = FindContinuousObserver(ObserverActor))
	{
		if (const int32* ReqPtr = Found->PIDInstanceLimits.Find(PID))
		{
			OutRequestedCount = *ReqPtr;
			if (const TArray<FPropSightDetectedItem>* ListPtr = Found->PerPIDDetectedItems.Find(PID))
			{
				OutCurrentObservedCount = ListPtr->Num();
			}
			return true;
		}
		else if (Found->TargetProductIDs.Contains(PID))
		{
			OutRequestedCount = 0;
			int32 Count = 0;
			for (const FPropSightDetectedItem& Item : Found->DetectedItems)
			{
				if (Item.ProductID == PID)
				{
					++Count;
				}
			}
			OutCurrentObservedCount = Count;
			return true;
		}
	}

	for (const FOneTimeObserverData& OneTime : OneTimeObservers)
	{
		if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
		{
			if (const int32* ReqPtr = OneTime.PIDInstanceLimits.Find(PID))
			{
				OutRequestedCount = *ReqPtr;
				if (const TArray<FPropSightDetectedItem>* ListPtr = OneTime.PerPIDDetectedItems.Find(PID))
				{
					OutCurrentObservedCount = ListPtr->Num();
				}
				return true;
			}
			else if (OneTime.TargetProductIDs.Contains(PID))
			{
				OutRequestedCount = 0;
				int32 Count = 0;
				for (const FPropSightDetectedItem& Item : OneTime.DetectedItems)
				{
					if (Item.ProductID == PID)
					{
						++Count;
					}
				}
				OutCurrentObservedCount = Count;
				return true;
			}
		}
	}

	return false;
}

// -----------------------------------------------------------------------------
// Debug Visualization API & Helpers
// -----------------------------------------------------------------------------

void UInteractablePropSenseSightComponent::SetDebugDrawOptions(bool bInDrawCone, bool bInDrawVisibleObjects)
{
	bDrawDebugCone = bInDrawCone;
	bDrawDebugVisibleObjects = bInDrawVisibleObjects;
}

void UInteractablePropSenseSightComponent::SetObserverDebugDrawOptions(AActor* ObserverActor, bool bInDrawCone, bool bInDrawVisibleObjects)
{
	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		Found->bDrawDebugCone = bInDrawCone;
		Found->bDrawDebugVisibleObjects = bInDrawVisibleObjects;
	}
}

// -----------------------------------------------------------------------------
// Group 3: Observer Debug Settings (Section 4 Group 3)
// -----------------------------------------------------------------------------

bool UInteractablePropSenseSightComponent::EnableObserverDebug(AActor* ObserverActor, bool bInDrawCone, bool bInDrawVisibleObjects)
{
	if (!ObserverActor) return false;

	bool bFoundAny = false;
	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		Found->bDrawDebugCone = bInDrawCone;
		Found->bDrawDebugVisibleObjects = bInDrawVisibleObjects;
		bFoundAny = true;
	}

	for (FOneTimeObserverData& OneTime : OneTimeObservers)
	{
		if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
		{
			OneTime.bDrawDebugCone = bInDrawCone;
			OneTime.bDrawDebugVisibleObjects = bInDrawVisibleObjects;
			bFoundAny = true;
		}
	}

	return bFoundAny;
}

bool UInteractablePropSenseSightComponent::DisableObserverDebug(AActor* ObserverActor)
{
	if (!ObserverActor) return false;

	bool bFoundAny = false;
	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		Found->bDrawDebugCone = false;
		Found->bDrawDebugVisibleObjects = false;
		bFoundAny = true;
	}

	for (FOneTimeObserverData& OneTime : OneTimeObservers)
	{
		if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
		{
			OneTime.bDrawDebugCone = false;
			OneTime.bDrawDebugVisibleObjects = false;
			bFoundAny = true;
		}
	}

	return bFoundAny;
}

bool UInteractablePropSenseSightComponent::UpdateObserverDebug(AActor* ObserverActor, bool bInDrawCone, bool bInDrawVisibleObjects, FColor NewDebugColor)
{
	if (!ObserverActor) return false;

	bool bFoundAny = false;
	if (FContinuousObserverData* Found = FindContinuousObserverMutable(ObserverActor))
	{
		Found->bDrawDebugCone = bInDrawCone;
		Found->bDrawDebugVisibleObjects = bInDrawVisibleObjects;
		Found->DebugColor = NewDebugColor;
		bFoundAny = true;
	}

	for (FOneTimeObserverData& OneTime : OneTimeObservers)
	{
		if (DoesObserverMatch(OneTime.ObserverActor.Get(), ObserverActor))
		{
			OneTime.bDrawDebugCone = bInDrawCone;
			OneTime.bDrawDebugVisibleObjects = bInDrawVisibleObjects;
			OneTime.DebugColor = NewDebugColor;
			bFoundAny = true;
		}
	}

	return bFoundAny;
}

void UInteractablePropSenseSightComponent::DrawDebugForObserver(UWorld* World, const FContinuousObserverData& Config) const
{
	if (!World) return;

	FVector ViewOrigin;
	FRotator ViewRotation;
	ResolveSightTransform(Config, ViewOrigin, ViewRotation);

	if (ViewOrigin.IsZero() && ViewRotation.IsZero())
	{
		return;
	}

	const bool bShouldDrawCone = (bDrawDebugCone || Config.bDrawDebugCone);
	const bool bShouldDrawShapes = (bDrawDebugVisibleObjects || Config.bDrawDebugVisibleObjects);

	// Section 1: Draw sphere at observer location to visualize vision range using assigned debug color
	if (bShouldDrawCone || bShouldDrawShapes)
	{
		DrawDebugSphere(
			World,
			ViewOrigin,
			Config.SightRadius,
			24,
			Config.DebugColor,
			false,
			-1.0f,
			0,
			1.0f
		);

		// Section 1 & Section 4 Group 3: Overhead HUD displaying observer name, registered PIDs, and Observed / Requested instance counts
		FString HUDText = FString::Printf(TEXT("Observer: %s\n"), Config.ObserverActor.IsValid() ? *Config.ObserverActor->GetName() : TEXT("None"));
		APropManager* Mgr = PropManagerRef.Get();
		if (Config.PIDInstanceLimits.Num() > 0)
		{
			HUDText += TEXT("PID Limits:\n");
			for (const auto& LimitPair : Config.PIDInstanceLimits)
			{
				const int32 PID = LimitPair.Key;
				const int32 ReqCount = LimitPair.Value;
				int32 ObsCount = 0;
				if (const TArray<FPropSightDetectedItem>* ListPtr = Config.PerPIDDetectedItems.Find(PID))
				{
					ObsCount = ListPtr->Num();
				}

				FString ProdName = TEXT("Unknown");
				if (Mgr)
				{
					if (const FPropProductDefinition* Def = Mgr->FindProductDefinition(PID))
					{
						ProdName = Def->DisplayName.ToString();
					}
				}

				HUDText += FString::Printf(TEXT("  PID %d (%s): %d / %d\n"), PID, *ProdName, ObsCount, ReqCount);
			}
		}
		else if (Config.TargetProductIDs.Num() > 0)
		{
			HUDText += FString::Printf(TEXT("Target PIDs (%d): "), Config.TargetProductIDs.Num());
			for (int32 PID : Config.TargetProductIDs)
			{
				HUDText += FString::Printf(TEXT("%d "), PID);
			}
			HUDText += FString::Printf(TEXT("\nObserved: %d\n"), Config.DetectedItems.Num());
		}
		else
		{
			HUDText += FString::Printf(TEXT("Observing: All Products (%d detected)\n"), Config.DetectedItems.Num());
		}

		DrawDebugString(
			World,
			ViewOrigin + FVector(0.0f, 0.0f, 80.0f),
			HUDText,
			nullptr,
			Config.DebugColor,
			-1.0f,
			false,
			1.1f
		);
	}

	// 2. Draw 3D vision cone and forward gaze ray using assigned debug color
	if (bShouldDrawCone)
	{
		const FVector ForwardDir = ViewRotation.Vector();
		const float HalfAngleRad = FMath::DegreesToRadians(FMath::Clamp(Config.PeripheralVisionHalfAngle, 1.0f, 89.0f));

		DrawDebugCone(
			World,
			ViewOrigin,
			ForwardDir,
			Config.SightRadius,
			HalfAngleRad,
			HalfAngleRad,
			16,
			Config.DebugColor,
			false,
			-1.0f,
			0,
			1.5f
		);

		DrawDebugLine(
			World,
			ViewOrigin,
			ViewOrigin + ForwardDir * FMath::Min(Config.SightRadius, 300.0f),
			Config.DebugColor,
			false,
			-1.0f,
			0,
			2.5f
		);
	}

	// 3. Draw 3D shapes, connecting lines, and text using assigned debug color
	if (bShouldDrawShapes)
	{
		for (const FPropSightDetectedItem& Item : Config.DetectedItems)
		{
			const bool bIsClosest = (Config.BestClosestProp.ISID == Item.ISID);
			const FVector DrawCenter = Item.Location + FVector(0.0f, 0.0f, 20.0f);

			DrawDebugSphere(
				World,
				DrawCenter,
				bIsClosest ? 28.0f : 20.0f,
				16,
				Config.DebugColor,
				false,
				-1.0f,
				0,
				bIsClosest ? 3.0f : 1.5f
			);

			DrawDebugLine(
				World,
				ViewOrigin,
				DrawCenter,
				Config.DebugColor,
				false,
				-1.0f,
				0,
				1.2f
			);

			FString ProdName = TEXT("");
			if (APropManager* Mgr = PropManagerRef.Get())
			{
				if (const FPropProductDefinition* Def = Mgr->FindProductDefinition(Item.ProductID))
				{
					ProdName = FString::Printf(TEXT(" (%s)"), *Def->DisplayName.ToString());
				}
			}

			DrawDebugString(
				World,
				DrawCenter + FVector(0.0f, 0.0f, 25.0f),
				FString::Printf(TEXT("ISID:%d PID:%d%s%s"), Item.ISID, Item.ProductID, *ProdName, bIsClosest ? TEXT(" [CLOSEST]") : TEXT("")),
				nullptr,
				Config.DebugColor,
				-1.0f,
				false,
				1.0f
			);
		}
	}
}

void UInteractablePropSenseSightComponent::DrawDebugForObserver(UWorld* World, const FOneTimeObserverData& Config) const
{
	if (!World) return;

	FVector ViewOrigin;
	FRotator ViewRotation;
	ResolveSightTransform(Config, ViewOrigin, ViewRotation);

	if (ViewOrigin.IsZero() && ViewRotation.IsZero())
	{
		return;
	}

	const bool bShouldDrawCone = (bDrawDebugCone || Config.bDrawDebugCone);
	const bool bShouldDrawShapes = (bDrawDebugVisibleObjects || Config.bDrawDebugVisibleObjects);

	// Section 1: Draw sphere at observer location to visualize vision range using assigned debug color
	if (bShouldDrawCone || bShouldDrawShapes)
	{
		DrawDebugSphere(
			World,
			ViewOrigin,
			Config.SightRadius,
			24,
			Config.DebugColor,
			false,
			-1.0f,
			0,
			1.0f
		);

		// Section 1 & Section 4 Group 3: Overhead HUD displaying observer name, registered PIDs, and Observed / Requested instance counts
		FString HUDText = FString::Printf(TEXT("One-Time Query (QID: %lld)\nObserver: %s\n"), Config.QueryID, Config.ObserverActor.IsValid() ? *Config.ObserverActor->GetName() : TEXT("None"));
		APropManager* Mgr = PropManagerRef.Get();
		if (Config.PIDInstanceLimits.Num() > 0)
		{
			HUDText += TEXT("PID Limits:\n");
			for (const auto& LimitPair : Config.PIDInstanceLimits)
			{
				const int32 PID = LimitPair.Key;
				const int32 ReqCount = LimitPair.Value;
				int32 ObsCount = 0;
				if (const TArray<FPropSightDetectedItem>* ListPtr = Config.PerPIDDetectedItems.Find(PID))
				{
					ObsCount = ListPtr->Num();
				}

				FString ProdName = TEXT("Unknown");
				if (Mgr)
				{
					if (const FPropProductDefinition* Def = Mgr->FindProductDefinition(PID))
					{
						ProdName = Def->DisplayName.ToString();
					}
				}

				HUDText += FString::Printf(TEXT("  PID %d (%s): %d / %d\n"), PID, *ProdName, ObsCount, ReqCount);
			}
		}
		else if (Config.TargetProductIDs.Num() > 0)
		{
			HUDText += FString::Printf(TEXT("Target PIDs (%d): "), Config.TargetProductIDs.Num());
			for (int32 PID : Config.TargetProductIDs)
			{
				HUDText += FString::Printf(TEXT("%d "), PID);
			}
			HUDText += FString::Printf(TEXT("\nObserved: %d\n"), Config.DetectedItems.Num());
		}
		else
		{
			HUDText += FString::Printf(TEXT("Observing: All Products (%d detected)\n"), Config.DetectedItems.Num());
		}

		DrawDebugString(
			World,
			ViewOrigin + FVector(0.0f, 0.0f, 80.0f),
			HUDText,
			nullptr,
			Config.DebugColor,
			-1.0f,
			false,
			1.1f
		);
	}

	// 2. Draw 3D vision cone and forward gaze ray using assigned debug color
	if (bShouldDrawCone)
	{
		const FVector ForwardDir = ViewRotation.Vector();
		const float HalfAngleRad = FMath::DegreesToRadians(FMath::Clamp(Config.PeripheralVisionHalfAngle, 1.0f, 89.0f));

		DrawDebugCone(
			World,
			ViewOrigin,
			ForwardDir,
			Config.SightRadius,
			HalfAngleRad,
			HalfAngleRad,
			16,
			Config.DebugColor,
			false,
			-1.0f,
			0,
			1.5f
		);

		DrawDebugLine(
			World,
			ViewOrigin,
			ViewOrigin + ForwardDir * FMath::Min(Config.SightRadius, 300.0f),
			Config.DebugColor,
			false,
			-1.0f,
			0,
			2.5f
		);
	}

	// 3. Draw 3D shapes, connecting lines, and text using assigned debug color
	if (bShouldDrawShapes)
	{
		for (const FPropSightDetectedItem& Item : Config.DetectedItems)
		{
			const bool bIsClosest = (Config.BestClosestProp.ISID == Item.ISID);
			const FVector DrawCenter = Item.Location + FVector(0.0f, 0.0f, 20.0f);

			DrawDebugSphere(
				World,
				DrawCenter,
				bIsClosest ? 28.0f : 20.0f,
				16,
				Config.DebugColor,
				false,
				-1.0f,
				0,
				bIsClosest ? 3.0f : 1.5f
			);

			DrawDebugLine(
				World,
				ViewOrigin,
				DrawCenter,
				Config.DebugColor,
				false,
				-1.0f,
				0,
				1.2f
			);

			FString ProdName = TEXT("");
			if (APropManager* Mgr = PropManagerRef.Get())
			{
				if (const FPropProductDefinition* Def = Mgr->FindProductDefinition(Item.ProductID))
				{
					ProdName = FString::Printf(TEXT(" (%s)"), *Def->DisplayName.ToString());
				}
			}

			DrawDebugString(
				World,
				DrawCenter + FVector(0.0f, 0.0f, 25.0f),
				FString::Printf(TEXT("ISID:%d PID:%d%s%s"), Item.ISID, Item.ProductID, *ProdName, bIsClosest ? TEXT(" [CLOSEST]") : TEXT("")),
				nullptr,
				Config.DebugColor,
				-1.0f,
				false,
				1.0f
			);
		}
	}
}

// -----------------------------------------------------------------------------
// Full World Evaluation Helpers
// -----------------------------------------------------------------------------

void UInteractablePropSenseSightComponent::EvaluateAllPropsForContinuous(FContinuousObserverData& Config)
{
	APropManager* Manager = PropManagerRef.Get();
	if (!Manager)
	{
		Manager = APropManager::Get(this);
		PropManagerRef = Manager;
	}
	if (!Manager) return;

	Config.DetectedItems.Empty();
	Config.ISIDToIndexMap.Empty();
	Config.PerPIDDetectedItems.Empty();
	Config.BestClosestProp = FPropSightPerceptionResult();
	Config.BestDistanceSq = MAX_FLT;

	for (const TPair<int32, FISIDRuntimeIndex>& Pair : Manager->ISIDData)
	{
		const FISIDRuntimeIndex& Record = Pair.Value;
		if (Record.ISID <= 0) continue;

		FPropBatchEvaluationItem Item;
		Item.ISID = Record.ISID;
		Item.ProductID = Record.ProductID;
		Item.Representation = Record.Representation;
		Item.State = Record.State;
		Item.CurrentTemperature = Record.PackedRuntimeData.Temperature;
		Item.RemainingPortions = Record.PackedRuntimeData.RemainingPortions;
		Item.IISMComponentID = Record.IISMComponentID;
		Item.ActiveActor = Record.ActiveActor;
		Item.bIsGrabbable = Record.PackedRuntimeData.bIsGrabbable;
		if (Record.ActiveActor)
		{
			Item.bIsGrabbable = Record.ActiveActor->bIsGrabbable;
		}
		if (Record.State == EPropState::Held)
		{
			Item.bIsGrabbable = false;
		}

		Item.WorldTransform = Record.ActiveTransform;
		Item.WorldLocation = Record.ActiveTransform.GetLocation();

		if (Record.Representation == EPropRepresentation::ISM)
		{
			if (UInteractableISMComponent* Comp = Manager->FindIISMComponent(Record.IISMComponentID))
			{
				FTransform InstTransform;
				if (Comp->GetInstanceTransform(Record.ISMI, InstTransform, true))
				{
					Item.WorldTransform = InstTransform;
					Item.WorldLocation = InstTransform.GetLocation();
				}
			}
		}
		else if (Record.ActiveActor)
		{
			Item.WorldTransform = Record.ActiveActor->GetActorTransform();
			Item.WorldLocation = Record.ActiveActor->GetActorLocation();
		}

		float DistSq = 0.0f;
		FVector TargetLoc = FVector::ZeroVector;
		const bool bVisible = IsItemVisible(Item, Config, DistSq, TargetLoc);
		ProcessItemForObserver(Config, Item, bVisible, DistSq, TargetLoc);
	}

	// Section 9: Sort results in ascending order of distance (closest at index 0, farthest at last index)
	FVector ViewOrigin;
	FRotator ViewRot;
	ResolveSightTransform(Config, ViewOrigin, ViewRot);
	SortObserverResults(Config, ViewOrigin);
}

void UInteractablePropSenseSightComponent::EvaluateAllPropsForOneTime(FOneTimeObserverData& Config)
{
	APropManager* Manager = PropManagerRef.Get();
	if (!Manager)
	{
		Manager = APropManager::Get(this);
		PropManagerRef = Manager;
	}
	if (!Manager) return;

	Config.DetectedItems.Empty();
	Config.ISIDToIndexMap.Empty();
	Config.PerPIDDetectedItems.Empty();
	Config.BestClosestProp = FPropSightPerceptionResult();
	Config.BestDistanceSq = MAX_FLT;

	for (const TPair<int32, FISIDRuntimeIndex>& Pair : Manager->ISIDData)
	{
		const FISIDRuntimeIndex& Record = Pair.Value;
		if (Record.ISID <= 0) continue;

		FPropBatchEvaluationItem Item;
		Item.ISID = Record.ISID;
		Item.ProductID = Record.ProductID;
		Item.Representation = Record.Representation;
		Item.State = Record.State;
		Item.CurrentTemperature = Record.PackedRuntimeData.Temperature;
		Item.RemainingPortions = Record.PackedRuntimeData.RemainingPortions;
		Item.IISMComponentID = Record.IISMComponentID;
		Item.ActiveActor = Record.ActiveActor;
		Item.bIsGrabbable = Record.PackedRuntimeData.bIsGrabbable;
		if (Record.ActiveActor)
		{
			Item.bIsGrabbable = Record.ActiveActor->bIsGrabbable;
		}
		if (Record.State == EPropState::Held)
		{
			Item.bIsGrabbable = false;
		}

		Item.WorldTransform = Record.ActiveTransform;
		Item.WorldLocation = Record.ActiveTransform.GetLocation();

		if (Record.Representation == EPropRepresentation::ISM)
		{
			if (UInteractableISMComponent* Comp = Manager->FindIISMComponent(Record.IISMComponentID))
			{
				FTransform InstTransform;
				if (Comp->GetInstanceTransform(Record.ISMI, InstTransform, true))
				{
					Item.WorldTransform = InstTransform;
					Item.WorldLocation = InstTransform.GetLocation();
				}
			}
		}
		else if (Record.ActiveActor)
		{
			Item.WorldTransform = Record.ActiveActor->GetActorTransform();
			Item.WorldLocation = Record.ActiveActor->GetActorLocation();
		}

		float DistSq = 0.0f;
		FVector TargetLoc = FVector::ZeroVector;
		const bool bVisible = IsItemVisible(Item, Config, DistSq, TargetLoc);
		ProcessItemForObserver(Config, Item, bVisible, DistSq, TargetLoc);
	}

	// Section 9: Sort results in ascending order of distance (closest at index 0, farthest at last index)
	FVector ViewOrigin;
	FRotator ViewRot;
	ResolveSightTransform(Config, ViewOrigin, ViewRot);
	SortObserverResults(Config, ViewOrigin);
}

// -----------------------------------------------------------------------------
// Wrapper Packaging
// -----------------------------------------------------------------------------

FPropSightDetectedProps UInteractablePropSenseSightComponent::BuildDetectedPropsWrapper(const TArray<FPropSightDetectedItem>& Items) const
{
	FPropSightDetectedProps Wrapper;
	TSet<int32> UniquePIDs;

	Wrapper.Props.Reserve(Items.Num());
	Wrapper.ISIDToProductID.Reserve(Items.Num());
	Wrapper.DetectedPropsList.Reserve(Items.Num());

	// Items are already sorted by ascending distance:
	// Index 0: Closest observed instance.
	// Middle indices: Ordered nearer to farther.
	// Last index: Farthest observed instance.
	for (const FPropSightDetectedItem& Item : Items)
	{
		Wrapper.Props.Add(Item.ISID, Item.Location);
		Wrapper.ISIDToProductID.Add(Item.ISID, Item.ProductID);

		if (Item.ProductID > 0)
		{
			UniquePIDs.Add(Item.ProductID);
		}

		FPropSightPerceptionResult Result;
		Result.ISID = Item.ISID;
		Result.ProductID = Item.ProductID;
		Result.WorldLocation = Item.Location;
		Result.Distance = Item.Distance;
		Result.Representation = Item.Representation;
		Result.State = Item.State;
		Result.ActiveActor = Item.ActiveActor;
		Wrapper.DetectedPropsList.Add(Result);
	}

	Wrapper.DetectedProductIDs = UniquePIDs.Array();
	Wrapper.DetectedProductIDs.Sort();

	return Wrapper;
}
