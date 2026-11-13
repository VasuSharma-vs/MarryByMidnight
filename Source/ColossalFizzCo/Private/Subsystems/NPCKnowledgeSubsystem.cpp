#include "Subsystems/NPCKnowledgeSubsystem.h"
#include "Subsystems/NPCKnowledgeSaveGame.h"
#include "Components/NPCWorldModelComponent.h"
#include "Kismet/GameplayStatics.h"

void UNPCKnowledgeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadKnowledgeFromDisk();
}

void UNPCKnowledgeSubsystem::Deinitialize()
{
	SaveKnowledgeToDisk();
	Super::Deinitialize();
}

void UNPCKnowledgeSubsystem::RegisterNPC(AActor* NPCActor)
{
	if (!NPCActor) return;

	RegisteredNPCs.Add(NPCActor);

	// Automatically seed the new NPC with permanently unlocked cultural knowledge
	UNPCWorldModelComponent* WorldModel = NPCActor->FindComponentByClass<UNPCWorldModelComponent>();
	if (WorldModel)
	{
		PopulateKnownCulturalData(WorldModel);
	}
}

void UNPCKnowledgeSubsystem::UnregisterNPC(AActor* NPCActor)
{
	if (!NPCActor) return;
	RegisteredNPCs.Remove(NPCActor);
}

void UNPCKnowledgeSubsystem::RecordDiscovery(FName ObjectId, FGameplayTag RequiredTag, FGameplayTag ProducedTag, FVector Location, AActor* DiscovererNPC)
{
	if (ObjectId.IsNone()) return;

	if (DiscovererNPC)
	{
		DiscoveredByNPCs.FindOrAdd(ObjectId).Add(DiscovererNPC);
	}

	if (!Location.IsZero())
	{
		DiscoveredLocationsMap.FindOrAdd(ObjectId).Add(Location);
	}

	FGlobalObjectKnowledge& Entry = GlobalKnowledgeMap.FindOrAdd(ObjectId);
	Entry.ObjectId = ObjectId;
	if (RequiredTag.IsValid()) Entry.RequiredTag = RequiredTag;
	if (ProducedTag.IsValid()) Entry.ProducedTag = ProducedTag;
	Entry.DiscovererCount = DiscoveredByNPCs[ObjectId].Num();

	// Check 50% discovery threshold across active NPCs in map
	const int32 TotalNPCs = FMath::Max(1, RegisteredNPCs.Num());
	const float DiscoveryRatio = (float)Entry.DiscovererCount / (float)TotalNPCs;

	if (DiscoveryRatio >= 0.50f && !Entry.bIsPermanentlyLearned)
	{
		PromoteToPermanentStorage(ObjectId);
	}
}

void UNPCKnowledgeSubsystem::PromoteToPermanentStorage(FName ObjectId)
{
	FGlobalObjectKnowledge* Entry = GlobalKnowledgeMap.Find(ObjectId);
	if (!Entry) return;

	Entry->bIsPermanentlyLearned = true;
	ComputeTop5Locations(ObjectId, Entry->Top5Locations);

	// Save to disk slot so future game sessions remember it
	SaveKnowledgeToDisk();

	OnPermanentKnowledgeUnlocked.Broadcast(ObjectId);
}

void UNPCKnowledgeSubsystem::ComputeTop5Locations(FName ObjectId, TArray<FVector>& OutTop5)
{
	OutTop5.Empty();

	const TArray<FVector>* RawLocs = DiscoveredLocationsMap.Find(ObjectId);
	if (!RawLocs || RawLocs->Num() == 0) return;

	// Simple spatial deduplication: collect top 5 distinct points separated by at least 100 units
	for (const FVector& Loc : *RawLocs)
	{
		bool bAlreadyNear = false;
		for (const FVector& Selected : OutTop5)
		{
			if (FVector::DistSquared(Loc, Selected) < 10000.0f) // 100 units
			{
				bAlreadyNear = true;
				break;
			}
		}

		if (!bAlreadyNear)
		{
			OutTop5.Add(Loc);
			if (OutTop5.Num() >= 5) break;
		}
	}
}

void UNPCKnowledgeSubsystem::RecordPlaceDiscovery(FName PlaceId, ENPCSimulationStat Stat, float ChangeRate, FVector Location, AActor* DiscovererNPC, bool bForceImmediatePermanentSave)
{
	if (PlaceId.IsNone()) return;

	if (DiscovererNPC)
	{
		DiscoveredByNPCs.FindOrAdd(PlaceId).Add(DiscovererNPC);
	}

	FDiscoveredPlaceInfo& Place = PermanentPlacesMap.FindOrAdd(PlaceId);
	Place.PlaceId = PlaceId;
	Place.AffectedStat = Stat;
	Place.ChangeRate = ChangeRate;
	Place.Location = Location;
	Place.VisitCount++;

	const int32 TotalNPCs = FMath::Max(1, RegisteredNPCs.Num());
	const int32 DiscovererCount = DiscoveredByNPCs[PlaceId].Num();
	const float DiscoveryRatio = (float)DiscovererCount / (float)TotalNPCs;

	// Save to disk if forced by editor option or if >= 50% of NPCs have visited
	if (bForceImmediatePermanentSave || DiscoveryRatio >= 0.50f)
	{
		SaveKnowledgeToDisk();
	}
}

