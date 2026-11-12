#include "Shop/ShopShelfStockComponent.h"
#include "Shop/ShopInstanceVolumeQuery.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Props/InteractablePropManager.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

UShopShelfStockComponent::UShopShelfStockComponent() { PrimaryComponentTick.bCanEverTick=false; }
void UShopShelfStockComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!StockVolume)
	{
		TArray<UBoxComponent*> Boxes; GetOwner()->GetComponents(Boxes);
		for (auto* Box:Boxes) if (Box->ComponentHasTag(TEXT("ShopShelfStock"))) { StockVolume=Box; break; }
	}
	if (GetOwner()->HasAuthority() && GetNetMode()!=NM_Client) GetWorld()->GetTimerManager().SetTimer(RefreshTimer,this,&UShopShelfStockComponent::RefreshStock,.5f,true);
}
void UShopShelfStockComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* ComponentWorld=GetWorld()) ComponentWorld->GetTimerManager().ClearTimer(RefreshTimer);
	Super::EndPlay(Reason);
}
void UShopShelfStockComponent::RefreshStock()
{
	if (!GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return;
	auto* Manager=APropManager::Get(this);
	if (!Manager || !Manager->ShopCheckout || !Manager->ShopSupplies) return;
	for (int32 ID:ShopInstanceVolumeQuery::Query(StockVolume))
	{
		FShopStockEntry Existing;
		if (Manager->ShopCheckout->GetStockEntry(ID,Existing)) continue;
		const auto* Unique=Manager->FindUniqueData(ID); const auto* Record=Manager->FindISIDData(ID);
		const auto* Def=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
		if (!Unique || !Def || Def->bIsBottleCap || !Def->InteractionProfile.bCanConsume || Unique->EventData.ContentLevel<=0
			|| Unique->EventData.CapSealed<1 || !Manager->ShopSupplies->IsNormalContent(Unique->EventData.ContentID)) continue;
		Manager->ShopCheckout->RegisterShopStock(ID,0,Manager->GetTransform(ID));
	}
}
