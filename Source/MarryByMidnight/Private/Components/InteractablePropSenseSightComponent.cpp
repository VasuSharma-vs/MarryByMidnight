#include "Components/InteractablePropSenseSightComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

UInteractablePropSenseSightComponent::UInteractablePropSenseSightComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UInteractablePropSenseSightComponent::BeginPlay()
{
	Super::BeginPlay();

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

	ActiveOneShotQueries.Empty();
	ContinuousObservers.Empty();

	Super::EndPlay(EndPlayReason);
}

void UInteractablePropSenseSightComponent::ResolveSightTransform(
	const FPropSightObserverConfig& Config,
	FVector& OutLocation,
	FRotator& OutRotation
) const
{
	if (!Config.SightLocationOverride.IsZero() || !Config.SightRotationOverride.IsZero())
	{
		OutLocation = Config.SightLocationOverride;
		OutRotation = Config.SightRotationOverride;
		return;
	}

	if (AActor* Observer = Config.ObserverActor.Get())
	{
		FVector EyesLoc;
		FRotator EyesRot;
		Observer->GetActorEyesViewPoint(EyesLoc, EyesRot);
		OutLocation = EyesLoc;
		OutRotation = EyesRot;
	}
	else
	{
		OutLocation = FVector::ZeroVector;
		OutRotation = FRotator::ZeroRotator;
	}
}

bool UInteractablePropSenseSightComponent::CheckMultiPointLineOfSight(
	const FVector& ViewOrigin,
	const FPropBatchEvaluationItem& Item,
	ECollisionChannel TraceChannel,
	AActor* ObserverActor
) const
{
	UWorld* World = GetWorld();
	if (!World) return false;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(PropSenseSightTrace), true);
	if (ObserverActor)
	{
		Params.AddIgnoredActor(ObserverActor);
	}
	if (Item.ActiveActor.IsValid())
	{
		Params.AddIgnoredActor(Item.ActiveActor.Get());
	}

	// Sample 4 target points (center, top, left, right)
	// If any point is unblocked, the prop is considered visible
	TArray<FVector, TInlineAllocator<4>> SamplePoints;
	SamplePoints.Add(Item.WorldLocation);
	SamplePoints.Add(Item.WorldLocation + FVector(0.0f, 0.0f, 35.0f));

	const FVector ViewDir = (Item.WorldLocation - ViewOrigin).GetSafeNormal();
	const FVector RightDir = (ViewDir ^ FVector::UpVector).GetSafeNormal() * 20.0f;
	SamplePoints.Add(Item.WorldLocation + RightDir);
	SamplePoints.Add(Item.WorldLocation - RightDir);

	for (const FVector& TargetPoint : SamplePoints)
	{
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(Hit, ViewOrigin, TargetPoint, TraceChannel, Params);

		// If nothing was hit between eyes and target point, it is unblocked!
		if (!bHit)
		{
			return true;
		}

		// If hit actor was the prop itself (active representation), it is visible!
		if (Item.ActiveActor.IsValid() && Hit.GetActor() == Item.ActiveActor.Get())
		{
			return true;
		}
	}

	return false;
}

bool UInteractablePropSenseSightComponent::IsItemVisibleToObserver(
	const FPropBatchEvaluationItem& Item,
	const FPropSightObserverConfig& Config,
	float& OutDistanceSq,
	FVector& OutTargetPoint
) const
{
	// 0. Check ignored ISIDs list
	if (Config.IgnoredISIDs.Contains(Item.ISID))
	{
		return false;
	}

	// 1. Check grabbable: ignore products that are not grabbable
	if (Config.bRequireGrabbable && !Item.bIsGrabbable)
	{
		return false;
	}

	// 2. ProductID match (empty array means match all)
	if (Config.TargetProductIDs.Num() > 0 && !Config.TargetProductIDs.Contains(Item.ProductID))
	{
		return false;
	}

	// 2. Resolve View Origin and Rotation
	FVector ViewOrigin;
	FRotator ViewRotation;
	ResolveSightTransform(Config, ViewOrigin, ViewRotation);

	// 3. Distance check
	const float DistSq = FVector::DistSquared(ViewOrigin, Item.WorldLocation);
	if (DistSq > FMath::Square(Config.SightRadius))
	{
		return false;
	}

	// 4. Angle / FOV Cone check
	const FVector DirectionToItem = (Item.WorldLocation - ViewOrigin).GetSafeNormal();
	const FVector ViewForward = ViewRotation.Vector();
	const float Dot = FVector::DotProduct(ViewForward, DirectionToItem);
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Config.PeripheralVisionHalfAngle, 1.0f, 180.0f)));
	if (Dot < MinDot)
	{
		return false;
	}

	// 5. Line of sight obstruction check
	if (!Config.bIgnoreBlocked)
	{
		if (!CheckMultiPointLineOfSight(ViewOrigin, Item, Config.TraceChannel, Config.ObserverActor.Get()))
		{
			return false;
		}
	}

	OutDistanceSq = DistSq;
	OutTargetPoint = Item.WorldLocation;
	return true;
}

