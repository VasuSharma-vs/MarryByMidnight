#include "Environment/NPCOperableObject.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Characters/NPCCharacter.h"
#include "Components/NPCPersonalityComponent.h"
#include "Components/NPCWorldModelComponent.h"
#include "Subsystems/NPCKnowledgeSubsystem.h"
#include "Props/NPCConsumableProp.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"


ANPCOperableObject::ANPCOperableObject()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f; // 10 Hz for smooth continuous stimuli
	bReplicates = true;
	SetNetUpdateFrequency(30.0f);
	SetMinNetUpdateFrequency(10.0f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	MachineMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MachineMesh"));
	MachineMesh->SetupAttachment(SceneRoot);
	MachineMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));

	InteractionVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionVolume"));
	InteractionVolume->SetupAttachment(SceneRoot);
	InteractionVolume->SetBoxExtent(FVector(120.0f, 120.0f, 100.0f));
	InteractionVolume->SetCollisionProfileName(TEXT("Trigger"));
	InteractionVolume->SetGenerateOverlapEvents(true);

	SeatTransform = CreateDefaultSubobject<USceneComponent>(TEXT("SeatTransform"));
	SeatTransform->SetupAttachment(SceneRoot);
	SeatTransform->SetRelativeLocation(FVector(0.0f, 0.0f, 40.0f));

	OverheadDebugText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("OverheadDebugText"));
	OverheadDebugText->SetupAttachment(SceneRoot);
	OverheadDebugText->SetRelativeLocation(FVector(0.0f, 0.0f, 160.0f));
	OverheadDebugText->SetHorizontalAlignment(EHTA_Center);
	OverheadDebugText->SetWorldSize(15.0f);
	OverheadDebugText->SetTextRenderColor(FColor::Yellow);

	SetupDefaultVendingOfferings();
}

void ANPCOperableObject::SetupDefaultVendingOfferings()
{
	AvailableOfferings.Empty();

	// 1. Cold Drink Vending Offering (Instant, $2.00, dispenses prop and applies stimuli)
	// User spec: Costs $2, increases power level by 10, decreases hunger by 5, thirst by 25, increases dopamine by 5
	FNPCOperableOffering ColdDrink;
	ColdDrink.OfferingId = FName("BuyColdDrink");
	ColdDrink.DisplayTitle = FText::FromString(TEXT("Cold Drink ($2.00)"));
	ColdDrink.ActionType = EAffordanceAction::Buy;
	ColdDrink.MoneyCost = 2.0f;
	ColdDrink.ExecutionDuration = 1.0f;
	ColdDrink.bIsContinuousOverTime = false;
	ColdDrink.DispensedPropClass = ANPCConsumableProp::StaticClass();

	ColdDrink.OfferedStimuli.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Power, 10.0f));
	ColdDrink.OfferedStimuli.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Hunger, -5.0f));
	ColdDrink.OfferedStimuli.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Thirst, -25.0f));
	ColdDrink.OfferedStimuli.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Dopamine, 5.0f));
	AvailableOfferings.Add(ColdDrink);

	// 2. Massage Sofa Offering (Continuous over-time, $0 or session fee, stress -0.2/min, dopamine +0.05/sec, sleepiness -0.5/sec)
	FNPCOperableOffering MassageSofa;
	MassageSofa.OfferingId = FName("UseMassageSofa");
	MassageSofa.DisplayTitle = FText::FromString(TEXT("Massage Sofa (Relax)"));
	MassageSofa.ActionType = EAffordanceAction::Sit;
	MassageSofa.MoneyCost = 0.0f;
	MassageSofa.ExecutionDuration = 10.0f;
	MassageSofa.bIsContinuousOverTime = true;

	// Stress: -0.2 per min = -0.2 / 60.0f = -0.00333/sec
	MassageSofa.OfferedStimuli.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Stress, -0.00333f));
	// Dopamine: +0.05 per sec
	MassageSofa.OfferedStimuli.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Dopamine, 0.05f));
	// Sleepiness: -0.5 per sec (relieves fatigue)
	MassageSofa.OfferedStimuli.Add(FNPCSimulationStatModifier(ENPCSimulationStat::Sleepiness, -0.5f));
	AvailableOfferings.Add(MassageSofa);
}

