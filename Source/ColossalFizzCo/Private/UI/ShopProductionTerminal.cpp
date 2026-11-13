#include "UI/ShopTerminalWidget.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "Props/InteractablePropManager.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"

namespace ShopTerminal { FString Money(int64 Cents); }

void UShopTerminalWidget::RefreshProduction(UShopProductionStationComponent* Production)
{
	auto* Manager=APropManager::Get(this);
	if (!Manager || !Manager->ShopSupplies) return;
	const auto* Catalog=Manager->ShopSupplies->Catalog.LoadSynchronous();
	const auto& View=Production->View;
	auto BottleDescription=[](const FShopProductionItem& Bottle)
	{
		return FString::Printf(TEXT("Bottle #%d — %.0f%% full, %.0f%% sealed"),Bottle.ISID,Bottle.ContentLevel*100,Bottle.CapSealed*100);
	};
	for (const auto& Bottle:View.Bottles)
		if (const auto* Label=ProductionBottleLabels.Find(Bottle.ISID); Label && IsValid(Label->Get()))
			(*Label)->SetText(FText::FromString(BottleDescription(Bottle)));
	FString Contents=View.SessionID.ToString()+FString::FromInt(View.bFilling);
	if (Production->bRequiresCooperativeCap)
	{
		Contents+=FString::Printf(TEXT("/manual/%d/%s/%s"),View.ManualBottleISID,*GetNameSafe(View.CapHolder),*GetNameSafe(View.CapRotator));
		const FString State=FString::Printf(TEXT("\nManual Wheel Rotator — two players, no electricity\nHold: %s | Turn: %s\nHolding force %.2f | Effectiveness %.0f%% | Rotation %.0f%%\nStart with G to claim and hold; your teammate taps R to claim and turn. Use role buttons to select between bottles. Escape leaves your role."),
			View.CapHolder ? (View.CapHolder==Actions->GetOwner() ? TEXT("you") : TEXT("teammate")) : TEXT("available"),
			View.CapRotator ? (View.CapRotator==Actions->GetOwner() ? TEXT("you") : TEXT("teammate")) : TEXT("available"),
			View.HoldingForce,View.HoldingEfficiency*100,View.RotationProgress*100);
		Summary->SetText(FText::FromString(Summary->GetText().ToString()+State));
	}
	auto Describe=[&](const TArray<FShopProductionItem>& Items)
	{
		// Quantities change during leakage/filling. Replacing buttons on each
		// change discards mouse-down events before mouse-up can claim a role.
		for (const auto& Item:Items) Contents+=FString::Printf(TEXT("/%d/%d/%d"),Item.ISID,Item.CapSealed>=1,Item.AttachedCapISID);
	};
	Describe(View.Bottles); Describe(View.Cylinders); Describe(View.Caps);
	if (Contents==LastContents) return;
	LastContents=Contents; Body->ClearChildren(); ProductionBottleLabels.Reset();
	AddText(TEXT("Normal Cola supplies and bottling\nPlace a cylinder, bottle and compatible cap in their marked input areas."));
	if (!View.SessionID.IsValid()) { AddText(TEXT("Waiting for station synchronization...")); AddButton(TEXT("Close"))->bClose=true; return; }
	auto Action=[&](const FString& Label, EShopProductionAction Kind, int32 Bottle, int32 Other=0)
	{
		auto* Button=AddButton(Label); Button->bProduction=true; Button->ProductionAction=Kind;
		Button->ISID=Bottle; Button->OtherISID=Other; Button->SessionID=View.SessionID;
	};
	if (View.bFilling) Action(TEXT("Stop filling"),EShopProductionAction::StopFilling,0);
	for (const auto& Bottle:View.Bottles)
	{
		ProductionBottleLabels.Add(Bottle.ISID,AddText(BottleDescription(Bottle)));
		if (!Bottle.AttachedCapISID && Bottle.CapSealed<=0)
		{
			if (!View.bFilling) for (const auto& Source:View.Cylinders)
				Action(TEXT("Fill from ")+Source.Name.ToString()+FString::Printf(TEXT(" #%d"),Source.ISID),EShopProductionAction::Fill,Bottle.ISID,Source.ISID);
			for (const auto& Cap:View.Caps) Action(FString::Printf(TEXT("Attach cap #%d"),Cap.ISID),EShopProductionAction::AttachCap,Bottle.ISID,Cap.ISID);
		}
		if (Bottle.AttachedCapISID)
		{
			if (Bottle.CapSealed<1)
			{
				if (Production->bRequiresCooperativeCap)
				{
					Action(TEXT("Hold cap role — tap G"),EShopProductionAction::HoldCapRole,Bottle.ISID);
					Action(TEXT("Rotate cap role — tap R"),EShopProductionAction::RotateCapRole,Bottle.ISID);
					if (View.CapHolder==Actions->GetOwner() || View.CapRotator==Actions->GetOwner()) Action(TEXT("Leave cap role"),EShopProductionAction::LeaveCapRole,Bottle.ISID);
				}
				else Action(TEXT("Close compatible cap"),EShopProductionAction::CloseCap,Bottle.ISID);
			}
			Action(TEXT("Open / remove cap"),EShopProductionAction::OpenCap,Bottle.ISID);
		}
	}
	if (Production->bOffersSupplies && Catalog)
	{
		for (const auto& Offer:Catalog->Offers)
		{
			auto* Button=AddButton(TEXT("Buy ")+Offer.DisplayName.ToString()+TEXT(" — ")+ShopTerminal::Money(Offer.UnitCostCents));
			Button->SupplyOfferID=Offer.OfferID; Button->SessionID=View.SessionID;
		}
		AddText(TEXT("Purchases use the shop bank. Deposit sale cash at the ATM to reinvest it."));
	}
	AddButton(TEXT("Close"))->bClose=true;
	if (Production->bRequiresCooperativeCap) SetKeyboardFocus();
}
