#include "Shop/ShopCustomerVisitComponent.h"
#include "Shop/ShopCustomerFlow.h"
#include "Shop/ShopCustomerCashComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopInteractionGeometry.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextRenderComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UShopCustomerVisitComponent::UShopCustomerVisitComponent() { PrimaryComponentTick.bCanEverTick=false; SetIsReplicatedByDefault(true); }
void UShopCustomerVisitComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UShopCustomerVisitComponent,Phase); DOREPLIFETIME(UShopCustomerVisitComponent,Status);
}
void UShopCustomerVisitComponent::OnRep_Phase()
{
	const bool Hidden=Phase==EShopVisitPhase::Inactive;
	GetOwner()->SetActorHiddenInGame(Hidden); GetOwner()->SetActorEnableCollision(!Hidden);
	TArray<UTextRenderComponent*> Labels; GetOwner()->GetComponents(Labels);
	for (auto* Label:Labels) if (Label->ComponentHasTag(TEXT("ShopVisitStatus"))) Label->SetText(FText::FromString(Status));
}
void UShopCustomerVisitComponent::SetPhase(EShopVisitPhase Next,const FString& Message)
{
	Phase=Next; Status=Message; OnRep_Phase(); GetOwner()->ForceNetUpdate();
}
bool UShopCustomerVisitComponent::StartVisit(AShopCustomerFlow* InFlow,AActor* InBucket)
{
	if (!GetOwner()->HasAuthority() || GetNetMode()==NM_Client || Phase!=EShopVisitPhase::Inactive || !IsValid(InFlow) || !IsValid(InBucket)) return false;
	Manager=APropManager::Get(this); Cash=GetOwner()->FindComponentByClass<UShopCustomerCashComponent>();
	if (!Manager.IsValid() || !Cash.IsValid() || !Cash->CustomerID.IsValid() || !Manager->ShopCheckout) return false;
	Flow=InFlow; Bucket=InBucket; Collected.Empty(); Wanted.Empty(); Rejected.Empty(); TargetISID=0; CheckoutID.Invalidate();
	for (int32 PID:Cash->BuildShoppingPlan(InFlow->BottlesPerCustomer)) Wanted.Add(PID,Cash->GetPlannedQuantity(PID));
	if (Wanted.IsEmpty()) return false;
	SetPhase(EShopVisitPhase::Entering,TEXT("Entering shop")); MoveTo(InFlow->EntrancePoint->GetActorLocation());
	GetWorld()->GetTimerManager().SetTimer(VisitTimer,this,&UShopCustomerVisitComponent::AdvanceVisit,.25f,true);
	return true;
}
bool UShopCustomerVisitComponent::MoveTo(FVector Destination)
{
	auto* Pawn=Cast<ACharacter>(GetOwner()); auto* Controller=Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
	auto* Nav=UNavigationSystemV1::GetCurrent(GetWorld()); FNavLocation Projected;
	bMoving=false; bMoveFailed=true; MoveStarted=GetWorld()->GetTimeSeconds(); MoveFailureReason.Empty();
	if (!Controller) { MoveFailureReason=TEXT("No AI controller"); return false; }
	if (!Nav) { MoveFailureReason=TEXT("No navigation system"); return false; }
	// Query at the customer's floor rather than selecting a disconnected shelf-top nav island.
	Destination.Z=Pawn->GetNavAgentLocation().Z;
	const float Extent=Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius()+400;
	if (!Nav->ProjectPointToNavigation(Destination,Projected,FVector(Extent,Extent,200))) { MoveFailureReason=TEXT("No navigation polygon near destination"); return false; }
	MoveGoal=Projected.Location;
	const auto Result=Controller->MoveToLocation(MoveGoal,60.f,false,true,false,true,nullptr,false);
	bMoveFailed=Result==EPathFollowingRequestResult::Failed;
	if (bMoveFailed) MoveFailureReason=TEXT("Path request failed");
	bMoving=!bMoveFailed; return bMoving;
}
bool UShopCustomerVisitComponent::Arrived() const
{
	const auto* Pawn=Cast<ACharacter>(GetOwner());
	return bMoving && Pawn && FVector::DistSquared2D(Pawn->GetNavAgentLocation(),MoveGoal)<=FMath::Square(100.f)
		&& FMath::Abs(Pawn->GetNavAgentLocation().Z-MoveGoal.Z)<200;
}
USceneComponent* UShopCustomerVisitComponent::NextPocket() const
{
	if (!Bucket.IsValid()) return nullptr;
	TArray<USceneComponent*> Pockets; Bucket->GetComponents(Pockets);
	Pockets.RemoveAll([](const auto* Part){return !Part->ComponentHasTag(TEXT("ShopBucketPocket"));});
	Pockets.Sort([](const auto& A,const auto& B){return A.GetFName().LexicalLess(B.GetFName());});
	return Pockets.IsValidIndex(Collected.Num()) ? Pockets[Collected.Num()] : nullptr;
}
void UShopCustomerVisitComponent::ChooseNextProduct()
{
	TargetISID=0; double Best=TNumericLimits<double>::Max();
	if (!NextPocket()) Wanted.Empty();
	for (int32 ID:Manager->ShopCheckout->GetAvailableColaStockISIDs())
	{
		const auto* Record=Manager->FindISIDData(ID);
		if (!Record || Wanted.FindRef(Record->ProductID)<=0 || Rejected.Contains(ID) || Collected.Contains(ID) || Flow->StockIsClaimed(ID) || Record->State==EPropState::Held) continue;
		const double Distance=FVector::DistSquared2D(GetOwner()->GetActorLocation(),Manager->GetTransform(ID).GetLocation());
		if (Distance<Best) { Best=Distance; TargetISID=ID; }
	}
	if (TargetISID && Flow->ClaimStock(TargetISID,this))
	{
		SetPhase(EShopVisitPhase::Shopping,TEXT("Collecting Normal Cola")); MoveTo(Manager->GetTransform(TargetISID).GetLocation()); return;
	}
	TargetISID=0;
	if (Collected.IsEmpty()) { AbortVisit(TEXT("No requested stock available")); return; }
	Flow->JoinQueue(this); SetPhase(EShopVisitPhase::Queuing,TEXT("Queuing for checkout")); MoveTo(Flow->QueueLocation(Flow->QueuePosition(this)));
}
void UShopCustomerVisitComponent::CarryBucket()
{
	const auto* Character=Cast<ACharacter>(GetOwner());
	if (!Bucket.IsValid() || !Character) return;
	auto* Body=Cast<UPrimitiveComponent>(Bucket->GetRootComponent());
	if (Body) Body->SetSimulatePhysics(false);
	const auto Bounds=Body ? Body->CalcBounds(FTransform(FQuat::Identity,FVector::ZeroVector,Bucket->GetActorScale3D())) : FBoxSphereBounds(FVector::ZeroVector,FVector(50),50);
	const auto* Capsule=Character->GetCapsuleComponent();
	Bucket->AttachToComponent(GetOwner()->GetRootComponent(),FAttachmentTransformRules::KeepWorldTransform);
	Bucket->SetActorRelativeRotation(FRotator::ZeroRotator);
	Bucket->SetActorRelativeLocation(FVector(Capsule->GetScaledCapsuleRadius()+Bounds.BoxExtent.X+25,0,-Capsule->GetScaledCapsuleHalfHeight()*.1f)-Bounds.Origin);
}
void UShopCustomerVisitComponent::AdvanceVisit()
{
	if (!GetOwner()->HasAuthority() || GetNetMode()==NM_Client || Phase==EShopVisitPhase::Inactive || Phase==EShopVisitPhase::Departed || Phase==EShopVisitPhase::Aborted) return;
	if (!Flow.IsValid() || !Manager.IsValid() || !Cash.IsValid()) { AbortVisit(TEXT("Customer visit services unavailable")); return; }
	const bool MovingPhase=Phase==EShopVisitPhase::Entering || Phase==EShopVisitPhase::GettingBucket || Phase==EShopVisitPhase::Shopping
		|| Phase==EShopVisitPhase::ReturningBucket || Phase==EShopVisitPhase::Departing;
	if (MovingPhase && (bMoveFailed || (!Arrived() && GetWorld()->GetTimeSeconds()-MoveStarted>MoveTimeoutSeconds)))
	{
		if (Phase==EShopVisitPhase::Shopping) { Rejected.Add(TargetISID); ChooseNextProduct(); }
		else { UE_LOG(LogTemp,Warning,TEXT("Customer route failure phase=%d reason=%s"),int32(Phase),*MoveFailureReason); AbortVisit(TEXT("Customer route is blocked")); }
		return;
	}
	switch (Phase)
	{
	case EShopVisitPhase::Entering:
		if (Arrived()) { SetPhase(EShopVisitPhase::GettingBucket,TEXT("Getting shopping bucket")); MoveTo(Bucket->GetActorLocation()); }
		break;
	case EShopVisitPhase::GettingBucket:
		if (Arrived())
		{
			if (!Bucket.IsValid()) { AbortVisit(TEXT("Shopping bucket unavailable")); break; }
			CarryBucket(); ChooseNextProduct();
		}
		break;
	case EShopVisitPhase::Shopping:
		if (Arrived())
		{
			const auto* Record=Manager->FindISIDData(TargetISID); const int32 PID=Record ? Record->ProductID : 0;
			AActivePropActor* Prop=nullptr; auto* Pocket=NextPocket();
			if (Pocket && Record && Record->State!=EPropState::Held && !Record->RuntimeData.bInteractionLock && Record->PackedRuntimeData.bIsGrabbable
				&& Manager->ShopCheckout->IsAvailableColaStockISID(TargetISID)
				&& ShopInteractionGeometry::DistanceToCustomer(GetOwner(),Manager->GetTransform(TargetISID).GetLocation())<300
				&& Manager->BeginGrab(TargetISID,GetOwner(),Pocket) && (Prop=Manager->GetActiveActorFromISID(TargetISID)))
			{
				Collected.Add(TargetISID); --Wanted.FindOrAdd(PID);
				if (Prop->PropMesh) { Prop->PropMesh->SetSimulatePhysics(false); Prop->PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
			}
			else Rejected.Add(TargetISID);
			ChooseNextProduct();
		}
		break;
	case EShopVisitPhase::Queuing:
	{
		const int32 Index=Flow->QueuePosition(this);
		if (Index==INDEX_NONE) { AbortVisit(TEXT("Queue assignment lost")); break; }
		// A stationary customer ahead can obstruct a valid path for an arbitrarily
		// long checkout. Waiting in line is not a failed shopping route.
		const double SinceMove=GetWorld()->GetTimeSeconds()-MoveStarted;
		if (SinceMove>=2 && (bMoveFailed || FVector::DistSquared2D(MoveGoal,Flow->QueueLocation(Index))>FMath::Square(120.f)
			|| (!Arrived() && SinceMove>MoveTimeoutSeconds))) MoveTo(Flow->QueueLocation(Index));
		if (Index!=0 || !Arrived()) break;
		auto* Scanner=Flow->GetScanner(); auto* Dock=Flow->GetBucketDock();
		if (!Scanner || !Dock || Cash->JoinScanner(Scanner)!=EShopCheckoutResult::Success) break;
		Bucket->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform); Bucket->SetActorLocationAndRotation(Dock->GetComponentLocation(),Dock->GetComponentRotation());
		for (int32 ID:Collected) if (auto* Prop=Manager->GetActiveActorFromISID(ID)) Prop->DetachForInteraction();
		SetPhase(EShopVisitPhase::WaitingForCashier,TEXT("Please scan my bottles and complete checkout"));
		break;
	}
	case EShopVisitPhase::WaitingForCashier:
	{
		FCompletedCheckoutReceipt Receipt;
		if (Cash->FindCompletedVisitReceipt(Receipt))
		{
			CheckoutID=Receipt.CheckoutID; SetPhase(EShopVisitPhase::CollectingPurchase,TEXT("Collecting purchased bottles"));
		}
		break;
	}
	case EShopVisitPhase::CollectingPurchase:
		if (CollectPaidGoods())
		{
			ReturnUnsoldGoods();
			if (!Cash->ReleaseStation(false)) break;
			Flow->ReleaseVisit(this);
			CarryBucket();
			SetPhase(EShopVisitPhase::ReturningBucket,TEXT("Returning empty bucket at exit")); MoveTo(Flow->BucketReturnPoint->GetActorLocation());
		}
		break;
	case EShopVisitPhase::ReturningBucket:
		if (Arrived()) { ReleaseBucket(); SetPhase(EShopVisitPhase::Departing,TEXT("Leaving with purchases")); MoveTo(Flow->DeparturePoint->GetActorLocation()); }
		break;
	case EShopVisitPhase::Departing:
		if (Arrived()) { SetPhase(EShopVisitPhase::Departed,TEXT("Visit completed")); GetWorld()->GetTimerManager().ClearTimer(VisitTimer); }
		break;
	default: break;
	}
}

