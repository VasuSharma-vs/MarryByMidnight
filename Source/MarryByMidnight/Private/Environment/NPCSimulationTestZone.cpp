#include "Environment/NPCSimulationTestZone.h"
#include "Characters/NPCCharacter.h"
#include "Components/NPCPersonalityComponent.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/NPCWorldModelComponent.h"
#include "Subsystems/NPCKnowledgeSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

ANPCSimulationTestZone::ANPCSimulationTestZone()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	SetRootComponent(TriggerBox);
	TriggerBox->SetBoxExtent(FVector(150.0f, 150.0f, 100.0f));
	TriggerBox->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	TriggerBox->SetGenerateOverlapEvents(true);
	TriggerBox->SetLineThickness(2.0f);

	LabelTextComponent = CreateDefaultSubobject<UTextRenderComponent>(TEXT("LabelTextComponent"));
	LabelTextComponent->SetupAttachment(RootComponent);
	LabelTextComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 125.0f));
	LabelTextComponent->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	LabelTextComponent->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	LabelTextComponent->SetWorldSize(16.0f);

	// Default setup: Power Recharging Volume that charges a fee ($2/sec)
	StimuliModifiers.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Power, 15.0f));
	StimuliModifiers.Add(FNPCSimulationStatModifier(ENPCSimulationStat::WalletCash, -2.0f));
	TargetStat = ENPCSimulationStat::Power;
	ChangeRate = 15.0f;
}

void ANPCSimulationTestZone::BeginPlay()
{
	Super::BeginPlay();

	// Backward compatibility fallback
	if (StimuliModifiers.Num() == 0)
	{
		StimuliModifiers.Add(FNPCSimulationStatModifier(TargetStat, ChangeRate));
		for (const FNPCSimulationStatModifier& Mod : AdditionalModifiers)
		{
			StimuliModifiers.Add(Mod);
		}
	}

	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ANPCSimulationTestZone::HandleBoxBeginOverlap);
	TriggerBox->OnComponentEndOverlap.AddDynamic(this, &ANPCSimulationTestZone::HandleBoxEndOverlap);

	// Check for any NPCs already inside the box at spawn
	TArray<AActor*> InitiallyOverlapping;
	TriggerBox->GetOverlappingActors(InitiallyOverlapping, ANPCCharacter::StaticClass());
	for (AActor* Act : InitiallyOverlapping)
	{
		if (ANPCCharacter* NPC = Cast<ANPCCharacter>(Act))
		{
			OverlappingNPCs.AddUnique(NPC);
			NPC->NotifyEnteredSimulationZone(this);
		}
	}
}

bool ANPCSimulationTestZone::HasPositiveStimulusFor(ENPCSimulationStat Stat) const
{
	if (StimuliModifiers.Num() > 0)
	{
		for (const FNPCSimulationStatModifier& Mod : StimuliModifiers)
		{
			if (Mod.Stat == Stat && Mod.ChangeRate > 0.0f)
			{
				return true;
			}
		}
		return false;
	}
	return TargetStat == Stat && ChangeRate > 0.0f;
}

float ANPCSimulationTestZone::GetChangeRateFor(ENPCSimulationStat Stat) const
{
	if (StimuliModifiers.Num() > 0)
	{
		float TotalRate = 0.0f;
		for (const FNPCSimulationStatModifier& Mod : StimuliModifiers)
		{
			if (Mod.Stat == Stat)
			{
				TotalRate += Mod.ChangeRate;
			}
		}
		return TotalRate;
	}
	return TargetStat == Stat ? ChangeRate : 0.0f;
}

float ANPCSimulationTestZone::GetMoneyCostRate() const
{
	float CostRate = 0.0f;
	if (StimuliModifiers.Num() > 0)
	{
		for (const FNPCSimulationStatModifier& Mod : StimuliModifiers)
		{
			if (Mod.Stat == ENPCSimulationStat::WalletCash && Mod.ChangeRate < 0.0f)
			{
				CostRate += (-Mod.ChangeRate);
			}
		}
	}
	else if (TargetStat == ENPCSimulationStat::WalletCash && ChangeRate < 0.0f)
	{
		CostRate = -ChangeRate;
	}
	return CostRate;
}

bool ANPCSimulationTestZone::CanNPCAfford(const ANPCCharacter* NPC) const
{
	const float Cost = GetMoneyCostRate();
	if (Cost <= 0.0f)
	{
		return true;
	}
	if (!NPC || !NPC->PersonalityComponent)
	{
		return false;
	}
	return NPC->PersonalityComponent->WalletCash > 0.0f;
}