void ANPCOperableObject::BeginPlay()
{
	Super::BeginPlay();

	InteractionVolume->OnComponentBeginOverlap.AddDynamic(this, &ANPCOperableObject::OnVolumeBeginOverlap);
	InteractionVolume->OnComponentEndOverlap.AddDynamic(this, &ANPCOperableObject::OnVolumeEndOverlap);

	UpdateDebugBillboard();
}

void ANPCOperableObject::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 1. Process continuous occupants (e.g. sitting in massage sofa)
	for (int32 i = CurrentOccupants.Num() - 1; i >= 0; --i)
	{
		if (!CurrentOccupants[i].IsValid())
		{
			CurrentOccupants.RemoveAt(i);
			continue;
		}

		ANPCCharacter* NPC = Cast<ANPCCharacter>(CurrentOccupants[i].Get());
		if (!NPC) continue;

		// Apply the continuous offering's rate modifiers
		for (const auto& Offering : AvailableOfferings)
		{
			if (Offering.bIsContinuousOverTime)
			{
				for (const auto& Mod : Offering.OfferedStimuli)
				{
					NPC->ApplySimulationStatRate(Mod.Stat, Mod.ChangeRate, DeltaSeconds);
				}
			}
		}
	}

	// 2. Process volume aura mode (passive relaxation/aura room)
	if (bOperatesAsVolumeAura)
	{
		for (int32 i = OverlappingActors.Num() - 1; i >= 0; --i)
		{
			if (!OverlappingActors[i].IsValid())
			{
				OverlappingActors.RemoveAt(i);
				continue;
			}

			ANPCCharacter* NPC = Cast<ANPCCharacter>(OverlappingActors[i].Get());
			if (!NPC) continue;

			for (const auto& Offering : AvailableOfferings)
			{
				for (const auto& Mod : Offering.OfferedStimuli)
				{
					NPC->ApplySimulationStatRate(Mod.Stat, Mod.ChangeRate, DeltaSeconds);
				}
			}
		}
	}

	UpdateDebugBillboard();
}

bool ANPCOperableObject::CanNPCAffordOffering(const AActor* NPCActor, const FNPCOperableOffering& Offering) const
{
	if (Offering.MoneyCost <= 0.0f) return true;
	if (!NPCActor) return false;

	const ANPCCharacter* NPC = Cast<ANPCCharacter>(NPCActor);
	if (!NPC) return false;

	// 1. Money cost check
	if (Offering.MoneyCost > 0.0f)
	{
		if (!NPC->PersonalityComponent || NPC->PersonalityComponent->GetCash() < Offering.MoneyCost)
		{
			return false;
		}
	}

	// 2. Multi-item requirements check with quantities
	if (!NPC->HasRequiredItems(Offering.RequiredItems))
	{
		return false;
	}

	// 3. Single required tag legacy check
	if (Offering.RequiredTag.IsValid() && NPC->GetInventoryItemCount(Offering.RequiredTag) < 1)
	{
		return false;
	}

	return true;
}

bool ANPCOperableObject::FindOffering(FName OfferingId, FNPCOperableOffering& OutOffering) const
{
	for (const auto& Offering : AvailableOfferings)
	{
		if (Offering.OfferingId == OfferingId)
		{
			OutOffering = Offering;
			return true;
		}
	}
	return false;
}

