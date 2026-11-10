#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Props/InteractablePropManager.h"
#include "Components/PrimitiveComponent.h"
#include "Net/UnrealNetwork.h"
#include "Templates/UnrealTemplate.h"

UShopScannerComponent::UShopScannerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

bool UShopScannerComponent::IsServer() const
{
	return GetOwner() && GetOwner()->HasAuthority() && GetOwner()->GetNetMode() != NM_Client;
}

void UShopScannerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UShopScannerComponent, ScannerState);
}

APropManager* UShopScannerComponent::ResolveManager()
{
	if (!Manager.IsValid()) Manager = APropManager::Get(this);
	return Manager.Get();
}

UShopCheckoutComponent* UShopScannerComponent::ResolveCheckout()
{
	APropManager* CurrentManager = ResolveManager();
	UShopCheckoutComponent* Checkout = CurrentManager ? CurrentManager->ShopCheckout.Get() : nullptr;
	if (Checkout != BoundCheckout.Get())
	{
		if (BoundCheckout.IsValid()) BoundCheckout->OnCheckoutsChanged.RemoveDynamic(this, &UShopScannerComponent::SynchronizeCheckout);
		BoundCheckout = Checkout;
		if (Checkout && IsServer()) Checkout->OnCheckoutsChanged.AddUniqueDynamic(this, &UShopScannerComponent::SynchronizeCheckout);
	}
	return Checkout;
}

void UShopScannerComponent::BeginPlay()
{
	Super::BeginPlay();
	if (IsServer()) ResolveCheckout();
}

void UShopScannerComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (BoundCheckout.IsValid())
	{
		BoundCheckout->OnCheckoutsChanged.RemoveDynamic(this, &UShopScannerComponent::SynchronizeCheckout);
		if (IsServer() && ScannerState.Phase == EShopScannerPhase::Checkout) BoundCheckout->CancelCheckout(ScannerState.Checkout.CheckoutID);
	}
	Super::EndPlay(Reason);
}

void UShopScannerComponent::OnRep_ScannerState() { PublishState(); }

void UShopScannerComponent::PublishState()
{
	if (bNotifying) return;
	TGuardValue<bool> Guard(bNotifying, true);
	OnScannerStateChanged.Broadcast(ScannerState);
	if (IsServer()) GetOwner()->ForceNetUpdate();
}

EShopCheckoutResult UShopScannerComponent::SetResult(EShopCheckoutResult Result)
{
	ScannerState.LastResult = Result;
	PublishState();
	return Result;
}

EShopCheckoutResult UShopScannerComponent::ScanProduct(int32 ISID)
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bNotifying || ScannerState.Phase != EShopScannerPhase::Scanning) return EShopCheckoutResult::Unavailable;
	APropManager* CurrentManager = ResolveManager();
	FShopProductScanData Scan;
	if (!CurrentManager || !CurrentManager->GetProductScanData(ISID, Scan))
	{
		ScannerState.LastScan = FShopProductScanData();
		return SetResult(EShopCheckoutResult::InvalidProduct);
	}
	ScannerState.LastScan = Scan;
	const FVector ScannerLocation = GetOwner()->GetActorLocation();
	const FVector ProductLocation = CurrentManager->GetTransform(ISID).GetLocation();
	if (ScannerLocation.ContainsNaN() || ProductLocation.ContainsNaN() || !FMath::IsFinite(MaxScanDistance) || MaxScanDistance <= 0 || FVector::DistSquared(ScannerLocation, ProductLocation) > FMath::Square(MaxScanDistance)) return SetResult(EShopCheckoutResult::Unavailable);
	if (Scan.Price.bExpired) return SetResult(EShopCheckoutResult::ExpiredProduct);
	UShopCheckoutComponent* Checkout = ResolveCheckout();
	if (!Checkout || !Checkout->IsAvailableShopStock(ISID)) return SetResult(EShopCheckoutResult::Unavailable);
	FShopCheckoutView& Cart = ScannerState.Checkout;
	FCheckoutItemQuote* Existing = Cart.Items.FindByPredicate([ISID](const auto& Q) { return Q.ISID == ISID; });
	if (!Existing && Cart.Items.Num() >= FMath::Clamp(MaxCartItems, 1, 64)) return SetResult(EShopCheckoutResult::Unavailable);
	const int64 PreviousPrice = Existing ? Existing->SaleCents : 0;
	if (Cart.TotalCents - PreviousPrice > MAX_int64 - Scan.Price.SaleCents) return SetResult(EShopCheckoutResult::InvalidRequest);
	Cart.TotalCents = Cart.TotalCents - PreviousPrice + Scan.Price.SaleCents;
	FCheckoutItemQuote Quote;
	Quote.ISID = ISID; Quote.ProductID = Scan.ProductID; Quote.ContentID = Scan.ContentID;
	Quote.EventSequence = Scan.EventSequence; Quote.SaleCents = Scan.Price.SaleCents;
	if (Existing) *Existing = Quote;
	else Cart.Items.Add(Quote);
	return SetResult(EShopCheckoutResult::Success);
}

EShopCheckoutResult UShopScannerComponent::ScanOverlap(UPrimitiveComponent* Component, int32 BodyIndex)
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (!IsValid(Component) || Component->GetWorld() != GetWorld()) return EShopCheckoutResult::InvalidRequest;
	APropManager* CurrentManager = ResolveManager();
	return CurrentManager ? ScanProduct(CurrentManager->GetISID(Component, BodyIndex)) : EShopCheckoutResult::Unavailable;
}

