#include "Shop/ShopCustomerFlow.h"
#include "Shop/ShopCustomerVisitComponent.h"
#include "Shop/ShopCustomerCashComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "Props/InteractablePropManager.h"
#include "GameFramework/Character.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"

AShopCustomerFlow::AShopCustomerFlow() { PrimaryActorTick.bCanEverTick=false; SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root"))); }
void AShopCustomerFlow::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority() || GetNetMode()==NM_Client) return;
	for (AActor* Bucket:Buckets) if (IsValid(Bucket)) BucketHomes.Add(Bucket,Bucket->GetActorTransform());
	GetWorldTimerManager().SetTimer(ArrivalTimer,FTimerDelegate::CreateWeakLambda(this,[this]{TryArrival();}),FMath::Max(1.f,ArrivalInterval),true,1.f);
}
void AShopCustomerFlow::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* ComponentWorld=GetWorld()) ComponentWorld->GetTimerManager().ClearTimer(ArrivalTimer);
	Super::EndPlay(Reason);
}
UShopScannerComponent* AShopCustomerFlow::GetScanner() const { return IsValid(CheckoutStation) ? CheckoutStation->FindComponentByClass<UShopScannerComponent>() : nullptr; }
USceneComponent* AShopCustomerFlow::GetBucketDock() const
{
	if (!IsValid(CheckoutStation)) return nullptr;
	TArray<USceneComponent*> Parts; CheckoutStation->GetComponents(Parts);
	for (auto* Part:Parts) if (Part->ComponentHasTag(TEXT("ShopBucketDock"))) return Part;
	return nullptr;
}
bool AShopCustomerFlow::StockIsClaimed(int32 ID) const
{
	const auto* Claimant=StockClaims.Find(ID); return Claimant && Claimant->IsValid();
}
bool AShopCustomerFlow::ClaimStock(int32 ID,UShopCustomerVisitComponent* Visit)
{
	auto* Manager=APropManager::Get(this);
	if (!HasAuthority() || !IsValid(Visit) || Visit->GetFlow()!=this || !Manager || !Manager->ShopCheckout || !Manager->ShopCheckout->IsAvailableColaStockISID(ID)) return false;
	if (const auto* Claimant=StockClaims.Find(ID)) if (Claimant->IsValid() && Claimant->Get()!=Visit) return false;
	StockClaims.Add(ID,Visit); return true;
}
void AShopCustomerFlow::ReleaseClaims(UShopCustomerVisitComponent* Visit)
{
	for (auto It=StockClaims.CreateIterator(); It; ++It) if (!It.Value().IsValid() || It.Value().Get()==Visit) It.RemoveCurrent();
}
int32 AShopCustomerFlow::QueuePosition(UShopCustomerVisitComponent* Visit)
{
	Queue.RemoveAll([](const auto& Item){return !Item.IsValid();}); return Queue.IndexOfByPredicate([Visit](const auto& Item){return Item.Get()==Visit;});
}
FVector AShopCustomerFlow::QueueLocation(int32 Index) const
{
	const auto* Default=CustomerClass ? CustomerClass->GetDefaultObject<ACharacter>() : nullptr;
	const float Spacing=Default ? FMath::Max(140.f,Default->GetCapsuleComponent()->GetScaledCapsuleRadius()*2+60) : 140.f;
	return IsValid(QueuePoint) ? QueuePoint->GetActorLocation()-QueuePoint->GetActorForwardVector()*FMath::Max(0,Index)*Spacing : GetActorLocation();
}
void AShopCustomerFlow::JoinQueue(UShopCustomerVisitComponent* Visit) { if (HasAuthority() && IsValid(Visit)) Queue.AddUnique(Visit); }
void AShopCustomerFlow::LeaveQueue(UShopCustomerVisitComponent* Visit) { Queue.RemoveAll([Visit](const auto& Item){return !Item.IsValid() || Item.Get()==Visit;}); }
void AShopCustomerFlow::ReturnBucket(AActor* Bucket,UShopCustomerVisitComponent* Visit)
{
	const auto* Claim=BucketClaims.Find(Bucket);
	if (!HasAuthority() || !IsValid(Bucket) || !Claim || Claim->Get()!=Visit) return;
	Bucket->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (const auto* Home=BucketHomes.Find(Bucket)) Bucket->SetActorTransform(*Home,false,nullptr,ETeleportType::TeleportPhysics);
	BucketClaims.Remove(Bucket); Bucket->ForceNetUpdate();
}
void AShopCustomerFlow::ReleaseVisit(UShopCustomerVisitComponent* Visit) { LeaveQueue(Visit); ReleaseClaims(Visit); }