bool ANPCOperableObject::StartOperatingOffering(AActor* UserActor, FName OfferingId, FString& OutFailureReason)
{
	if (!UserActor)
	{
		OutFailureReason = TEXT("Invalid user actor");
		return false;
	}

	if (!HasAuthority())
	{
		Server_StartOperatingOffering(UserActor, OfferingId);
		return true;
	}

	ANPCCharacter* NPC = Cast<ANPCCharacter>(UserActor);
	if (!NPC)
	{
		OutFailureReason = TEXT("User is not an NPC character");
		return false;
	}

	FNPCOperableOffering Offering;
	if (!FindOffering(OfferingId, Offering))
	{
		OutFailureReason = TEXT("Offering not found on machine");
		return false;
	}

	// Affordability check (both cash and required items)
	if (!CanNPCAffordOffering(NPC, Offering))
	{
		OutFailureReason = FString::Printf(TEXT("Cannot afford offering (Requires $%.2f + items, NPC has $%.2f)"),
			Offering.MoneyCost, NPC->PersonalityComponent ? NPC->PersonalityComponent->GetCash() : 0.0f);
		return false;
	}

	// Deduct cash payment
	if (Offering.MoneyCost > 0.0f && NPC->PersonalityComponent)
	{
		NPC->PersonalityComponent->SpendCash(Offering.MoneyCost);
	}

	// Deduct required items from NPC inventory
	for (const auto& Req : Offering.RequiredItems)
	{
		NPC->RemoveInventoryItem(Req.ItemTag, Req.Quantity);
	}
	if (Offering.RequiredTag.IsValid())
	{
		NPC->RemoveInventoryItem(Offering.RequiredTag, 1);
	}


	// Continuous vs Instant Execution
	if (Offering.bIsContinuousOverTime)
	{
		if (IsFull())
		{
			OutFailureReason = TEXT("Machine is at maximum occupancy");
			return false;
		}

		CurrentOccupants.AddUnique(UserActor);
		ReplicatedOccupants.AddUnique(UserActor);

		// Halt movement and enter stationary mode
		NPC->bIsRestingInSimulationZone = true;
		if (AController* C = NPC->GetController())
		{
			C->StopMovement();
		}
	}
	else
	{
		// 1. Spawn physical prop if configured (e.g. cold drink can)
		if (Offering.DispensedPropClass)
		{
			const FVector SpawnLoc = GetActorLocation() + GetActorRotation().RotateVector(DispenserSpawnOffset);
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			GetWorld()->SpawnActor<AActor>(Offering.DispensedPropClass, SpawnLoc, FRotator::ZeroRotator, SpawnParams);
		}

		// 2. Apply direct instant stimuli deltas
		for (const auto& Mod : Offering.OfferedStimuli)
		{
			NPC->ApplySimulationStatDelta(Mod.Stat, Mod.ChangeRate);
		}
	}

	// 3. Internet Computer / Laptop Knowledge Sync
	if (bIsComputerTerminal && bHasInternetConnection)
	{
		UWorld* World = GetWorld();
		if (World)
		{
			UNPCKnowledgeSubsystem* Subsystem = World->GetSubsystem<UNPCKnowledgeSubsystem>();
			if (Subsystem && NPC->WorldModelComponent)
			{
				Subsystem->UploadFromNPC(NPC->WorldModelComponent, NPC);
				Subsystem->DownloadToNPC(NPC->WorldModelComponent);
			}
		}
	}

	Multicast_OnOfferingOperated(UserActor, OfferingId);
	return true;
}

void ANPCOperableObject::Server_StartOperatingOffering_Implementation(AActor* UserActor, FName OfferingId)
{
	FString FailReason;
	StartOperatingOffering(UserActor, OfferingId, FailReason);
}

bool ANPCOperableObject::Server_StartOperatingOffering_Validate(AActor* UserActor, FName OfferingId)
{
	return UserActor != nullptr;
}

void ANPCOperableObject::Server_StopOperatingOffering_Implementation(AActor* UserActor)
{
	StopOperatingOffering(UserActor);
}

bool ANPCOperableObject::Server_StopOperatingOffering_Validate(AActor* UserActor)
{
	return UserActor != nullptr;
}

void ANPCOperableObject::Multicast_OnOfferingOperated_Implementation(AActor* UserActor, FName OfferingId)
{
	OnOperableInteracted.Broadcast(this, UserActor, OfferingId);
	UpdateDebugBillboard();
}

void ANPCOperableObject::OnRep_Occupants()
{
	CurrentOccupants.Empty();
	for (AActor* Act : ReplicatedOccupants)
	{
		if (Act)
		{
			CurrentOccupants.Add(Act);
		}
	}
	UpdateDebugBillboard();
}

void ANPCOperableObject::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ANPCOperableObject, ReplicatedOccupants);
}