#if WITH_EDITOR
void ANPCSimulationTestZone::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (StimuliModifiers.Num() > 0)
	{
		TargetStat = StimuliModifiers[0].Stat;
		ChangeRate = StimuliModifiers[0].ChangeRate;
	}

	// Color-code zone based on the primary stimulus
	if (HasPositiveStimulusFor(ENPCSimulationStat::Power))
	{
		ZoneColor = FColor(255, 215, 0); // Gold / Energy
	}
	else if (HasPositiveStimulusFor(ENPCSimulationStat::Thirst))
	{
		ZoneColor = FColor(0, 191, 255); // Deep Sky Blue
	}
	else if (HasPositiveStimulusFor(ENPCSimulationStat::Hunger))
	{
		ZoneColor = FColor(255, 140, 0); // Dark Orange
	}
	else if (HasPositiveStimulusFor(ENPCSimulationStat::WalletCash))
	{
		ZoneColor = FColor(46, 139, 87); // Sea Green / Money
	}
	else
	{
		switch (TargetStat)
		{
		case ENPCSimulationStat::Power:
			ZoneColor = FColor(255, 215, 0); // Gold / Energy
			break;
		case ENPCSimulationStat::Thirst:
			ZoneColor = FColor(0, 191, 255); // Deep Sky Blue
			break;
		case ENPCSimulationStat::Hunger:
			ZoneColor = FColor(255, 140, 0); // Dark Orange
			break;
		case ENPCSimulationStat::BodyTemperature:
			ZoneColor = ChangeRate >= 0.0f ? FColor(255, 69, 0) : FColor(30, 144, 255); // Red-Orange / Dodger Blue
			break;
		case ENPCSimulationStat::Sickness:
			ZoneColor = FColor(50, 205, 50); // Lime / Biohazard
			break;
		case ENPCSimulationStat::Dopamine:
			ZoneColor = FColor(255, 105, 180); // Pink / Pleasure
			break;
		case ENPCSimulationStat::Stress:
			ZoneColor = FColor(178, 34, 34); // Firebrick / Stress
			break;
		case ENPCSimulationStat::WalletCash:
			ZoneColor = FColor(46, 139, 87); // Sea Green / Money
			break;
		default:
			ZoneColor = FColor(0, 255, 128);
			break;
		}
	}
}
#endif

void ANPCSimulationTestZone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Apply simulation stat rate to all overlapping NPCs on authority
	if (HasAuthority())
	{
		const float CostPerSec = GetMoneyCostRate();

		for (int32 i = OverlappingNPCs.Num() - 1; i >= 0; --i)
		{
			if (OverlappingNPCs[i].IsValid())
			{
				ANPCCharacter* NPC = OverlappingNPCs[i].Get();

				// If volume charges money, verify affordability before granting positive stimuli
				if (CostPerSec > 0.0f && !CanNPCAfford(NPC))
				{
					// Broke NPC cannot afford to charge or receive benefits
					continue;
				}

				if (StimuliModifiers.Num() > 0)
				{
					for (const FNPCSimulationStatModifier& Mod : StimuliModifiers)
					{
						NPC->ApplySimulationStatRate(Mod.Stat, Mod.ChangeRate, DeltaSeconds);
					}
				}
				else
				{
					// Legacy fallback
					NPC->ApplySimulationStatRate(TargetStat, ChangeRate, DeltaSeconds);
					for (const FNPCSimulationStatModifier& Mod : AdditionalModifiers)
					{
						NPC->ApplySimulationStatRate(Mod.Stat, Mod.ChangeRate, DeltaSeconds);
					}
				}
			}
			else
			{
				OverlappingNPCs.RemoveAt(i);
			}
		}
	}

	if (bDrawDebugVisuals)
	{
		DrawZoneDebugDisplay();
	}
}

void ANPCSimulationTestZone::HandleBoxBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (ANPCCharacter* NPC = Cast<ANPCCharacter>(OtherActor))
	{
		OverlappingNPCs.AddUnique(NPC);
		NPC->NotifyEnteredSimulationZone(this);

		if (bRememberPlaceAndEffect)
		{
			UWorld* World = GetWorld();
			UNPCKnowledgeSubsystem* Subsystem = World ? World->GetSubsystem<UNPCKnowledgeSubsystem>() : nullptr;

			if (StimuliModifiers.Num() > 0)
			{
				for (const FNPCSimulationStatModifier& Mod : StimuliModifiers)
				{
					if (NPC->WorldModelComponent)
					{
						NPC->WorldModelComponent->RecordPlaceEffect(PlaceId, Mod.Stat, Mod.ChangeRate, GetActorLocation());
					}
					if (Subsystem)
					{
						Subsystem->RecordPlaceDiscovery(PlaceId, Mod.Stat, Mod.ChangeRate, GetActorLocation(), NPC, bSaveToDisk);
					}
				}
			}
			else
			{
				if (NPC->WorldModelComponent)
				{
					NPC->WorldModelComponent->RecordPlaceEffect(PlaceId, TargetStat, ChangeRate, GetActorLocation());
				}
				if (Subsystem)
				{
					Subsystem->RecordPlaceDiscovery(PlaceId, TargetStat, ChangeRate, GetActorLocation(), NPC, bSaveToDisk);
				}
			}
		}
	}
}

