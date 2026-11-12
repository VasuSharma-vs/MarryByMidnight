#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractableProp.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopProcurementTest,"MarryByMidnight.Shop.Procurement",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopProcurementTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World = UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Manager = World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	auto* Product = LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/Assest/DataAssets/DA_EmptyBottel.DA_EmptyBottel"));
	if (!TestNotNull(TEXT("Real empty-bottle product exists"),Product) || !TestNotNull(TEXT("Manager owns procurement service"),Manager->ShopSupplies.Get())) return false;
	auto* Catalog = NewObject<UShopSupplyCatalog>(Manager);
	FShopSupplyOffer Offer; Offer.OfferID=TEXT("EmptyGlass"); Offer.Product=Product; Offer.UnitCostCents=400;
	Catalog->Offers.Add(Offer); Manager->ShopSupplies->Catalog=Catalog;
	TArray<int32> Delivered;
	const FTransform Delivery(FRotator::ZeroRotator,FVector(0,0,200),FVector::OneVector);
	TestEqual(TEXT("New game bank remains zero"),Manager->SharedWallet->GetWalletState().BalanceCents,int64(0));
	TestEqual(TEXT("Supplies cannot be bought without funds"),Manager->ShopSupplies->Purchase(FGuid::NewGuid(),Offer.OfferID,3,Delivery,Delivered),EShopPurchaseResult::InsufficientFunds);
	TestTrue(TEXT("Rejected purchase delivers nothing"),Delivered.IsEmpty());
	Manager->SharedWallet->Credit(FGuid::NewGuid(),10000,TEXT("IsolatedProcurementTestFunding"));
	const FGuid PurchaseID=FGuid::NewGuid();
	TestEqual(TEXT("Funded purchase succeeds"),Manager->ShopSupplies->Purchase(PurchaseID,Offer.OfferID,3,Delivery,Delivered),EShopPurchaseResult::Success);
	TestEqual(TEXT("Three physical bottle identities are delivered"),Delivered.Num(),3);
	TSet<int32> UniqueIDs(Delivered);
	TestEqual(TEXT("Purchased identities are distinct"),UniqueIDs.Num(),3);
	for (int32 I=0;I<Delivered.Num();++I)
		for (int32 J=I+1;J<Delivered.Num();++J)
			TestFalse(TEXT("Native sized bottle deliveries do not intersect"),
				Product->StaticMesh->GetBoundingBox().TransformBy(Manager->GetTransform(Delivered[I])).Intersect(
				Product->StaticMesh->GetBoundingBox().TransformBy(Manager->GetTransform(Delivered[J]))));
	for (int32 ID : Delivered)
	{
		const auto* Unique=Manager->FindUniqueData(ID);
		if (!TestNotNull(TEXT("Purchased bottle has persistent state"),Unique)) return false;
		TestEqual(TEXT("Purchased bottle is empty"),Unique->EventData.ContentLevel,0.f);
		TestTrue(TEXT("Purchased bottle retains clean condition"),Unique->EventData.bClean);
		const auto* Actor=Manager->GetActiveActorFromISID(ID);
		TestTrue(TEXT("Delivered supply starts as a physical active object"),Actor && Actor->PropMesh->IsSimulatingPhysics());
	}
	TestEqual(TEXT("Bank debits exact configured cost"),Manager->SharedWallet->GetWalletState().BalanceCents,int64(8800));
	const int64 Revision=Manager->SharedWallet->GetWalletState().Revision;
	TestEqual(TEXT("Retry does not purchase twice"),Manager->ShopSupplies->Purchase(PurchaseID,Offer.OfferID,3,Delivery,Delivered),EShopPurchaseResult::AlreadyPurchased);
	TestTrue(TEXT("Retry creates no new deliverables"),Delivered.IsEmpty());
	TestEqual(TEXT("Retry keeps ledger revision"),Manager->SharedWallet->GetWalletState().Revision,Revision);
	TestEqual(TEXT("Reusing purchase ID for another quantity rejected"),Manager->ShopSupplies->Purchase(PurchaseID,Offer.OfferID,2,Delivery,Delivered),EShopPurchaseResult::OperationConflict);
	TestEqual(TEXT("Negative quantity rejected"),Manager->ShopSupplies->Purchase(FGuid::NewGuid(),Offer.OfferID,-1,Delivery,Delivered),EShopPurchaseResult::InvalidQuantity);
	Manager->ShopSupplies->bUseDeliveryArea=true; Manager->ShopSupplies->DeliveryColumns=2; Manager->ShopSupplies->DeliveryRows=1;
	const FTransform Area(FVector(1000,0,200)); TArray<int32> First,Second,Blocked;
	TestEqual(TEXT("First independent order occupies a delivery slot"),Manager->ShopSupplies->Purchase(FGuid::NewGuid(),Offer.OfferID,1,Area,First),EShopPurchaseResult::Success);
	TestEqual(TEXT("Second independent order finds another slot"),Manager->ShopSupplies->Purchase(FGuid::NewGuid(),Offer.OfferID,1,Area,Second),EShopPurchaseResult::Success);
	if (First.Num()!=1 || Second.Num()!=1) return false;
	TestFalse(TEXT("Independent orders never overlap their physical bottles"),Product->StaticMesh->GetBoundingBox().TransformBy(Manager->GetTransform(First[0])).Intersect(Product->StaticMesh->GetBoundingBox().TransformBy(Manager->GetTransform(Second[0]))));
	const int64 BeforeFull=Manager->SharedWallet->GetWalletState().BalanceCents;
	TestEqual(TEXT("Full delivery area rejects another order"),Manager->ShopSupplies->Purchase(FGuid::NewGuid(),Offer.OfferID,1,Area,Blocked),EShopPurchaseResult::DeliveryFailed);
	TestEqual(TEXT("Full delivery area cannot charge the bank"),Manager->SharedWallet->GetWalletState().BalanceCents,BeforeFull);
	TestTrue(TEXT("Failed delivery has no partial goods"),Blocked.IsEmpty());
	Manager->InitializeInstanceTransform(First[0],FTransform(FVector(2000,0,200)));
	TestEqual(TEXT("Removing goods makes the physical slot reusable"),Manager->ShopSupplies->Purchase(FGuid::NewGuid(),Offer.OfferID,1,Area,Blocked),EShopPurchaseResult::Success);
	const auto Saved=Manager->SharedWallet->ExportCommittedState();
	TestTrue(TEXT("Purchase ledger restores"),Manager->SharedWallet->RestoreCommittedState(Saved));
	TestEqual(TEXT("Purchase retry remains idempotent after ledger restore"),Manager->ShopSupplies->Purchase(PurchaseID,Offer.OfferID,3,Delivery,Delivered),EShopPurchaseResult::AlreadyPurchased);
	Catalog->Offers.Add(Offer);
	int64 Quote=0;
	TestFalse(TEXT("Ambiguous offer IDs rejected"),Manager->ShopSupplies->Quote(Offer.OfferID,1,Quote));
	return true;
}
#endif