int64 UInteractablePropSenseSightComponent::RequestClosestProductInSight(
	const TArray<int32>& TargetProductIDs,
	float SightRadius,
	float PeripheralVisionHalfAngle,
	AActor* ObserverActor,
	const FOnPropSightQueryCompleted& OnCompleted,
	bool bIgnoreBlocked,
	FVector SightLocationOverride,
	FRotator SightRotationOverride,
	ECollisionChannel TraceChannel
)
{
	FPropSightObserverConfig NewQuery;
	NewQuery.QueryID = NextQueryID++;
	NewQuery.TargetProductIDs = TargetProductIDs;
	NewQuery.SightRadius = FMath::Max(50.0f, SightRadius);
	NewQuery.PeripheralVisionHalfAngle = FMath::Clamp(PeripheralVisionHalfAngle, 1.0f, 180.0f);
	NewQuery.bIgnoreBlocked = bIgnoreBlocked;
	NewQuery.ObserverActor = ObserverActor;
	NewQuery.SightLocationOverride = SightLocationOverride;
	NewQuery.SightRotationOverride = SightRotationOverride;
	NewQuery.TraceChannel = TraceChannel;
	NewQuery.bContinuous = false;
	NewQuery.OnQueryCompleted = OnCompleted;

	if (BatchUpdateComponentRef.IsValid())
	{
		NewQuery.StartISIDIndex = BatchUpdateComponentRef->GetNextBatchIndex();
	}

	ActiveOneShotQueries.Add(NewQuery.QueryID, NewQuery);
	return NewQuery.QueryID;
}

bool UInteractablePropSenseSightComponent::CancelSightQuery(int64 QueryID)
{
	return ActiveOneShotQueries.Remove(QueryID) > 0;
}

void UInteractablePropSenseSightComponent::RegisterObserverForContinuousSight(
	AActor* ObserverActor,
	const TArray<int32>& TargetProductIDs,
	float SightRadius,
	float PeripheralVisionHalfAngle,
	bool bIgnoreBlocked,
	FVector SightLocationOverride,
	FRotator SightRotationOverride,
	ECollisionChannel TraceChannel
)
{
	if (!ObserverActor) return;

	FPropSightObserverConfig Config;
	Config.ObserverActor = ObserverActor;
	Config.TargetProductIDs = TargetProductIDs;
	Config.SightRadius = FMath::Max(50.0f, SightRadius);
	Config.PeripheralVisionHalfAngle = FMath::Clamp(PeripheralVisionHalfAngle, 1.0f, 180.0f);
	Config.bIgnoreBlocked = bIgnoreBlocked;
	Config.SightLocationOverride = SightLocationOverride;
	Config.SightRotationOverride = SightRotationOverride;
	Config.TraceChannel = TraceChannel;
	Config.bContinuous = true;

	ContinuousObservers.Add(ObserverActor, Config);
}

void UInteractablePropSenseSightComponent::UnregisterObserver(AActor* ObserverActor)
{
	if (!ObserverActor) return;
	ContinuousObservers.Remove(ObserverActor);
}

bool UInteractablePropSenseSightComponent::GetVisiblePropsForObserver(AActor* ObserverActor, TMap<int32, FVector>& OutVisibleProps) const
{
	OutVisibleProps.Empty();
	if (!ObserverActor) return false;

	if (const FPropSightObserverConfig* Found = ContinuousObservers.Find(ObserverActor))
	{
		OutVisibleProps = Found->DetectedProps;
		return OutVisibleProps.Num() > 0;
	}
	return false;
}

bool UInteractablePropSenseSightComponent::GetClosestVisiblePropForObserver(AActor* ObserverActor, FPropSightPerceptionResult& OutClosestProp) const
{
	if (!ObserverActor) return false;

	if (const FPropSightObserverConfig* Found = ContinuousObservers.Find(ObserverActor))
	{
		if (Found->BestClosestProp.ISID > 0)
		{
			OutClosestProp = Found->BestClosestProp;
			return true;
		}
	}
	return false;
}

void UInteractablePropSenseSightComponent::IgnoreISIDForObserver(AActor* ObserverActor, int32 InISID)
{
	if (!ObserverActor || InISID <= 0) return;

	if (FPropSightObserverConfig* Found = ContinuousObservers.Find(ObserverActor))
	{
		Found->IgnoredISIDs.Add(InISID);
		Found->DetectedProps.Remove(InISID);

		if (Found->BestClosestProp.ISID == InISID)
		{
			Found->BestClosestProp = FPropSightPerceptionResult();
			Found->BestDistanceSq = MAX_FLT;
		}
	}
}