void ANPCOperableObject::StopOperatingOffering(AActor* UserActor)
{
	if (!UserActor) return;

	if (!HasAuthority())
	{
		Server_StopOperatingOffering(UserActor);
		return;
	}

	CurrentOccupants.Remove(UserActor);
	ReplicatedOccupants.Remove(UserActor);

	if (ANPCCharacter* NPC = Cast<ANPCCharacter>(UserActor))
	{
		NPC->bIsRestingInSimulationZone = false;
	}

	UpdateDebugBillboard();
}

void ANPCOperableObject::OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || OtherActor == this) return;

	OverlappingActors.AddUnique(OtherActor);

	if (ANPCCharacter* NPC = Cast<ANPCCharacter>(OtherActor))
	{
		// Let NPC know it entered this operable object's proximity
		UpdateDebugBillboard();
	}
}

void ANPCOperableObject::OnVolumeEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!OtherActor) return;

	OverlappingActors.Remove(OtherActor);
	StopOperatingOffering(OtherActor);
	UpdateDebugBillboard();
}

bool ANPCOperableObject::CanInteract_Implementation(AActor* InstigatorActor)
{
	return !IsFull();
}

void ANPCOperableObject::QueryAffordanceOptions_Implementation(AActor* InstigatorActor, TArray<FAffordanceOption>& OutOptions)
{
	for (const auto& Offering : AvailableOfferings)
	{
		const bool bCanAfford = CanNPCAffordOffering(InstigatorActor, Offering);
		const bool bCapacityAvailable = !Offering.bIsContinuousOverTime || !IsFull();
		const bool bAllowed = bCanAfford && bCapacityAvailable;

		FText Reason = FText::GetEmpty();
		if (!bCanAfford)
		{
			Reason = FText::Format(FText::FromString(TEXT("Requires ${0}")), FText::AsNumber(Offering.MoneyCost));
		}
		else if (!bCapacityAvailable)
		{
			Reason = FText::FromString(TEXT("In use by another NPC"));
		}

		FAffordanceOption Option = Offering.ToAffordanceOption(bAllowed, Reason);
		OutOptions.Add(Option);
	}
}

bool ANPCOperableObject::ExecuteAffordanceOption_Implementation(FName OptionId, AActor* InstigatorActor)
{
	FString FailureReason;
	return StartOperatingOffering(InstigatorActor, OptionId, FailureReason);
}

void ANPCOperableObject::UpdateDebugBillboard()
{
	if (!OverheadDebugText) return;

	FString Status = FString::Printf(TEXT("[%s]\nMode: %s | Users: %d/%d\n"),
		*MachineDisplayName.ToString(),
		bOperatesAsVolumeAura ? TEXT("Volume Aura") : TEXT("Direct Interaction"),
		CurrentOccupants.Num(),
		MaxConcurrentUsers
	);

	for (const auto& Offering : AvailableOfferings)
	{
		Status += FString::Printf(TEXT("• %s | $%0.2f%s\n"),
			*Offering.DisplayTitle.ToString(),
			Offering.MoneyCost,
			Offering.bIsContinuousOverTime ? TEXT(" (Over-Time)") : TEXT(" (Instant)")
		);

		for (const auto& Mod : Offering.OfferedStimuli)
		{
			const FString Sign = Mod.ChangeRate >= 0.0f ? TEXT("+") : TEXT("");
			Status += FString::Printf(TEXT("   %s%0.2f %s%s\n"),
				*Sign,
				Mod.ChangeRate,
				*UEnum::GetValueAsString(Mod.Stat),
				Offering.bIsContinuousOverTime ? TEXT("/s") : TEXT("")
			);
		}
	}

	OverheadDebugText->SetText(FText::FromString(Status));
	OverheadDebugText->SetTextRenderColor(CurrentOccupants.Num() > 0 ? FColor::Green : FColor::Yellow);
}

TArray<AActor*> ANPCOperableObject::GetCurrentOccupantsBP() const
{
	TArray<AActor*> Result;
	for (const auto& Ptr : CurrentOccupants)
	{
		if (Ptr.IsValid()) Result.Add(Ptr.Get());
	}
	return Result;
}

TArray<AActor*> ANPCOperableObject::GetOverlappingActorsBP() const
{
	TArray<AActor*> Result;
	for (const auto& Ptr : OverlappingActors)
	{
		if (Ptr.IsValid()) Result.Add(Ptr.Get());
	}
	return Result;
}