void ANPCSimulationTestZone::HandleBoxEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	if (ANPCCharacter* NPC = Cast<ANPCCharacter>(OtherActor))
	{
		OverlappingNPCs.Remove(NPC);
		NPC->NotifyExitedSimulationZone(this);
	}
}

void ANPCSimulationTestZone::DrawZoneDebugDisplay()
{
	UWorld* World = GetWorld();
	if (!World || !TriggerBox) return;

	const FVector BoxCenter = TriggerBox->GetComponentLocation();
	const FVector BoxExtent = TriggerBox->GetScaledBoxExtent();
	const FQuat BoxRotation = TriggerBox->GetComponentQuat();

	// 1. Draw 3D wireframe box
	DrawDebugBox(World, BoxCenter, BoxExtent, BoxRotation, ZoneColor, false, 0.0f, 0, 2.0f);

	// 2. 3D World-space label (scales with perspective distance)
	if (!LabelTextComponent)
	{
		LabelTextComponent = NewObject<UTextRenderComponent>(this, TEXT("DynamicLabelTextComp"));
		if (LabelTextComponent)
		{
			LabelTextComponent->RegisterComponent();
			LabelTextComponent->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
			LabelTextComponent->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
			LabelTextComponent->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
			LabelTextComponent->SetWorldSize(16.0f);
		}
	}

	if (LabelTextComponent)
	{
		FString StimuliListStr;
		if (StimuliModifiers.Num() > 0)
		{
			for (const FNPCSimulationStatModifier& Mod : StimuliModifiers)
			{
				const FString StatName = UEnum::GetDisplayValueAsText(Mod.Stat).ToString();
				const FString RateSign = Mod.ChangeRate >= 0.0f ? TEXT("+") : TEXT("");
				StimuliListStr += FString::Printf(TEXT("%s: %s%.1f/s\n"), *StatName, *RateSign, Mod.ChangeRate);
			}
		}
		else
		{
			const FString StatName = UEnum::GetDisplayValueAsText(TargetStat).ToString();
			const FString RateSign = ChangeRate >= 0.0f ? TEXT("+") : TEXT("");
			StimuliListStr = FString::Printf(TEXT("%s: %s%.1f/s\n"), *StatName, *RateSign, ChangeRate);
		}

		const float CostPerSec = GetMoneyCostRate();
		FString CostFeeStr;
		if (CostPerSec > 0.0f)
		{
			CostFeeStr = FString::Printf(TEXT("[Fee: $%.1f/sec]\n"), CostPerSec);
		}

		const FString Label = FString::Printf(
			TEXT("[Zone: %s]\n%s%sOverlapping NPCs: %d"),
			*PlaceId.ToString(),
			*StimuliListStr,
			*CostFeeStr,
			OverlappingNPCs.Num()
		);

		LabelTextComponent->SetRelativeLocation(FVector(0.0f, 0.0f, BoxExtent.Z + 25.0f));
		LabelTextComponent->SetText(FText::FromString(Label));
		LabelTextComponent->SetTextRenderColor(ZoneColor);

		// Billboard: face camera
		if (APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(World, 0))
		{
			const FVector CamLoc = CamMgr->GetCameraLocation();
			const FVector TextLoc = LabelTextComponent->GetComponentLocation();
			FRotator FaceRot = (CamLoc - TextLoc).Rotation();
			FaceRot.Pitch = 0.0f;
			FaceRot.Roll = 0.0f;
			LabelTextComponent->SetWorldRotation(FaceRot);
		}
	}
}

void ANPCSimulationTestZone::OnRep_StimuliModifiers()
{
	DrawZoneDebugDisplay();
}

void ANPCSimulationTestZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ANPCSimulationTestZone, StimuliModifiers);
	DOREPLIFETIME(ANPCSimulationTestZone, TargetStat);
	DOREPLIFETIME(ANPCSimulationTestZone, ChangeRate);
	DOREPLIFETIME(ANPCSimulationTestZone, AdditionalModifiers);
}