bool AShopCustomerFlow::TryArrival()
{
	if (!HasAuthority() || GetNetMode()==NM_Client) return false;
	auto* Manager=APropManager::Get(this);
	if (!Manager || !Manager->PropManagerReady || !Manager->ShopSupplies)
	{ ArrivalStatus=TEXT("Waiting for Prop Manager and supply service"); return false; }
	if (!Manager->ShopSupplies->InitializeCatalog()) { ArrivalStatus=TEXT("Supply catalog initialization failed"); return false; }
	if (!CustomerClass || !GetScanner() || !GetBucketDock() || !SpawnPoint || !EntrancePoint || !QueuePoint || !BucketReturnPoint || !DeparturePoint)
	{ ArrivalStatus=TEXT("Missing customer, checkout, bucket dock or visit route configuration"); return false; }
	auto* Nav=UNavigationSystemV1::GetCurrent(GetWorld()); FNavLocation Entrance;
	const auto* CustomerDefault=CustomerClass->GetDefaultObject<ACharacter>();
	const FVector EntranceFloor=EntrancePoint->GetActorLocation()-FVector(0,0,CustomerDefault->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	if (!Nav || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld())
		|| !Nav->ProjectPointToNavigation(EntranceFloor,Entrance,FVector(400,400,200)))
	{
		const auto* Data=Nav ? Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr;
		ArrivalStatus=FString::Printf(TEXT("Waiting for walkable giant entrance navigation: bounds=%d activeTiles=%d building=%d"),
			Nav ? Nav->GetNavigationBounds().Num() : 0,Data ? Data->GetNumActiveTiles() : 0,
			Nav && UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()));
		return false;
	}
	Customers.RemoveAll([](const auto& Customer){return !IsValid(Customer);});
	for (ACharacter* Customer:Customers) if (auto* Visit=Customer->FindComponentByClass<UShopCustomerVisitComponent>())
		if (bRecycleDepartedCustomers && (Visit->Phase==EShopVisitPhase::Departed || Visit->Phase==EShopVisitPhase::Aborted)) Visit->RetireVisit();
	Queue.RemoveAll([](const auto& Item){return !Item.IsValid();});
	// A destroyed visitor cannot leave a station locked forever. Existing compensation gates still apply.
	if (Queue.IsEmpty())
	{
		auto* Scanner=GetScanner(); const auto State=Scanner->GetScannerState();
		if (State.Checkout.CustomerID.IsValid()) Scanner->ReleaseCustomer(State.Checkout.CustomerID,State.SessionID);
	}
	const auto Available=Manager->ShopCheckout->GetAvailableColaStockISIDs();
	if (!Available.ContainsByPredicate([this](int32 ID){return !StockIsClaimed(ID);}))
	{ ArrivalStatus=TEXT("Waiting for available shelf stock"); return false; }
	AActor* FreeBucket=nullptr;
	for (AActor* Bucket:Buckets) if (IsValid(Bucket))
	{
		const auto* Claim=BucketClaims.Find(Bucket);
		if (!Claim || !Claim->IsValid()) { FreeBucket=Bucket; break; }
	}
	if (!FreeBucket) { ArrivalStatus=TEXT("Waiting for a free shopping bucket"); return false; }
	ACharacter* Next=nullptr;
	for (ACharacter* Customer:Customers) if (const auto* Visit=Customer->FindComponentByClass<UShopCustomerVisitComponent>()) if (Visit->Phase==EShopVisitPhase::Inactive) { Next=Customer; break; }
	if (!Next && Customers.Num()<FMath::Clamp(MaxCustomers,1,8))
	{
		Next=GetWorld()->SpawnActor<ACharacter>(CustomerClass,SpawnPoint->GetActorTransform());
		if (Next) Customers.Add(Next);
	}
	if (!Next) { ArrivalStatus=TEXT("Waiting for a customer slot or clear spawn location"); return false; }
	auto* Visit=Next->FindComponentByClass<UShopCustomerVisitComponent>();
	if (!Visit) { ArrivalStatus=TEXT("Customer class has no visit component"); UE_LOG(LogTemp,Error,TEXT("Prototype customer %s has no native visit component"),*GetNameSafe(Next)); return false; }
	Next->SetActorLocationAndRotation(SpawnPoint->GetActorLocation(),SpawnPoint->GetActorRotation(),false,nullptr,ETeleportType::TeleportPhysics);
	Next->SetActorHiddenInGame(false); Next->SetActorEnableCollision(true);
	if (!Next->GetController()) Next->SpawnDefaultController();
	BucketClaims.Add(FreeBucket,Visit);
	if (Visit->StartVisit(this,FreeBucket)) { ArrivalStatus=TEXT("Customer visit started"); return true; }
	ArrivalStatus=TEXT("Customer could not begin its configured visit"); BucketClaims.Remove(FreeBucket); return false;
}