bool UShopCustomerVisitComponent::CollectPaidGoods()
{
	for (const auto& Case:Manager->ShopCheckout->PendingExpiryCases)
		if (Case.CheckoutID==CheckoutID && Case.Choice==EShopCompensationChoice::Unselected) { Status=TEXT("Choose expiry compensation to finish service"); OnRep_Phase(); GetOwner()->ForceNetUpdate(); return false; }
	const auto Goods=Manager->ShopCheckout->GetCustomerOwnedStockISIDs(Cash->CustomerID);
	for (int32 ID:Goods)
	{
		const auto* Record=Manager->FindISIDData(ID);
		if (!Record) { Status=TEXT("A purchased bottle is missing"); OnRep_Phase(); GetOwner()->ForceNetUpdate(); return false; }
		if (Record->State==EPropState::Held && Record->RuntimeData.InteractionOwner.IsValid() && Record->RuntimeData.InteractionOwner.Get()!=GetOwner())
		{ Status=TEXT("Please release my purchased bottles"); OnRep_Phase(); GetOwner()->ForceNetUpdate(); return false; }
	}
	int32 Index=0;
	for (int32 ID:Goods)
	{
		if (!Manager->BeginGrab(ID,GetOwner(),GetOwner()->GetRootComponent())) return false;
		if (auto* Prop=Manager->GetActiveActorFromISID(ID))
		{
			Prop->SetActorRelativeLocation(FVector(40+(Index%2)*20,(Index/2)*20,15));
			if (Prop->PropMesh) { Prop->PropMesh->SetSimulatePhysics(false); Prop->PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
		}
		++Index;
	}
	return true;
}

bool UShopCustomerVisitComponent::AcceptsInvoiceItems(const TArray<FCheckoutItemQuote>& Items) const
{
	if (Phase!=EShopVisitPhase::WaitingForCashier || !Manager.IsValid() || Items.Num()!=Collected.Num()) return false;
	TMap<FIntPoint,int32> Required;
	for (int32 ID:Collected)
	{
		FShopProductScanData Data;
		if (!Manager->GetProductScanData(ID,Data)) return false;
		++Required.FindOrAdd(FIntPoint(Data.ContentID,Data.NominalPortions));
	}
	for (const auto& Item:Items)
	{
		auto* Count=Required.Find(FIntPoint(Item.ContentID,Item.Snapshot.NominalPortions));
		if (!Count || *Count<=0) return false;
		--*Count;
	}
	return true;
}
void UShopCustomerVisitComponent::ReturnUnsoldGoods()
{
	if (!Manager.IsValid()) return;
	for (int32 ID:Collected)
	{
		FShopStockEntry Stock;
		if (!Manager->ShopCheckout->GetStockEntry(ID,Stock) || Stock.CustomerOwner.IsValid()) continue;
		if (auto* Prop=Manager->GetActiveActorFromISID(ID))
		{
			const auto* Record=Manager->FindISIDData(ID);
			if (Record && Record->RuntimeData.InteractionOwner.Get()==GetOwner()) Prop->DetachForInteraction();
			Manager->ConvertASMToISM(ID,EPropState::AtRest,true,true);
		}
		Manager->InitializeInstanceTransform(ID,Stock.StockTransform);
	}
}
void UShopCustomerVisitComponent::ReleaseBucket()
{
	if (Flow.IsValid() && Bucket.IsValid()) Flow->ReturnBucket(Bucket.Get(),this);
	Bucket.Reset();
}
void UShopCustomerVisitComponent::AbortVisit(const FString& Reason)
{
	if (!GetOwner()->HasAuthority() || bAborting) return;
	TGuardValue<bool> Guard(bAborting,true);
	if (Cash.IsValid() && !Cash->ReleaseStation(true)) { Status=Reason+TEXT("; resolve checkout compensation"); OnRep_Phase(); return; }
	if (auto* Pawn=Cast<APawn>(GetOwner())) if (auto* AI=Cast<AAIController>(Pawn->GetController())) AI->StopMovement();
	ReturnUnsoldGoods(); ReleaseBucket();
	if (Flow.IsValid()) Flow->ReleaseVisit(this);
	SetPhase(EShopVisitPhase::Aborted,Reason); GetWorld()->GetTimerManager().ClearTimer(VisitTimer);
	UE_LOG(LogTemp,Warning,TEXT("Shop visit aborted for %s: %s"),*GetNameSafe(GetOwner()),*Reason);
}
bool UShopCustomerVisitComponent::RetireVisit()
{
	if (!GetOwner()->HasAuthority() || !Manager.IsValid() || !Cash.IsValid()
		|| (Phase!=EShopVisitPhase::Departed && (Phase!=EShopVisitPhase::Aborted || CheckoutID.IsValid()))) return false;
	if (!Cash->ReleaseStation(true)) return false;
	// Financial stock/receipt records remain immutable; only the departed customer's world representations retire.
	for (int32 ID:Manager->ShopCheckout->GetCustomerOwnedStockISIDs(Cash->CustomerID))
	{
		if (const auto* Unique=Manager->FindUniqueData(ID))
		{
			const int32 Cap=Unique->EventData.AttachedCapISID;
			if (Cap>0) Manager->ReleaseInstanceToEmpty(Cap);
		}
		Manager->ReleaseInstanceToEmpty(ID);
	}
	if (!Cash->ResetVisit()) return false;
	Collected.Empty(); Wanted.Empty(); CheckoutID.Invalidate();
	SetPhase(EShopVisitPhase::Inactive,TEXT("")); return true;
}
void UShopCustomerVisitComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	auto* ComponentWorld=GetWorld();
	if (ComponentWorld) ComponentWorld->GetTimerManager().ClearTimer(VisitTimer);
	if (ComponentWorld && GetOwner() && GetOwner()->HasAuthority() && !ComponentWorld->bIsTearingDown)
	{
		if (Cash.IsValid()) Cash->ReleaseStation(true);
		ReturnUnsoldGoods(); ReleaseBucket(); if (Flow.IsValid()) Flow->ReleaseVisit(this);
		if (Manager.IsValid() && Cash.IsValid())
		{
			for (int32 ID:Manager->ShopCheckout->GetCustomerOwnedStockISIDs(Cash->CustomerID)) if (auto* Prop=Manager->GetActiveActorFromISID(ID)) if (Prop->InteractionOwner==GetOwner()) Prop->DetachForInteraction();
			Manager->ShopCheckout->ReclaimCustomerCoins(Cash->CustomerID,Manager->ShopCheckout->GetCustomerCoinISIDs(Cash->CustomerID));
		}
	}
	Super::EndPlay(Reason);
}