void UInteractablePropSenseSightComponent::IgnoreISIDsForObserver(AActor* ObserverActor, const TArray<int32>& InISIDs)
{
	if (!ObserverActor || InISIDs.Num() == 0) return;

	if (FPropSightObserverConfig* Found = ContinuousObservers.Find(ObserverActor))
	{
		for (int32 ID : InISIDs)
		{
			if (ID > 0)
			{
				Found->IgnoredISIDs.Add(ID);
				Found->DetectedProps.Remove(ID);

				if (Found->BestClosestProp.ISID == ID)
				{
					Found->BestClosestProp = FPropSightPerceptionResult();
					Found->BestDistanceSq = MAX_FLT;
				}
			}
		}
	}
}

void UInteractablePropSenseSightComponent::UnignoreISIDForObserver(AActor* ObserverActor, int32 InISID)
{
	if (!ObserverActor || InISID <= 0) return;

	if (FPropSightObserverConfig* Found = ContinuousObservers.Find(ObserverActor))
	{
		Found->IgnoredISIDs.Remove(InISID);
	}
}

void UInteractablePropSenseSightComponent::ClearIgnoredISIDsForObserver(AActor* ObserverActor)
{
	if (!ObserverActor) return;

	if (FPropSightObserverConfig* Found = ContinuousObservers.Find(ObserverActor))
	{
		Found->IgnoredISIDs.Empty();
	}
}

bool UInteractablePropSenseSightComponent::IsISIDIgnoredForObserver(AActor* ObserverActor, int32 InISID) const
{
	if (!ObserverActor || InISID <= 0) return false;

	if (const FPropSightObserverConfig* Found = ContinuousObservers.Find(ObserverActor))
	{
		return Found->IgnoredISIDs.Contains(InISID);
	}
	return false;
}

void UInteractablePropSenseSightComponent::SetTargetProductIDsForObserver(AActor* ObserverActor, const TArray<int32>& NewProductIDs)
{
	if (!ObserverActor) return;

	if (FPropSightObserverConfig* Found = ContinuousObservers.Find(ObserverActor))
	{
		Found->TargetProductIDs = NewProductIDs;

		// Purge any detected props that no longer match the new product list
		if (NewProductIDs.Num() > 0)
		{
			if (APropManager* Manager = PropManagerRef.Get())
			{
				for (auto It = Found->DetectedProps.CreateIterator(); It; ++It)
				{
					if (const FISIDRuntimeIndex* Rec = Manager->FindISIDData(It.Key()))
					{
						if (!NewProductIDs.Contains(Rec->ProductID))
						{
							It.RemoveCurrent();
						}
					}
				}
			}

			if (Found->BestClosestProp.ISID > 0 && !NewProductIDs.Contains(Found->BestClosestProp.ProductID))
			{
				Found->BestClosestProp = FPropSightPerceptionResult();
				Found->BestDistanceSq = MAX_FLT;
			}
		}
	}
}

void UInteractablePropSenseSightComponent::AddTargetProductIDForObserver(AActor* ObserverActor, int32 InProductID)
{
	if (!ObserverActor || InProductID <= 0) return;

	if (FPropSightObserverConfig* Found = ContinuousObservers.Find(ObserverActor))
	{
		Found->TargetProductIDs.AddUnique(InProductID);
	}
}

void UInteractablePropSenseSightComponent::RemoveTargetProductIDForObserver(AActor* ObserverActor, int32 InProductID)
{
	if (!ObserverActor || InProductID <= 0) return;

	if (FPropSightObserverConfig* Found = ContinuousObservers.Find(ObserverActor))
	{
		Found->TargetProductIDs.Remove(InProductID);

		// Purge detected props of this product ID
		if (APropManager* Manager = PropManagerRef.Get())
		{
			for (auto It = Found->DetectedProps.CreateIterator(); It; ++It)
			{
				if (const FISIDRuntimeIndex* Rec = Manager->FindISIDData(It.Key()))
				{
					if (Rec->ProductID == InProductID)
					{
						It.RemoveCurrent();
					}
				}
			}
		}

		if (Found->BestClosestProp.ProductID == InProductID)
		{
			Found->BestClosestProp = FPropSightPerceptionResult();
			Found->BestDistanceSq = MAX_FLT;
		}
	}
}

bool UInteractablePropSenseSightComponent::GetTargetProductIDsForObserver(AActor* ObserverActor, TArray<int32>& OutProductIDs) const
{
	OutProductIDs.Empty();
	if (!ObserverActor) return false;

	if (const FPropSightObserverConfig* Found = ContinuousObservers.Find(ObserverActor))
	{
		OutProductIDs = Found->TargetProductIDs;
		return true;
	}
	return false;
}