void UNPCKnowledgeSubsystem::PopulateKnownCulturalData(UNPCWorldModelComponent* TargetModel)
{
	if (!TargetModel) return;

	for (const auto& Pair : GlobalKnowledgeMap)
	{
		if (Pair.Value.bIsPermanentlyLearned)
		{
			FDiscoveredObjectInfo& Info = TargetModel->DiscoveredObjects.FindOrAdd(Pair.Key);
			Info.ObjectId = Pair.Key;
			if (Pair.Value.RequiredTag.IsValid()) Info.KnownInputs.AddTag(Pair.Value.RequiredTag);
			if (Pair.Value.ProducedTag.IsValid()) Info.KnownOutputs.AddTag(Pair.Value.ProducedTag);
			Info.DiscoveredLocations = Pair.Value.Top5Locations;
		}
	}

	// Seed permanently known places and their stat effects
	for (const auto& Pair : PermanentPlacesMap)
	{
		TargetModel->DiscoveredPlaces.Add(Pair.Key, Pair.Value);
	}

	// Seed global network machines and props
	for (const auto& Pair : GlobalMachinesMap)
	{
		TargetModel->RememberedMachines.Add(Pair.Key, Pair.Value);
	}
	for (const auto& Pair : GlobalPropsMap)
	{
		TargetModel->RememberedProps.Add(Pair.Key, Pair.Value);
	}
}

void UNPCKnowledgeSubsystem::UploadFromNPC(UNPCWorldModelComponent* SourceModel, AActor* NPCActor)
{
	if (!SourceModel) return;

	for (const auto& Pair : SourceModel->RememberedMachines)
	{
		GlobalMachinesMap.Add(Pair.Key, Pair.Value);
	}

	for (const auto& Pair : SourceModel->RememberedProps)
	{
		GlobalPropsMap.Add(Pair.Key, Pair.Value);
	}

	SaveKnowledgeToDisk();

	UE_LOG(LogTemp, Log, TEXT("[%s] Uploaded knowledge to global internet network (Total: %d machines, %d props). Saved to disk."),
		NPCActor ? *NPCActor->GetName() : TEXT("NPC"), GlobalMachinesMap.Num(), GlobalPropsMap.Num());
}

void UNPCKnowledgeSubsystem::DownloadToNPC(UNPCWorldModelComponent* TargetModel)
{
	if (!TargetModel) return;

	for (const auto& Pair : GlobalMachinesMap)
	{
		TargetModel->RememberedMachines.Add(Pair.Key, Pair.Value);
	}

	for (const auto& Pair : GlobalPropsMap)
	{
		TargetModel->RememberedProps.Add(Pair.Key, Pair.Value);
	}

	UE_LOG(LogTemp, Log, TEXT("NPC downloaded global internet knowledge (Received %d machines, %d props)."),
		GlobalMachinesMap.Num(), GlobalPropsMap.Num());
}

void UNPCKnowledgeSubsystem::SaveKnowledgeToDisk()
{
	UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_Client)
	{
		// Clients in multiplayer should never write server savegames to disk
		return;
	}

	UNPCKnowledgeSaveGame* SaveInst = Cast<UNPCKnowledgeSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UNPCKnowledgeSaveGame::StaticClass())
	);

	if (SaveInst)
	{
		// 1. Persist permanent objects
		for (const auto& Pair : GlobalKnowledgeMap)
		{
			if (Pair.Value.bIsPermanentlyLearned)
			{
				SaveInst->PermanentKnowledge.Add(Pair.Key, Pair.Value);
			}
		}

		// 2. Persist permanent places and their effects
		SaveInst->PermanentPlaces = PermanentPlacesMap;

		// 3. Persist global machines and props uploaded via computers
		SaveInst->PermanentMachines = GlobalMachinesMap;
		SaveInst->PermanentProps = GlobalPropsMap;

		UGameplayStatics::SaveGameToSlot(SaveInst, SaveSlotName, 0);
	}
}

void UNPCKnowledgeSubsystem::LoadKnowledgeFromDisk()
{
	UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_Client)
	{
		return;
	}

	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, 0))
	{
		UNPCKnowledgeSaveGame* LoadInst = Cast<UNPCKnowledgeSaveGame>(
			UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0)
		);

		if (LoadInst)
		{
			for (const auto& Pair : LoadInst->PermanentKnowledge)
			{
				GlobalKnowledgeMap.Add(Pair.Key, Pair.Value);
			}

			for (const auto& Pair : LoadInst->PermanentPlaces)
			{
				PermanentPlacesMap.Add(Pair.Key, Pair.Value);
			}

			GlobalMachinesMap = LoadInst->PermanentMachines;
			GlobalPropsMap = LoadInst->PermanentProps;
		}
	}
}

