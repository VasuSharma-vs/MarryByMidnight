#include "UI/NPCDebugWidget.h"
#include "Characters/NPCCharacter.h"
#include "Components/NPCNeedsComponent.h"
#include "Components/NPCStateComponent.h"
#include "Components/NPCMentalStateComponent.h"
#include "Components/NPCPersonalityComponent.h"

void UNPCDebugWidget::SetTargetNPC(ANPCCharacter* InNPC)
{
	TargetNPC = InNPC;
}

void UNPCDebugWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!TargetNPC.IsValid())
	{
		// Try to resolve target NPC from owning actor if not explicitly set
		TargetNPC = Cast<ANPCCharacter>(GetOwningPlayerPawn());
		if (!TargetNPC.IsValid())
		{
			AActor* OwnerActor = GetTypedOuter<AActor>();
			TargetNPC = Cast<ANPCCharacter>(OwnerActor);
		}
	}

	if (TargetNPC.IsValid())
	{
		if (TargetNPC->NeedsComponent)
		{
			Hunger = TargetNPC->NeedsComponent->Needs.Hunger;
			Thirst = TargetNPC->NeedsComponent->Needs.Thirst;
		}

		if (TargetNPC->StateComponent)
		{
			Power = TargetNPC->StateComponent->PhysicalState.Power;
			BodyTemperature = TargetNPC->StateComponent->PhysicalState.BodyTemperature;
			Sickness = TargetNPC->StateComponent->PhysicalState.Sickness;
		}

		if (TargetNPC->MentalStateComponent)
		{
			MoodName = UEnum::GetDisplayValueAsText(TargetNPC->MentalStateComponent->CurrentMoodTier).ToString();
		}

		if (TargetNPC->PersonalityComponent)
		{
			Money = TargetNPC->PersonalityComponent->WalletCash;
		}

		FormattedStatsText = TargetNPC->GetDebugStatsFormattedString();

		OnStatsUpdated();
	}
}