void UInteractablePropSenseSightComponent::HandleBatchEvaluated(
	const TArray<FPropBatchEvaluationItem>& Batch,
	const FPropBatchMetadata& Metadata
)
{
	// -------------------------------------------------------------------------
	// 1. Process One-Shot Queries
	// -------------------------------------------------------------------------
	TArray<int64> CompletedQueryIDs;

	for (auto& Pair : ActiveOneShotQueries)
	{
		FPropSightObserverConfig& Query = Pair.Value;

		// Clean up if observer was destroyed and overrides were not provided
		if (!Query.ObserverActor.IsValid() && Query.SightLocationOverride.IsZero())
		{
			CompletedQueryIDs.Add(Pair.Key);
			continue;
		}

		for (const FPropBatchEvaluationItem& Item : Batch)
		{
			float DistSq = 0.0f;
			FVector TargetLoc = FVector::ZeroVector;
			if (IsItemVisibleToObserver(Item, Query, DistSq, TargetLoc))
			{
				Query.DetectedProps.Add(Item.ISID, TargetLoc);

				if (DistSq < Query.BestDistanceSq)
				{
					Query.BestDistanceSq = DistSq;
					Query.BestClosestProp.ISID = Item.ISID;
					Query.BestClosestProp.ProductID = Item.ProductID;
					Query.BestClosestProp.WorldLocation = TargetLoc;
					Query.BestClosestProp.Distance = FMath::Sqrt(DistSq);
					Query.BestClosestProp.Representation = Item.Representation;
					Query.BestClosestProp.State = Item.State;
					Query.BestClosestProp.ActiveActor = Item.ActiveActor;
				}
			}
		}

		// When a full round-robin cycle of world props has completed, resolve this query
		if (Metadata.bCycleCompleted)
		{
			const bool bFoundAny = Query.DetectedProps.Num() > 0;
			FPropSightDetectedProps DetectedWrapper;
			DetectedWrapper.Props = MoveTemp(Query.DetectedProps);
			Query.OnQueryCompleted.ExecuteIfBound(bFoundAny, DetectedWrapper, Query.BestClosestProp);
			CompletedQueryIDs.Add(Pair.Key);
		}
	}

	for (int64 QueryID : CompletedQueryIDs)
	{
		ActiveOneShotQueries.Remove(QueryID);
	}

	// -------------------------------------------------------------------------
	// 2. Process Continuous Observers
	// -------------------------------------------------------------------------
	TArray<TWeakObjectPtr<AActor>> DeadObservers;

	for (auto& Pair : ContinuousObservers)
	{
		AActor* Observer = Pair.Key.Get();
		if (!Observer)
		{
			DeadObservers.Add(Pair.Key);
			continue;
		}

		FPropSightObserverConfig& ObserverConfig = Pair.Value;

		for (const FPropBatchEvaluationItem& Item : Batch)
		{
			float DistSq = 0.0f;
			FVector TargetLoc = FVector::ZeroVector;
			const bool bVisible = IsItemVisibleToObserver(Item, ObserverConfig, DistSq, TargetLoc);

			if (bVisible)
			{
				ObserverConfig.DetectedProps.Add(Item.ISID, TargetLoc);

				if (DistSq < ObserverConfig.BestDistanceSq)
				{
					ObserverConfig.BestDistanceSq = DistSq;
					ObserverConfig.BestClosestProp.ISID = Item.ISID;
					ObserverConfig.BestClosestProp.ProductID = Item.ProductID;
					ObserverConfig.BestClosestProp.WorldLocation = TargetLoc;
					ObserverConfig.BestClosestProp.Distance = FMath::Sqrt(DistSq);
					ObserverConfig.BestClosestProp.Representation = Item.Representation;
					ObserverConfig.BestClosestProp.State = Item.State;
					ObserverConfig.BestClosestProp.ActiveActor = Item.ActiveActor;
				}
			}
			else
			{
				// Item was inspected and is no longer visible to this observer
				ObserverConfig.DetectedProps.Remove(Item.ISID);
				if (ObserverConfig.BestClosestProp.ISID == Item.ISID)
				{
					ObserverConfig.BestClosestProp = FPropSightPerceptionResult();
					ObserverConfig.BestDistanceSq = MAX_FLT;
				}
			}
		}

		// On cycle wrap, notify listeners if bound
		if (Metadata.bCycleCompleted && OnPropSightPerceptionUpdated.IsBound())
		{
			FPropSightDetectedProps DetectedWrapper;
			DetectedWrapper.Props = ObserverConfig.DetectedProps;
			OnPropSightPerceptionUpdated.Broadcast(
				Observer,
				ObserverConfig.DetectedProps.Num() > 0,
				DetectedWrapper,
				ObserverConfig.BestClosestProp
			);
		}
	}

	for (const auto& DeadPtr : DeadObservers)
	{
		ContinuousObservers.Remove(DeadPtr);
	}
}
