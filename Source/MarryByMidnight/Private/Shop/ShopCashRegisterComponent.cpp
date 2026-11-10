#include "Shop/ShopCashRegisterComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Props/InteractablePropManager.h"
#include "Components/BoxComponent.h"
#include "Engine/OverlapResult.h"
#include "TimerManager.h"
#include "Misc/SecureHash.h"

UShopCashRegisterComponent::UShopCashRegisterComponent()
{
	PrimaryComponentTick.bCanEverTick = false; SetIsReplicatedByDefault(true);
}

void UShopCashRegisterComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetOwner()->HasAuthority() || GetOwner()->GetNetMode() == NM_Client) return;
	if (!RegisterID.IsValid())
	{
		FString Path = GetOwner()->GetPathName();
		// PIE package prefixes vary between peers; the authored object path is stable.
		int32 Start = Path.Find(TEXT("UEDPIE_"));
		while (Start != INDEX_NONE)
		{
			int32 End = Start + 7; while (End < Path.Len() && FChar::IsDigit(Path[End])) ++End;
			if (End < Path.Len() && Path[End] == '_') ++End;
			Path.RemoveAt(Start, End - Start); Start = Path.Find(TEXT("UEDPIE_"));
		}
		FGuid::ParseExact(FMD5::HashAnsiString(*Path), EGuidFormats::Digits, RegisterID);
	}
	if (!Scanner) Scanner = GetOwner()->FindComponentByClass<UShopScannerComponent>();
	TArray<UBoxComponent*> Boxes; GetOwner()->GetComponents(Boxes);
	for (auto* Box : Boxes)
	{
		if (!TenderVolume && (Box->GetFName() == TEXT("TenderVolume") || Box->ComponentHasTag(TEXT("ShopTender")))) TenderVolume = Box;
		if (!ChangeVolume && (Box->GetFName() == TEXT("ChangeVolume") || Box->ComponentHasTag(TEXT("ShopChange")))) ChangeVolume = Box;
	}
	GetWorld()->GetTimerManager().SetTimer(PollTimer, FTimerDelegate::CreateWeakLambda(this, [this] { RefreshPayment(); }), .2f, true);
}

TSet<int32> UShopCashRegisterComponent::QueryCoinsInVolume(const UBoxComponent* Box)
{
	TSet<int32> IDs;
	if (!IsValid(Box) || !Box->GetOwner()->HasAuthority() || Box->GetNetMode() == NM_Client || !Box->GetWorld() || !Box->GetWorld()->GetPhysicsScene()) return IDs;
	APropManager* Manager = APropManager::Get(Box);
	if (!Manager) return IDs;
	TArray<FOverlapResult> Hits; FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic); Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	Box->GetWorld()->OverlapMultiByObjectType(Hits, Box->GetComponentLocation(), Box->GetComponentQuat(), Objects,
		FCollisionShape::MakeBox(Box->GetScaledBoxExtent()), FCollisionQueryParams(SCENE_QUERY_STAT(ShopCash), false, Box->GetOwner()));
	for (const auto& Hit : Hits)
	{
		const int32 ID = Manager->GetISID(Hit.GetComponent(), Hit.GetItemIndex());
		FShopCashCoin Coin; if (Manager->ShopCheckout->GetCoin(ID, Coin)) IDs.Add(ID);
	}
	return IDs;
}

EShopCheckoutResult UShopCashRegisterComponent::RefreshPayment()
{
	if (!GetOwner()->HasAuthority() || GetOwner()->GetNetMode() == NM_Client) return EShopCheckoutResult::NotAuthority;
	APropManager* Manager = APropManager::Get(this);
	if (!Manager || !Manager->ShopCheckout || !Scanner || !TenderVolume || !ChangeVolume || TenderVolume == ChangeVolume || !RegisterID.IsValid()) return EShopCheckoutResult::CashUnavailable;
	const auto State = Scanner->GetScannerState();
	if (State.Phase != EShopScannerPhase::Checkout)
	{
		if (ActiveCheckoutID.IsValid()) Manager->ShopCheckout->ReleasePhysicalCash(ActiveCheckoutID);
		ActiveCheckoutID.Invalidate(); return EShopCheckoutResult::NotFound;
	}
	ActiveCheckoutID = State.Checkout.CheckoutID;
	TArray<int32> Tender, Change;
	for (int32 ID : QueryCoinsInVolume(TenderVolume))
	{
		FShopCashCoin Coin; Manager->ShopCheckout->GetCoin(ID, Coin);
		if (Coin.CustomerOwner == State.Checkout.CustomerID) Tender.Add(ID);
	}
	for (int32 ID : QueryCoinsInVolume(ChangeVolume))
	{
		FShopCashCoin Coin; Manager->ShopCheckout->GetCoin(ID, Coin);
		if (!Coin.CustomerOwner.IsValid()) Change.Add(ID);
	}
	Tender.Sort(); Change.Sort();
	const auto Result = Manager->ShopCheckout->ReconcilePhysicalCash(ActiveCheckoutID, RegisterID, Tender, Change);
	if (Result != EShopCheckoutResult::Success)
	{
		// A coin stolen by another station or overlapping trays must invalidate the previous count.
		Manager->ShopCheckout->ReconcilePhysicalCash(ActiveCheckoutID, RegisterID, {}, {});
	}
	return Result;
}

void UShopCashRegisterComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorld()->GetTimerManager().ClearTimer(PollTimer);
	if (GetOwner()->HasAuthority()) if (auto* Manager = APropManager::Get(this)) if (Manager->ShopCheckout) Manager->ShopCheckout->ReleasePhysicalCash(ActiveCheckoutID);
	Super::EndPlay(Reason);
}