EShopCheckoutResult UShopScannerComponent::RemoveProduct(int32 ISID)
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bNotifying || ScannerState.Phase != EShopScannerPhase::Scanning) return EShopCheckoutResult::Unavailable;
	FShopCheckoutView& Cart = ScannerState.Checkout;
	const int32 Index = Cart.Items.IndexOfByPredicate([ISID](const auto& Q) { return Q.ISID == ISID; });
	if (Index == INDEX_NONE) return SetResult(EShopCheckoutResult::NotFound);
	Cart.TotalCents -= Cart.Items[Index].SaleCents;
	Cart.Items.RemoveAt(Index);
	return SetResult(EShopCheckoutResult::Success);
}

EShopCheckoutResult UShopScannerComponent::AssignCustomer(FGuid CustomerID)
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bNotifying || ScannerState.Phase != EShopScannerPhase::Scanning) return EShopCheckoutResult::Unavailable;
	if (!CustomerID.IsValid()) return SetResult(EShopCheckoutResult::InvalidRequest);
	ScannerState.Checkout.CustomerID = CustomerID;
	return SetResult(EShopCheckoutResult::Success);
}

EShopCheckoutResult UShopScannerComponent::BeginCheckout()
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bNotifying) return EShopCheckoutResult::Unavailable;
	if (ScannerState.Phase == EShopScannerPhase::Completed) return SetResult(EShopCheckoutResult::AlreadyCompleted);
	if (ScannerState.Phase == EShopScannerPhase::Checkout) return SetResult(EShopCheckoutResult::Success);
	if (!ScannerState.Checkout.CustomerID.IsValid() || ScannerState.Checkout.Items.IsEmpty()) return SetResult(EShopCheckoutResult::InvalidRequest);
	UShopCheckoutComponent* Checkout = ResolveCheckout();
	if (!Checkout) return SetResult(EShopCheckoutResult::Unavailable);
	TArray<int32> IDs;
	for (const auto& Item : ScannerState.Checkout.Items) IDs.Add(Item.ISID);
	const FGuid ID = FGuid::NewGuid();
	const EShopCheckoutResult Result = Checkout->BeginCheckout(ID, ScannerState.Checkout.CustomerID, IDs);
	if (Result == EShopCheckoutResult::Success)
	{
		ScannerState.Phase = EShopScannerPhase::Checkout;
		Checkout->GetCheckoutView(ID, ScannerState.Checkout);
	}
	return SetResult(Result);
}

EShopCheckoutResult UShopScannerComponent::ReceiveTender(FGuid ID, int64 Amount) { return ApplyCash(ID, Amount, false); }
EShopCheckoutResult UShopScannerComponent::ReturnChange(FGuid ID, int64 Amount) { return ApplyCash(ID, Amount, true); }

EShopCheckoutResult UShopScannerComponent::ApplyCash(FGuid ID, int64 Amount, bool bChange)
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bNotifying || ScannerState.Phase != EShopScannerPhase::Checkout) return EShopCheckoutResult::Unavailable;
	UShopCheckoutComponent* Checkout = ResolveCheckout();
	if (!Checkout) return SetResult(EShopCheckoutResult::Unavailable);
	const FGuid Transaction = ScannerState.Checkout.CheckoutID;
	const EShopCheckoutResult Result = bChange ? Checkout->ReturnChange(Transaction, ID, Amount) : Checkout->ReceiveTender(Transaction, ID, Amount);
	SynchronizeCheckout();
	return SetResult(Result);
}

EShopCheckoutResult UShopScannerComponent::CompleteCheckout()
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bNotifying) return EShopCheckoutResult::Unavailable;
	if (ScannerState.Phase == EShopScannerPhase::Completed) return SetResult(EShopCheckoutResult::AlreadyCompleted);
	if (ScannerState.Phase != EShopScannerPhase::Checkout) return SetResult(EShopCheckoutResult::InvalidRequest);
	UShopCheckoutComponent* Checkout = ResolveCheckout();
	if (!Checkout) return SetResult(EShopCheckoutResult::Unavailable);
	const EShopCheckoutResult Result = Checkout->CompleteCheckout(ScannerState.Checkout.CheckoutID);
	SynchronizeCheckout();
	return SetResult(Result);
}

EShopCheckoutResult UShopScannerComponent::ClearCart()
{
	if (!IsServer()) return EShopCheckoutResult::NotAuthority;
	if (bNotifying) return EShopCheckoutResult::Unavailable;
	const FGuid Customer = ScannerState.Phase == EShopScannerPhase::Completed ? FGuid() : ScannerState.Checkout.CustomerID;
	if (ScannerState.Phase == EShopScannerPhase::Checkout)
	{
		UShopCheckoutComponent* Checkout = ResolveCheckout();
		if (!Checkout || !Checkout->CancelCheckout(ScannerState.Checkout.CheckoutID)) return SetResult(EShopCheckoutResult::Unavailable);
	}
	ScannerState = FShopScannerState();
	ScannerState.Checkout.CustomerID = Customer;
	return SetResult(EShopCheckoutResult::Success);
}

void UShopScannerComponent::SynchronizeCheckout()
{
	if (!IsServer() || bNotifying || ScannerState.Phase != EShopScannerPhase::Checkout || !BoundCheckout.IsValid()) return;
	FShopCheckoutView View;
	if (BoundCheckout->GetCheckoutView(ScannerState.Checkout.CheckoutID, View)) ScannerState.Checkout = View;
	else
	{
		FCompletedCheckoutReceipt Receipt;
		if (BoundCheckout->GetCompletedReceipt(ScannerState.Checkout.CheckoutID, Receipt)) ScannerState.Phase = EShopScannerPhase::Completed;
		else
		{
			const FGuid Customer = ScannerState.Checkout.CustomerID;
			ScannerState = FShopScannerState();
			ScannerState.Checkout.CustomerID = Customer;
		}
	}
	PublishState();
}
