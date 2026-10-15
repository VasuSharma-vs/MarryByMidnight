#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropDataAsset.h"
#include "Components/ASMComponent.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Characters/NPCCharacter.h"
#include "Components/NPCPersonalityComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Async/Async.h"
#include "TimerManager.h"

APropManager::APropManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f;
	bReplicates = true;
	bAlwaysRelevant = true;

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	SetRootComponent(SceneRoot);

	ASMComponent = CreateDefaultSubobject<UASMComponent>(TEXT("ASMComponent"));

	DefaultResetTransform = FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, -100000.0f), FVector(0.0001f));
	ActivePropActorClass = AActivePropActor::StaticClass();
}

APropManager* APropManager::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = WorldContextObject->GetWorld();
	if (!World) return nullptr;

	for (TActorIterator<APropManager> It(World); It; ++It)
	{
		return *It;
	}

	if (World->GetAuthGameMode() || World->GetNetMode() != NM_Client)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<APropManager>(SpawnParams);
	}

	return nullptr;
}

void APropManager::PostInitProperties()
{
	Super::PostInitProperties();
	bReplicates = true;
	bAlwaysRelevant = true;
}

void APropManager::PostLoad()
{
	Super::PostLoad();
	bReplicates = true;
	bAlwaysRelevant = true;
}

void APropManager::PreInitializeComponents()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	Super::PreInitializeComponents();
}

void APropManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APropManager, LoadState);
	DOREPLIFETIME(APropManager, PropManagerReady);
	DOREPLIFETIME(APropManager, bDebugPrintInstanceHit);
}

void APropManager::SetLoadState(EPropManagerLoadState NewState)
{
	LoadState = NewState;
	OnPropManagerLoadStateChanged.Broadcast(LoadState, PropManagerReady, PropSyncReady);
}

void APropManager::EnsureLevelPropsInitialized()
{
	if (!bHasInitializedLevelProps)
	{
		bHasInitializedLevelProps = true;
		InitializePropManager();
	}
}

void APropManager::ApplyProductDefinitionToComponent(UInteractableISMComponent* Comp, const FPropProductDefinition& Def)
{
	if (!Comp || Def.ProductID <= 0)
	{
		return;
	}

	Comp->ApplyProductDefinition(Def);
	if (!Comp->IsRegistered() && GetWorld())
	{
		if (!Comp->GetAttachParent() && GetRootComponent())
		{
			Comp->SetupAttachment(GetRootComponent());
		}
		Comp->RegisterComponent();
	}
}

void APropManager::BeginPlay()
{
	Super::BeginPlay();

	EnsureLevelPropsInitialized();

	UWorld* World = GetWorld();
	if (World && HasAuthority())
	{
		World->GetTimerManager().SetTimer(SettlingTimerHandle, this, &APropManager::CheckSettlingQueue, SettlingCheckInterval, true);
		World->GetTimerManager().SetTimerForNextTick(this, &APropManager::PrepareClientPropSync);
	}
}

void APropManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(SettlingTimerHandle);
	}

	// Remove all Empty instances across registered IISM components when session ends
	for (TPair<int32, TObjectPtr<UInteractableISMComponent>>& Pair : ComponentsByICID)
	{
		if (Pair.Value)
		{
			Pair.Value->ClearEmptyInstances(true);
		}
	}

	ClearLevelRuntimeState();
	bHasInitializedLevelProps = false;

	Super::EndPlay(EndPlayReason);
}

void APropManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		UpdateDistributedProps(DeltaSeconds);
		UpdateDistributedISM(DeltaSeconds);
		ProcessPendingTasks(DeltaSeconds);
		ProcessBulkReplication(DeltaSeconds);
		ProcessQueuedPlacedConversions();
		ProcessQueuedISMPromotions();
		ProcessActivePhysicsReplication(DeltaSeconds);
		ProcessTimestampedThermalUpdates(DeltaSeconds);
	}
	else
	{
		if (QueuedPromotionISIDs.Num() > 0)
		{
			ProcessQueuedISMPromotions();
		}
	}
}

void APropManager::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
}

void APropManager::CheckCharacterFloorISMHits()
{
	// Character hit detection is handled in Blueprint by the user.
}

void APropManager::HandleExternalActorHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit)
{
}

void APropManager::HandleExternalComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
}

bool APropManager::SendISMHitResultToPropManager(const FHitResult& Hit, FVector NormalImpulse, AActor* HittingActor)
{
	UInteractableISMComponent* HitISM = Cast<UInteractableISMComponent>(Hit.GetComponent());
	if (!HitISM)
	{
		return false;
	}

	const int32 HitISMI = (Hit.Item != INDEX_NONE) ? Hit.Item : Hit.MyItem;
	return SendISMHitToPropManager(
		HitISM,
		HitISMI,
		Hit.ImpactPoint,
		Hit.ImpactNormal,
		Hit.Normal,
		NormalImpulse,
		HittingActor);
}

bool APropManager::SendISMHitToPropManager(
	UPrimitiveComponent* HitISMComponent,
	int32 InISMI,
	FVector ImpactPoint,
	FVector ImpactNormal,
	FVector Normal,
	FVector NormalImpulse,
	AActor* HittingActor)
{
	UInteractableISMComponent* ISMComp = Cast<UInteractableISMComponent>(HitISMComponent);
	if (!ISMComp)
	{
		return false;
	}

	if (ISMComp->IISMComponentID <= 0)
	{
		RegisterIISMComponent(ISMComp);
	}

	return SendISMHitByICIDToPropManager(
		ISMComp->IISMComponentID,
		InISMI,
		ImpactPoint,
		ImpactNormal,
		Normal,
		NormalImpulse,
		HittingActor);
}

bool APropManager::SendISMHitByICIDToPropManager(
	int32 InICID,
	int32 InISMI,
	FVector ImpactPoint,
	FVector ImpactNormal,
	FVector Normal,
	FVector NormalImpulse,
	AActor* HittingActor)
{
	UInteractableISMComponent* ISMComp = FindIISMComponent(InICID);
	if (!ISMComp)
	{
		return false;
	}

	if (!ISMComp->IsValidInstance(InISMI) || ISMComp->IsEmptyInstance(InISMI) || ISMComp->IsInstanceParkedAtResetTransform(InISMI))
	{
		return false;
	}

	FVector EffectiveImpulse = NormalImpulse;
	if (EffectiveImpulse.IsNearlyZero() && HittingActor)
	{
		EffectiveImpulse = HittingActor->GetVelocity();
	}
	if (EffectiveImpulse.IsNearlyZero() && !ImpactNormal.IsNearlyZero())
	{
		EffectiveImpulse = -ImpactNormal.GetSafeNormal() * 250.0f;
	}

	return SetMeshInstanceToUnsettled(ISMComp, InISMI, EffectiveImpulse, ImpactPoint, -1.0f);
}

void APropManager::Server_ReceiveISMHit_Implementation(
	int32 InICID,
	int32 InISMI,
	FVector_NetQuantize ImpactPoint,
	FVector_NetQuantizeNormal ImpactNormal,
	FVector_NetQuantizeNormal Normal,
	FVector NormalImpulse,
	AActor* HittingActor)
{
	const int32 TargetISID = GetISIDFromISMI(InICID, InISMI);
	if (TargetISID > 0 && IsISIDConvertingOrConvertedToASM(TargetISID))
	{
		return;
	}

	SendISMHitByICIDToPropManager(InICID, InISMI, ImpactPoint, ImpactNormal, Normal, NormalImpulse, HittingActor);
}

bool APropManager::Server_ReceiveISMHit_Validate(
	int32 InICID,
	int32 InISMI,
	FVector_NetQuantize ImpactPoint,
	FVector_NetQuantizeNormal ImpactNormal,
	FVector_NetQuantizeNormal Normal,
	FVector NormalImpulse,
	AActor* HittingActor)
{
	return true;
}

int32 APropManager::GetCurrentWorldMinutes() const
{
	if (const UWorld* World = GetWorld())
	{
		return FMath::Clamp(FMath::FloorToInt(World->GetTimeSeconds() / 60.0f), 0, 65535);
	}
	return 0;
}

FTransform APropManager::GetResetTransformForSlot(int32 SlotIndex) const
{
	if (ASMComponent)
	{
		return ASMComponent->GetResetTransformForSlot(SlotIndex);
	}
	if (ResetTransforms.Num() > 0)
	{
		return ResetTransforms[FMath::Abs(SlotIndex) % ResetTransforms.Num()];
	}
	FVector OffsetLoc = DefaultResetTransform.GetLocation() + FVector(static_cast<float>(SlotIndex % 256) * 50.0f, static_cast<float>((SlotIndex / 256) % 256) * 50.0f, 0.0f);
	return FTransform(DefaultResetTransform.GetRotation(), OffsetLoc, DefaultResetTransform.GetScale3D());
}

// =========================================================================
// 37.A & Section 45, 51, 66, 69 — Initialization & Level Initialization
// =========================================================================
void APropManager::InitializePropManager()
{
	InitializeLevelProps();
}

void APropManager::InitializeIISMDB()
{
	int32 AutoPID = 1;
	for (UInteractablePropDataAsset* Asset : DefaultProductDataAssets)
	{
		if (Asset)
		{
			while (ProductDefinitions.Contains(AutoPID))
			{
				++AutoPID;
			}
			FPropProductDefinition Def = FPropProductDefinition::FromDataAsset(Asset, AutoPID, AutoPID);
			RegisterProductDefinition(Def);
			++AutoPID;
		}
	}
}

bool APropManager::RegisterProductDefinition(const FPropProductDefinition& InDefinition)
{
	EnsureLevelPropsInitialized();

	if (InDefinition.ProductID <= 0)
	{
		return false;
	}
	ProductDefinitions.Add(InDefinition.ProductID, InDefinition);

	// Ensure any existing IISM component for this ProductID immediately receives its mesh and materials
	for (const TPair<int32, TObjectPtr<UInteractableISMComponent>>& Pair : ComponentsByICID)
	{
		if (Pair.Value && Pair.Value->ProductID == InDefinition.ProductID)
		{
			ApplyProductDefinitionToComponent(Pair.Value, InDefinition);
		}
	}

	return true;
}

int32 APropManager::AllocateProductID()
{
	int32 MaxPID = 0;
	for (const TPair<int32, FPropProductDefinition>& Pair : ProductDefinitions)
	{
		if (Pair.Key > MaxPID)
		{
			MaxPID = Pair.Key;
		}
	}
	for (const TPair<int32, TObjectPtr<UInteractableISMComponent>>& Pair : ComponentsByICID)
	{
		if (Pair.Value && Pair.Value->ProductID > MaxPID)
		{
			MaxPID = Pair.Value->ProductID;
		}
	}
	return (MaxPID >= 100) ? (MaxPID + 1) : 101;
}

bool APropManager::IsProductRegistered(int32 InProductID) const
{
	return InProductID > 0 && ProductDefinitions.Contains(InProductID);
}

int32 APropManager::FindProductIDByDataAsset(const UInteractablePropDataAsset* InAsset) const
{
	if (!InAsset)
	{
		return 0;
	}
	for (const TPair<int32, FPropProductDefinition>& Pair : ProductDefinitions)
	{
		if (Pair.Value.SourceDataAsset == InAsset)
		{
			return Pair.Key;
		}
	}
	return 0;
}

int32 APropManager::FindProductIDByStaticMesh(const UStaticMesh* InMesh) const
{
	if (!InMesh)
	{
		return 0;
	}
	for (const TPair<int32, FPropProductDefinition>& Pair : ProductDefinitions)
	{
		if (Pair.Value.StaticMesh == InMesh)
		{
			return Pair.Key;
		}
	}
	for (const TPair<int32, TObjectPtr<UInteractableISMComponent>>& Pair : ComponentsByICID)
	{
		if (Pair.Value && Pair.Value->GetStaticMesh() == InMesh && Pair.Value->ProductID > 0)
		{
			return Pair.Value->ProductID;
		}
	}
	return 0;
}

int32 APropManager::RegisterProductFromActor(AActivePropActor* InActor)
{
	if (!IsValid(InActor))
	{
		return 0;
	}

	EnsureLevelPropsInitialized();

	// If already registered and valid
	if (InActor->ProductID > 0 && IsProductRegistered(InActor->ProductID))
	{
		return InActor->ProductID;
	}

	// 1. Try finding/building from PropDataAsset
	if (InActor->PropDataAsset)
	{
		const int32 ExistingPID = FindProductIDByDataAsset(InActor->PropDataAsset);
		if (ExistingPID > 0)
		{
			InActor->ProductID = ExistingPID;
			return ExistingPID;
		}

		const int32 NewPID = (InActor->ProductID > 0) ? InActor->ProductID : AllocateProductID();
		FPropProductDefinition Def = FPropProductDefinition::FromDataAsset(InActor->PropDataAsset, NewPID);
		if (!Def.CustomActiveActorClass)
		{
			Def.CustomActiveActorClass = InActor->GetClass();
		}
		RegisterProductDefinition(Def);
		InActor->ProductID = NewPID;
		return NewPID;
	}

	// 2. Try finding/building from PropMesh
	UStaticMesh* ActorMesh = InActor->PropMesh ? InActor->PropMesh->GetStaticMesh() : nullptr;
	if (ActorMesh)
	{
		if (InActor->ProductID <= 0)
		{
			const int32 ExistingPID = FindProductIDByStaticMesh(ActorMesh);
			if (ExistingPID > 0)
			{
				InActor->ProductID = ExistingPID;
				return ExistingPID;
			}
		}

		const int32 NewPID = (InActor->ProductID > 0) ? InActor->ProductID : AllocateProductID();
		FPropProductDefinition Def;
		Def.ProductID = NewPID;
		Def.StaticMeshID = NewPID;
		Def.StaticMesh = ActorMesh;
		Def.DisplayName = InActor->ItemDisplayName.IsEmpty() ? FText::FromName(ActorMesh->GetFName()) : InActor->ItemDisplayName;
		Def.CustomActiveActorClass = InActor->GetClass();

		if (InActor->PropMesh)
		{
			const int32 NumMats = InActor->PropMesh->GetNumMaterials();
			for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
			{
				Def.MaterialOverrides.Add(InActor->PropMesh->GetMaterial(MatIdx));
			}
		}

		Def.ConsumableProfile.Price = InActor->Price;
		Def.ConsumableProfile.NumOfPortions = InActor->NumOfPortions;
		Def.ConsumableProfile.ConsumableType = InActor->ConsumableType;
		Def.TemperatureProfile.bEnableTemperatureUpdate = InActor->bEnableTemperatureUpdate;
		Def.TemperatureProfile.ConsumableTemperature = InActor->ConsumableTemperature;
		Def.ExpiryProfile.bHasExpiry = InActor->bHasExpiry;
		Def.ExpiryProfile.ExpiryLifetimeMinutes = InActor->ExpiryLifetimeMinutes;

		RegisterProductDefinition(Def);
		InActor->ProductID = NewPID;
		return NewPID;
	}

	return 0;
}

int32 APropManager::RegisterIISMComponent(UInteractableISMComponent* InComponent, int32 ExplicitICID)
{
	if (!InComponent) return 0;

	int32 AssignedICID = (ExplicitICID > 0) ? ExplicitICID : InComponent->IISMComponentID;
	if (AssignedICID <= 0)
	{
		while (ComponentsByICID.Contains(NextICID))
		{
			++NextICID;
		}
		AssignedICID = NextICID++;
	}

	// If another component is already registered for AssignedICID on a client (e.g. local batch-created vs replicated empty subobject),
	// keep the populated component so no orphaned ISM component remains rendering in the world.
	if (TObjectPtr<UInteractableISMComponent>* ExistingPtr = ComponentsByICID.Find(AssignedICID))
	{
		UInteractableISMComponent* ExistingComp = ExistingPtr->Get();
		if (ExistingComp && ExistingComp != InComponent)
		{
			if (ExistingComp->GetInstanceCount() > 0 && InComponent->GetInstanceCount() == 0)
			{
				InComponent->SetVisibility(false, true);
				InComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				return AssignedICID;
			}
			else if (InComponent->GetInstanceCount() > 0 && ExistingComp->GetInstanceCount() == 0)
			{
				ExistingComp->ClearInstances();
				ExistingComp->SetVisibility(false, true);
				ExistingComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
	}

	InComponent->IISMComponentID = AssignedICID;
	if (bDebugPrintInstanceHit)
	{
		InComponent->bDebugPrintInstanceHit = true;
		InComponent->SetNotifyRigidBodyCollision(true);
	}
	ComponentsByICID.Add(AssignedICID, InComponent);

	if (!RegisteredISMComponents.Contains(InComponent))
	{
		RegisteredISMComponents.Add(InComponent);
	}

	// Ensure ProductDefinition is registered if component has a DefaultDataAsset, IISMDB, or StaticMesh
	if (InComponent->ProductID <= 0)
	{
		if (InComponent->IISMDB.ProductID > 0)
		{
			InComponent->ProductID = InComponent->IISMDB.ProductID;
		}
		else if (InComponent->DefaultDataAsset)
		{
			InComponent->ProductID = GetTypeHash(InComponent->DefaultDataAsset->GetFName()) & 0x7FFFFFFF;
			if (InComponent->ProductID == 0) InComponent->ProductID = AssignedICID;
		}
		else if (InComponent->GetStaticMesh())
		{
			InComponent->ProductID = GetTypeHash(InComponent->GetStaticMesh()->GetFName()) & 0x7FFFFFFF;
			if (InComponent->ProductID == 0) InComponent->ProductID = AssignedICID;
		}
		else
		{
			InComponent->ProductID = AssignedICID;
		}
	}
	if (InComponent->ProductID > 0)
	{
		if (const FPropProductDefinition* ExistingDef = ProductDefinitions.Find(InComponent->ProductID))
		{
			ApplyProductDefinitionToComponent(InComponent, *ExistingDef);
		}
		else if (InComponent->IISMDB.ProductID > 0 && InComponent->IISMDB.StaticMesh)
		{
			RegisterProductDefinition(InComponent->IISMDB);
			ApplyProductDefinitionToComponent(InComponent, InComponent->IISMDB);
		}
		else if (InComponent->DefaultDataAsset)
		{
			FPropProductDefinition Def = FPropProductDefinition::FromDataAsset(InComponent->DefaultDataAsset, InComponent->ProductID, InComponent->StaticMeshID);
			RegisterProductDefinition(Def);
			ApplyProductDefinitionToComponent(InComponent, Def);
		}
		else if (InComponent->GetStaticMesh())
		{
			FPropProductDefinition Def;
			Def.ProductID = InComponent->ProductID;
			Def.StaticMeshID = (InComponent->StaticMeshID > 0) ? InComponent->StaticMeshID : InComponent->ProductID;
			Def.StaticMesh = InComponent->GetStaticMesh();
			Def.DisplayName = FText::FromName(InComponent->GetStaticMesh()->GetFName());
			const int32 NumMats = InComponent->GetNumMaterials();
			for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
			{
				Def.MaterialOverrides.Add(InComponent->GetMaterial(MatIdx));
			}
			RegisterProductDefinition(Def);
			ApplyProductDefinitionToComponent(InComponent, Def);
		}
	}

	InComponent->SyncDatabaseWithInstanceCount();

	// Assign new session ISIDs on the authoritative server to pre-placed ISM instances that are not Empty
	if (HasAuthority())
	{
		const int32 NumInstances = InComponent->GetInstanceCount();
		for (int32 ISMI = 0; ISMI < NumInstances; ++ISMI)
		{
			if (InComponent->IsEmptyInstance(ISMI))
			{
				continue;
			}

			int32 ExistingISID = InComponent->FindISID(ISMI);
			if (ExistingISID <= 0 || !ISIDData.Contains(ExistingISID))
			{
				ExistingISID = AllocateISIDForInstance(InComponent->ProductID, AssignedICID, ISMI);
				FTransform InstTransform = FTransform::Identity;
				InComponent->GetInstanceTransform(ISMI, InstTransform, true);
				InitializeGeneratedInstance(ExistingISID, InComponent->ProductID, AssignedICID, ISMI, InstTransform);
			}
			else
			{
				InComponent->BindISIDToISMI(ExistingISID, ISMI);
			}
		}
	}
	else
	{
		InComponent->RebuildLocalIndexMap();
		for (const TPair<int32, int32>& Pair : InComponent->ISIDToISMI)
		{
			const int32 MappedISID = Pair.Key;
			const int32 MappedISMI = Pair.Value;
			if (MappedISID > 0 && InComponent->IsValidInstance(MappedISMI))
			{
				if (FISIDRuntimeIndex* ExistingRecord = FindISIDData(MappedISID))
				{
					ExistingRecord->IISMComponentID = AssignedICID;
					ExistingRecord->ISMI = MappedISMI;
					if (ExistingRecord->ProductID <= 0)
					{
						ExistingRecord->ProductID = InComponent->ProductID;
					}
				}
				else
				{
					FTransform InstTransform = FTransform::Identity;
					InComponent->GetInstanceTransform(MappedISMI, InstTransform, true);
					RegisterInstance(MappedISID, InComponent->ProductID, AssignedICID, MappedISMI, InstTransform);
				}
			}
		}
	}

	return AssignedICID;
}

UInteractableISMComponent* APropManager::GetOrCreateIISMComponent(int32 InProductID)
{
	const FPropProductDefinition* Def = ProductDefinitions.Find(InProductID);

	// 1. Search existing registered components matching InProductID
	for (const TPair<int32, TObjectPtr<UInteractableISMComponent>>& Pair : ComponentsByICID)
	{
		if (Pair.Value && Pair.Value->ProductID == InProductID)
		{
			if (Def)
			{
				ApplyProductDefinitionToComponent(Pair.Value, *Def);
			}
			return Pair.Value;
		}
	}

	// 1b. Also check any UInteractableISMComponent already attached to this manager actor
	TInlineComponentArray<UInteractableISMComponent*> AttachedComps(this);
	for (UInteractableISMComponent* AttachedComp : AttachedComps)
	{
		if (AttachedComp && (AttachedComp->ProductID == InProductID || AttachedComp->IISMDB.ProductID == InProductID))
		{
			AttachedComp->ProductID = InProductID;
			if (Def)
			{
				ApplyProductDefinitionToComponent(AttachedComp, *Def);
			}
			RegisterIISMComponent(AttachedComp, AttachedComp->IISMComponentID);
			return AttachedComp;
		}
	}

	// 2. Also check if any registered component is compatible (e.g., unassigned ProductID == 0 with EmptyInstances)
	for (const TPair<int32, TObjectPtr<UInteractableISMComponent>>& Pair : ComponentsByICID)
	{
		if (Pair.Value && Pair.Value->ProductID <= 0 && CanComponentReuseForProduct(Pair.Value, InProductID))
		{
			Pair.Value->ProductID = InProductID;
			if (Def)
			{
				ApplyProductDefinitionToComponent(Pair.Value, *Def);
			}
			return Pair.Value;
		}
	}

	// 3. Create a new UInteractableISMComponent attached to this manager
	// Disable component subobject replication on dynamically spawned manager batches because APropManager
	// deterministically creates and synchronizes client IISM batches via Multicast_ReceivePropInitializationBatch / Multicast_ReceiveBulkBatch.
	UInteractableISMComponent* NewComp = NewObject<UInteractableISMComponent>(this);
	if (!NewComp) return nullptr;

	NewComp->SetIsReplicated(false);
	NewComp->ProductID = InProductID;
	NewComp->ResetTransform = DefaultResetTransform;
	NewComp->bDebugPrintInstanceHit = bDebugPrintInstanceHit;

	if (Def)
	{
		ApplyProductDefinitionToComponent(NewComp, *Def);
	}

	if (!NewComp->IsRegistered())
	{
		NewComp->SetupAttachment(GetRootComponent());
		NewComp->RegisterComponent();
	}

	RegisterIISMComponent(NewComp);
	return NewComp;
}

bool APropManager::ValidateDefinitions() const
{
	for (const TPair<int32, FPropProductDefinition>& Pair : ProductDefinitions)
	{
		if (Pair.Key <= 0 || Pair.Value.ProductID != Pair.Key)
		{
			return false;
		}
	}
	return true;
}

void APropManager::InitializeLevelProps(const FString& SaveSlotName, int32 SaveUserIndex)
{
	// Section 45, 51, 59, 69: Level Initialization Sequence
	bHasInitializedLevelProps = true;
	PropManagerReady = false;
	PropSyncReady = false;
	SetLoadState(EPropManagerLoadState::LoadingComponents);

	// 1. Clear stale runtime EmptyInstances and reset ISID allocator for the new session
	ClearLevelRuntimeState();

	// 2. Initialize ASM Pool via ASMComponent (Min 100 to Max 500 ASM units, populating EmptyASMIDs)
	if (ASMComponent && HasAuthority())
	{
		ASMComponent->InitializeASMPool(ASMComponent->MinASMCount);
	}

	// 3. Initialize Product Definitions (IISMDB)
	InitializeIISMDB();

	// 4. Discover and initialize level IISM components
	InitializeIISMComponents();

	// 5. Load saved Prop Database if a valid save slot was provided
	bool bLoadedFromSave = false;
	if (!SaveSlotName.IsEmpty() && HasAuthority() && UGameplayStatics::DoesSaveGameExist(SaveSlotName, SaveUserIndex))
	{
		SetLoadState(EPropManagerLoadState::LoadingDatabase);
		bLoadedFromSave = LoadLevelPropDatabase(SaveSlotName, SaveUserIndex);
	}

	// 6. Restore or allocate fresh ISIDs for level instances
	SetLoadState(EPropManagerLoadState::RestoringInstances);
	if (!bLoadedFromSave && HasAuthority())
	{
		RestoreLevelInstances();
	}

	// 7. Build ISID -> ICID + ISMI mappings and populate EmptyInstances for any unused ISM slots on the server
	RebuildLevelMappings();
	if (HasAuthority())
	{
		if (UWorld* World = GetWorld())
		{
			if (!World->GetTimerManager().IsTimerActive(SettlingTimerHandle))
			{
				World->GetTimerManager().SetTimer(SettlingTimerHandle, this, &APropManager::CheckSettlingQueue, SettlingCheckInterval, true);
			}
		}
		RebuildLevelEmptyInstances();
		PropManagerReady = true;
		SetLoadState(EPropManagerLoadState::SynchronizingClients);
		PrepareClientPropSync();
	}
}

void APropManager::InitializeIISMComponents()
{
	UWorld* World = GetWorld();
	if (!World) return;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TArray<UInteractableISMComponent*> FoundComps;
		It->GetComponents<UInteractableISMComponent>(FoundComps);
		for (UInteractableISMComponent* Comp : FoundComps)
		{
			if (!Comp) continue;

			// Section 51: Clear EmptyInstances and stale editor ISIDs on the authoritative server before allocating new session ISIDs
			Comp->ClearEmptyInstances(false);
			if (HasAuthority())
			{
				Comp->ISIDToISMI.Empty();
				Comp->ISMIToISID.Empty();
				for (FInteractablePropData& Entry : Comp->PropDatabase)
				{
					Entry.ISID = 0;
				}
			}

			RegisterIISMComponent(Comp, Comp->IISMComponentID);
		}
	}
}

void APropManager::ClearLevelRuntimeState()
{
	// Return any active ASMs to pool via ASMComponent
	if (ASMComponent)
	{
		ASMComponent->ResetActiveASMsToPool();
	}
	ActiveActorByISID.Empty();

	// Clear EmptyInstances and local runtime mappings across all components
	for (TPair<int32, TObjectPtr<UInteractableISMComponent>>& Pair : ComponentsByICID)
	{
		if (Pair.Value)
		{
			Pair.Value->ClearEmptyInstances(false);
			Pair.Value->ISIDToISMI.Empty();
			Pair.Value->ISMIToISID.Empty();
		}
	}

	ISIDData.Empty();
	DirtyISIDs.Empty();
	PendingReleasedToEmptyISIDs.Empty();
	PendingDeletedISIDs.Empty();
	PendingTasks.Empty();

	// Section 47: Reset ISID allocator when a new level/session starts so new ISIDs are created
	NextISID = 10001;
}

bool APropManager::LoadLevelPropDatabase(const FString& SlotName, int32 UserIndex)
{
	return LoadPropDatabase(SlotName, UserIndex);
}

void APropManager::RestoreLevelInstances()
{
	// Allocate new ISIDs for all active ISM instances in the level (Section 45 & 47)
	for (TPair<int32, TObjectPtr<UInteractableISMComponent>>& Pair : ComponentsByICID)
	{
		const int32 ICID = Pair.Key;
		UInteractableISMComponent* Comp = Pair.Value;
		if (!Comp) continue;

		const int32 NumInstances = Comp->GetInstanceCount();
		for (int32 ISMI = 0; ISMI < NumInstances; ++ISMI)
		{
			if (Comp->IsEmptyInstance(ISMI))
			{
				continue;
			}

			int32 ExistingISID = Comp->FindISID(ISMI);
			if (ExistingISID <= 0 || !ISIDData.Contains(ExistingISID))
			{
				const int32 NewISID = AllocateISIDForInstance(Comp->ProductID, ICID, ISMI);
				FTransform InstTransform = FTransform::Identity;
				Comp->GetInstanceTransform(ISMI, InstTransform, true);
				InitializeGeneratedInstance(NewISID, Comp->ProductID, ICID, ISMI, InstTransform);
			}
		}
	}
}

void APropManager::RebuildLevelMappings()
{
	RebuildISMMappings();
}

void APropManager::RebuildLevelEmptyInstances()
{
	RebuildEmptyInstances(0);
}

void APropManager::PrepareClientPropSync()
{
	if (!HasAuthority()) return;

	// Send initial snapshot batches to clients
	SendInitialPropSnapshot(nullptr);

	// In standalone or listen-server host, mark local client sync ready immediately
	PropSyncReady = true;
	SetLoadState(EPropManagerLoadState::Ready);
}

// =========================================================================
// 37.B & Section 22 — Identity & Mapping Functions
// =========================================================================
int32 APropManager::AllocateISID()
{
	while (ISIDData.Contains(NextISID) || NextISID <= 0)
	{
		++NextISID;
	}
	return NextISID++;
}

int32 APropManager::AllocateASMID()
{
	if (ASMComponent)
	{
		return ASMComponent->AllocateASMID();
	}
	while (ActiveActorByASMID.Contains(NextASMID) || NextASMID <= 0)
	{
		++NextASMID;
	}
	return NextASMID++;
}

FISIDRuntimeIndex* APropManager::FindISIDData(int32 InISID)
{
	return ISIDData.Find(InISID);
}

const FISIDRuntimeIndex* APropManager::FindISIDData(int32 InISID) const
{
	return ISIDData.Find(InISID);
}

bool APropManager::K2_FindISIDData(int32 InISID, FISIDRuntimeIndex& OutRecord) const
{
	if (const FISIDRuntimeIndex* Found = FindISIDData(InISID))
	{
		OutRecord = *Found;
		return true;
	}
	return false;
}

bool APropManager::RegisterISID(const FISIDRuntimeIndex& InRecord)
{
	if (InRecord.ISID <= 0) return false;
	ISIDData.Add(InRecord.ISID, InRecord);
	AllActiveISIDs.AddUnique(InRecord.ISID);
	NextISID = FMath::Max(NextISID, InRecord.ISID + 1);
	return true;
}

bool APropManager::UnregisterISID(int32 InISID)
{
	if (ASMComponent)
	{
		ASMComponent->UnregisterActiveActor(InISID);
	}
	ActiveActorByISID.Remove(InISID);
	DirtyISIDs.Remove(InISID);
	AllActiveISIDs.Remove(InISID);
	return ISIDData.Remove(InISID) > 0;
}

int32 APropManager::GetISMI(int32 InISID) const
{
	return GetISMIFromISID(InISID);
}

int32 APropManager::GetISMIFromISID(int32 InISID) const
{
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		return Record->ISMI;
	}
	return INDEX_NONE;
}

int32 APropManager::GetICID(int32 InISID) const
{
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		return Record->IISMComponentID;
	}
	return 0;
}

int32 APropManager::GetProductID(int32 InISID) const
{
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		return Record->ProductID;
	}
	return 0;
}

int32 APropManager::GetASMID(int32 InISID) const
{
	if (ASMComponent)
	{
		const int32 FoundASMID = ASMComponent->GetASMIDFromISID(InISID);
		if (FoundASMID > 0)
		{
			return FoundASMID;
		}
	}
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		return Record->ASMID;
	}
	return 0;
}

bool APropManager::ValidateISID(int32 InISID) const
{
	const FISIDRuntimeIndex* Record = FindISIDData(InISID);
	return Record != nullptr && Record->ISID == InISID;
}

int32 APropManager::GetISIDFromISMI(int32 InICID, int32 InISMI) const
{
	if (UInteractableISMComponent* Comp = FindIISMComponent(InICID))
	{
		return Comp->FindISID(InISMI);
	}
	return 0;
}

UInteractableISMComponent* APropManager::GetComponentFromISID(int32 InISID) const
{
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		if (UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
		{
			return Comp;
		}
	}
	for (const TWeakObjectPtr<UInteractableISMComponent>& WeakComp : RegisteredISMComponents)
	{
		if (UInteractableISMComponent* Comp = WeakComp.Get())
		{
			if (Comp->FindISMI(InISID) != INDEX_NONE)
			{
				return Comp;
			}
		}
	}
	return nullptr;
}

AActivePropActor* APropManager::GetActiveActorFromISID(int32 InISID) const
{
	if (ASMComponent)
	{
		if (AActivePropActor* FoundASM = ASMComponent->GetActiveActorFromISID(InISID))
		{
			return FoundASM;
		}
	}
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		return Record->ActiveActor;
	}
	return nullptr;
}

AActivePropActor* APropManager::GetActiveActorFromASMID(int32 InASMID) const
{
	if (ASMComponent)
	{
		return ASMComponent->GetActiveActorFromASMID(InASMID);
	}
	if (const TObjectPtr<AActivePropActor>* Found = ActiveActorByASMID.Find(InASMID))
	{
		return Found->Get();
	}
	return nullptr;
}

AActivePropActor* APropManager::FindActiveActor(int32 InISID) const
{
	return GetActiveActorFromISID(InISID);
}

void APropManager::RegisterActiveActor(int32 InISID, AActivePropActor* InActor)
{
	if (ASMComponent)
	{
		ASMComponent->RegisterActiveActor(InISID, InActor);
		return;
	}
	if (InISID <= 0 || !InActor) return;
	if (InActor->ASMID <= 0)
	{
		InActor->ASMID = AllocateASMID();
	}
	ActiveActorByASMID.Add(InActor->ASMID, InActor);
	ActiveActorByISID.Add(InISID, InActor);
	if (FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		Record->ActiveActor = InActor;
		Record->ASMID = InActor->ASMID;
		Record->Representation = EPropRepresentation::ASM;
	}
}

void APropManager::UnregisterActiveActor(int32 InISID)
{
	if (ASMComponent)
	{
		ASMComponent->UnregisterActiveActor(InISID);
		return;
	}
	ActiveActorByISID.Remove(InISID);
	if (FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		Record->ActiveActor = nullptr;
		Record->ASMID = 0;
	}
}

UInteractableISMComponent* APropManager::FindIISMComponent(int32 InICID) const
{
	if (InICID <= 0) return nullptr;
	if (const TObjectPtr<UInteractableISMComponent>* Found = ComponentsByICID.Find(InICID))
	{
		if (Found->Get())
		{
			return Found->Get();
		}
	}
	for (const TWeakObjectPtr<UInteractableISMComponent>& WeakComp : RegisteredISMComponents)
	{
		if (UInteractableISMComponent* Comp = WeakComp.Get())
		{
			if (Comp->IISMComponentID == InICID)
			{
				return Comp;
			}
		}
	}
	return nullptr;
}

bool APropManager::RegisterISMI(int32 InISID, int32 InICID, int32 InISMI)
{
	if (InISID <= 0 || InICID <= 0 || InISMI == INDEX_NONE) return false;

	if (FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		Record->IISMComponentID = InICID;
		Record->ISMI = InISMI;
	}

	if (UInteractableISMComponent* Comp = FindIISMComponent(InICID))
	{
		Comp->BindISIDToISMI(InISID, InISMI);
	}
	return true;
}

bool APropManager::UpdateISMI(int32 InISID, int32 NewISMI)
{
	if (FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		Record->ISMI = NewISMI;
		if (UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
		{
			Comp->BindISIDToISMI(InISID, NewISMI);
		}
		return true;
	}
	return false;
}

bool APropManager::RemoveISMI(int32 InISID)
{
	if (FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		if (UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
		{
			Comp->ISIDToISMI.Remove(InISID);
			Comp->ISMIToISID.Remove(Record->ISMI);
		}
		Record->ISMI = INDEX_NONE;
		return true;
	}
	return false;
}

void APropManager::RebuildISMIMap(int32 InICID)
{
	if (UInteractableISMComponent* Comp = FindIISMComponent(InICID))
	{
		Comp->RebuildLocalIndexMap();
		for (const TPair<int32, int32>& Pair : Comp->ISIDToISMI)
		{
			if (FISIDRuntimeIndex* Record = FindISIDData(Pair.Key))
			{
				Record->IISMComponentID = InICID;
				Record->ISMI = Pair.Value;
			}
		}
	}
}

bool APropManager::ValidateISMIMap(int32 InICID) const
{
	const UInteractableISMComponent* Comp = FindIISMComponent(InICID);
	if (!Comp || !Comp->Validate())
	{
		return false;
	}

	for (const TPair<int32, int32>& Pair : Comp->ISIDToISMI)
	{
		const FISIDRuntimeIndex* Record = FindISIDData(Pair.Key);
		if (!Record || Record->IISMComponentID != InICID || Record->ISMI != Pair.Value)
		{
			return false;
		}
	}
	return true;
}

// =========================================================================
// 37.C & Sections 15, 42, 43, 52, 53, 66 — Product Generation & Add Functions
// =========================================================================
void APropManager::RequestAddProp(int32 InProductID, const FTransform& InTransform, AActor* RequestingActor)
{
	if (HasAuthority())
	{
		GenerateProductInstance(InProductID, InTransform);
	}
	else
	{
		TArray<FTransform> SingleBatch = { InTransform };
		Server_RequestGenerateProducts(InProductID, SingleBatch);
	}
}

void APropManager::RequestAddProp(int32 InProductID, int32 Quantity, const FTransform& BaseTransform, AActor* RequestingActor)
{
	RequestAddPropByQuantity(InProductID, Quantity, BaseTransform, RequestingActor);
}

void APropManager::RequestAddPropByQuantity(int32 InProductID, int32 Quantity, const FTransform& BaseTransform, AActor* RequestingActor)
{
	if (Quantity <= 0 || InProductID <= 0) return;

	TArray<FTransform> Transforms;
	Transforms.Reserve(Quantity);
	for (int32 i = 0; i < Quantity; ++i)
	{
		FTransform OffsetTransform = BaseTransform;
		if (i > 0)
		{
			OffsetTransform.AddToTranslation(FVector((i % 10) * 25.0f, ((i / 10) % 10) * 25.0f, (i / 100) * 25.0f));
		}
		Transforms.Add(OffsetTransform);
	}

	if (HasAuthority())
	{
		GenerateProductInstances(InProductID, Transforms);
	}
	else
	{
		Server_RequestGenerateProducts(InProductID, Transforms);
	}
}

void APropManager::RequestAdd(int32 InProductID, const FTransform& InTransform, AActor* RequestingActor)
{
	RequestAddProp(InProductID, InTransform, RequestingActor);
}

void APropManager::Server_RequestGenerateProducts_Implementation(int32 InProductID, const TArray<FTransform>& InTransforms)
{
	GenerateProductInstances(InProductID, InTransforms);
}

bool APropManager::Server_RequestGenerateProducts_Validate(int32 InProductID, const TArray<FTransform>& InTransforms)
{
	return InProductID > 0 && InTransforms.Num() <= 2048;
}

int32 APropManager::AddPropInstance(int32 InProductID, const FTransform& InTransform)
{
	return GenerateProductInstance(InProductID, InTransform);
}

TArray<int32> APropManager::AddPropInstances(int32 InProductID, const TArray<FTransform>& InTransforms)
{
	return GenerateProductInstances(InProductID, InTransforms);
}

TArray<int32> APropManager::GenerateProductInstances(int32 InProductID, const TArray<FTransform>& InTransforms)
{
	EnsureLevelPropsInitialized();

	// Section 53: Server-Side Product Generation Algorithm
	// 1. Validate ProductID.
	TArray<int32> GeneratedISIDs;
	const int32 RequestedQuantity = InTransforms.Num();
	if (InProductID <= 0 || RequestedQuantity <= 0 || !HasAuthority())
	{
		return GeneratedISIDs;
	}

	if (!ProductDefinitions.Contains(InProductID))
	{
		FPropProductDefinition DefaultDef;
		DefaultDef.ProductID = InProductID;
		ProductDefinitions.Add(InProductID, DefaultDef);
	}

	// 2. Find the appropriate IISMComponent.
	UInteractableISMComponent* Comp = GetOrCreateIISMComponent(InProductID);
	if (!Comp)
	{
		return GeneratedISIDs;
	}

	GeneratedISIDs.Reserve(RequestedQuantity);

	// 3, 4, 5. Read EmptyInstances and reuse as many Empty ISMI slots as possible.
	const TArray<int32> ReusedISIDs = GenerateProductInstancesFromEmpty(InProductID, RequestedQuantity, InTransforms);
	GeneratedISIDs.Append(ReusedISIDs);

	// 6. Calculate RemainingQuantity = RequestedQuantity - ReusedQuantity.
	const int32 ReusedQuantity = ReusedISIDs.Num();
	const int32 RemainingQuantity = RequestedQuantity - ReusedQuantity;

	// 7. If RemainingQuantity > 0, add new ISM instances, allocate new ISIDs, initialize runtime data, register mappings.
	if (RemainingQuantity > 0)
	{
		TArray<FTransform> RemainingTransforms;
		RemainingTransforms.Reserve(RemainingQuantity);
		for (int32 i = ReusedQuantity; i < RequestedQuantity; ++i)
		{
			RemainingTransforms.Add(InTransforms[i]);
		}

		const TArray<int32> NewlyCreatedISIDs = GenerateNewISMInstances(InProductID, RemainingQuantity, RemainingTransforms);
		GeneratedISIDs.Append(NewlyCreatedISIDs);
	}

	return GeneratedISIDs;
}

TArray<int32> APropManager::GenerateProductInstances(int32 InProductID, int32 Quantity, const FTransform& BaseTransform)
{
	return GenerateProductInstancesByQuantity(InProductID, Quantity, BaseTransform);
}

TArray<int32> APropManager::GenerateProductInstancesByQuantity(int32 InProductID, int32 Quantity, const FTransform& BaseTransform)
{
	TArray<FTransform> Transforms;
	if (Quantity <= 0) return TArray<int32>();

	Transforms.Reserve(Quantity);
	for (int32 i = 0; i < Quantity; ++i)
	{
		FTransform OffsetTransform = BaseTransform;
		if (i > 0)
		{
			OffsetTransform.AddToTranslation(FVector((i % 10) * 25.0f, ((i / 10) % 10) * 25.0f, (i / 100) * 25.0f));
		}
		Transforms.Add(OffsetTransform);
	}
	return GenerateProductInstances(InProductID, Transforms);
}

int32 APropManager::GenerateProductInstance(int32 InProductID, const FTransform& InTransform)
{
	EnsureLevelPropsInitialized();

	if (InProductID <= 0 || !HasAuthority()) return 0;

	if (!ProductDefinitions.Contains(InProductID))
	{
		FPropProductDefinition DefaultDef;
		DefaultDef.ProductID = InProductID;
		ProductDefinitions.Add(InProductID, DefaultDef);
	}

	// Rule 29: Generating a product must check EmptyInstances before creating new ISM instances.
	int32 FoundICID = 0;
	const int32 EmptyISMI = FindEmptyInstance(InProductID, FoundICID);
	if (EmptyISMI != INDEX_NONE && FoundICID > 0)
	{
		return ReuseEmptyInstance(InProductID, FoundICID, EmptyISMI, InTransform);
	}

	const TArray<int32> Created = GenerateNewISMInstances(InProductID, 1, { InTransform });
	return Created.Num() > 0 ? Created[0] : 0;
}

TArray<int32> APropManager::GenerateProductInstancesFromEmpty(int32 InProductID, int32 MaxQuantity, const TArray<FTransform>& InTransforms)
{
	TArray<int32> ReusedISIDs;
	const int32 EffectiveMaxQuantity = (InTransforms.Num() > 0)
		? ((MaxQuantity > 0) ? FMath::Min(MaxQuantity, InTransforms.Num()) : InTransforms.Num())
		: MaxQuantity;

	if (InProductID <= 0 || EffectiveMaxQuantity <= 0)
	{
		return ReusedISIDs;
	}

	int32 TargetICID = 0;
	TArray<int32> AvailableEmptyISMIs;
	const int32 FoundCount = FindEmptyInstances(InProductID, EffectiveMaxQuantity, TargetICID, AvailableEmptyISMIs);
	if (FoundCount <= 0 || TargetICID <= 0)
	{
		return ReusedISIDs;
	}

	ReusedISIDs.Reserve(FoundCount);
	for (int32 i = 0; i < FoundCount; ++i)
	{
		const FTransform& InstTransform = InTransforms.IsValidIndex(i) ? InTransforms[i] : FTransform::Identity;
		const int32 NewISID = ReuseEmptyInstance(InProductID, TargetICID, AvailableEmptyISMIs[i], InstTransform);
		if (NewISID > 0)
		{
			ReusedISIDs.Add(NewISID);
		}
	}

	return ReusedISIDs;
}

TArray<int32> APropManager::GenerateNewISMInstances(int32 InProductID, int32 Quantity, const TArray<FTransform>& InTransforms)
{
	EnsureLevelPropsInitialized();

	TArray<int32> CreatedISIDs;
	const int32 EffectiveQuantity = (InTransforms.Num() > 0)
		? ((Quantity > 0) ? FMath::Min(Quantity, InTransforms.Num()) : InTransforms.Num())
		: Quantity;

	if (InProductID <= 0 || EffectiveQuantity <= 0 || !HasAuthority())
	{
		return CreatedISIDs;
	}

	UInteractableISMComponent* Comp = GetOrCreateIISMComponent(InProductID);
	if (!Comp)
	{
		return CreatedISIDs;
	}

	const FPropProductDefinition* ProductDef = ProductDefinitions.Find(InProductID);
	FPropPackedRuntimeData InitialPacked = ProductDef ? ProductDef->RuntimeDefaults : FPropPackedRuntimeData();
	const int32 CurrentMinutes = GetCurrentWorldMinutes();
	InitialPacked.CreationMinute = CurrentMinutes;
	InitialPacked.LastUpdatedMinute = CurrentMinutes;

	CreatedISIDs.Reserve(EffectiveQuantity);
	for (int32 i = 0; i < EffectiveQuantity; ++i)
	{
		const FTransform& InstTransform = InTransforms.IsValidIndex(i) ? InTransforms[i] : FTransform::Identity;
		const int32 NewISID = AllocateISID();
		const int32 NewISMI = Comp->AddInstanceWithISID(NewISID, InstTransform, InitialPacked, true);
		if (NewISMI != INDEX_NONE)
		{
			InitializeGeneratedInstance(NewISID, InProductID, Comp->IISMComponentID, NewISMI, InstTransform);
			CreatedISIDs.Add(NewISID);
		}
	}

	return CreatedISIDs;
}

int32 APropManager::AllocateISIDForInstance(int32 InProductID, int32 InICID, int32 InISMI)
{
	const int32 NewISID = AllocateISID();
	if (UInteractableISMComponent* Comp = FindIISMComponent(InICID))
	{
		Comp->BindISIDToISMI(NewISID, InISMI);
	}
	return NewISID;
}

bool APropManager::InitializeGeneratedInstance(int32 InISID, int32 InProductID, int32 InICID, int32 InISMI, const FTransform& InTransform)
{
	if (InISID <= 0 || InICID <= 0 || InISMI == INDEX_NONE)
	{
		return false;
	}

	RegisterInstance(InISID, InProductID, InICID, InISMI, InTransform);
	if (const FPropProductDefinition* ProductDef = ProductDefinitions.Find(InProductID))
	{
		InitializeInstanceData(InISID, *ProductDef);
	}
	InitializeInstanceTransform(InISID, InTransform);
	MarkInstanceDirty(InISID, EPropDirtyFlags::Created | EPropDirtyFlags::Transform | EPropDirtyFlags::State | EPropDirtyFlags::RuntimeData);
	return true;
}

TArray<int32> APropManager::CreateInstanceBatch(int32 InProductID, const TArray<FTransform>& InTransforms)
{
	// If the batch is large, also register a Task record for tracking (Section 35)
	if (InTransforms.Num() > MaxInstancesPerFrame)
	{
		const int32 TaskID = CreateTask(EPropTaskType::GenerateProps, 0, NextRequestID++);
		for (FPropTaskRecord& Task : PendingTasks)
		{
			if (Task.TaskID == TaskID)
			{
				Task.ProductID = InProductID;
				Task.PendingTransforms = InTransforms;
				Task.Status = EPropTaskStatus::InProgress;
				break;
			}
		}
	}

	return GenerateProductInstances(InProductID, InTransforms);
}

bool APropManager::RegisterInstance(int32 InISID, int32 InProductID, int32 InICID, int32 InISMI, const FTransform& InTransform)
{
	if (InISID <= 0) return false;

	const int32 NowMin = GetCurrentWorldMinutes();

	FISIDRuntimeIndex Record;
	Record.ISID = InISID;
	Record.ProductID = InProductID;
	Record.IISMComponentID = InICID;
	Record.ComponentRef = FindIISMComponent(InICID);
	Record.ISMI = InISMI;
	Record.Representation = EPropRepresentation::ISM;
	Record.State = EPropState::AtRest;
	Record.Version = 1;
	Record.ActiveTransform = InTransform;
	Record.PackedRuntimeData.CreationMinute = NowMin;
	Record.PackedRuntimeData.LastUpdatedMinute = NowMin;
	Record.DirtyFlags = EPropDirtyFlags::Created;

	RegisterISID(Record);
	RegisterISMI(InISID, InICID, InISMI);
	return true;
}

bool APropManager::InitializeInstance(int32 InISID, int32 InProductID, const FTransform& InTransform)
{
	if (!InitializeInstanceTransform(InISID, InTransform))
	{
		return false;
	}
	if (const FPropProductDefinition* Def = ProductDefinitions.Find(InProductID))
	{
		return InitializeInstanceData(InISID, *Def);
	}
	return true;
}

bool APropManager::InitializeInstanceData(int32 InISID, const FPropProductDefinition& ProductDef)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	const int32 NowMin = GetCurrentWorldMinutes();
	Record->ProductID = ProductDef.ProductID;
	Record->PackedRuntimeData = ProductDef.RuntimeDefaults;
	Record->PackedRuntimeData.CreationMinute = NowMin;
	Record->PackedRuntimeData.LastUpdatedMinute = NowMin;
	Record->RuntimeData.RemainingPortions = ProductDef.ConsumableProfile.NumOfPortions;
	Record->RuntimeData.Temperature = ProductDef.TemperatureProfile.ConsumableTemperature;

	if (UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
	{
		Comp->SetInstanceData(InISID, Record->PackedRuntimeData);
	}
	return true;
}

bool APropManager::InitializeInstanceTransform(int32 InISID, const FTransform& InTransform)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	Record->ActiveTransform = InTransform;
	if (Record->Representation == EPropRepresentation::ISM)
	{
		if (UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
		{
			Comp->SetInstanceTransformByISID(InISID, InTransform, true, true, true);
		}
	}
	else if (Record->ActiveActor)
	{
		Record->ActiveActor->SetActorTransform(InTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}
	return true;
}

// =========================================================================
// Sections 40, 41, 54, 55, 66 — Empty Instance Management & Product Conversion
// =========================================================================
bool APropManager::CanComponentReuseForProduct(const UInteractableISMComponent* Comp, int32 TargetProductID) const
{
	if (!Comp || TargetProductID <= 0) return false;
	if (Comp->ProductID == TargetProductID) return true;

	// Section 55: Validate mesh, material, collision, and product-definition compatibility for cross-product reuse
	const FPropProductDefinition* TargetDef = ProductDefinitions.Find(TargetProductID);
	if (!TargetDef) return false;

	if (Comp->GetStaticMesh() && TargetDef->StaticMesh && Comp->GetStaticMesh() != TargetDef->StaticMesh)
	{
		return false;
	}

	if (TargetDef->MaterialOverrides.Num() > 0)
	{
		for (int32 MatIdx = 0; MatIdx < TargetDef->MaterialOverrides.Num(); ++MatIdx)
		{
			if (TargetDef->MaterialOverrides[MatIdx] && Comp->GetMaterial(MatIdx) != TargetDef->MaterialOverrides[MatIdx])
			{
				return false;
			}
		}
	}

	return true;
}

int32 APropManager::FindEmptyInstance(int32 InProductID, int32& OutICID) const
{
	OutICID = 0;
	TArray<int32> FoundISMIs;
	if (FindEmptyInstances(InProductID, 1, OutICID, FoundISMIs) > 0)
	{
		return FoundISMIs[0];
	}
	return INDEX_NONE;
}

int32 APropManager::FindEmptyInstances(int32 InProductID, int32 MaxCount, int32& OutICID, TArray<int32>& OutISMIs) const
{
	OutICID = 0;
	OutISMIs.Empty();
	if (MaxCount <= 0) return 0;

	// 1. Prefer exact ProductID match first
	for (const TPair<int32, TObjectPtr<UInteractableISMComponent>>& Pair : ComponentsByICID)
	{
		const UInteractableISMComponent* Comp = Pair.Value;
		if (Comp && Comp->ProductID == InProductID && Comp->GetEmptyInstanceCount() > 0)
		{
			OutICID = Pair.Key;
			const int32 CountToTake = FMath::Min(MaxCount, Comp->GetEmptyInstanceCount());
			for (int32 i = 0; i < CountToTake; ++i)
			{
				OutISMIs.Add(Comp->GetEmptyInstance(i));
			}
			return OutISMIs.Num();
		}
	}

	// 2. Check cross-product compatible components (Section 55)
	for (const TPair<int32, TObjectPtr<UInteractableISMComponent>>& Pair : ComponentsByICID)
	{
		const UInteractableISMComponent* Comp = Pair.Value;
		if (Comp && Comp->GetEmptyInstanceCount() > 0 && CanComponentReuseForProduct(Comp, InProductID))
		{
			OutICID = Pair.Key;
			const int32 CountToTake = FMath::Min(MaxCount, Comp->GetEmptyInstanceCount());
			for (int32 i = 0; i < CountToTake; ++i)
			{
				OutISMIs.Add(Comp->GetEmptyInstance(i));
			}
			return OutISMIs.Num();
		}
	}

	return 0;
}

int32 APropManager::ReuseEmptyInstance(int32 InProductID, int32 InICID, int32 InISMI, const FTransform& InTransform)
{
	UInteractableISMComponent* Comp = FindIISMComponent(InICID);
	if (!Comp || !Comp->IsValidInstance(InISMI))
	{
		return 0;
	}

	// Section 53 Step 5 & Section 64:
	// - Remove Empty status
	Comp->RemoveEmptyInstance(InISMI);

	// - Allocate a new ISID
	const int32 NewISID = AllocateISIDForInstance(InProductID, InICID, InISMI);

	// - Initialize product data, assign ProductID, register ISID -> ICID + ISMI, set transform, initialize runtime state
	const FPropProductDefinition* ProductDef = ProductDefinitions.Find(InProductID);
	FPropPackedRuntimeData InitialPacked = ProductDef ? ProductDef->RuntimeDefaults : FPropPackedRuntimeData();
	const int32 NowMin = GetCurrentWorldMinutes();
	InitialPacked.CreationMinute = NowMin;
	InitialPacked.LastUpdatedMinute = NowMin;

	Comp->ReuseOrAddInstanceWithISID(NewISID, InTransform, InitialPacked, InISMI, true);
	InitializeGeneratedInstance(NewISID, InProductID, InICID, InISMI, InTransform);

	return NewISID;
}

bool APropManager::ReleaseInstanceToEmpty(int32 InISID)
{
	// Section 41 & 64:
	// ISID -> Remove logical association -> Clear instance runtime data -> Move ISM instance to ResetTransform -> Add ISMI to EmptyInstances
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record)
	{
		return false;
	}

	const int32 CompICID = Record->IISMComponentID;
	const int32 SlotISMI = Record->ISMI;

	// If currently promoted to ASM, return the ASM to the pool first
	if (AActivePropActor* ActiveASM = GetActiveActorFromISID(InISID))
	{
		ReturnActiveActorToPool(ActiveASM);
	}

	// Clear logical association from authoritative ISIDData
	UnregisterInstance(InISID);

	// Move ISM instance to ResetTransform, clear runtime data, and add ISMI to EmptyInstances
	AddEmptyInstance(CompICID, SlotISMI);

	// Queue for client synchronization so clients also release their slot to EmptyInstances
	PendingReleasedToEmptyISIDs.AddUnique(InISID);
	WorldSaveData.DestroyedISIDs.AddUnique(InISID);

	return true;
}

bool APropManager::AddEmptyInstance(int32 InICID, int32 InISMI)
{
	if (UInteractableISMComponent* Comp = FindIISMComponent(InICID))
	{
		return Comp->AddEmptyInstance(InISMI);
	}
	return false;
}

bool APropManager::RemoveEmptyInstance(int32 InICID, int32 InISMI)
{
	if (UInteractableISMComponent* Comp = FindIISMComponent(InICID))
	{
		return Comp->RemoveEmptyInstance(InISMI);
	}
	return false;
}

void APropManager::ClearEmptyInstances(int32 InICID)
{
	if (InICID > 0)
	{
		if (UInteractableISMComponent* Comp = FindIISMComponent(InICID))
		{
			Comp->ClearEmptyInstances(false);
		}
		return;
	}

	for (TPair<int32, TObjectPtr<UInteractableISMComponent>>& Pair : ComponentsByICID)
	{
		if (Pair.Value)
		{
			Pair.Value->ClearEmptyInstances(false);
		}
	}
}

void APropManager::RebuildEmptyInstances(int32 InICID)
{
	if (InICID > 0)
	{
		if (UInteractableISMComponent* Comp = FindIISMComponent(InICID))
		{
			Comp->RebuildEmptyInstances();
		}
		return;
	}

	for (TPair<int32, TObjectPtr<UInteractableISMComponent>>& Pair : ComponentsByICID)
	{
		if (Pair.Value)
		{
			Pair.Value->RebuildEmptyInstances();
		}
	}
}

int32 APropManager::GetEmptyInstanceCount(int32 InProductID) const
{
	int32 TotalEmpty = 0;
	for (const TPair<int32, TObjectPtr<UInteractableISMComponent>>& Pair : ComponentsByICID)
	{
		if (Pair.Value)
		{
			if (InProductID <= 0 || Pair.Value->ProductID == InProductID)
			{
				TotalEmpty += Pair.Value->GetEmptyInstanceCount();
			}
		}
	}
	return TotalEmpty;
}

TArray<int32> APropManager::ConvertProduct(int32 SourceProductID, int32 TargetProductID, int32 Quantity, const TArray<FTransform>& TargetTransforms)
{
	// Section 54: Find Quantity SourceProductID ISIDs and convert them into TargetProductID
	if (SourceProductID <= 0 || TargetProductID <= 0 || Quantity <= 0)
	{
		return TArray<int32>();
	}

	TArray<int32> SourceISIDs;
	SourceISIDs.Reserve(Quantity);
	for (const TPair<int32, FISIDRuntimeIndex>& Pair : ISIDData)
	{
		if (Pair.Value.ProductID == SourceProductID)
		{
			SourceISIDs.Add(Pair.Key);
			if (SourceISIDs.Num() >= Quantity)
			{
				break;
			}
		}
	}

	return ConvertProductInstances(SourceISIDs, TargetProductID, TargetTransforms);
}

TArray<int32> APropManager::ConvertProductInstances(const TArray<int32>& SourceISIDs, int32 TargetProductID, const TArray<FTransform>& TargetTransforms)
{
	// Section 54:
	// Find Source ISIDs -> Validate -> Remove logical associations -> Move ISM representations to ResetTransform -> Add ISMIs to EmptyInstances -> Generate TargetProductID instances reusing EmptyInstances -> Allocate new ISIDs -> Synchronize clients
	if (SourceISIDs.Num() == 0 || TargetProductID <= 0)
	{
		return TArray<int32>();
	}

	TArray<FTransform> EffectiveTransforms;
	EffectiveTransforms.Reserve(SourceISIDs.Num());
	for (int32 i = 0; i < SourceISIDs.Num(); ++i)
	{
		if (TargetTransforms.IsValidIndex(i))
		{
			EffectiveTransforms.Add(TargetTransforms[i]);
		}
		else
		{
			EffectiveTransforms.Add(GetTransform(SourceISIDs[i]));
		}
	}

	const int32 ConsumedCount = ConsumeProductInstances(SourceISIDs);
	if (ConsumedCount <= 0)
	{
		return TArray<int32>();
	}

	if (EffectiveTransforms.Num() > ConsumedCount)
	{
		EffectiveTransforms.SetNum(ConsumedCount);
	}

	return GenerateProductInstances(TargetProductID, EffectiveTransforms);
}

int32 APropManager::ConsumeProductInstances(const TArray<int32>& SourceISIDs)
{
	int32 Consumed = 0;
	for (int32 SourceISID : SourceISIDs)
	{
		if (ReleaseInstanceToEmpty(SourceISID))
		{
			++Consumed;
		}
	}
	return Consumed;
}

TArray<int32> APropManager::ReplaceProductInstances(const TArray<int32>& SourceISIDs, int32 TargetProductID)
{
	return ConvertProductInstances(SourceISIDs, TargetProductID, TArray<FTransform>());
}

TArray<int32> APropManager::ConvertInstancesToProduct(const TArray<int32>& SourceISIDs, int32 TargetProductID, const TArray<FTransform>& TargetTransforms)
{
	return ConvertProductInstances(SourceISIDs, TargetProductID, TargetTransforms);
}

// =========================================================================
// 37.D & Section 16 & 64 — Prop Remove / Destroy Functions
// =========================================================================
void APropManager::RequestRemoveProp(int32 InISID, AActor* RequestingActor)
{
	if (HasAuthority())
	{
		if (ValidateRemoveProp(InISID, RequestingActor))
		{
			RemoveProp(InISID);
		}
	}
	else
	{
		Server_RequestRemoveProp(InISID, RequestingActor);
	}
}

void APropManager::RequestRemove(int32 InISID, AActor* RequestingActor)
{
	RequestRemoveProp(InISID, RequestingActor);
}

void APropManager::Server_RequestRemoveProp_Implementation(int32 InISID, AActor* RequestingActor)
{
	if (ValidateRemoveProp(InISID, RequestingActor))
	{
		RemoveProp(InISID);
	}
}

bool APropManager::Server_RequestRemoveProp_Validate(int32 InISID, AActor* RequestingActor)
{
	return InISID > 0;
}

bool APropManager::ValidateRemoveProp(int32 InISID, AActor* RequestingActor) const
{
	const FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	// Reject deletion if another actor exclusively holds this prop
	if (Record->RuntimeData.InteractionState == EPropInteractionLockState::Held &&
		Record->RuntimeData.InteractionOwner.IsValid() &&
		RequestingActor != nullptr &&
		Record->RuntimeData.InteractionOwner.Get() != RequestingActor)
	{
		return false;
	}
	return true;
}

bool APropManager::RemoveProp(int32 InISID)
{
	// Section 64 & 70 Rule 28: Removing a logical product instance clears its logical association and moves its ISMI to EmptyInstances
	return ReleaseInstanceToEmpty(InISID);
}

bool APropManager::RemovePropInstance(int32 InISID)
{
	return ReleaseInstanceToEmpty(InISID);
}

bool APropManager::DestroyPropInstance(int32 InISID)
{
	// Permanent physical ISM slot destruction (Section 16 Steps 22-30):
	// 22. Validate ISID.
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record)
	{
		return false;
	}

	const int32 CompICID = Record->IISMComponentID;

	// 23 & 24. Check whether an ASM is active; if ASM exists, disable/return it to the pool.
	if (AActivePropActor* ActiveASM = GetActiveActorFromISID(InISID))
	{
		ReturnActiveActorToPool(ActiveASM);
	}

	// 25 & 26. Remove the ISM instance and repair affected ISMI mappings.
	RemoveISMInstance(CompICID, InISID);

	// 27 & 28. Remove ISID from authoritative database and ActiveActorByISID.
	UnregisterInstance(InISID);

	// 29. Mark deletion for replication.
	PendingDeletedISIDs.AddUnique(InISID);

	// 30. Persist deletion if save/load is enabled.
	WorldSaveData.DestroyedISIDs.AddUnique(InISID);

	return true;
}

bool APropManager::UnregisterInstance(int32 InISID)
{
	UnregisterActiveActor(InISID);
	return UnregisterISID(InISID);
}

bool APropManager::RemoveISMInstance(int32 InICID, int32 InISID)
{
	UInteractableISMComponent* Comp = FindIISMComponent(InICID);
	if (!Comp) return false;

	TMap<int32, int32> ShiftedMappings;
	const bool bRemoved = Comp->RemoveInstanceByISIDWithRepair(InISID, ShiftedMappings);
	if (bRemoved)
	{
		// Step 26: Repair affected ISMI mappings in authoritative ISIDData
		for (const TPair<int32, int32>& ShiftedPair : ShiftedMappings)
		{
			if (FISIDRuntimeIndex* ShiftedRecord = FindISIDData(ShiftedPair.Key))
			{
				ShiftedRecord->ISMI = ShiftedPair.Value;
			}
		}
	}
	return bRemoved;
}

void APropManager::RepairISMIMap(int32 InICID)
{
	RebuildISMIMap(InICID);
}

// =========================================================================
// 37.E & Section 7 & 21 — Representation Management (ISM <-> ASM)
// =========================================================================
bool APropManager::ConvertISMToASM(
	uint32 ISID,
	EPropActivationReason Reason,
	bool bEnableReplication,
	AActivePropActor*& OutActor)
{
	OutActor = nullptr;
	if (!ASMComponent)
	{
		return false;
	}

	const int32 SignedISID = static_cast<int32>(ISID);
	FISIDRuntimeIndex* Record = FindISIDData(SignedISID);
	if (!Record)
	{
		return false;
	}

	if (Record->Representation == EPropRepresentation::ASM)
	{
		if (Record->ActiveActor)
		{
			OutActor = Record->ActiveActor;
			return true;
		}
		if (AActivePropActor* ExistingASM = GetActiveActorFromISID(SignedISID))
		{
			Record->ActiveActor = ExistingASM;
			OutActor = ExistingASM;
			return true;
		}
		return true;
	}

	if (PendingConversionISIDs.Contains(SignedISID))
	{
		return false;
	}

	PendingConversionISIDs.Add(SignedISID);

	const int32 PID = Record->ProductID;
	const int32 ICID = Record->IISMComponentID;
	const int32 ISMI = Record->ISMI;
	UInteractableISMComponent* ISMComp = FindIISMComponent(ICID);
	if (!ISMComp && PID > 0)
	{
		ISMComp = GetOrCreateIISMComponent(PID);
	}
	const FPropProductDefinition* ProductDef = ProductDefinitions.Find(PID);
	FPropProductDefinition FallbackDef;
	if ((!ProductDef || !ProductDef->StaticMesh) && ISMComp)
	{
		const int32 ResolvedProductID = PID > 0 ? PID : ISMComp->ProductID;
		if (ISMComp->IISMDB.StaticMesh)
		{
			FallbackDef = ISMComp->IISMDB;
			FallbackDef.ProductID = ResolvedProductID;
		}
		else if (ISMComp->DefaultDataAsset)
		{
			FallbackDef = FPropProductDefinition::FromDataAsset(ISMComp->DefaultDataAsset, ResolvedProductID, ISMComp->StaticMeshID);
		}
		else if (ISMComp->GetStaticMesh())
		{
			FallbackDef.ProductID = ResolvedProductID;
			FallbackDef.StaticMeshID = (ISMComp->StaticMeshID > 0) ? ISMComp->StaticMeshID : ResolvedProductID;
			FallbackDef.StaticMesh = ISMComp->GetStaticMesh();
			FallbackDef.DisplayName = FText::FromName(ISMComp->GetStaticMesh()->GetFName());
		}

		if (FallbackDef.MaterialOverrides.Num() == 0)
		{
			const int32 NumMats = ISMComp->GetNumMaterials();
			for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
			{
				FallbackDef.MaterialOverrides.Add(ISMComp->GetMaterial(MatIdx));
			}
		}

		if (FallbackDef.ProductID > 0 && FallbackDef.StaticMesh)
		{
			RegisterProductDefinition(FallbackDef);
			ProductDef = ProductDefinitions.Find(FallbackDef.ProductID);
		}
		if (!ProductDef && FallbackDef.StaticMesh)
		{
			ProductDef = &FallbackDef;
		}
	}

	// Read current ISM transform & runtime data before parking ISM
	FTransform GameplayTransform = Record->ActiveTransform;
	if (ISMComp && ISMI != INDEX_NONE && ISMComp->IsValidInstance(ISMI))
	{
		ISMComp->BindISIDToISMI(SignedISID, ISMI);

		FTransform CurrentISMTransform;
		if (ISMComp->GetInstanceTransform(ISMI, CurrentISMTransform, true))
		{
			if (!ISMComp->IsInstanceParkedAtResetTransform(ISMI))
			{
				GameplayTransform = CurrentISMTransform;
				Record->ActiveTransform = GameplayTransform;
			}
		}

		FPropPackedRuntimeData ISMRuntimeData;
		if (ISMComp->GetInstanceData(SignedISID, ISMRuntimeData))
		{
			Record->PackedRuntimeData = ISMRuntimeData;
		}
	}

	// Park the persistent ISM instance at ResetTransform first (Rule 1: never removed on promotion)
	// so it is already out of the way when the ASM becomes active and enables collision.
	if (ISMComp)
	{
		if (ISMI != INDEX_NONE && ISMComp->IsValidInstance(ISMI))
		{
			ISMComp->BindISIDToISMI(SignedISID, ISMI);
		}
		if (!ISMComp->MoveInstanceToResetTransform(SignedISID) && ISMI != INDEX_NONE && ISMComp->IsValidInstance(ISMI))
		{
			ISMComp->UpdateInstanceTransform(ISMI, ISMComp->GetEffectiveResetTransform(), true, true, true);
		}
	}

	// Delegate ASM acquisition, data initialization, OnBecomeActive event, and collision/physics activation to ASMComponent
	if (!ASMComponent->PromoteRecordToASM(*Record, ProductDef, GameplayTransform, Reason, bEnableReplication, OutActor))
	{
		PendingConversionISIDs.Remove(SignedISID);
		if (ISMComp)
		{
			ISMComp->RestoreInstanceToGameplayTransform(SignedISID, GameplayTransform);
		}
		return false;
	}

	PendingConversionISIDs.Remove(SignedISID);
	QueuedPromotionISIDs.Remove(SignedISID);

	if (HasAuthority())
	{
		const int32 OutASMID = OutActor ? OutActor->ASMID : Record->ASMID;
		Multicast_NotifyISMConvertedToASM(SignedISID, PID, OutASMID, GameplayTransform, Record->State, Reason);
		MarkInstanceDirty(SignedISID, EPropDirtyFlags::Representation | EPropDirtyFlags::State | EPropDirtyFlags::Transform);
	}
	OnPropRepresentationChanged.Broadcast(SignedISID, EPropRepresentation::ASM, OutActor);
	return true;
}

bool APropManager::ConvertASMToISM(
	uint32 ISID,
	EPropState NewState,
	bool bPoolActor,
	bool bWriteBackTransform)
{
	if (!ASMComponent)
	{
		return false;
	}

	const int32 SignedISID = static_cast<int32>(ISID);
	FISIDRuntimeIndex* Record = FindISIDData(SignedISID);
	if (!Record)
	{
		return false;
	}

	const int32 SavedASMID = (Record->ASMID > 0) ? Record->ASMID : (Record->ActiveActor ? Record->ActiveActor->ASMID : 0);
	const int32 PID = Record->ProductID;

	UInteractableISMComponent* ISMComp = FindIISMComponent(Record->IISMComponentID);
	if (!ISMComp && Record->ProductID > 0)
	{
		ISMComp = GetOrCreateIISMComponent(Record->ProductID);
	}

	// Delegate ASM state readback, unbinding, deactivation, and return to EmptyASMIDs to ASMComponent
	ASMComponent->DemoteRecordFromASM(*Record, NewState, bPoolActor, bWriteBackTransform);
	Record->PackedRuntimeData.LastUpdatedMinute = GetCurrentWorldMinutes();

	PendingConversionISIDs.Remove(SignedISID);
	QueuedPromotionISIDs.Remove(SignedISID);

	const FTransform FinalTransform = Record->ActiveTransform;

	// Restore the SAME ISM instance back to gameplay transform and update its runtime data
	if (ISMComp)
	{
		if (Record->ISMI != INDEX_NONE && ISMComp->IsValidInstance(Record->ISMI))
		{
			ISMComp->BindISIDToISMI(SignedISID, Record->ISMI);
		}
		ISMComp->RestoreInstanceToGameplayTransform(SignedISID, FinalTransform);
		ISMComp->SetInstanceData(SignedISID, Record->PackedRuntimeData);
	}

	if (HasAuthority())
	{
		Multicast_NotifyASMConvertedToISM(SignedISID, PID, SavedASMID, FinalTransform, NewState);
		MarkInstanceDirty(SignedISID, EPropDirtyFlags::Representation | EPropDirtyFlags::Transform | EPropDirtyFlags::State | EPropDirtyFlags::RuntimeData);
	}
	OnPropRepresentationChanged.Broadcast(SignedISID, EPropRepresentation::ISM, nullptr);
	return true;
}

bool APropManager::PromoteInstanceToASM(int32 InISID, EPropActivationReason Reason, bool bEnableReplication, AActivePropActor*& OutActor)
{
	if (InISID <= 0)
	{
		OutActor = nullptr;
		return false;
	}
	return ConvertISMToASM(static_cast<uint32>(InISID), Reason, bEnableReplication, OutActor);
}

bool APropManager::DemoteActorToISM(int32 InISID, EPropState NewState, bool bPoolActor, bool bWriteBackTransform)
{
	if (InISID <= 0) return false;
	return ConvertASMToISM(static_cast<uint32>(InISID), NewState, bPoolActor, bWriteBackTransform);
}

int32 APropManager::ConvertPlacedActorToISM(AActivePropActor* InActor, bool bForceConvert)
{
	if (!IsValid(InActor))
	{
		return 0;
	}

	if (!bForceConvert && InActor->State != EPropState::AtRest)
	{
		return 0;
	}

	EnsureLevelPropsInitialized();

	// Register product if not yet registered
	if (!IsProductRegistered(InActor->ProductID))
	{
		RegisterProductFromActor(InActor);
	}
	if (InActor->ProductID <= 0)
	{
		return 0;
	}

	UInteractableISMComponent* ISMComp = GetOrCreateIISMComponent(InActor->ProductID);
	if (!ISMComp)
	{
		return 0;
	}

	// Only server/standalone authoritatively creates ISM instances and persists mappings
	if (!HasAuthority())
	{
		return InActor->ISID;
	}

	const int32 NewISID = (InActor->ISID > 0) ? InActor->ISID : AllocateISID();
	InActor->ISID = NewISID;

	const FTransform ActorTransform = InActor->GetActorTransform();

	FPropPackedRuntimeData Packed = InActor->PackedRuntimeData;
	Packed.RemainingPortions = InActor->RemainingPortions;
	Packed.Temperature = InActor->CurrentTemperature;
	Packed.bIsGrabbable = InActor->bIsGrabbable;
	const int32 NowMinutes = GetCurrentWorldMinutes();
	Packed.CreationMinute = NowMinutes;
	Packed.LastUpdatedMinute = NowMinutes;

	const int32 NewISMI = ISMComp->AddInstanceWithISID(NewISID, ActorTransform, Packed, true);
	if (NewISMI == INDEX_NONE)
	{
		return 0;
	}

	InitializeGeneratedInstance(NewISID, InActor->ProductID, ISMComp->IISMComponentID, NewISMI, ActorTransform);
	FISIDRuntimeIndex* Record = FindISIDData(NewISID);
	if (Record)
	{
		Record->Representation = EPropRepresentation::ISM;
		Record->State = EPropState::AtRest;
		Record->ActiveTransform = ActorTransform;
		Record->PackedRuntimeData = Packed;
		Record->ActiveActor = nullptr;
		Record->ASMID = 0;
	}

	// Deactivate the placed actor and park it into the ASMComponent pool at ResetTransform
	InActor->DeactivateToPoolState(GetResetTransformForSlot(InActor->ASMID));
	InActor->SetOwner(this);
	if (ASMComponent)
	{
		ASMComponent->ASMActors.AddUnique(InActor);
		if (InActor->ASMID > 0)
		{
			ASMComponent->EmptyASMIDs.AddUnique(InActor->ASMID);
		}
	}

	OnPropRepresentationChanged.Broadcast(NewISID, EPropRepresentation::ISM, nullptr);
	MarkInstanceDirty(NewISID, EPropDirtyFlags::All);

	return NewISID;
}

AActivePropActor* APropManager::MakeISMToASM(
	int32 InISID,
	EPropState TargetState,
	EPropActivationReason Reason,
	bool bSimulatePhysics)
{
	if (InISID <= 0)
	{
		return nullptr;
	}

	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record)
	{
		return nullptr;
	}

	// If already an active ASM, update its state/physics if needed and return it
	if (Record->Representation == EPropRepresentation::ASM)
	{
		AActivePropActor* ExistingASM = Record->ActiveActor.Get();
		if (!ExistingASM)
		{
			ExistingASM = GetActiveActorFromISID(InISID);
			Record->ActiveActor = ExistingASM;
		}
		if (ExistingASM)
		{
			if (TargetState == EPropState::Held)
			{
				SetPropState(InISID, EPropState::Held);
				ExistingASM->bIsHeld = true;
				ExistingASM->SetGrabbable(false);
				if (ExistingASM->PropMesh)
				{
					ExistingASM->PropMesh->SetSimulatePhysics(false);
					ExistingASM->PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
					ExistingASM->PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
					ExistingASM->PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
				}
				if (ExistingASM->PhysicsSimComponent)
				{
					ExistingASM->PhysicsSimComponent->SetStateHeld();
				}
			}
			else if (bSimulatePhysics && ExistingASM->PhysicsSimComponent)
			{
				ExistingASM->PhysicsSimComponent->SetStateUnsettled(3.0f);
			}
		}
		return ExistingASM;
	}

	// If currently in-flight converting to ASM, do not duplicate
	if (PendingConversionISIDs.Contains(InISID))
	{
		return GetActiveActorFromISID(InISID);
	}

	const EPropState EffectiveState = (TargetState == EPropState::AtRest)
		? (bSimulatePhysics ? EPropState::Unsettled : EPropState::InMotion)
		: TargetState;

	if (!HasAuthority())
	{
		// Client predicted promotion using dedicated 25-ASM pool partition
		AActivePropActor* PredictedASM = nullptr;
		if (PromoteInstanceToASM(InISID, Reason, false, PredictedASM) && PredictedASM)
		{
			SetPropState(InISID, EffectiveState);
			if (EffectiveState == EPropState::Held)
			{
				PredictedASM->bIsHeld = true;
				PredictedASM->SetGrabbable(false);
				if (PredictedASM->PropMesh)
				{
					PredictedASM->PropMesh->SetSimulatePhysics(false);
					PredictedASM->PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
					PredictedASM->PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
					PredictedASM->PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
				}
				if (PredictedASM->PhysicsSimComponent)
				{
					PredictedASM->PhysicsSimComponent->SetStateHeld();
				}
			}
			else if (bSimulatePhysics && PredictedASM->PhysicsSimComponent)
			{
				PredictedASM->PhysicsSimComponent->SetStateUnsettled(3.0f);
			}
			else if (EffectiveState == EPropState::InMotion && PredictedASM->PhysicsSimComponent)
			{
				PredictedASM->PhysicsSimComponent->SetStateInMotion();
			}

			if (GetNetConnection() != nullptr)
			{
				Server_SetInstanceState(InISID, EffectiveState, FVector::ZeroVector, FVector_NetQuantize(PredictedASM->GetActorLocation()), 3.0f, PredictedASM);
			}
		}
		return PredictedASM;
	}

	// Server authoritative promotion
	AActivePropActor* PromotedASM = nullptr;
	if (!PromoteInstanceToASM(InISID, Reason, (EffectiveState != EPropState::Held && bSimulatePhysics), PromotedASM) || !PromotedASM)
	{
		return nullptr;
	}

	SetPropState(InISID, EffectiveState);
	if (EffectiveState == EPropState::Held)
	{
		PromotedASM->bIsHeld = true;
		PromotedASM->SetGrabbable(false);
		if (PromotedASM->PropMesh)
		{
			PromotedASM->PropMesh->SetSimulatePhysics(false);
			PromotedASM->PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
			PromotedASM->PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			PromotedASM->PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
		}
		if (PromotedASM->PhysicsSimComponent)
		{
			PromotedASM->PhysicsSimComponent->SetStateHeld();
		}
	}
	else if (PromotedASM->PhysicsSimComponent)
	{
		if (bSimulatePhysics || EffectiveState == EPropState::Unsettled)
		{
			PromotedASM->PhysicsSimComponent->SetStateUnsettled(3.0f);
		}
		else if (EffectiveState == EPropState::InMotion)
		{
			PromotedASM->PhysicsSimComponent->SetStateInMotion();
		}
	}

	PromotedASM->ForceNetUpdate();
	return PromotedASM;
}

AActivePropActor* APropManager::ConvertISMToASMFromISID(int32 InISID)
{
	return MakeISMToASM(InISID, EPropState::Unsettled, EPropActivationReason::Interaction, false);
}

void APropManager::QueuePlacedASMForConversion(int32 InASMID)
{
	if (InASMID <= 0)
	{
		return;
	}

	if (!HasAuthority())
	{
		if (GetNetConnection() != nullptr)
		{
			Server_QueuePlacedASM(InASMID);
		}
		return;
	}

	QueuedPlacedASMIDs.AddUnique(InASMID);
}

void APropManager::Server_QueuePlacedASM_Implementation(int32 InASMID)
{
	if (InASMID > 0)
	{
		QueuedPlacedASMIDs.AddUnique(InASMID);
	}
}

bool APropManager::Server_QueuePlacedASM_Validate(int32 InASMID)
{
	return InASMID > 0;
}

void APropManager::Multicast_OnPlacedASMsConvertedToISM_Implementation(const TArray<int32>& ConvertedASMIDs)
{
	if (HasAuthority())
	{
		// Server already handled local conversion in ProcessQueuedPlacedConversions
		return;
	}

	// On Client: Deactivate the corresponding placed level actors so they are parked to the pool at ResetTransform
	for (const int32 ASMIDToDeactivate : ConvertedASMIDs)
	{
		if (AActivePropActor* ClientActor = GetActiveActorFromASMID(ASMIDToDeactivate))
		{
			if (ClientActor->bIsActive)
			{
				ClientActor->DeactivateToPoolState(GetResetTransformForSlot(ASMIDToDeactivate));
				if (ASMComponent)
				{
					ASMComponent->ASMActors.AddUnique(ClientActor);
					ASMComponent->EmptyASMIDs.AddUnique(ASMIDToDeactivate);
				}
			}
		}
	}
}

void APropManager::Multicast_NotifyISMConvertedToASM_Implementation(
	int32 InISID,
	int32 InProductID,
	int32 InASMID,
	const FTransform& InTransform,
	EPropState InState,
	EPropActivationReason InReason)
{
	if (HasAuthority())
	{
		return;
	}

	EnsureLevelPropsInitialized();

	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record)
	{
		FISIDRuntimeIndex NewRecord;
		NewRecord.ISID = InISID;
		NewRecord.ProductID = InProductID;
		NewRecord.ASMID = InASMID;
		NewRecord.Representation = EPropRepresentation::ASM;
		NewRecord.State = InState;
		NewRecord.ActiveTransform = InTransform;
		ISIDData.Add(InISID, NewRecord);
		AllActiveISIDs.AddUnique(InISID);
		Record = FindISIDData(InISID);
	}

	if (Record)
	{
		Record->Representation = EPropRepresentation::ASM;
		Record->State = InState;
		Record->ASMID = InASMID;
		if (InProductID > 0)
		{
			Record->ProductID = InProductID;
		}
		if (InTransform.GetLocation().Z > -50000.0f && InTransform.GetScale3D().GetMin() >= 0.005f)
		{
			Record->ActiveTransform = InTransform;
		}
	}

	// Hide client ISM instance
	if (UInteractableISMComponent* Comp = GetComponentFromISID(InISID))
	{
		Comp->MoveInstanceToResetTransform(InISID);
	}
	else if (InProductID > 0)
	{
		if (UInteractableISMComponent* ProductComp = GetOrCreateIISMComponent(InProductID))
		{
			ProductComp->MoveInstanceToResetTransform(InISID);
		}
	}

	// Acquire/activate client ASM representation
	AActivePropActor* ASM = GetActiveActorFromISID(InISID);
	if (!ASM && InASMID > 0)
	{
		ASM = GetActiveActorFromASMID(InASMID);
	}
	if (!ASM)
	{
		ASM = AcquireActiveActor(InProductID);
	}

	if (ASM)
	{
		ASM->ISID = InISID;
		ASM->ASMID = InASMID;
		if (InProductID > 0)
		{
			ASM->ProductID = InProductID;
		}
		if (Record)
		{
			ASM->InitializeFromISIDRecord(*Record, ProductDefinitions.Find(InProductID), this);
		}
		ASM->ActivateASMRepresentation(InTransform, InState, false);
		if (InState == EPropState::Held)
		{
			ASM->bIsHeld = true;
			ASM->SetGrabbable(false);
			if (ASM->PropMesh)
			{
				ASM->PropMesh->SetSimulatePhysics(false);
				ASM->PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
				ASM->PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
				ASM->PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
			}
			if (ASM->PhysicsSimComponent)
			{
				ASM->PhysicsSimComponent->SetStateHeld();
			}
		}
		RegisterActiveActor(InISID, ASM);
		if (Record)
		{
			Record->ActiveActor = ASM;
		}
	}

	OnPropRepresentationChanged.Broadcast(InISID, EPropRepresentation::ASM, ASM);
}

void APropManager::Multicast_NotifyASMConvertedToISM_Implementation(
	int32 InISID,
	int32 InProductID,
	int32 InASMID,
	const FTransform& InTransform,
	EPropState InState)
{
	if (HasAuthority())
	{
		return;
	}

	EnsureLevelPropsInitialized();

	// Deactivate any ASM actor currently associated with this ISID or ASMID
	AActivePropActor* ASM = GetActiveActorFromISID(InISID);
	if (!ASM && InASMID > 0)
	{
		ASM = GetActiveActorFromASMID(InASMID);
	}

	if (ASM)
	{
		ASM->bIsInterpolatingToRest = false;
		ASM->OnRestInterpComplete = nullptr;
		ASM->ISID = 0;
		ASM->bIsActive = false;
		ASM->DeactivateToPoolState(GetResetTransformForSlot(InASMID > 0 ? InASMID : ASM->ASMID));

		UnregisterActiveActor(InISID);
		if (ASM->ASMID > 0)
		{
			if (ASMComponent)
			{
				ASMComponent->EmptyASMIDs.AddUnique(ASM->ASMID);
			}
			EmptyASMIDs.AddUnique(ASM->ASMID);
		}
	}

	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record)
	{
		FISIDRuntimeIndex NewRecord;
		NewRecord.ISID = InISID;
		NewRecord.ProductID = InProductID;
		NewRecord.Representation = EPropRepresentation::ISM;
		NewRecord.State = InState;
		NewRecord.ActiveTransform = InTransform;
		ISIDData.Add(InISID, NewRecord);
		AllActiveISIDs.AddUnique(InISID);
		Record = FindISIDData(InISID);
	}

	if (Record)
	{
		Record->Representation = EPropRepresentation::ISM;
		Record->State = InState;
		Record->ActiveActor = nullptr;
		Record->ASMID = 0;
		if (InProductID > 0)
		{
			Record->ProductID = InProductID;
		}
		if (InTransform.GetLocation().Z > -50000.0f && InTransform.GetScale3D().GetMin() >= 0.005f)
		{
			Record->ActiveTransform = InTransform;
		}
	}

	// Immediately and unconditionally restore the ISM instance on client
	UInteractableISMComponent* Comp = nullptr;
	if (Record && Record->IISMComponentID > 0)
	{
		Comp = FindIISMComponent(Record->IISMComponentID);
	}
	if (!Comp)
	{
		Comp = GetComponentFromISID(InISID);
	}
	if (!Comp && InProductID > 0)
	{
		Comp = GetOrCreateIISMComponent(InProductID);
	}

	if (Comp)
	{
		const FTransform FinalTransform = (Record && Record->ActiveTransform.GetLocation().Z > -50000.0f)
			? Record->ActiveTransform
			: InTransform;

		int32 LocalISMI = Comp->FindISMI(InISID);
		if (LocalISMI == INDEX_NONE || !Comp->IsValidInstance(LocalISMI) || Comp->IsEmptyInstance(LocalISMI))
		{
			FPropPackedRuntimeData PackedData = Record ? Record->PackedRuntimeData : FPropPackedRuntimeData();
			const int32 DesiredISMI = Record ? Record->ISMI : INDEX_NONE;
			LocalISMI = Comp->ReuseOrAddInstanceWithISID(InISID, FinalTransform, PackedData, DesiredISMI, true);
			if (Record)
			{
				Record->ISMI = LocalISMI;
			}
		}
		else
		{
			Comp->BindISIDToISMI(InISID, LocalISMI);
		}

		Comp->RestoreInstanceToGameplayTransform(InISID, FinalTransform);
		if (Record)
		{
			Comp->SetInstanceData(InISID, Record->PackedRuntimeData);
		}
	}

	OnPropRepresentationChanged.Broadcast(InISID, EPropRepresentation::ISM, nullptr);
}

bool APropManager::IsISIDConvertingOrConvertedToASM(int32 InISID) const
{
	if (InISID <= 0)
	{
		return false;
	}

	// 1. Check if currently in-flight during conversion or queued for conversion
	if (PendingConversionISIDs.Contains(InISID) || QueuedPromotionISIDs.Contains(InISID))
	{
		return true;
	}

	// 2. Check ISID runtime record representation
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		if (Record->Representation == EPropRepresentation::ASM)
		{
			return true;
		}
		if (Record->ASMID > 0 || Record->ActiveActor)
		{
			return true;
		}
	}

	// 3. Check active actor registry
	if (GetActiveActorFromISID(InISID) != nullptr)
	{
		return true;
	}

	return false;
}

void APropManager::ProcessQueuedPlacedConversions(int32 BatchSize)
{
	if (!HasAuthority() || QueuedPlacedASMIDs.Num() == 0)
	{
		return;
	}

	const int32 CountToProcess = (BatchSize > 0)
		? FMath::Min(BatchSize, QueuedPlacedASMIDs.Num())
		: FMath::Clamp(PlacedConversionsPerFrame, 10, 20);

	TArray<int32> ConvertedASMIDs;
	ConvertedASMIDs.Reserve(CountToProcess);

	const int32 ActualCount = FMath::Min(CountToProcess, QueuedPlacedASMIDs.Num());
	for (int32 i = 0; i < ActualCount; ++i)
	{
		const int32 TargetASMID = QueuedPlacedASMIDs[0];
		QueuedPlacedASMIDs.RemoveAt(0);

		AActivePropActor* PlacedActor = GetActiveActorFromASMID(TargetASMID);
		if (IsValid(PlacedActor) && PlacedActor->State == EPropState::AtRest)
		{
			const int32 NewISID = ConvertPlacedActorToISM(PlacedActor, true);
			if (NewISID > 0)
			{
				ConvertedASMIDs.Add(TargetASMID);
			}
		}
	}

	if (ConvertedASMIDs.Num() > 0)
	{
		Multicast_OnPlacedASMsConvertedToISM(ConvertedASMIDs);
	}
}

void APropManager::BatchMakeISMToASM(
	const TArray<int32>& InISIDs,
	EPropState TargetState,
	EPropActivationReason Reason,
	bool bSimulatePhysics)
{
	if (InISIDs.Num() == 0)
	{
		return;
	}

	if (!HasAuthority())
	{
		if (GetNetConnection() != nullptr)
		{
			Server_BatchMakeISMToASM(InISIDs, TargetState, Reason, bSimulatePhysics);
		}
		// Also fast-promote the first batch locally on client for immediate responsiveness
		const int32 ImmediateCount = FMath::Min(PromotionsPerFrame, InISIDs.Num());
		for (int32 i = 0; i < ImmediateCount; ++i)
		{
			MakeISMToASM(InISIDs[i], TargetState, Reason, bSimulatePhysics);
		}
		// Queue remainder on client
		for (int32 i = ImmediateCount; i < InISIDs.Num(); ++i)
		{
			QueuedPromotionISIDs.AddUnique(InISIDs[i]);
		}
		return;
	}

	for (const int32 ISIDToQueue : InISIDs)
	{
		if (ISIDToQueue > 0 && !IsISIDConvertingOrConvertedToASM(ISIDToQueue))
		{
			QueuedPromotionISIDs.AddUnique(ISIDToQueue);
		}
	}
}

void APropManager::Server_BatchMakeISMToASM_Implementation(
	const TArray<int32>& InISIDs,
	EPropState TargetState,
	EPropActivationReason Reason,
	bool bSimulatePhysics)
{
	for (const int32 ISIDToQueue : InISIDs)
	{
		if (ISIDToQueue > 0 && !IsISIDConvertingOrConvertedToASM(ISIDToQueue))
		{
			QueuedPromotionISIDs.AddUnique(ISIDToQueue);
		}
	}
}

bool APropManager::Server_BatchMakeISMToASM_Validate(
	const TArray<int32>& InISIDs,
	EPropState TargetState,
	EPropActivationReason Reason,
	bool bSimulatePhysics)
{
	return InISIDs.Num() > 0;
}

void APropManager::ProcessQueuedISMPromotions(int32 BatchSize)
{
	if (QueuedPromotionISIDs.Num() == 0)
	{
		return;
	}

	const int32 CountToProcess = (BatchSize > 0)
		? FMath::Min(BatchSize, QueuedPromotionISIDs.Num())
		: FMath::Clamp(PromotionsPerFrame, 10, 30);

	const int32 ActualCount = FMath::Min(CountToProcess, QueuedPromotionISIDs.Num());
	for (int32 i = 0; i < ActualCount; ++i)
	{
		const int32 TargetISID = QueuedPromotionISIDs[0];
		QueuedPromotionISIDs.RemoveAt(0);

		MakeISMToASM(TargetISID, EPropState::Unsettled, EPropActivationReason::Interaction, true);
	}
}

AActivePropActor* APropManager::AcquireActiveActor(int32 InProductID)
{
	return ASMComponent ? ASMComponent->AcquireActiveActor(InProductID) : nullptr;
}

void APropManager::ReturnActiveActorToPool(AActivePropActor* InActor)
{
	if (ASMComponent)
	{
		ASMComponent->ReturnActiveActorToPool(InActor);
	}
}

void APropManager::SetCustomActivePropActorClass(TSubclassOf<AActivePropActor> NewActorClass, bool bRebuildPoolNow)
{
	ActivePropActorClass = NewActorClass ? NewActorClass : TSubclassOf<AActivePropActor>(AActivePropActor::StaticClass());
	if (ASMComponent)
	{
		ASMComponent->SetCustomActivePropActorClass(ActivePropActorClass, bRebuildPoolNow);
	}
}

void APropManager::SetCustomActorClassForProduct(int32 InProductID, TSubclassOf<AActivePropActor> NewActorClass)
{
	if (ASMComponent)
	{
		ASMComponent->SetCustomActorClassForProduct(InProductID, NewActorClass);
	}
	if (FPropProductDefinition* Def = ProductDefinitions.Find(InProductID))
	{
		Def->CustomActiveActorClass = NewActorClass;
	}
}

// =========================================================================
// 37.F & Sections 9, 10, 25 — Interaction Architecture & Validation
// =========================================================================
bool APropManager::CanInteract(int32 InISID, AActor* InstigatorActor, EPropInteractionType InteractionType) const
{
	return ValidateInteraction(InISID, InteractionType, InstigatorActor, -1);
}

bool APropManager::GetInteractionData(int32 InISID, FPropInteractionRuntimeData& OutInteractionData) const
{
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		OutInteractionData = Record->RuntimeData;
		return true;
	}
	return false;
}

void APropManager::GetInteractionOptions(int32 InISID, AActor* InstigatorActor, TArray<EPropInteractionType>& OutOptions) const
{
	OutOptions.Empty();
	const FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return;

	const FPropProductDefinition* ProductDef = ProductDefinitions.Find(Record->ProductID);
	if (!ProductDef) return;

	const FPropInteractionProfile& Profile = ProductDef->InteractionProfile;
	if (Profile.bCanGrab && Record->PackedRuntimeData.bIsGrabbable && Record->State != EPropState::Held)
	{
		OutOptions.Add(EPropInteractionType::Grab);
	}
	if (Profile.bCanUse)      OutOptions.Add(EPropInteractionType::Use);
	if (Profile.bCanConsume && Record->PackedRuntimeData.RemainingPortions > 0)
	{
		OutOptions.Add(EPropInteractionType::Consume);
	}
	if (Profile.bCanOpen && Record->RuntimeData.OpenAmount < 1.0f)  OutOptions.Add(EPropInteractionType::Open);
	if (Profile.bCanClose && Record->RuntimeData.OpenAmount > 0.0f) OutOptions.Add(EPropInteractionType::Close);
	if (Profile.bCanToggle)   OutOptions.Add(EPropInteractionType::Toggle);
	if (Profile.bCanActivate) OutOptions.Add(Record->RuntimeData.bActivated ? EPropInteractionType::Deactivate : EPropInteractionType::Activate);
	if (Profile.bCanInspect)  OutOptions.Add(EPropInteractionType::Inspect);
	if (Profile.bCanThrow && Record->State == EPropState::Held) OutOptions.Add(EPropInteractionType::Throw);
	if (Profile.bCanDrop && Record->State == EPropState::Held)  OutOptions.Add(EPropInteractionType::Drop);
	if (Profile.bCanBreak)    OutOptions.Add(EPropInteractionType::Break);
	if (Profile.bCanRepair && Record->RuntimeData.Damage > 0.0f) OutOptions.Add(EPropInteractionType::Repair);
	if (Profile.bCanDamage)   OutOptions.Add(EPropInteractionType::Damage);
	if (Profile.bCanCombine)  OutOptions.Add(EPropInteractionType::Combine);
	if (Profile.bCanSplit)    OutOptions.Add(EPropInteractionType::Split);
	if (Profile.bCanMove)     OutOptions.Add(EPropInteractionType::Move);
	if (Profile.bCanRotate)   OutOptions.Add(EPropInteractionType::Rotate);
}

void APropManager::RequestInteraction(int32 InISID, EPropInteractionType InteractionType, AActor* InstigatorActor, FVector OptionalVector, float OptionalFloat)
{
	const int32 CurrentVer = GetVersion(InISID);
	if (HasAuthority())
	{
		if (ValidateInteraction(InISID, InteractionType, InstigatorActor, CurrentVer))
		{
			ExecuteInteraction(InISID, InteractionType, InstigatorActor, OptionalVector, OptionalFloat);
		}
	}
	else
	{
		Server_RequestInteraction(InISID, InteractionType, InstigatorActor, OptionalVector, OptionalFloat, CurrentVer);
	}
}

void APropManager::RequestInteract(int32 InISID, EPropInteractionType InteractionType, AActor* InstigatorActor)
{
	RequestInteraction(InISID, InteractionType, InstigatorActor);
}

void APropManager::Server_RequestInteraction_Implementation(int32 InISID, EPropInteractionType InteractionType, AActor* InstigatorActor, FVector OptionalVector, float OptionalFloat, int32 ExpectedVersion)
{
	if (ValidateInteraction(InISID, InteractionType, InstigatorActor, ExpectedVersion))
	{
		ExecuteInteraction(InISID, InteractionType, InstigatorActor, OptionalVector, OptionalFloat);
	}
}

bool APropManager::Server_RequestInteraction_Validate(int32 InISID, EPropInteractionType InteractionType, AActor* InstigatorActor, FVector OptionalVector, float OptionalFloat, int32 ExpectedVersion)
{
	return InISID > 0;
}

bool APropManager::ValidateInteraction(int32 InISID, EPropInteractionType InteractionType, AActor* InstigatorActor, int32 ExpectedVersion) const
{
	// Exact 10-step Validation Checklist from Section 25 (Steps 31-40):
	// 31. ISID exists?
	const FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	// 32. Product definition exists?
	const FPropProductDefinition* ProductDef = ProductDefinitions.Find(Record->ProductID);
	if (!ProductDef) return false;

	// 33. Requesting player/actor is valid? (Allow nullptr only for non-actor system queries)
	if (InstigatorActor && !IsValid(InstigatorActor))
	{
		return false;
	}

	// 34. Prop is in an interactable state?
	if (Record->RuntimeData.bInteractionLock && Record->RuntimeData.InteractionOwner.Get() != InstigatorActor)
	{
		return false;
	}

	// 35. Player is close enough?
	if (InstigatorActor && ProductDef->InteractionProfile.MaxInteractionDistance > 0.0f)
	{
		const FVector PropLocation = GetTransform(InISID).GetLocation();
		const float DistSq = FVector::DistSquared(InstigatorActor->GetActorLocation(), PropLocation);
		const float MaxDist = ProductDef->InteractionProfile.MaxInteractionDistance;
		if (DistSq > FMath::Square(MaxDist * 1.5f)) // 1.5x tolerance for network latency
		{
			return false;
		}
	}

	// 36. Player has required permission?
	// (If RequiredPermissionTag is set, check actor tags)
	if (InstigatorActor && ProductDef->InteractionProfile.RequiredPermissionTag.IsValid())
	{
		if (!InstigatorActor->ActorHasTag(ProductDef->InteractionProfile.RequiredPermissionTag.GetTagName()))
		{
			return false;
		}
	}

	// 37. Prop is not already exclusively held by another player/NPC? (Section 26)
	if (Record->RuntimeData.InteractionState == EPropInteractionLockState::Held)
	{
		if (Record->RuntimeData.InteractionOwner.IsValid() && Record->RuntimeData.InteractionOwner.Get() != InstigatorActor)
		{
			return false;
		}
	}

	// 38. Interaction is supported by Product Definition?
	if (!ProductDef->InteractionProfile.SupportsInteraction(InteractionType))
	{
		return false;
	}

	// 39. Expected Version is valid?
	if (ExpectedVersion > 0 && Record->Version != ExpectedVersion)
	{
		return false;
	}

	// 40. Interaction cooldown/state rules valid?
	if (const UWorld* World = GetWorld())
	{
		const float Now = World->GetTimeSeconds();
		if (Now - Record->RuntimeData.LastInteractionTime < ProductDef->InteractionProfile.InteractionCooldown)
		{
			return false;
		}
	}

	return true;
}

bool APropManager::BeginInteraction(int32 InISID, EPropInteractionType InteractionType, AActor* InstigatorActor)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	Record->RuntimeData.CurrentInteractor = InstigatorActor;
	Record->RuntimeData.InteractionRequestID = NextRequestID++;
	if (const UWorld* World = GetWorld())
	{
		Record->RuntimeData.LastInteractionTime = World->GetTimeSeconds();
	}

	if (InteractionType == EPropInteractionType::Grab || InteractionType == EPropInteractionType::PickUp)
	{
		Record->RuntimeData.InteractionOwner = InstigatorActor;
		Record->RuntimeData.InteractionState = EPropInteractionLockState::Held;
		Record->RuntimeData.bInteractionLock = true;
	}
	else
	{
		Record->RuntimeData.InteractionState = EPropInteractionLockState::Interacting;
	}

	return true;
}

bool APropManager::ExecuteInteraction(int32 InISID, EPropInteractionType InteractionType, AActor* InstigatorActor, FVector OptionalVector, float OptionalFloat)
{
	if (!BeginInteraction(InISID, InteractionType, InstigatorActor))
	{
		return false;
	}

	bool bSuccess = false;
	switch (InteractionType)
	{
	case EPropInteractionType::Grab:
	case EPropInteractionType::PickUp:
		bSuccess = ExecuteGrab(InISID, InstigatorActor, InstigatorActor ? InstigatorActor->GetRootComponent() : nullptr, NAME_None);
		break;

	case EPropInteractionType::Release:
	case EPropInteractionType::Drop:
		bSuccess = ReleaseGrab(InISID, InstigatorActor, OptionalVector);
		break;

	case EPropInteractionType::Throw:
		bSuccess = ThrowProp(InISID, InstigatorActor, OptionalVector);
		break;

	case EPropInteractionType::Use:
		bSuccess = ExecuteUse(InISID, InstigatorActor);
		break;

	case EPropInteractionType::Consume:
		bSuccess = ApplyConsume(InISID, InstigatorActor, FMath::Max(1, FMath::RoundToInt(OptionalFloat)));
		break;

	case EPropInteractionType::Open:
		bSuccess = SetOpenState(InISID, 1.0f);
		break;

	case EPropInteractionType::Close:
		bSuccess = SetClosedState(InISID);
		break;

	case EPropInteractionType::Toggle:
		{
			const FISIDRuntimeIndex* Rec = FindISIDData(InISID);
			const bool bNext = Rec ? !Rec->RuntimeData.bActivated : true;
			bSuccess = SetToggleState(InISID, bNext);
		}
		break;

	case EPropInteractionType::Activate:
		bSuccess = SetActiveState(InISID, true);
		break;

	case EPropInteractionType::Deactivate:
		bSuccess = SetActiveState(InISID, false);
		break;

	case EPropInteractionType::Damage:
	case EPropInteractionType::Break:
		{
			FISIDRuntimeIndex* Rec = FindISIDData(InISID);
			if (Rec)
			{
				const float Dmg = (InteractionType == EPropInteractionType::Break) ? Rec->RuntimeData.Durability : FMath::Max(1.0f, OptionalFloat);
				Rec->RuntimeData.Damage += Dmg;
				Rec->RuntimeData.Durability = FMath::Max(0.0f, Rec->RuntimeData.Durability - Dmg);
				AActivePropActor* ASM = nullptr;
				PromoteInstanceToASM(InISID, EPropActivationReason::Damage, true, ASM);
				if (!OptionalVector.IsNearlyZero())
				{
					ApplyImpulse(InISID, OptionalVector, true);
				}
				MarkInstanceDirty(InISID, EPropDirtyFlags::RuntimeData | EPropDirtyFlags::State);
				bSuccess = true;
			}
		}
		break;

	case EPropInteractionType::Repair:
		{
			FISIDRuntimeIndex* Rec = FindISIDData(InISID);
			if (Rec)
			{
				const float Rep = FMath::Max(1.0f, OptionalFloat);
				Rec->RuntimeData.Damage = FMath::Max(0.0f, Rec->RuntimeData.Damage - Rep);
				Rec->RuntimeData.Durability = FMath::Clamp(Rec->RuntimeData.Durability + Rep, 0.0f, 100.0f);
				MarkInstanceDirty(InISID, EPropDirtyFlags::RuntimeData);
				bSuccess = true;
			}
		}
		break;

	case EPropInteractionType::Move:
		{
			FTransform NewT = GetTransform(InISID);
			NewT.SetLocation(OptionalVector);
			bSuccess = InitializeInstanceTransform(InISID, NewT);
			MarkInstanceDirty(InISID, EPropDirtyFlags::Transform);
		}
		break;

	case EPropInteractionType::Rotate:
		{
			FTransform NewT = GetTransform(InISID);
			NewT.SetRotation(FRotator::MakeFromEuler(OptionalVector).Quaternion());
			bSuccess = InitializeInstanceTransform(InISID, NewT);
			MarkInstanceDirty(InISID, EPropDirtyFlags::Transform);
		}
		break;

	case EPropInteractionType::Inspect:
	case EPropInteractionType::Combine:
	case EPropInteractionType::Split:
		bSuccess = true;
		break;

	default:
		break;
	}

	if (InteractionType != EPropInteractionType::Grab && InteractionType != EPropInteractionType::PickUp)
	{
		EndInteraction(InISID, InstigatorActor);
	}

	if (bSuccess)
	{
		OnPropInteracted.Broadcast(InISID, InteractionType, InstigatorActor);
	}
	return bSuccess;
}

bool APropManager::EndInteraction(int32 InISID, AActor* InstigatorActor)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	if (Record->State != EPropState::Held)
	{
		Record->RuntimeData.CurrentInteractor = nullptr;
		Record->RuntimeData.InteractionState = EPropInteractionLockState::Free;
		Record->RuntimeData.bInteractionLock = false;
	}
	return true;
}

bool APropManager::CancelInteraction(int32 InISID, AActor* InstigatorActor)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	if (Record->State == EPropState::Held)
	{
		return CancelGrab(InISID, InstigatorActor);
	}

	Record->RuntimeData.CurrentInteractor = nullptr;
	Record->RuntimeData.InteractionOwner = nullptr;
	Record->RuntimeData.InteractionState = EPropInteractionLockState::Free;
	Record->RuntimeData.bInteractionLock = false;
	return true;
}

// =========================================================================
// 37.G & Section 11 & 26 — Grab / Carry Functions & Ownership Lock
// =========================================================================
bool APropManager::CanGrab(int32 InISID, AActor* InstigatorActor) const
{
	return ValidateGrab(InISID, InstigatorActor);
}

void APropManager::RequestGrab(int32 InISID, AActor* InstigatorActor, USceneComponent* AttachParent, FName SocketName)
{
	if (HasAuthority())
	{
		if (ValidateGrab(InISID, InstigatorActor))
		{
			ExecuteGrab(InISID, InstigatorActor, AttachParent, SocketName);
		}
	}
	else
	{
		Server_RequestGrab(InISID, InstigatorActor, AttachParent, SocketName);
	}
}

void APropManager::Server_RequestGrab_Implementation(int32 InISID, AActor* InstigatorActor, USceneComponent* AttachParent, FName SocketName)
{
	if (ValidateGrab(InISID, InstigatorActor))
	{
		ExecuteGrab(InISID, InstigatorActor, AttachParent, SocketName);
	}
}

bool APropManager::Server_RequestGrab_Validate(int32 InISID, AActor* InstigatorActor, USceneComponent* AttachParent, FName SocketName)
{
	return InISID > 0;
}

bool APropManager::ValidateGrab(int32 InISID, AActor* InstigatorActor) const
{
	const FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;
	if (!Record->PackedRuntimeData.bIsGrabbable) return false;

	// Section 26: A second player attempting to grab the same exclusively held prop should be rejected by the server.
	if (Record->State == EPropState::Held || Record->RuntimeData.InteractionState == EPropInteractionLockState::Held)
	{
		if (Record->RuntimeData.InteractionOwner.IsValid() && Record->RuntimeData.InteractionOwner.Get() != InstigatorActor)
		{
			return false;
		}
	}

	return ValidateInteraction(InISID, EPropInteractionType::Grab, InstigatorActor, -1);
}

bool APropManager::BeginGrab(int32 InISID, AActor* InstigatorActor, USceneComponent* AttachParent, FName SocketName)
{
	return ExecuteGrab(InISID, InstigatorActor, AttachParent, SocketName);
}

bool APropManager::ExecuteGrab(int32 InISID, AActor* InstigatorActor, USceneComponent* AttachParent, FName SocketName)
{
	// Section 11 Flow: Validate -> Promote ISM -> ASM -> Set State = Held -> Set interaction owner
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	AActivePropActor* ASM = nullptr;
	if (!PromoteInstanceToASM(InISID, EPropActivationReason::Grab, false, ASM) || !ASM)
	{
		return false;
	}

	SetGrabber(InISID, InstigatorActor);
	SetPropState(InISID, EPropState::Held);

	ASM->bIsHeld = true;
	ASM->SetGrabbable(false);
	if (ASM->PropMesh)
	{
		ASM->PropMesh->SetSimulatePhysics(false);
		ASM->PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		ASM->PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		ASM->PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
	}
	if (ASM->PhysicsSimComponent)
	{
		ASM->PhysicsSimComponent->SetStateHeld();
	}

	USceneComponent* TargetAttach = AttachParent;
	if (!TargetAttach && InstigatorActor)
	{
		TargetAttach = InstigatorActor->GetRootComponent();
	}

	if (TargetAttach)
	{
		ASM->OnPlayerGrab(TargetAttach, SocketName);
	}

	return true;
}

bool APropManager::SetGrabber(int32 InISID, AActor* GrabberActor)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	Record->RuntimeData.CurrentInteractor = GrabberActor;
	Record->RuntimeData.InteractionOwner = GrabberActor;
	Record->RuntimeData.InteractionState = GrabberActor ? EPropInteractionLockState::Held : EPropInteractionLockState::Free;
	Record->RuntimeData.bInteractionLock = (GrabberActor != nullptr);
	Record->PackedRuntimeData.bIsGrabbable = (GrabberActor == nullptr);

	if (AActivePropActor* ASM = GetActiveActorFromISID(InISID))
	{
		ASM->InteractionOwner = GrabberActor;
		ASM->InteractionState = Record->RuntimeData.InteractionState;
		ASM->bIsHeld = (GrabberActor != nullptr);
		ASM->SetGrabbable(GrabberActor == nullptr);
	}

	MarkInstanceDirty(InISID, EPropDirtyFlags::RuntimeData | EPropDirtyFlags::State);
	return true;
}

bool APropManager::SetGrabTransform(int32 InISID, const FTransform& NewTransform)
{
	return UpdateHeldTransform(InISID, NewTransform);
}

bool APropManager::UpdateHeldTransform(int32 InISID, const FTransform& HeldWorldTransform)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	Record->ActiveTransform = HeldWorldTransform;
	if (AActivePropActor* ASM = GetActiveActorFromISID(InISID))
	{
		ASM->SetActorTransform(HeldWorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}
	return true;
}

void APropManager::RequestRelease(int32 InISID, AActor* InstigatorActor, FVector ReleaseVelocity)
{
	if (HasAuthority())
	{
		ReleaseGrab(InISID, InstigatorActor, ReleaseVelocity);
	}
	else
	{
		RequestInteraction(InISID, EPropInteractionType::Release, InstigatorActor, ReleaseVelocity, 0.0f);
	}
}

bool APropManager::ReleaseGrab(int32 InISID, AActor* InstigatorActor, FVector ReleaseVelocity)
{
	// Section 11 Release Flow: Release -> Set State = InMotion / Settling -> Physics -> Stable -> ASM -> ISM
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	if (InstigatorActor && Record->RuntimeData.InteractionOwner.IsValid() && Record->RuntimeData.InteractionOwner.Get() != InstigatorActor)
	{
		return false;
	}

	SetGrabber(InISID, nullptr);

	if (AActivePropActor* ASM = GetActiveActorFromISID(InISID))
	{
		ASM->OnPlayerRelease(ReleaseVelocity);
		Record->ActiveTransform = ASM->GetActorTransform();
		Record->State = ASM->State;
	}
	else
	{
		SetPropState(InISID, ReleaseVelocity.SizeSquared() > 100.0f ? EPropState::InMotion : EPropState::Settling);
	}

	MarkInstanceDirty(InISID, EPropDirtyFlags::State | EPropDirtyFlags::Transform | EPropDirtyFlags::RuntimeData);
	return true;
}

bool APropManager::CancelGrab(int32 InISID, AActor* InstigatorActor)
{
	return ReleaseGrab(InISID, InstigatorActor, FVector::ZeroVector);
}

bool APropManager::DropProp(int32 InISID, AActor* InstigatorActor)
{
	return ReleaseGrab(InISID, InstigatorActor, FVector::ZeroVector);
}

bool APropManager::ThrowProp(int32 InISID, AActor* InstigatorActor, FVector ThrowImpulse)
{
	return ReleaseGrab(InISID, InstigatorActor, ThrowImpulse);
}

// =========================================================================
// 37.H & Section 13 & 19.1 & 29 — Generic Prop Interaction Functions
// =========================================================================
bool APropManager::CanUse(int32 InISID, AActor* InstigatorActor) const
{
	return ValidateInteraction(InISID, EPropInteractionType::Use, InstigatorActor);
}

void APropManager::RequestUse(int32 InISID, AActor* InstigatorActor)
{
	if (HasAuthority()) ExecuteUse(InISID, InstigatorActor);
	else Server_RequestUse(InISID, InstigatorActor);
}

void APropManager::Server_RequestUse_Implementation(int32 InISID, AActor* InstigatorActor)
{
	if (ValidateInteraction(InISID, EPropInteractionType::Use, InstigatorActor))
	{
		ExecuteUse(InISID, InstigatorActor);
	}
}

bool APropManager::Server_RequestUse_Validate(int32 InISID, AActor* InstigatorActor)
{
	return InISID > 0;
}

bool APropManager::ExecuteUse(int32 InISID, AActor* InstigatorActor)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	if (Record->PackedRuntimeData.RemainingPortions > 0)
	{
		return ApplyConsume(InISID, InstigatorActor, 1);
	}
	return SetToggleState(InISID, !Record->RuntimeData.bActivated);
}

bool APropManager::CanOpen(int32 InISID, AActor* InstigatorActor) const
{
	return ValidateInteraction(InISID, EPropInteractionType::Open, InstigatorActor);
}

void APropManager::RequestOpen(int32 InISID, AActor* InstigatorActor)
{
	if (HasAuthority())
	{
		if (CanOpen(InISID, InstigatorActor)) SetOpenState(InISID, 1.0f);
	}
	else
	{
		Server_RequestOpen(InISID, InstigatorActor);
	}
}

void APropManager::Server_RequestOpen_Implementation(int32 InISID, AActor* InstigatorActor)
{
	if (CanOpen(InISID, InstigatorActor))
	{
		SetOpenState(InISID, 1.0f);
	}
}

bool APropManager::Server_RequestOpen_Validate(int32 InISID, AActor* InstigatorActor)
{
	return InISID > 0;
}

bool APropManager::SetOpenState(int32 InISID, float OpenAmount)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	Record->RuntimeData.OpenAmount = FMath::Clamp(OpenAmount, 0.0f, 1.0f);
	IncrementInstanceVersion(InISID);
	MarkInstanceDirty(InISID, EPropDirtyFlags::RuntimeData);
	return true;
}

bool APropManager::CanClose(int32 InISID, AActor* InstigatorActor) const
{
	return ValidateInteraction(InISID, EPropInteractionType::Close, InstigatorActor);
}

void APropManager::RequestClose(int32 InISID, AActor* InstigatorActor)
{
	RequestInteraction(InISID, EPropInteractionType::Close, InstigatorActor);
}

bool APropManager::SetClosedState(int32 InISID)
{
	return SetOpenState(InISID, 0.0f);
}

bool APropManager::CanToggle(int32 InISID, AActor* InstigatorActor) const
{
	return ValidateInteraction(InISID, EPropInteractionType::Toggle, InstigatorActor);
}

void APropManager::RequestToggle(int32 InISID, AActor* InstigatorActor)
{
	RequestInteraction(InISID, EPropInteractionType::Toggle, InstigatorActor);
}

bool APropManager::SetToggleState(int32 InISID, bool bToggledOn)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	Record->RuntimeData.bActivated = bToggledOn;
	Record->RuntimeData.OpenAmount = bToggledOn ? 1.0f : 0.0f;
	IncrementInstanceVersion(InISID);
	MarkInstanceDirty(InISID, EPropDirtyFlags::RuntimeData);
	return true;
}

bool APropManager::CanActivate(int32 InISID, AActor* InstigatorActor) const
{
	return ValidateInteraction(InISID, EPropInteractionType::Activate, InstigatorActor);
}

void APropManager::RequestActivate(int32 InISID, AActor* InstigatorActor)
{
	RequestInteraction(InISID, EPropInteractionType::Activate, InstigatorActor);
}

bool APropManager::SetActiveState(int32 InISID, bool bNewActivated)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	Record->RuntimeData.bActivated = bNewActivated;
	IncrementInstanceVersion(InISID);
	MarkInstanceDirty(InISID, EPropDirtyFlags::RuntimeData);
	return true;
}

bool APropManager::CanDeactivate(int32 InISID, AActor* InstigatorActor) const
{
	return ValidateInteraction(InISID, EPropInteractionType::Deactivate, InstigatorActor);
}

void APropManager::RequestDeactivate(int32 InISID, AActor* InstigatorActor)
{
	RequestInteraction(InISID, EPropInteractionType::Deactivate, InstigatorActor);
}

bool APropManager::CanConsume(int32 InISID, AActor* InstigatorActor) const
{
	const FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record || Record->PackedRuntimeData.RemainingPortions <= 0) return false;
	return ValidateInteraction(InISID, EPropInteractionType::Consume, InstigatorActor);
}

void APropManager::RequestConsume(int32 InISID, AActor* InstigatorActor, int32 Portions)
{
	if (HasAuthority())
	{
		if (CanConsume(InISID, InstigatorActor))
		{
			ApplyConsume(InISID, InstigatorActor, Portions);
		}
	}
	else
	{
		Server_RequestConsume(InISID, InstigatorActor, Portions);
	}
}

void APropManager::Server_RequestConsume_Implementation(int32 InISID, AActor* InstigatorActor, int32 Portions)
{
	if (CanConsume(InISID, InstigatorActor))
	{
		ApplyConsume(InISID, InstigatorActor, Portions);
	}
}

bool APropManager::Server_RequestConsume_Validate(int32 InISID, AActor* InstigatorActor, int32 Portions)
{
	return InISID > 0 && Portions > 0;
}

bool APropManager::ConsumePortion(int32 InISID, AActor* ConsumerActor, int32 Portions)
{
	return ApplyConsume(InISID, ConsumerActor, Portions);
}

bool APropManager::ApplyConsume(int32 InISID, AActor* ConsumerActor, int32 Portions)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record || Record->PackedRuntimeData.RemainingPortions <= 0)
	{
		return false;
	}

	const FPropProductDefinition* ProductDef = ProductDefinitions.Find(Record->ProductID);
	const int32 TotalPortions = ProductDef ? FMath::Max(1, ProductDef->ConsumableProfile.NumOfPortions) : 4;
	const int32 ActualConsumed = FMath::Clamp(Portions, 1, Record->PackedRuntimeData.RemainingPortions);

	// If currently represented as an active ASM, delegate consumption through ASM so its events fire
	if (Record->Representation == EPropRepresentation::ASM && Record->ActiveActor)
	{
		const float Ratio = static_cast<float>(ActualConsumed) / static_cast<float>(TotalPortions);
		return Record->ActiveActor->ConsumePortion(ConsumerActor, Ratio);
	}

	// If currently represented as a resting ISM, consume directly on the component & authoritative ISID record
	if (UInteractableISMComponent* ISMComp = FindIISMComponent(Record->IISMComponentID))
	{
		int32 OutSips = 0;
		ISMComp->ConsumeInstancePortions(Record->ISMI, ActualConsumed, ConsumerActor, OutSips);
	}

	Record->PackedRuntimeData.RemainingPortions = FMath::Max(0, Record->PackedRuntimeData.RemainingPortions - ActualConsumed);
	Record->PackedRuntimeData.LastUpdatedMinute = GetCurrentWorldMinutes();
	Record->RuntimeData.RemainingPortions = Record->PackedRuntimeData.RemainingPortions;
	IncrementInstanceVersion(InISID);
	MarkInstanceDirty(InISID, EPropDirtyFlags::RuntimeData);

	if (Record->PackedRuntimeData.RemainingPortions <= 0 && ProductDef && ProductDef->ConsumableProfile.bDestroyWhenEmpty)
	{
		ReleaseInstanceToEmpty(InISID);
	}

	return true;
}

bool APropManager::CanPickUp(int32 InISID, AActor* InstigatorActor) const
{
	return CanGrab(InISID, InstigatorActor);
}

void APropManager::RequestPickUp(int32 InISID, AActor* InstigatorActor)
{
	RequestGrab(InISID, InstigatorActor);
}

bool APropManager::CanDrop(int32 InISID, AActor* InstigatorActor) const
{
	return IsHeld(InISID);
}

void APropManager::RequestDrop(int32 InISID, AActor* InstigatorActor)
{
	if (HasAuthority()) DropProp(InISID, InstigatorActor);
	else Server_RequestDrop(InISID, InstigatorActor);
}

void APropManager::Server_RequestDrop_Implementation(int32 InISID, AActor* InstigatorActor)
{
	DropProp(InISID, InstigatorActor);
}

bool APropManager::Server_RequestDrop_Validate(int32 InISID, AActor* InstigatorActor)
{
	return InISID > 0;
}

bool APropManager::CanThrow(int32 InISID, AActor* InstigatorActor) const
{
	return ValidateInteraction(InISID, EPropInteractionType::Throw, InstigatorActor);
}

void APropManager::RequestThrow(int32 InISID, AActor* InstigatorActor, FVector ThrowImpulse)
{
	RequestInteraction(InISID, EPropInteractionType::Throw, InstigatorActor, ThrowImpulse, 0.0f);
}

bool APropManager::CanBreak(int32 InISID, AActor* InstigatorActor) const
{
	return ValidateInteraction(InISID, EPropInteractionType::Break, InstigatorActor);
}

void APropManager::RequestBreak(int32 InISID, AActor* InstigatorActor)
{
	RequestInteraction(InISID, EPropInteractionType::Break, InstigatorActor);
}

bool APropManager::CanRepair(int32 InISID, AActor* InstigatorActor) const
{
	return ValidateInteraction(InISID, EPropInteractionType::Repair, InstigatorActor);
}

void APropManager::RequestRepair(int32 InISID, AActor* InstigatorActor, float RepairAmount)
{
	RequestInteraction(InISID, EPropInteractionType::Repair, InstigatorActor, FVector::ZeroVector, RepairAmount);
}

bool APropManager::CanDamage(int32 InISID, AActor* InstigatorActor) const
{
	return ValidateInteraction(InISID, EPropInteractionType::Damage, InstigatorActor);
}

void APropManager::RequestDamage(int32 InISID, AActor* InstigatorActor, float DamageAmount)
{
	RequestInteraction(InISID, EPropInteractionType::Damage, InstigatorActor, FVector::ZeroVector, DamageAmount);
}

bool APropManager::CanInspect(int32 InISID, AActor* InstigatorActor) const
{
	return ValidateInteraction(InISID, EPropInteractionType::Inspect, InstigatorActor);
}

void APropManager::RequestInspect(int32 InISID, AActor* InstigatorActor)
{
	RequestInteraction(InISID, EPropInteractionType::Inspect, InstigatorActor);
}

bool APropManager::CanCombine(int32 InISID, AActor* InstigatorActor) const
{
	return ValidateInteraction(InISID, EPropInteractionType::Combine, InstigatorActor);
}

void APropManager::RequestCombine(int32 InISID, int32 OtherISID, AActor* InstigatorActor)
{
	RequestInteraction(InISID, EPropInteractionType::Combine, InstigatorActor, FVector::ZeroVector, static_cast<float>(OtherISID));
}

bool APropManager::CanSplit(int32 InISID, AActor* InstigatorActor) const
{
	return ValidateInteraction(InISID, EPropInteractionType::Split, InstigatorActor);
}

void APropManager::RequestSplit(int32 InISID, AActor* InstigatorActor)
{
	RequestInteraction(InISID, EPropInteractionType::Split, InstigatorActor);
}

bool APropManager::CanMove(int32 InISID, AActor* InstigatorActor) const
{
	return ValidateInteraction(InISID, EPropInteractionType::Move, InstigatorActor);
}

void APropManager::RequestMove(int32 InISID, const FTransform& TargetTransform, AActor* InstigatorActor)
{
	RequestInteraction(InISID, EPropInteractionType::Move, InstigatorActor, TargetTransform.GetLocation(), 0.0f);
}

bool APropManager::CanRotate(int32 InISID, AActor* InstigatorActor) const
{
	return ValidateInteraction(InISID, EPropInteractionType::Rotate, InstigatorActor);
}

void APropManager::RequestRotate(int32 InISID, const FRotator& TargetRotation, AActor* InstigatorActor)
{
	RequestInteraction(InISID, EPropInteractionType::Rotate, InstigatorActor, TargetRotation.Euler(), 0.0f);
}

// =========================================================================
// 37.I & Section 12 & 20 — Physics Functions
// =========================================================================
bool APropManager::EnablePhysics(int32 InISID)
{
	AActivePropActor* ASM = nullptr;
	if (!PromoteInstanceToASM(InISID, EPropActivationReason::PhysicsImpulse, true, ASM) || !ASM)
	{
		return false;
	}
	SetPropState(InISID, EPropState::Unsettled);
	if (ASM->PhysicsSimComponent)
	{
		ASM->PhysicsSimComponent->SetStateUnsettled(-1.0f);
	}
	return true;
}

bool APropManager::DisablePhysics(int32 InISID)
{
	if (AActivePropActor* ASM = GetActiveActorFromISID(InISID))
	{
		if (ASM->PropMesh)
		{
			ASM->PropMesh->SetSimulatePhysics(false);
		}
	}
	return SetPropState(InISID, EPropState::AtRest);
}

bool APropManager::SetPhysicsMode(int32 InISID, EPropPhysicsMode NewMode)
{
	switch (NewMode)
	{
	case EPropPhysicsMode::Disabled:  return DisablePhysics(InISID);
	case EPropPhysicsMode::Simulated: return EnablePhysics(InISID);
	case EPropPhysicsMode::Settling:  return StartSettling(InISID);
	case EPropPhysicsMode::Held:      return StartHeld(InISID);
	default:                          return false;
	}
}

bool APropManager::ApplyImpulse(int32 InISID, FVector Impulse, bool bVelChange)
{
	if (!HasAuthority())
	{
		ApplyImpulse_Server(InISID, Impulse, bVelChange);
		return true;
	}

	AActivePropActor* ASM = nullptr;
	if (!PromoteInstanceToASM(InISID, EPropActivationReason::PhysicsImpulse, true, ASM) || !ASM)
	{
		return false;
	}

	if (ASM->PhysicsSimComponent)
	{
		ASM->PhysicsSimComponent->SetStateUnsettled(-1.0f);
	}
	if (ASM->PropMesh && ASM->PropMesh->IsSimulatingPhysics())
	{
		ASM->PropMesh->AddImpulse(Impulse, NAME_None, bVelChange);
	}
	return true;
}

void APropManager::ApplyImpulse_Server_Implementation(int32 InISID, FVector Impulse, bool bVelChange)
{
	ApplyImpulse(InISID, Impulse, bVelChange);
}

bool APropManager::ApplyImpulse_Server_Validate(int32 InISID, FVector Impulse, bool bVelChange)
{
	return InISID > 0;
}

bool APropManager::ApplyForce(int32 InISID, FVector Force)
{
	if (!HasAuthority())
	{
		ApplyForce_Server(InISID, Force);
		return true;
	}

	AActivePropActor* ASM = nullptr;
	if (!PromoteInstanceToASM(InISID, EPropActivationReason::PhysicsImpulse, true, ASM) || !ASM)
	{
		return false;
	}

	if (ASM->PropMesh && ASM->PropMesh->IsSimulatingPhysics())
	{
		ASM->PropMesh->AddForce(Force, NAME_None, false);
	}
	return true;
}

void APropManager::ApplyForce_Server_Implementation(int32 InISID, FVector Force)
{
	ApplyForce(InISID, Force);
}

bool APropManager::ApplyForce_Server_Validate(int32 InISID, FVector Force)
{
	return InISID > 0;
}

bool APropManager::ApplyTorque(int32 InISID, FVector TorqueInRadians)
{
	AActivePropActor* ASM = nullptr;
	if (!PromoteInstanceToASM(InISID, EPropActivationReason::PhysicsImpulse, true, ASM) || !ASM)
	{
		return false;
	}

	if (ASM->PropMesh && ASM->PropMesh->IsSimulatingPhysics())
	{
		ASM->PropMesh->AddTorqueInRadians(TorqueInRadians, NAME_None, false);
		return true;
	}
	return false;
}

bool APropManager::SetLinearVelocity(int32 InISID, FVector NewVelocity)
{
	AActivePropActor* ASM = nullptr;
	if (!PromoteInstanceToASM(InISID, EPropActivationReason::PhysicsImpulse, true, ASM) || !ASM)
	{
		return false;
	}

	if (ASM->PropMesh)
	{
		ASM->PropMesh->SetPhysicsLinearVelocity(NewVelocity);
		ASM->LinearVelocity = NewVelocity;
		return true;
	}
	return false;
}

void APropManager::SetVelocity_Server_Implementation(int32 InISID, FVector NewLinearVelocity, FVector NewAngularVelocityDegrees)
{
	SetLinearVelocity(InISID, NewLinearVelocity);
	SetAngularVelocity(InISID, NewAngularVelocityDegrees);
}

bool APropManager::SetVelocity_Server_Validate(int32 InISID, FVector NewLinearVelocity, FVector NewAngularVelocityDegrees)
{
	return InISID > 0;
}

bool APropManager::SetAngularVelocity(int32 InISID, FVector NewAngularVelocityDegrees)
{
	AActivePropActor* ASM = nullptr;
	if (!PromoteInstanceToASM(InISID, EPropActivationReason::PhysicsImpulse, true, ASM) || !ASM)
	{
		return false;
	}

	if (ASM->PropMesh)
	{
		ASM->PropMesh->SetPhysicsAngularVelocityInDegrees(NewAngularVelocityDegrees);
		ASM->AngularVelocity = NewAngularVelocityDegrees;
		return true;
	}
	return false;
}

FVector APropManager::GetLinearVelocity(int32 InISID) const
{
	if (const AActivePropActor* ASM = GetActiveActorFromISID(InISID))
	{
		return ASM->PropMesh ? ASM->PropMesh->GetComponentVelocity() : ASM->LinearVelocity;
	}
	return FVector::ZeroVector;
}

FVector APropManager::GetAngularVelocity(int32 InISID) const
{
	if (const AActivePropActor* ASM = GetActiveActorFromISID(InISID))
	{
		return ASM->PropMesh ? ASM->PropMesh->GetPhysicsAngularVelocityInDegrees() : ASM->AngularVelocity;
	}
	return FVector::ZeroVector;
}

bool APropManager::CheckSettled(int32 InISID) const
{
	return EvaluateRestTolerance(InISID);
}

bool APropManager::EvaluateRestTolerance(int32 InISID) const
{
	// Section 12: Settling should evaluate both linear and angular movement rather than only one value.
	const FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return true;
	if (Record->Representation == EPropRepresentation::ISM) return true;

	const AActivePropActor* ASM = GetActiveActorFromISID(InISID);
	if (!ASM) return true;

	const FPropProductDefinition* ProductDef = ProductDefinitions.Find(Record->ProductID);
	const float LinTolerance = ProductDef ? ProductDef->PhysicsProfile.LinearVelocityTolerance : 10.0f;
	const float AngTolerance = ProductDef ? ProductDef->PhysicsProfile.AngularVelocityTolerance : 15.0f;

	if (ASM->PhysicsSimComponent)
	{
		return ASM->PhysicsSimComponent->CheckSettled(LinTolerance, AngTolerance);
	}

	const float LinSpeed = GetLinearVelocity(InISID).Size();
	const float AngSpeed = GetAngularVelocity(InISID).Size();
	return (LinSpeed <= LinTolerance) && (AngSpeed <= AngTolerance);
}

bool APropManager::StartSettling(int32 InISID)
{
	SetPropState(InISID, EPropState::Settling);
	if (AActivePropActor* ASM = GetActiveActorFromISID(InISID))
	{
		if (ASM->PhysicsSimComponent)
		{
			ASM->PhysicsSimComponent->SetStateSettling();
		}
	}
	return true;
}

bool APropManager::FinishSettling(int32 InISID)
{
	// Demote settled ASM back into its persistent ISM slot (Section 7.3 & 11)
	return ConvertASMToISM(static_cast<uint32>(InISID), EPropState::AtRest, true, true);
}

void APropManager::UpdateSettling(float DeltaSeconds)
{
	CheckSettlingQueue();
}

bool APropManager::WakeProp(int32 InISID)
{
	if (!HasAuthority())
	{
		WakeProp_Server(InISID);
		return true;
	}
	return EnablePhysics(InISID);
}

void APropManager::WakeProp_Server_Implementation(int32 InISID)
{
	EnablePhysics(InISID);
}

bool APropManager::WakeProp_Server_Validate(int32 InISID)
{
	return InISID > 0;
}

bool APropManager::SleepProp(int32 InISID)
{
	if (!HasAuthority())
	{
		SleepProp_Server(InISID);
		return true;
	}
	return FinishSettling(InISID);
}

void APropManager::SleepProp_Server_Implementation(int32 InISID)
{
	FinishSettling(InISID);
}

bool APropManager::SleepProp_Server_Validate(int32 InISID)
{
	return InISID > 0;
}

// =========================================================================
// 37.J & Sections 17, 18, 19.2 — Runtime Data & State Functions
// =========================================================================
bool APropManager::GetPropInstanceData(int32 InISID, FISIDRuntimeIndex& OutData) const
{
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		OutData = *Record;
		return true;
	}
	return false;
}

bool APropManager::GetPropData(int32 InISID, FInteractablePropData& OutData) const
{
	const FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	const FPropProductDefinition* ProductDef = ProductDefinitions.Find(Record->ProductID);
	OutData.SyncFromISIDRecord(*Record, ProductDef);
	return true;
}

bool APropManager::SetPropInstanceData(int32 InISID, const FISIDRuntimeIndex& InData)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	*Record = InData;
	Record->ISID = InISID;
	IncrementInstanceVersion(InISID);

	if (UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
	{
		Comp->SetInstanceData(InISID, Record->PackedRuntimeData);
	}
	if (AActivePropActor* ASM = GetActiveActorFromISID(InISID))
	{
		ASM->InitializeFromISIDRecord(*Record, ProductDefinitions.Find(Record->ProductID), this);
	}

	MarkInstanceDirty(InISID, EPropDirtyFlags::All);
	return true;
}

bool APropManager::UpdatePropInstanceData(int32 InISID, const FPropPackedRuntimeData& InPackedData)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	Record->PackedRuntimeData = InPackedData;
	Record->PackedRuntimeData.LastUpdatedMinute = GetCurrentWorldMinutes();
	Record->RuntimeData.RemainingPortions = InPackedData.RemainingPortions;
	Record->RuntimeData.Temperature = InPackedData.Temperature;
	IncrementInstanceVersion(InISID);

	if (UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
	{
		Comp->SetInstanceData(InISID, Record->PackedRuntimeData);
	}
	if (AActivePropActor* ASM = GetActiveActorFromISID(InISID))
	{
		ASM->PackedRuntimeData = Record->PackedRuntimeData;
		ASM->RemainingPortions = InPackedData.RemainingPortions;
		ASM->CurrentTemperature = InPackedData.Temperature;
		ASM->SetGrabbable(InPackedData.bIsGrabbable);
	}

	MarkInstanceDirty(InISID, EPropDirtyFlags::RuntimeData | EPropDirtyFlags::State);
	return true;
}

bool APropManager::UpdateProductInstanceInfo(int32 InISID, const FPropPackedRuntimeData& InPackedData)
{
	return UpdatePropInstanceData(InISID, InPackedData);
}

bool APropManager::PatchPropInstanceData(int32 InISID, const FPropInstancePatch& InPatch)
{
	return ApplyInstancePatch(InISID, InPatch);
}

bool APropManager::ApplyInstancePatch(int32 InISID, const FPropInstancePatch& InPatch)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	int32 Flags = EPropDirtyFlags::Clean;

	if (InPatch.bModifyState)
	{
		Record->State = InPatch.NewState;
		if (Record->ActiveActor)
		{
			Record->ActiveActor->State = InPatch.NewState;
		}
		Flags |= EPropDirtyFlags::State;
	}
	if (InPatch.bModifyGrabbable)
	{
		Record->PackedRuntimeData.bIsGrabbable = InPatch.bNewIsGrabbable;
		Flags |= EPropDirtyFlags::RuntimeData;
	}
	if (InPatch.bModifyPortions)
	{
		Record->PackedRuntimeData.RemainingPortions = FMath::Max(0, InPatch.NewRemainingPortions);
		Record->RuntimeData.RemainingPortions = Record->PackedRuntimeData.RemainingPortions;
		Flags |= EPropDirtyFlags::RuntimeData;
	}
	if (InPatch.bModifyTemperature)
	{
		Record->PackedRuntimeData.Temperature = InPatch.NewTemperature;
		Record->RuntimeData.Temperature = InPatch.NewTemperature;
		Flags |= EPropDirtyFlags::RuntimeData;
	}
	if (InPatch.bModifyOpenAmount)
	{
		Record->RuntimeData.OpenAmount = FMath::Clamp(InPatch.NewOpenAmount, 0.0f, 1.0f);
		Flags |= EPropDirtyFlags::RuntimeData;
	}
	if (InPatch.bModifyActivated)
	{
		Record->RuntimeData.bActivated = InPatch.bNewActivated;
		Flags |= EPropDirtyFlags::RuntimeData;
	}
	if (InPatch.bModifyDamage)
	{
		Record->RuntimeData.Damage = FMath::Max(0.0f, InPatch.NewDamage);
		Flags |= EPropDirtyFlags::RuntimeData;
	}
	if (InPatch.bModifyDurability)
	{
		Record->RuntimeData.Durability = FMath::Clamp(InPatch.NewDurability, 0.0f, 100.0f);
		Flags |= EPropDirtyFlags::RuntimeData;
	}
	if (InPatch.bModifyCustomFlags)
	{
		Record->RuntimeData.CustomFlags = InPatch.NewCustomFlags;
		Flags |= EPropDirtyFlags::RuntimeData;
	}
	if (InPatch.bModifyTransform)
	{
		InitializeInstanceTransform(InISID, InPatch.NewTransform);
		Flags |= EPropDirtyFlags::Transform;
	}

	if (Flags != EPropDirtyFlags::Clean)
	{
		Record->PackedRuntimeData.LastUpdatedMinute = GetCurrentWorldMinutes();
		IncrementInstanceVersion(InISID);
		if (UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
		{
			Comp->SetInstanceData(InISID, Record->PackedRuntimeData);
		}
		MarkInstanceDirty(InISID, Flags);
	}
	return true;
}

bool APropManager::ResetPropInstanceData(int32 InISID)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	if (const FPropProductDefinition* ProductDef = ProductDefinitions.Find(Record->ProductID))
	{
		return InitializeInstanceData(InISID, *ProductDef);
	}
	return false;
}

bool APropManager::ValidatePropInstanceData(int32 InISID) const
{
	const FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record || Record->ISID != InISID || Record->ProductID <= 0)
	{
		return false;
	}
	return ProductDefinitions.Contains(Record->ProductID);
}

int32 APropManager::GetInstanceVersion(int32 InISID) const
{
	return GetVersion(InISID);
}

int32 APropManager::GetVersion(int32 InISID) const
{
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		return Record->Version;
	}
	return 0;
}

int32 APropManager::IncrementInstanceVersion(int32 InISID)
{
	return IncrementVersion(InISID);
}

int32 APropManager::IncrementVersion(int32 InISID)
{
	if (FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		Record->Version++;
		if (Record->ActiveActor)
		{
			Record->ActiveActor->Version = Record->Version;
		}
		return Record->Version;
	}
	return 0;
}

void APropManager::MarkInstanceDirty(int32 InISID, int32 DirtyFlags)
{
	if (FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		Record->DirtyFlags |= DirtyFlags;
		DirtyISIDs.AddUnique(InISID);
	}
}

void APropManager::ClearInstanceDirty(int32 InISID)
{
	if (FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		Record->DirtyFlags = EPropDirtyFlags::Clean;
	}
	DirtyISIDs.Remove(InISID);
}

int32 APropManager::GetRemainingPortions(int32 InISID) const
{
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		return Record->PackedRuntimeData.RemainingPortions;
	}
	return 0;
}

bool APropManager::SetRemainingPortions(int32 InISID, int32 NewPortions)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	Record->PackedRuntimeData.RemainingPortions = FMath::Max(0, NewPortions);
	Record->PackedRuntimeData.LastUpdatedMinute = GetCurrentWorldMinutes();
	Record->RuntimeData.RemainingPortions = Record->PackedRuntimeData.RemainingPortions;
	IncrementInstanceVersion(InISID);

	if (UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
	{
		Comp->SetInstanceData(InISID, Record->PackedRuntimeData);
	}
	if (AActivePropActor* ASM = GetActiveActorFromISID(InISID))
	{
		ASM->RemainingPortions = Record->PackedRuntimeData.RemainingPortions;
		ASM->QuantityLevel = (ASM->NumOfPortions > 0) ? static_cast<float>(ASM->RemainingPortions) / static_cast<float>(ASM->NumOfPortions) : 0.0f;
	}

	MarkInstanceDirty(InISID, EPropDirtyFlags::RuntimeData);
	return true;
}

bool APropManager::AddPortions(int32 InISID, int32 PortionsToAdd)
{
	if (PortionsToAdd <= 0) return false;
	return SetRemainingPortions(InISID, GetRemainingPortions(InISID) + PortionsToAdd);
}

bool APropManager::RemovePortions(int32 InISID, int32 PortionsToRemove)
{
	if (PortionsToRemove <= 0) return false;
	return SetRemainingPortions(InISID, FMath::Max(0, GetRemainingPortions(InISID) - PortionsToRemove));
}

float APropManager::GetTemperature(int32 InISID) const
{
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		return Record->PackedRuntimeData.Temperature;
	}
	return AmbientTemperature;
}

bool APropManager::SetTemperature(int32 InISID, float NewTemperature)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	Record->PackedRuntimeData.Temperature = NewTemperature;
	Record->PackedRuntimeData.LastUpdatedMinute = GetCurrentWorldMinutes();
	Record->RuntimeData.Temperature = NewTemperature;
	IncrementInstanceVersion(InISID);

	if (UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
	{
		Comp->SetInstanceData(InISID, Record->PackedRuntimeData);
	}
	if (AActivePropActor* ASM = GetActiveActorFromISID(InISID))
	{
		ASM->CurrentTemperature = NewTemperature;
		ASM->PackedRuntimeData.Temperature = NewTemperature;
	}

	MarkInstanceDirty(InISID, EPropDirtyFlags::RuntimeData);
	return true;
}

bool APropManager::ModifyTemperature(int32 InISID, float DeltaTemperature)
{
	return SetTemperature(InISID, GetTemperature(InISID) + DeltaTemperature);
}

int32 APropManager::GetCreationTime(int32 InISID) const
{
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		return Record->PackedRuntimeData.CreationMinute;
	}
	return 0;
}

int32 APropManager::GetLastUpdatedTime(int32 InISID) const
{
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		return Record->PackedRuntimeData.LastUpdatedMinute;
	}
	return 0;
}

bool APropManager::GetIsGrabbable(int32 InISID) const
{
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		return Record->PackedRuntimeData.bIsGrabbable && Record->State != EPropState::Held;
	}
	return false;
}

bool APropManager::SetIsGrabbable(int32 InISID, bool bNewGrabbable)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	Record->PackedRuntimeData.bIsGrabbable = bNewGrabbable;
	IncrementInstanceVersion(InISID);

	if (UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
	{
		Comp->SetInstanceData(InISID, Record->PackedRuntimeData);
	}
	if (AActivePropActor* ASM = GetActiveActorFromISID(InISID))
	{
		ASM->SetGrabbable(bNewGrabbable);
	}

	MarkInstanceDirty(InISID, EPropDirtyFlags::RuntimeData);
	return true;
}

EPropState APropManager::GetPropState(int32 InISID) const
{
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		return Record->State;
	}
	return EPropState::AtRest;
}

bool APropManager::SetPropState(int32 InISID, EPropState NewState)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	const EPropState PrevState = Record->State;
	if (PrevState == NewState) return true;

	Record->State = NewState;
	Record->PackedRuntimeData.bIsGrabbable = (NewState != EPropState::Held);
	Record->PackedRuntimeData.LastUpdatedMinute = GetCurrentWorldMinutes();
	IncrementInstanceVersion(InISID);

	if (Record->ActiveActor)
	{
		Record->ActiveActor->State = NewState;
		if (NewState == EPropState::Held)
		{
			Record->ActiveActor->bIsHeld = true;
			Record->ActiveActor->SetGrabbable(false);
			if (Record->ActiveActor->PropMesh)
			{
				Record->ActiveActor->PropMesh->SetSimulatePhysics(false);
				Record->ActiveActor->PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
				Record->ActiveActor->PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
				Record->ActiveActor->PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
			}
			if (Record->ActiveActor->PhysicsSimComponent)
			{
				Record->ActiveActor->PhysicsSimComponent->SetStateHeld();
			}
		}
		else if (NewState != EPropState::Held && Record->ActiveActor->bIsHeld)
		{
			Record->ActiveActor->bIsHeld = false;
			Record->ActiveActor->SetGrabbable(true);
		}
	}

	if (UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
	{
		Comp->SetInstanceData(InISID, Record->PackedRuntimeData);
	}

	MarkInstanceDirty(InISID, EPropDirtyFlags::State | EPropDirtyFlags::RuntimeData);
	OnPropStateChanged.Broadcast(InISID, NewState, PrevState);
	return true;
}

FTransform APropManager::GetTransform(int32 InISID) const
{
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		if (Record->Representation == EPropRepresentation::ASM && Record->ActiveActor)
		{
			return Record->ActiveActor->GetActorTransform();
		}
		return Record->ActiveTransform;
	}
	return FTransform::Identity;
}

FTransform APropManager::GetPropTransform(int32 InISID) const
{
	return GetTransform(InISID);
}

EPropRepresentation APropManager::GetRepresentation(int32 InISID) const
{
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		return Record->Representation;
	}
	return EPropRepresentation::ISM;
}

int32 APropManager::GetInteractionFlags(int32 InISID) const
{
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		if (const FPropProductDefinition* ProductDef = ProductDefinitions.Find(Record->ProductID))
		{
			return ProductDef->InteractionProfile.ToBitmask();
		}
	}
	return EPropInteractionCapFlags::None;
}

int32 APropManager::GetISID(UPrimitiveComponent* TargetComponent, int32 InstanceIndex) const
{
	if (!TargetComponent) return 0;

	if (const UInteractableISMComponent* ISMComp = Cast<UInteractableISMComponent>(TargetComponent))
	{
		return ISMComp->FindISID(InstanceIndex);
	}

	if (const AActivePropActor* PropActor = Cast<AActivePropActor>(TargetComponent->GetOwner()))
	{
		return PropActor->ISID;
	}

	return 0;
}

bool APropManager::IsAtRest(int32 InISID) const     { return GetPropState(InISID) == EPropState::AtRest; }
bool APropManager::IsInMotion(int32 InISID) const   { return GetPropState(InISID) == EPropState::InMotion; }
bool APropManager::IsHeld(int32 InISID) const       { return GetPropState(InISID) == EPropState::Held; }
bool APropManager::IsSettling(int32 InISID) const   { return GetPropState(InISID) == EPropState::Settling; }
bool APropManager::IsUnsettled(int32 InISID) const  { return GetPropState(InISID) == EPropState::Unsettled; }

bool APropManager::StartMotion(int32 InISID)
{
	return SetInstanceToInMotion(InISID);
}

bool APropManager::StopMotion(int32 InISID)
{
	return SetInstanceToRest(InISID);
}

bool APropManager::StartHeld(int32 InISID, AActor* Holder)
{
	return SetInstanceToHeld(InISID, Holder);
}

bool APropManager::EndHeld(int32 InISID)
{
	return ReleaseGrab(InISID, nullptr, FVector::ZeroVector);
}

// =========================================================================
// Mesh Instance State Functions (Rest, Unsettled, Settling, Held, InMotion)
// =========================================================================
bool APropManager::SetMeshInstanceToRest(UPrimitiveComponent* TargetComponent, int32 InstanceIndex)
{
	if (!TargetComponent) return false;

	int32 TargetISID = GetISID(TargetComponent, InstanceIndex);
	if (TargetISID <= 0)
	{
		if (UInteractableISMComponent* ISMComp = Cast<UInteractableISMComponent>(TargetComponent))
		{
			if (ISMComp->IISMComponentID > 0)
			{
				TargetISID = GetISIDFromISMI(ISMComp->IISMComponentID, InstanceIndex);
			}
		}
	}
	return SetInstanceToRest(TargetISID);
}

bool APropManager::SetMeshInstanceToUnsettled(
	UPrimitiveComponent* TargetComponent,
	int32 InstanceIndex,
	FVector HitImpulse,
	FVector ImpactPoint,
	float UnsettledDelay)
{
	if (!TargetComponent) return false;

	if (UInteractableISMComponent* ISMComp = Cast<UInteractableISMComponent>(TargetComponent))
	{
		if (!ISMComp->IsValidInstance(InstanceIndex) || ISMComp->IsEmptyInstance(InstanceIndex) || ISMComp->IsInstanceParkedAtResetTransform(InstanceIndex))
		{
			return false;
		}
		if (ISMComp->IISMComponentID <= 0)
		{
			RegisterIISMComponent(ISMComp);
		}
		int32 TargetISID = ISMComp->FindISID(InstanceIndex);
		if (TargetISID <= 0)
		{
			TargetISID = GetISIDFromISMI(ISMComp->IISMComponentID, InstanceIndex);
		}
		if (TargetISID <= 0)
		{
			return false;
		}
		ISMComp->BindISIDToISMI(TargetISID, InstanceIndex);
		return SetInstanceToUnsettled(TargetISID, HitImpulse, ImpactPoint, UnsettledDelay);
	}

	const int32 TargetISID = GetISID(TargetComponent, InstanceIndex);
	return SetInstanceToUnsettled(TargetISID, HitImpulse, ImpactPoint, UnsettledDelay);
}

bool APropManager::SetMeshInstanceToSettling(UPrimitiveComponent* TargetComponent, int32 InstanceIndex, float VelocityTolerance)
{
	if (!TargetComponent) return false;

	int32 TargetISID = GetISID(TargetComponent, InstanceIndex);
	if (TargetISID <= 0)
	{
		if (UInteractableISMComponent* ISMComp = Cast<UInteractableISMComponent>(TargetComponent))
		{
			if (ISMComp->IISMComponentID > 0)
			{
				TargetISID = GetISIDFromISMI(ISMComp->IISMComponentID, InstanceIndex);
			}
		}
	}
	return SetInstanceToSettling(TargetISID, VelocityTolerance);
}

bool APropManager::SetMeshInstanceToHeld(
	UPrimitiveComponent* TargetComponent,
	int32 InstanceIndex,
	AActor* HolderActor,
	USceneComponent* AttachParent,
	FName SocketName)
{
	if (!TargetComponent) return false;

	int32 TargetISID = GetISID(TargetComponent, InstanceIndex);
	if (TargetISID <= 0)
	{
		if (UInteractableISMComponent* ISMComp = Cast<UInteractableISMComponent>(TargetComponent))
		{
			if (ISMComp->IISMComponentID > 0)
			{
				TargetISID = GetISIDFromISMI(ISMComp->IISMComponentID, InstanceIndex);
			}
		}
	}
	return SetInstanceToHeld(TargetISID, HolderActor, AttachParent, SocketName);
}

bool APropManager::SetMeshInstanceToInMotion(
	UPrimitiveComponent* TargetComponent,
	int32 InstanceIndex,
	FVector HitImpulse,
	FVector ImpactPoint)
{
	if (!TargetComponent) return false;

	int32 TargetISID = GetISID(TargetComponent, InstanceIndex);
	if (TargetISID <= 0)
	{
		if (UInteractableISMComponent* ISMComp = Cast<UInteractableISMComponent>(TargetComponent))
		{
			if (ISMComp->IISMComponentID > 0)
			{
				TargetISID = GetISIDFromISMI(ISMComp->IISMComponentID, InstanceIndex);
			}
		}
	}
	return SetInstanceToInMotion(TargetISID, HitImpulse, ImpactPoint);
}

bool APropManager::SetInstanceToRest(int32 InISID)
{
	if (InISID <= 0) return false;

	if (!HasAuthority())
	{
		if (GetNetConnection() != nullptr)
		{
			Server_SetInstanceState(InISID, EPropState::AtRest, FVector::ZeroVector, FVector::ZeroVector, -1.0f, nullptr);
		}
		return true;
	}

	return ConvertASMToISM(static_cast<uint32>(InISID), EPropState::AtRest, true, true);
}

bool APropManager::SetInstanceToUnsettled(
	int32 InISID,
	FVector HitImpulse,
	FVector ImpactPoint,
	float UnsettledDelay)
{
	if (InISID <= 0) return false;

	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	UInteractableISMComponent* ISMComp = FindIISMComponent(Record->IISMComponentID);
	if (bDebugPrintInstanceHit || (ISMComp && ISMComp->bDebugPrintInstanceHit))
	{
		const FString NetRoleStr = HasAuthority() ? TEXT("Server") : TEXT("Client");
		const FString DebugMsg = FString::Printf(
			TEXT("[%s] IISM Instance Hit -> PID: %d | ISID: %d | ISMI: %d (ICID: %d, ImpactPoint: %s)"),
			*NetRoleStr,
			Record->ProductID,
			InISID,
			Record->ISMI,
			Record->IISMComponentID,
			*ImpactPoint.ToCompactString());

		UE_LOG(LogTemp, Log, TEXT("%s"), *DebugMsg);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Cyan, DebugMsg);
		}
	}

	FVector SafeImpulse = HitImpulse;
	SafeImpulse.Z = FMath::Clamp(SafeImpulse.Z, -40.0f, 35.0f);
	SafeImpulse = SafeImpulse.GetClampedToMaxSize(220.0f);

	// If already simulating as an ASM, simply retrigger unsettled timer and apply impulse locally without flooding RPCs
	if (Record->Representation == EPropRepresentation::ASM)
	{
		if (AActivePropActor* ExistingASM = GetActiveActorFromISID(InISID))
		{
			if (ExistingASM->PhysicsSimComponent)
			{
				ExistingASM->PhysicsSimComponent->SetStateUnsettled(UnsettledDelay);
			}
			if (!SafeImpulse.IsNearlyZero() && ExistingASM->PropMesh && ExistingASM->PropMesh->IsSimulatingPhysics())
			{
				ExistingASM->PropMesh->AddImpulse(SafeImpulse, NAME_None, true);
			}
			return true;
		}
	}

	if (!HasAuthority())
	{
		// Client-Predicted Promotion ("Correction Method"):
		// Immediately promote the ISM to a dedicated pooled ASM on the Client, start physics simulation locally,
		// and notify the Server which ISID and which ASMID (via HolderActor) the Client is simulating.
		AActivePropActor* PredictedASM = nullptr;
		if (PromoteInstanceToASM(InISID, EPropActivationReason::Drop, false, PredictedASM) && PredictedASM)
		{
			SetPropState(InISID, EPropState::Unsettled);
			if (PredictedASM->PhysicsSimComponent)
			{
				PredictedASM->PhysicsSimComponent->SetStateUnsettled(UnsettledDelay);
			}
			if (!SafeImpulse.IsNearlyZero() && PredictedASM->PropMesh && PredictedASM->PropMesh->IsSimulatingPhysics())
			{
				PredictedASM->PropMesh->AddImpulse(SafeImpulse, NAME_None, true);
			}
		}
		else if (ISMComp)
		{
			ISMComp->MoveInstanceToResetTransform(InISID);
		}

		if (GetNetConnection() != nullptr)
		{
			Server_SetInstanceState(InISID, EPropState::Unsettled, SafeImpulse, ImpactPoint, UnsettledDelay, PredictedASM);
		}
		return true;
	}

	AActivePropActor* PromotedASM = nullptr;
	if (!PromoteInstanceToASM(InISID, EPropActivationReason::Drop, true, PromotedASM) || !PromotedASM)
	{
		return false;
	}

	SetPropState(InISID, EPropState::Unsettled);
	if (PromotedASM->PhysicsSimComponent)
	{
		PromotedASM->PhysicsSimComponent->SetStateUnsettled(UnsettledDelay);
	}

	if (!SafeImpulse.IsNearlyZero() && PromotedASM->PropMesh && PromotedASM->PropMesh->IsSimulatingPhysics())
	{
		PromotedASM->PropMesh->AddImpulse(SafeImpulse, NAME_None, true);
		PromotedASM->LinearVelocity = PromotedASM->PropMesh->GetComponentVelocity();
		PromotedASM->AngularVelocity = PromotedASM->PropMesh->GetPhysicsAngularVelocityInDegrees();
	}

	PromotedASM->ForceNetUpdate();
	return true;
}

bool APropManager::SetInstanceToSettling(int32 InISID, float VelocityTolerance)
{
	if (InISID <= 0) return false;

	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	if (Record->Representation == EPropRepresentation::ASM)
	{
		if (AActivePropActor* ExistingASM = GetActiveActorFromISID(InISID))
		{
			if (ExistingASM->PhysicsSimComponent)
			{
				ExistingASM->PhysicsSimComponent->SetStateSettling(VelocityTolerance);
			}
			return true;
		}
	}

	if (!HasAuthority())
	{
		AActivePropActor* PredictedASM = nullptr;
		if (PromoteInstanceToASM(InISID, EPropActivationReason::Drop, false, PredictedASM) && PredictedASM)
		{
			SetPropState(InISID, EPropState::Settling);
			if (PredictedASM->PhysicsSimComponent)
			{
				PredictedASM->PhysicsSimComponent->SetStateSettling(VelocityTolerance);
			}
		}
		if (GetNetConnection() != nullptr)
		{
			Server_SetInstanceState(InISID, EPropState::Settling, FVector::ZeroVector, FVector::ZeroVector, VelocityTolerance, PredictedASM);
		}
		return true;
	}

	AActivePropActor* PromotedASM = nullptr;
	if (!PromoteInstanceToASM(InISID, EPropActivationReason::Drop, true, PromotedASM) || !PromotedASM)
	{
		return false;
	}

	SetPropState(InISID, EPropState::Settling);
	if (PromotedASM->PhysicsSimComponent)
	{
		PromotedASM->PhysicsSimComponent->SetStateSettling(VelocityTolerance);
	}

	PromotedASM->ForceNetUpdate();
	return true;
}

bool APropManager::SetInstanceToHeld(
	int32 InISID,
	AActor* HolderActor,
	USceneComponent* AttachParent,
	FName SocketName)
{
	if (InISID <= 0) return false;

	if (!HasAuthority())
	{
		if (GetNetConnection() != nullptr)
		{
			if (UInteractableISMComponent* ISMComp = GetComponentFromISID(InISID))
			{
				ISMComp->MoveInstanceToResetTransform(InISID);
			}
			Server_SetInstanceState(InISID, EPropState::Held, FVector::ZeroVector, FVector::ZeroVector, -1.0f, HolderActor);
		}
		return true;
	}

	if (HolderActor || AttachParent)
	{
		return ExecuteGrab(InISID, HolderActor, AttachParent ? AttachParent : (HolderActor ? HolderActor->GetRootComponent() : nullptr), SocketName);
	}

	AActivePropActor* PromotedASM = nullptr;
	if (!PromoteInstanceToASM(InISID, EPropActivationReason::Grab, false, PromotedASM) || !PromotedASM)
	{
		return false;
	}

	SetPropState(InISID, EPropState::Held);
	PromotedASM->bIsHeld = true;
	PromotedASM->SetGrabbable(false);
	if (PromotedASM->PropMesh)
	{
		PromotedASM->PropMesh->SetSimulatePhysics(false);
		PromotedASM->PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		PromotedASM->PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		PromotedASM->PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
	}
	if (PromotedASM->PhysicsSimComponent)
	{
		PromotedASM->PhysicsSimComponent->SetStateHeld();
	}

	PromotedASM->ForceNetUpdate();
	return true;
}

bool APropManager::SetInstanceToInMotion(
	int32 InISID,
	FVector HitImpulse,
	FVector ImpactPoint)
{
	if (InISID <= 0) return false;

	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	FVector SafeImpulse = HitImpulse;
	SafeImpulse.Z = FMath::Clamp(SafeImpulse.Z, -40.0f, 35.0f);
	SafeImpulse = SafeImpulse.GetClampedToMaxSize(250.0f);

	if (Record->Representation == EPropRepresentation::ASM)
	{
		if (AActivePropActor* ExistingASM = GetActiveActorFromISID(InISID))
		{
			if (ExistingASM->PhysicsSimComponent)
			{
				ExistingASM->PhysicsSimComponent->SetStateInMotion();
			}
			if (!SafeImpulse.IsNearlyZero() && ExistingASM->PropMesh && ExistingASM->PropMesh->IsSimulatingPhysics())
			{
				ExistingASM->PropMesh->AddImpulse(SafeImpulse, NAME_None, true);
			}
			return true;
		}
	}

	if (!HasAuthority())
	{
		AActivePropActor* PredictedASM = nullptr;
		if (PromoteInstanceToASM(InISID, EPropActivationReason::PhysicsImpulse, false, PredictedASM) && PredictedASM)
		{
			SetPropState(InISID, EPropState::InMotion);
			if (PredictedASM->PhysicsSimComponent)
			{
				PredictedASM->PhysicsSimComponent->SetStateInMotion();
			}
			if (!SafeImpulse.IsNearlyZero() && PredictedASM->PropMesh && PredictedASM->PropMesh->IsSimulatingPhysics())
			{
				PredictedASM->PropMesh->AddImpulse(SafeImpulse, NAME_None, true);
			}
		}
		else if (UInteractableISMComponent* ISMComp = GetComponentFromISID(InISID))
		{
			ISMComp->MoveInstanceToResetTransform(InISID);
		}

		if (GetNetConnection() != nullptr)
		{
			Server_SetInstanceState(InISID, EPropState::InMotion, SafeImpulse, ImpactPoint, -1.0f, PredictedASM);
		}
		return true;
	}

	AActivePropActor* PromotedASM = nullptr;
	if (!PromoteInstanceToASM(InISID, EPropActivationReason::PhysicsImpulse, true, PromotedASM) || !PromotedASM)
	{
		return false;
	}

	SetPropState(InISID, EPropState::InMotion);
	if (PromotedASM->PhysicsSimComponent)
	{
		PromotedASM->PhysicsSimComponent->SetStateInMotion();
	}

	if (!SafeImpulse.IsNearlyZero() && PromotedASM->PropMesh && PromotedASM->PropMesh->IsSimulatingPhysics())
	{
		PromotedASM->PropMesh->AddImpulse(SafeImpulse, NAME_None, true);
		PromotedASM->LinearVelocity = PromotedASM->PropMesh->GetComponentVelocity();
		PromotedASM->AngularVelocity = PromotedASM->PropMesh->GetPhysicsAngularVelocityInDegrees();
	}

	PromotedASM->ForceNetUpdate();
	return true;
}

void APropManager::Server_SetInstanceState_Implementation(
	int32 InISID,
	EPropState TargetState,
	FVector HitImpulse,
	FVector_NetQuantize ImpactPoint,
	float CustomParam,
	AActor* HolderActor)
{
	AActivePropActor* ClientPredictedASM = (TargetState != EPropState::Held) ? Cast<AActivePropActor>(HolderActor) : nullptr;
	FISIDRuntimeIndex* Record = FindISIDData(InISID);

	if (!Record)
	{
		// Client predicted an invalid/deleted ISID: force the client's predicted ASM to reconcile back to pool
		if (ClientPredictedASM)
		{
			ClientPredictedASM->ForceNetUpdate();
		}
		return;
	}

	// If the client is requesting to convert/promote to ASM (TargetState != AtRest):
	// Check if already converted to ASM OR currently in process of converting to ASM!
	if (TargetState != EPropState::AtRest)
	{
		if (IsISIDConvertingOrConvertedToASM(InISID))
		{
			// Already converted or currently in process of converting: DO NOTHING!
			if (ClientPredictedASM)
			{
				ClientPredictedASM->ForceNetUpdate();
			}
			if (AActivePropActor* ExistingServerASM = GetActiveActorFromISID(InISID))
			{
				ExistingServerASM->ForceNetUpdate();
				if (TargetState == EPropState::Held)
				{
					SetInstanceToHeld(InISID, HolderActor);
				}
				else if (TargetState == EPropState::Unsettled)
				{
					if (ExistingServerASM->PhysicsSimComponent)
					{
						ExistingServerASM->PhysicsSimComponent->SetStateUnsettled(CustomParam);
					}
					if (!HitImpulse.IsNearlyZero() && ExistingServerASM->PropMesh && ExistingServerASM->PropMesh->IsSimulatingPhysics())
					{
						ExistingServerASM->PropMesh->AddImpulse(HitImpulse, NAME_None, true);
					}
				}
			}
			return;
		}
	}

	// If the Client reported which dedicated pooled ASM (ASMID) it started simulating locally, verify that ASMID is free on the Server
	if (ClientPredictedASM && Record->Representation == EPropRepresentation::ISM)
	{
		if (!ClientPredictedASM->bIsActive && ClientPredictedASM->ISID <= 0 && ClientPredictedASM->ASMID > 0)
		{
			// Pre-bind Record->ASMID so PromoteRecordToASM promotes using the exact same ASMID the Client is already simulating!
			Record->ASMID = ClientPredictedASM->ASMID;
		}
	}

	switch (TargetState)
	{
	case EPropState::AtRest:
		SetInstanceToRest(InISID);
		break;
	case EPropState::Unsettled:
		SetInstanceToUnsettled(InISID, HitImpulse, ImpactPoint, CustomParam);
		break;
	case EPropState::Settling:
		SetInstanceToSettling(InISID, CustomParam);
		break;
	case EPropState::Held:
		SetInstanceToHeld(InISID, HolderActor);
		break;
	case EPropState::InMotion:
		SetInstanceToInMotion(InISID, HitImpulse, ImpactPoint);
		break;
	default:
		break;
	}

	// Server verification & correction check:
	// If the Server ended up promoting a DIFFERENT ASM than ClientPredictedASM (or rejected the promotion),
	// force network replication on both the authoritative ASM and the Client's predicted ASM so the Client corrects immediately.
	AActivePropActor* AuthoritativeASM = GetActiveActorFromISID(InISID);
	if (ClientPredictedASM && ClientPredictedASM != AuthoritativeASM)
	{
		ClientPredictedASM->ForceNetUpdate();
		if (AuthoritativeASM)
		{
			AuthoritativeASM->ForceNetUpdate();
		}
		MarkInstanceDirty(InISID, EPropDirtyFlags::Representation | EPropDirtyFlags::State | EPropDirtyFlags::Transform);
	}
	else if (!AuthoritativeASM && Record->Representation == EPropRepresentation::ISM)
	{
		MarkInstanceDirty(InISID, EPropDirtyFlags::Representation | EPropDirtyFlags::State | EPropDirtyFlags::Transform);
	}
}

bool APropManager::Server_SetInstanceState_Validate(
	int32 InISID,
	EPropState TargetState,
	FVector HitImpulse,
	FVector_NetQuantize ImpactPoint,
	float CustomParam,
	AActor* HolderActor)
{
	return InISID > 0;
}

// =========================================================================
// Section 19.3 — Client Presentation Functions
// =========================================================================
void APropManager::ApplyPropState(int32 InISID, EPropState InState)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return;

	Record->State = InState;
	Record->PackedRuntimeData.bIsGrabbable = (InState != EPropState::Held);
	if (Record->ActiveActor)
	{
		Record->ActiveActor->State = InState;
		if (InState == EPropState::Held)
		{
			Record->ActiveActor->bIsHeld = true;
			Record->ActiveActor->SetGrabbable(false);
			if (Record->ActiveActor->PropMesh)
			{
				Record->ActiveActor->PropMesh->SetSimulatePhysics(false);
				Record->ActiveActor->PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
				Record->ActiveActor->PropMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
				Record->ActiveActor->PropMesh->SetCollisionProfileName(FName(TEXT("PhyBody")));
			}
			if (Record->ActiveActor->PhysicsSimComponent)
			{
				Record->ActiveActor->PhysicsSimComponent->SetStateHeld();
			}
		}
		else if (Record->ActiveActor->bIsHeld)
		{
			Record->ActiveActor->bIsHeld = false;
			Record->ActiveActor->SetGrabbable(true);
		}
	}
	UpdateClientASM(InISID);
	UpdateClientISM(InISID);
}

void APropManager::ApplyPropTransform(int32 InISID, const FTransform& InTransform)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return;

	Record->ActiveTransform = InTransform;
	if (Record->Representation == EPropRepresentation::ISM)
	{
		UpdateClientISM(InISID);
	}
	else
	{
		UpdateClientASM(InISID);
	}
}

void APropManager::ApplyPropRuntimeData(int32 InISID, const FPropPackedRuntimeData& InData)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return;

	Record->PackedRuntimeData = InData;
	UpdateClientASM(InISID);
	UpdateClientISM(InISID);
}

AActivePropActor* APropManager::CreateClientASM(int32 InISID)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return nullptr;

	if (Record->ActiveActor)
	{
		return Record->ActiveActor;
	}

	AActivePropActor* ASM = AcquireActiveActor(Record->ProductID);
	if (ASM)
	{
		ASM->InitializeFromISIDRecord(*Record, ProductDefinitions.Find(Record->ProductID), this);
		ASM->ActivateASMRepresentation(Record->ActiveTransform, Record->State, false);
		RegisterActiveActor(InISID, ASM);
	}
	return ASM;
}

void APropManager::DestroyClientASM(int32 InISID)
{
	if (AActivePropActor* ASM = GetActiveActorFromISID(InISID))
	{
		UnregisterActiveActor(InISID);
		ReturnActiveActorToPool(ASM);
	}
}

void APropManager::ActivateClientRepresentation(int32 InISID, EPropRepresentation Rep)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return;

	Record->Representation = Rep;
	if (Rep == EPropRepresentation::ASM)
	{
		if (UInteractableISMComponent* Comp = GetComponentFromISID(InISID))
		{
			Comp->MoveInstanceToResetTransform(InISID);
		}
	}
	else
	{
		if (UInteractableISMComponent* Comp = GetComponentFromISID(InISID))
		{
			Comp->RestoreInstanceToGameplayTransform(InISID, Record->ActiveTransform);
		}
	}
}

void APropManager::DeactivateClientRepresentation(int32 InISID)
{
	if (UInteractableISMComponent* Comp = GetComponentFromISID(InISID))
	{
		Comp->MoveInstanceToResetTransform(InISID);
	}
	DestroyClientASM(InISID);
}

void APropManager::UpdateClientISM(int32 InISID)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return;

	UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID);
	if (!Comp)
	{
		Comp = GetComponentFromISID(InISID);
	}
	if (Comp && Comp->ProductID > 0 && Record->ProductID > 0 && Comp->ProductID != Record->ProductID)
	{
		Comp = nullptr;
	}
	if (!Comp && Record->ProductID > 0)
	{
		Comp = GetOrCreateIISMComponent(Record->ProductID);
		if (Comp)
		{
			Record->IISMComponentID = Comp->IISMComponentID;
		}
	}

	if (Comp)
	{
		if (Record->IISMComponentID <= 0 && Comp->IISMComponentID > 0)
		{
			Record->IISMComponentID = Comp->IISMComponentID;
		}

		if (!Comp->GetStaticMesh() || (Record->ProductID > 0 && Comp->ProductID != Record->ProductID))
		{
			if (const FPropProductDefinition* Def = ProductDefinitions.Find(Record->ProductID))
			{
				ApplyProductDefinitionToComponent(Comp, *Def);
			}
		}

		const FTransform TargetTransform = (Record->Representation == EPropRepresentation::ASM)
			? Comp->GetEffectiveResetTransform()
			: Record->ActiveTransform;

		int32 LocalISMI = Comp->FindISMI(InISID);
		if (LocalISMI == INDEX_NONE || !Comp->IsValidInstance(LocalISMI) || Comp->IsEmptyInstance(LocalISMI))
		{
			LocalISMI = Comp->ReuseOrAddInstanceWithISID(InISID, TargetTransform, Record->PackedRuntimeData, Record->ISMI, true);
			Record->ISMI = LocalISMI;
		}
		else
		{
			Comp->BindISIDToISMI(InISID, LocalISMI);
			Record->ISMI = LocalISMI;
		}

		Comp->SetInstanceData(InISID, Record->PackedRuntimeData);
		if (Record->Representation == EPropRepresentation::ISM)
		{
			Comp->RestoreInstanceToGameplayTransform(InISID, Record->ActiveTransform);
		}
		else
		{
			Comp->MoveInstanceToResetTransform(InISID);
		}
	}
}

void APropManager::UpdateClientASM(int32 InISID)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return;

	AActivePropActor* ASM = GetActiveActorFromISID(InISID);
	if (!ASM && Record->Representation == EPropRepresentation::ASM && Record->ASMID > 0)
	{
		if (AActivePropActor* ByASMID = GetActiveActorFromASMID(Record->ASMID))
		{
			if (IsValid(ByASMID) && (ByASMID->ISID == InISID || ByASMID->ISID <= 0))
			{
				RegisterActiveActor(InISID, ByASMID);
				ASM = ByASMID;
			}
		}
	}
	if (!ASM && ASMComponent && Record->Representation == EPropRepresentation::ASM)
	{
		for (AActivePropActor* Candidate : ASMComponent->ASMActors)
		{
			if (Candidate && ((Candidate->ISID == InISID && Candidate->bIsActive) || (Record->ASMID > 0 && Candidate->ASMID == Record->ASMID)))
			{
				RegisterActiveActor(InISID, Candidate);
				ASM = Candidate;
				break;
			}
		}
	}

	if (ASM)
	{
		if (Record->Representation == EPropRepresentation::ISM)
		{
			UnregisterActiveActor(InISID);
			ASM->bIsInterpolatingToRest = false;
			ASM->OnRestInterpComplete = nullptr;
			ASM->ISID = 0;
			ASM->bIsActive = false;
			ASM->DeactivateToPoolState(DefaultResetTransform);
			if (ASM->ASMID > 0)
			{
				if (ASMComponent)
				{
					ASMComponent->EmptyASMIDs.AddUnique(ASM->ASMID);
				}
				EmptyASMIDs.AddUnique(ASM->ASMID);
			}
		}
		else
		{
			// If this exact ASM is ALREADY active, visible, and simulating in the gameplay world for this ISID
			// (e.g., Client predicted it locally and the Server confirmed the same ASMID), do NOT touch its transform or physics!
			if (ASM->bIsActive && !ASM->IsHidden() && ASM->GetActorEnableCollision() && ASM->GetActorLocation().Z > -50000.0f && ASM->PropMesh && ASM->PropMesh->GetStaticMesh() != nullptr)
			{
				return;
			}

			UInteractableISMComponent* Comp = GetComponentFromISID(InISID);
			if (Record->ProductID <= 0 && Comp && Comp->ProductID > 0)
			{
				Record->ProductID = Comp->ProductID;
			}

			const FPropProductDefinition* ProductDef = ProductDefinitions.Find(Record->ProductID);
			FPropProductDefinition FallbackDef;
			if ((!ProductDef || !ProductDef->StaticMesh) && Comp)
			{
				const int32 ResolvedProductID = Record->ProductID > 0 ? Record->ProductID : Comp->ProductID;
				if (Comp->IISMDB.StaticMesh)
				{
					FallbackDef = Comp->IISMDB;
					FallbackDef.ProductID = ResolvedProductID;
				}
				else if (Comp->DefaultDataAsset)
				{
					FallbackDef = FPropProductDefinition::FromDataAsset(Comp->DefaultDataAsset, ResolvedProductID, Comp->StaticMeshID);
				}
				else if (Comp->GetStaticMesh())
				{
					FallbackDef.ProductID = ResolvedProductID;
					FallbackDef.StaticMeshID = (Comp->StaticMeshID > 0) ? Comp->StaticMeshID : ResolvedProductID;
					FallbackDef.StaticMesh = Comp->GetStaticMesh();
					FallbackDef.DisplayName = FText::FromName(Comp->GetStaticMesh()->GetFName());
				}

				if (FallbackDef.MaterialOverrides.Num() == 0)
				{
					const int32 NumMats = Comp->GetNumMaterials();
					for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
					{
						FallbackDef.MaterialOverrides.Add(Comp->GetMaterial(MatIdx));
					}
				}

				if (FallbackDef.ProductID > 0 && FallbackDef.StaticMesh)
				{
					RegisterProductDefinition(FallbackDef);
					ProductDef = ProductDefinitions.Find(FallbackDef.ProductID);
				}
				if (!ProductDef && FallbackDef.StaticMesh)
				{
					ProductDef = &FallbackDef;
				}
			}

			ASM->InitializeFromISIDRecord(*Record, ProductDef, this);

			FTransform TargetTransform = Record->ActiveTransform;
			if ((TargetTransform.GetLocation().Z < -50000.0f || TargetTransform.GetScale3D().GetMin() < 0.005f) && Comp)
			{
				FTransform CachedTransform;
				if (Comp->GetInstanceTransformByISID(InISID, CachedTransform, true))
				{
					TargetTransform = CachedTransform;
				}
			}

			if (TargetTransform.GetScale3D().GetMin() < 0.005f)
			{
				TargetTransform.SetScale3D(Comp ? Comp->GetComponentScale() : FVector::OneVector);
			}
			Record->ActiveTransform = TargetTransform;

			// Activate the Server-promoted ASM cleanly on the Client (places at TargetTransform, fires OnBecomeActive, enables collision & physics)
			ASM->ActivateASMRepresentation(TargetTransform, Record->State, false);
			if (ASM->PropMesh && ASM->PropMesh->IsSimulatingPhysics() && !ASM->LinearVelocity.IsNearlyZero())
			{
				ASM->PropMesh->SetPhysicsLinearVelocity(ASM->LinearVelocity.GetClampedToMaxSize(250.0f));
			}
		}
	}
}

void APropManager::SetClientCollision(int32 InISID, bool bEnabled)
{
	if (AActivePropActor* ASM = GetActiveActorFromISID(InISID))
	{
		ASM->SetActorEnableCollision(bEnabled);
	}
}

void APropManager::SetClientVisibility(int32 InISID, bool bVisible)
{
	if (AActivePropActor* ASM = GetActiveActorFromISID(InISID))
	{
		ASM->SetActorHiddenInGame(!bVisible);
	}
}

void APropManager::SetClientPhysics(int32 InISID, bool bSimulate)
{
	if (AActivePropActor* ASM = GetActiveActorFromISID(InISID))
	{
		if (ASM->PropMesh)
		{
			ASM->PropMesh->SetSimulatePhysics(bSimulate);
		}
	}
}

// =========================================================================
// 37.K & Section 34 — Bulk Replication Functions
// =========================================================================
FPropInstanceDelta APropManager::BuildDeltaForRecord(const FISIDRuntimeIndex& Record) const
{
	FPropInstanceDelta Delta;
	Delta.ISID = Record.ISID;
	Delta.ProductID = Record.ProductID;
	Delta.IISMComponentID = Record.IISMComponentID;
	Delta.ISMI = Record.ISMI;
	Delta.Version = Record.Version;
	Delta.DirtyFlags = Record.DirtyFlags;
	Delta.Representation = Record.Representation;
	Delta.State = Record.State;
	Delta.PackedRuntimeBits = static_cast<int64>(Record.PackedRuntimeData.PackToUint64());
	Delta.Transform = GetTransform(Record.ISID);
	Delta.OpenAmount = Record.RuntimeData.OpenAmount;
	Delta.bActivated = Record.RuntimeData.bActivated;
	Delta.Damage = Record.RuntimeData.Damage;
	Delta.Durability = Record.RuntimeData.Durability;
	const int32 PackedASMID = (Record.Representation == EPropRepresentation::ASM && Record.ASMID > 0) ? (Record.ASMID & 0xFFFF) : 0;
	Delta.CustomFlags = (Record.RuntimeData.CustomFlags & 0xFFFF) | (PackedASMID << 16);
	return Delta;
}

bool APropManager::BuildBulkUpdateBatch(int32 MaxRecords, FPropBulkUpdateBatch& OutBatch)
{
	OutBatch = FPropBulkUpdateBatch();
	if (DirtyISIDs.Num() == 0 && PendingDeletedISIDs.Num() == 0 && PendingReleasedToEmptyISIDs.Num() == 0)
	{
		return false;
	}

	OutBatch.BatchSequence = NextBatchSequence++;
	const int32 BoundedMax = FMath::Clamp(MaxRecords, 1, FMath::Min(MaxRecordsPerBatch, MaxInstancesPerFrame));

	// Include pending deleted ISIDs
	while (PendingDeletedISIDs.Num() > 0 && OutBatch.DeletedISIDs.Num() < BoundedMax)
	{
		OutBatch.DeletedISIDs.Add(PendingDeletedISIDs.Pop(EAllowShrinking::No));
	}

	// Include pending released-to-empty ISIDs (Sections 40, 41, 61)
	while (PendingReleasedToEmptyISIDs.Num() > 0 && OutBatch.ReleasedToEmptyISIDs.Num() < BoundedMax)
	{
		OutBatch.ReleasedToEmptyISIDs.Add(PendingReleasedToEmptyISIDs.Pop(EAllowShrinking::No));
	}

	TSet<int32> IncludedProductIDs;

	// Build compact delta records within MaxBytesPerFrame & MaxRecordsPerBatch budget
	while (DirtyISIDs.Num() > 0 && OutBatch.Records.Num() < BoundedMax)
	{
		if (OutBatch.GetEstimatedByteSize() + static_cast<int32>(sizeof(FPropInstanceDelta)) > MaxBytesPerFrame)
		{
			break;
		}

		const int32 DirtyISID = DirtyISIDs[0];
		DirtyISIDs.RemoveAt(0, EAllowShrinking::No);

		if (FISIDRuntimeIndex* Record = FindISIDData(DirtyISID))
		{
			if (Record->ProductID > 0 && !IncludedProductIDs.Contains(Record->ProductID))
			{
				if (const FPropProductDefinition* Def = ProductDefinitions.Find(Record->ProductID))
				{
					OutBatch.ProductDefinitions.Add(*Def);
					IncludedProductIDs.Add(Record->ProductID);
				}
			}
			OutBatch.Records.Add(BuildDeltaForRecord(*Record));
			Record->DirtyFlags = EPropDirtyFlags::Clean;
		}
	}

	return (OutBatch.Records.Num() > 0 || OutBatch.DeletedISIDs.Num() > 0 || OutBatch.ReleasedToEmptyISIDs.Num() > 0);
}

void APropManager::QueueClientSync(APlayerController* ClientController, const TArray<int32>& ISIDsToSync)
{
	if (!ClientController) return;
	FPropClientSyncState& Stream = ClientDeliveryStreams.FindOrAdd(ClientController);
	Stream.ClientController = ClientController;
	for (int32 ID : ISIDsToSync)
	{
		Stream.QueuedISIDs.AddUnique(ID);
	}
}

void APropManager::SendInstanceDelta(int32 InISID)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return;

	DirtyISIDs.Remove(InISID);
	Record->DirtyFlags = EPropDirtyFlags::Clean;

	FPropBulkUpdateBatch SingleBatch;
	SingleBatch.BatchSequence = NextBatchSequence++;
	if (Record->ProductID > 0)
	{
		if (const FPropProductDefinition* Def = ProductDefinitions.Find(Record->ProductID))
		{
			SingleBatch.ProductDefinitions.Add(*Def);
		}
	}
	SingleBatch.Records.Add(BuildDeltaForRecord(*Record));
	Multicast_ReceiveBulkBatch(SingleBatch);
}

void APropManager::SendFullSnapshot(APlayerController* ClientController)
{
	SendInitialPropSnapshot(ClientController);
}

int32 APropManager::GenerateClientProductInstance(const FPropInstanceDelta& Delta)
{
	if (Delta.ISID <= 0 || (Delta.ProductID <= 0 && Delta.IISMComponentID <= 0))
	{
		return 0;
	}

	FPropPackedRuntimeData UnpackedData;
	UnpackedData.UnpackFromUint64(static_cast<uint64>(Delta.PackedRuntimeBits));

	// Resolve or create the client IISM component for this ProductID / ICID
	UInteractableISMComponent* Comp = FindIISMComponent(Delta.IISMComponentID);
	if (!Comp)
	{
		Comp = GetComponentFromISID(Delta.ISID);
	}
	if (Comp && Comp->ProductID > 0 && Delta.ProductID > 0 && Comp->ProductID != Delta.ProductID)
	{
		Comp = nullptr;
	}
	if (!Comp && Delta.ProductID > 0)
	{
		Comp = GetOrCreateIISMComponent(Delta.ProductID);
		if (Comp && Delta.IISMComponentID > 0 && Comp->IISMComponentID != Delta.IISMComponentID)
		{
			ComponentsByICID.Remove(Comp->IISMComponentID);
			Comp->IISMComponentID = Delta.IISMComponentID;
			ComponentsByICID.Add(Delta.IISMComponentID, Comp);
		}
	}

	if (!Comp)
	{
		return 0;
	}

	const int32 EffectiveProductID = Delta.ProductID > 0 ? Delta.ProductID : Comp->ProductID;

	if (!Comp->GetStaticMesh() || (EffectiveProductID > 0 && Comp->ProductID != EffectiveProductID))
	{
		if (const FPropProductDefinition* Def = ProductDefinitions.Find(EffectiveProductID))
		{
			ApplyProductDefinitionToComponent(Comp, *Def);
		}
	}

	const FTransform TargetTransform = (Delta.Representation == EPropRepresentation::ASM)
		? Comp->GetEffectiveResetTransform()
		: Delta.Transform;

	int32 LocalISMI = Comp->FindISMI(Delta.ISID);
	if (LocalISMI == INDEX_NONE || !Comp->IsValidInstance(LocalISMI) || Comp->IsEmptyInstance(LocalISMI))
	{
		// Section 42 & 59: Client reuses EmptyInstances first or creates a new ISM instance
		LocalISMI = Comp->ReuseOrAddInstanceWithISID(Delta.ISID, TargetTransform, UnpackedData, Delta.ISMI, true);
	}
	else
	{
		Comp->BindISIDToISMI(Delta.ISID, LocalISMI);
		Comp->UpdateInstanceTransform(LocalISMI, TargetTransform, true, true, true);
		Comp->SetInstanceDataByISMI(LocalISMI, UnpackedData);
	}

	RegisterInstance(Delta.ISID, EffectiveProductID, Comp->IISMComponentID, LocalISMI, Delta.Transform);
	return Delta.ISID;
}

void APropManager::ApplyInstanceDelta(const FPropInstanceDelta& Delta)
{
	if (Delta.ISID <= 0) return;

	FISIDRuntimeIndex* Existing = FindISIDData(Delta.ISID);

	FPropPackedRuntimeData UnpackedData;
	UnpackedData.UnpackFromUint64(static_cast<uint64>(Delta.PackedRuntimeBits));

	if (!Existing)
	{
		GenerateClientProductInstance(Delta);
		Existing = FindISIDData(Delta.ISID);
	}

	if (!Existing) return;

	const int32 UnpackedASMID = (Delta.CustomFlags >> 16) & 0xFFFF;
	const int32 UnpackedCustomFlags = Delta.CustomFlags & 0xFFFF;

	// If the Client had predicted a DIFFERENT ASMID than the Server's authoritative UnpackedASMID,
	// return the Client's mispredicted ASM back to the pool so the Server's authoritative ASM takes over cleanly.
	if (Delta.Representation == EPropRepresentation::ASM && UnpackedASMID > 0)
	{
		if (AActivePropActor* CurrentClientASM = GetActiveActorFromISID(Delta.ISID))
		{
			if (CurrentClientASM->ASMID != UnpackedASMID)
			{
				UnregisterActiveActor(Delta.ISID);
				CurrentClientASM->ISID = 0;
				CurrentClientASM->bIsActive = false;
				CurrentClientASM->DeactivateToPoolState(DefaultResetTransform);
				if (CurrentClientASM->ASMID > 0)
				{
					if (ASMComponent)
					{
						ASMComponent->EmptyASMIDs.AddUnique(CurrentClientASM->ASMID);
					}
					EmptyASMIDs.AddUnique(CurrentClientASM->ASMID);
				}
			}
		}
		Existing->ASMID = UnpackedASMID;
	}
	else if (Delta.Representation == EPropRepresentation::ISM)
	{
		Existing->ASMID = 0;
	}

	if (Delta.ProductID > 0)
	{
		Existing->ProductID = Delta.ProductID;
	}
	if (Delta.IISMComponentID > 0)
	{
		Existing->IISMComponentID = Delta.IISMComponentID;
	}
	if (Delta.ISMI != INDEX_NONE)
	{
		Existing->ISMI = Delta.ISMI;
	}
	Existing->Version = Delta.Version;
	Existing->State = Delta.State;
	Existing->Representation = Delta.Representation;
	Existing->PackedRuntimeData = UnpackedData;
	if (Delta.Transform.GetLocation().Z > -50000.0f && Delta.Transform.GetScale3D().GetMin() >= 0.005f)
	{
		Existing->ActiveTransform = Delta.Transform;
	}
	Existing->RuntimeData.OpenAmount = Delta.OpenAmount;
	Existing->RuntimeData.bActivated = Delta.bActivated;
	Existing->RuntimeData.Damage = Delta.Damage;
	Existing->RuntimeData.Durability = Delta.Durability;
	Existing->RuntimeData.CustomFlags = UnpackedCustomFlags;
	Existing->RuntimeData.RemainingPortions = UnpackedData.RemainingPortions;
	Existing->RuntimeData.Temperature = UnpackedData.Temperature;

	UpdateClientASM(Delta.ISID);
	UpdateClientISM(Delta.ISID);
}

void APropManager::AcknowledgeBulkBatch_Implementation(int32 BatchSequence)
{
	for (TPair<TObjectPtr<APlayerController>, FPropClientSyncState>& Pair : ClientDeliveryStreams)
	{
		Pair.Value.LastAckedBatchSequence = FMath::Max(Pair.Value.LastAckedBatchSequence, BatchSequence);
		Pair.Value.PendingBatches.RemoveAll([BatchSequence](const FPropBulkUpdateBatch& B)
		{
			return B.BatchSequence <= BatchSequence;
		});
	}
}

bool APropManager::AcknowledgeBulkBatch_Validate(int32 BatchSequence)
{
	return BatchSequence >= 0;
}

void APropManager::RetryBulkBatch(int32 BatchSequence, APlayerController* ClientController)
{
	for (TPair<TObjectPtr<APlayerController>, FPropClientSyncState>& Pair : ClientDeliveryStreams)
	{
		if (!ClientController || Pair.Key == ClientController)
		{
			for (const FPropBulkUpdateBatch& Pending : Pair.Value.PendingBatches)
			{
				if (Pending.BatchSequence == BatchSequence)
				{
					Multicast_ReceiveBulkBatch(Pending);
					return;
				}
			}
		}
	}
}

void APropManager::ResyncClient(APlayerController* ClientController)
{
	SendInitialPropSnapshot(ClientController);
}

void APropManager::Multicast_ReceiveBulkBatch_Implementation(const FPropBulkUpdateBatch& Batch)
{
	if (HasAuthority()) return;

	EnsureLevelPropsInitialized();

	// 1. Register or update any product definitions included in the batch on the client
	for (const FPropProductDefinition& Def : Batch.ProductDefinitions)
	{
		if (Def.ProductID > 0 && !ProductDefinitions.Contains(Def.ProductID))
		{
			RegisterProductDefinition(Def);
		}
	}

	// 2. Release instances that became Empty on the server to the client's EmptyInstances pool
	for (int32 ReleasedISID : Batch.ReleasedToEmptyISIDs)
	{
		ReleaseInstanceToEmpty(ReleasedISID);
	}

	// 3. Permanently destroy any deleted instances
	for (int32 DeletedISID : Batch.DeletedISIDs)
	{
		DestroyPropInstance(DeletedISID);
	}

	// 4. Apply all created/reused/updated instance deltas
	for (const FPropInstanceDelta& RecordDelta : Batch.Records)
	{
		ApplyInstanceDelta(RecordDelta);
	}

	if (GetNetConnection() != nullptr)
	{
		AcknowledgeBulkBatch(Batch.BatchSequence);
	}
}

void APropManager::ProcessBulkReplication(float DeltaSeconds)
{
	// Ensure any newly connected or map-loaded remote clients receive the initial prop snapshot
	if (UWorld* World = GetWorld())
	{
		const float Now = World->GetTimeSeconds();
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (PC && !PC->IsLocalController() && PC->HasClientLoadedCurrentWorld())
			{
				if (GetOwner() == nullptr)
				{
					SetOwner(PC);
				}
				FPropClientSyncState& Stream = ClientDeliveryStreams.FindOrAdd(PC);
				Stream.ClientController = PC;
				if (!Stream.bClientSyncReady && Stream.SnapshotSendCount < 3 && (Now - Stream.LastSnapshotSendTime) >= 0.35f)
				{
					Stream.LastSnapshotSendTime = Now;
					++Stream.SnapshotSendCount;
					SendInitialPropSnapshot(PC);
				}
			}
		}
	}

	if (DirtyISIDs.Num() == 0 && PendingDeletedISIDs.Num() == 0 && PendingReleasedToEmptyISIDs.Num() == 0)
	{
		return;
	}

	FPropBulkUpdateBatch OutBatch;
	if (BuildBulkUpdateBatch(MaxRecordsPerBatch, OutBatch))
	{
		Multicast_ReceiveBulkBatch(OutBatch);
	}
}

// =========================================================================
// 66.F & Sections 57–61 — Client Synchronization Functions
// =========================================================================
void APropManager::BuildInitialPropSnapshot(TArray<FPropBulkUpdateBatch>& OutBatches)
{
	OutBatches.Empty();

	FPropBulkUpdateBatch CurrentBatch;
	CurrentBatch.BatchSequence = NextBatchSequence++;
	CurrentBatch.bIsFullSnapshot = true;
	CurrentBatch.bIsFirstInitBatch = true;
	ProductDefinitions.GenerateValueArray(CurrentBatch.ProductDefinitions);

	const int32 BoundedMax = FMath::Clamp(MaxRecordsPerBatch, 1, MaxInstancesPerFrame);

	for (const TPair<int32, FISIDRuntimeIndex>& Pair : ISIDData)
	{
		CurrentBatch.Records.Add(BuildDeltaForRecord(Pair.Value));
		if (CurrentBatch.Records.Num() >= BoundedMax)
		{
			OutBatches.Add(CurrentBatch);
			CurrentBatch = FPropBulkUpdateBatch();
			CurrentBatch.BatchSequence = NextBatchSequence++;
			CurrentBatch.bIsFullSnapshot = true;
			CurrentBatch.bIsFirstInitBatch = false;
		}
	}

	if (CurrentBatch.Records.Num() > 0 || OutBatches.Num() == 0)
	{
		OutBatches.Add(CurrentBatch);
	}

	if (OutBatches.Num() > 0)
	{
		OutBatches.Last().bIsFinalInitBatch = true;
	}
}

void APropManager::SendInitialPropSnapshot(APlayerController* ClientController)
{
	TArray<FPropBulkUpdateBatch> InitBatches;
	BuildInitialPropSnapshot(InitBatches);

	if (ClientController)
	{
		FPropClientSyncState& Stream = ClientDeliveryStreams.FindOrAdd(ClientController);
		Stream.ClientController = ClientController;
		Stream.PendingBatches.Append(InitBatches);
	}

	for (const FPropBulkUpdateBatch& Batch : InitBatches)
	{
		SendPropInitializationBatch(Batch, ClientController);
	}
}

void APropManager::SendPropInitializationBatch(const FPropBulkUpdateBatch& Batch, APlayerController* ClientController)
{
	Multicast_ReceivePropInitializationBatch(Batch);
}

void APropManager::Multicast_ReceivePropInitializationBatch_Implementation(const FPropBulkUpdateBatch& Batch)
{
	if (HasAuthority())
	{
		return;
	}

	ApplyPropInitializationBatch(Batch);
}

void APropManager::ApplyPropInitializationBatch(const FPropBulkUpdateBatch& Batch)
{
	EnsureLevelPropsInitialized();

	if (Batch.bIsFirstInitBatch)
	{
		SetLoadState(EPropManagerLoadState::SynchronizingClients);

		// Register all product definitions from server so client can generate products & IISM components
		for (const FPropProductDefinition& Def : Batch.ProductDefinitions)
		{
			if (Def.ProductID > 0)
			{
				RegisterProductDefinition(Def);
			}
		}
	}

	for (const FPropInstanceDelta& RecordDelta : Batch.Records)
	{
		ApplyInstanceDelta(RecordDelta);
	}

	if (GetNetConnection() != nullptr)
	{
		AcknowledgePropInitialization(Batch.BatchSequence, Batch.bIsFinalInitBatch);
	}

	if (Batch.bIsFinalInitBatch)
	{
		CompleteClientPropSync();
	}
}

void APropManager::AcknowledgePropInitialization_Implementation(int32 BatchSequence, bool bIsFinalBatch)
{
	for (TPair<TObjectPtr<APlayerController>, FPropClientSyncState>& Pair : ClientDeliveryStreams)
	{
		Pair.Value.LastAckedBatchSequence = FMath::Max(Pair.Value.LastAckedBatchSequence, BatchSequence);
		Pair.Value.PendingBatches.RemoveAll([BatchSequence](const FPropBulkUpdateBatch& B)
		{
			return B.BatchSequence <= BatchSequence;
		});
		if (bIsFinalBatch)
		{
			Pair.Value.bClientSyncReady = true;
		}
	}
}

bool APropManager::AcknowledgePropInitialization_Validate(int32 BatchSequence, bool bIsFinalBatch)
{
	return BatchSequence >= 0;
}

bool APropManager::IsClientPropSyncReady(APlayerController* ClientController) const
{
	if (!ClientController)
	{
		return PropSyncReady;
	}

	if (const FPropClientSyncState* Stream = ClientDeliveryStreams.Find(ClientController))
	{
		return Stream->bClientSyncReady;
	}

	return PropSyncReady;
}

void APropManager::CompleteClientPropSync()
{
	RebuildLevelMappings();
	PropManagerReady = true;
	PropSyncReady = true;
	SetLoadState(EPropManagerLoadState::Ready);
}

// =========================================================================
// 37.L & Section 35 — Task System Functions
// =========================================================================
int32 APropManager::CreateTask(EPropTaskType TaskType, int32 InISID, int32 InRequestID)
{
	FPropTaskRecord NewTask;
	NewTask.TaskID = NextTaskID++;
	NewTask.RequestID = (InRequestID > 0) ? InRequestID : NextRequestID++;
	NewTask.ISID = InISID;
	NewTask.TaskType = TaskType;
	NewTask.Status = EPropTaskStatus::Pending;
	NewTask.TargetVersion = (InISID > 0) ? GetVersion(InISID) : 1;
	if (const UWorld* World = GetWorld())
	{
		NewTask.CreatedTime = World->GetTimeSeconds();
	}

	PendingTasks.Add(NewTask);
	return NewTask.TaskID;
}

bool APropManager::StartTask(int32 InTaskID)
{
	for (FPropTaskRecord& Task : PendingTasks)
	{
		if (Task.TaskID == InTaskID)
		{
			Task.Status = EPropTaskStatus::InProgress;
			if (const UWorld* World = GetWorld())
			{
				Task.LastSendTime = World->GetTimeSeconds();
			}
			return true;
		}
	}
	return false;
}

bool APropManager::UpdateTaskProgress(int32 InTaskID, float NewProgress, int32 ProcessedCount)
{
	for (FPropTaskRecord& Task : PendingTasks)
	{
		if (Task.TaskID == InTaskID)
		{
			Task.ProgressRatio = FMath::Clamp(NewProgress, 0.0f, 1.0f);
			Task.ProcessedCount = ProcessedCount;
			if (Task.ProgressRatio >= 1.0f)
			{
				Task.Status = EPropTaskStatus::Completed;
			}
			return true;
		}
	}
	return false;
}

bool APropManager::CompleteTask(int32 InTaskID)
{
	for (int32 i = 0; i < PendingTasks.Num(); ++i)
	{
		if (PendingTasks[i].TaskID == InTaskID)
		{
			PendingTasks[i].Status = EPropTaskStatus::Completed;
			PendingTasks[i].ProgressRatio = 1.0f;
			PendingTasks.RemoveAt(i);
			return true;
		}
	}
	return false;
}

bool APropManager::FailTask(int32 InTaskID)
{
	for (FPropTaskRecord& Task : PendingTasks)
	{
		if (Task.TaskID == InTaskID)
		{
			Task.Status = EPropTaskStatus::Failed;
			return true;
		}
	}
	return false;
}

bool APropManager::CancelTask(int32 InTaskID)
{
	for (int32 i = 0; i < PendingTasks.Num(); ++i)
	{
		if (PendingTasks[i].TaskID == InTaskID)
		{
			PendingTasks[i].Status = EPropTaskStatus::Cancelled;
			PendingTasks.RemoveAt(i);
			return true;
		}
	}
	return false;
}

EPropTaskStatus APropManager::GetTaskStatus(int32 InTaskID) const
{
	for (const FPropTaskRecord& Task : PendingTasks)
	{
		if (Task.TaskID == InTaskID)
		{
			return Task.Status;
		}
	}
	return EPropTaskStatus::Completed;
}

bool APropManager::RetryTask(int32 InTaskID)
{
	for (FPropTaskRecord& Task : PendingTasks)
	{
		if (Task.TaskID == InTaskID)
		{
			Task.RetryCount++;
			Task.Status = EPropTaskStatus::InProgress;
			if (const UWorld* World = GetWorld())
			{
				Task.LastSendTime = World->GetTimeSeconds();
			}
			return true;
		}
	}
	return false;
}

void APropManager::ProcessPendingTasks(float DeltaSeconds)
{
	// Section 35: Tasks execute independently. One slow client does not block unrelated operations.
	for (int32 i = PendingTasks.Num() - 1; i >= 0; --i)
	{
		FPropTaskRecord& Task = PendingTasks[i];
		if (Task.Status == EPropTaskStatus::Pending)
		{
			Task.Status = EPropTaskStatus::InProgress;
		}

		if (Task.Status == EPropTaskStatus::InProgress)
		{
			if (Task.TaskType == EPropTaskType::UpdateInstance && Task.ISID > 0)
			{
				MarkInstanceDirty(Task.ISID, EPropDirtyFlags::All);
				Task.Status = EPropTaskStatus::Completed;
			}
			else if (Task.TaskType == EPropTaskType::GenerateProps)
			{
				Task.Status = EPropTaskStatus::Completed;
			}
		}

		if (Task.Status == EPropTaskStatus::Completed || Task.Status == EPropTaskStatus::Cancelled)
		{
			PendingTasks.RemoveAt(i);
		}
	}
}

// =========================================================================
// 37.M, 66.E & Sections 36, 48–52 — Save / Load & Persistence Functions
// =========================================================================
bool APropManager::SavePropDatabase(const FString& SlotName, int32 UserIndex)
{
	// Step 1: Flush any active ASM actor transforms/states back into ISIDData before saving
	FlushActiveActorState();

	WorldSaveData.SchemaVersion = SchemaVersion;
	WorldSaveData.NextISID = NextISID;
	WorldSaveData.SavedInstances.Empty(ISIDData.Num());

	// Rule 23 & Section 50: Save/load uses ISID as logical identity; Empty Instances are not saved as permanent props.
	for (const TPair<int32, FISIDRuntimeIndex>& Pair : ISIDData)
	{
		FPropSerializedInstance Serialized;
		if (SerializeInstance(Pair.Key, Serialized))
		{
			WorldSaveData.SavedInstances.Add(Serialized);
		}
	}

	UPropDatabaseSaveGame* SaveObj = Cast<UPropDatabaseSaveGame>(UGameplayStatics::CreateSaveGameObject(UPropDatabaseSaveGame::StaticClass()));
	if (!SaveObj) return false;

	SaveObj->WorldSaveData = WorldSaveData;
	return UGameplayStatics::SaveGameToSlot(SaveObj, SlotName, UserIndex);
}

bool APropManager::LoadPropDatabase(const FString& SlotName, int32 UserIndex)
{
	// Section 36 & 51 Load Flow:
	// Load Product Definitions -> Create IISM Components -> Rebuild EmptyInstances -> Restore/Repair ISID Data -> Rebuild ISID -> ISMI mappings -> Activate ASMs only for props that require active representation
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
	{
		return false;
	}

	UPropDatabaseSaveGame* LoadedObj = Cast<UPropDatabaseSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
	if (!LoadedObj) return false;

	WorldSaveData = LoadedObj->WorldSaveData;

	// Return all current active ASMs to pool
	TArray<int32> CurrentActiveISIDs;
	ActiveActorByISID.GetKeys(CurrentActiveISIDs);
	for (int32 ActiveID : CurrentActiveISIDs)
	{
		if (AActivePropActor* ASM = GetActiveActorFromISID(ActiveID))
		{
			ReturnActiveActorToPool(ASM);
		}
	}
	ActiveActorByISID.Empty();

	// Clear existing ISM instances and EmptyInstances
	for (TPair<int32, TObjectPtr<UInteractableISMComponent>>& CompPair : ComponentsByICID)
	{
		if (CompPair.Value)
		{
			CompPair.Value->ClearInstances();
			CompPair.Value->PackedInstanceData.Empty();
			CompPair.Value->PropDatabase.Empty();
			CompPair.Value->ISIDToISMI.Empty();
			CompPair.Value->ISMIToISID.Empty();
			CompPair.Value->EmptyInstances.Empty();
		}
	}
	ISIDData.Empty();
	DirtyISIDs.Empty();
	PendingDeletedISIDs.Empty();
	PendingReleasedToEmptyISIDs.Empty();

	InitializeIISMDB();

	// Deserialize each saved logical prop by ISID (reusing EmptyInstances or creating ISM instances via RepairLoadedISMI)
	for (const FPropSerializedInstance& SavedInst : WorldSaveData.SavedInstances)
	{
		int32 RestoredISID = 0;
		DeserializeInstance(SavedInst, RestoredISID);
	}

	NextISID = FMath::Max(NextISID, WorldSaveData.NextISID);

	RebuildLevelMappings();
	RebuildLevelEmptyInstances();
	RestoreActiveActors();

	return true;
}

int32 APropManager::SaveDirtyInstances(const FString& SlotName, int32 UserIndex)
{
	FlushActiveActorState();

	WorldSaveData.SchemaVersion = SchemaVersion;
	WorldSaveData.NextISID = NextISID;

	int32 SavedCount = 0;
	for (int32 DirtyID : DirtyISIDs)
	{
		FPropSerializedInstance Serialized;
		if (SerializeInstance(DirtyID, Serialized))
		{
			int32 ExistingIdx = WorldSaveData.SavedInstances.IndexOfByPredicate([DirtyID](const FPropSerializedInstance& Item)
			{
				return Item.ISID == DirtyID;
			});

			if (ExistingIdx != INDEX_NONE)
			{
				WorldSaveData.SavedInstances[ExistingIdx] = Serialized;
			}
			else
			{
				WorldSaveData.SavedInstances.Add(Serialized);
			}
			SavedCount++;
		}
		else
		{
			WorldSaveData.SavedInstances.RemoveAll([DirtyID](const FPropSerializedInstance& Item)
			{
				return Item.ISID == DirtyID;
			});
		}
	}

	UPropDatabaseSaveGame* SaveObj = Cast<UPropDatabaseSaveGame>(UGameplayStatics::CreateSaveGameObject(UPropDatabaseSaveGame::StaticClass()));
	if (!SaveObj)
	{
		return 0;
	}

	SaveObj->WorldSaveData = WorldSaveData;
	return UGameplayStatics::SaveGameToSlot(SaveObj, SlotName, UserIndex) ? SavedCount : 0;
}

bool APropManager::FlushPropDatabase(const FString& SlotName, int32 UserIndex)
{
	FlushActiveActorState();
	return SavePropDatabase(SlotName, UserIndex);
}

bool APropManager::ValidateLoadedInstance(const FPropSerializedInstance& InSerialized) const
{
	if (InSerialized.ISID <= 0 || InSerialized.ProductID <= 0)
	{
		return false;
	}
	return true;
}

int32 APropManager::RepairLoadedISMI(int32 InISID, int32 InProductID, int32 InICID, int32 SavedISMI, const FTransform& InTransform, const FPropPackedRuntimeData& InPackedData)
{
	if (InISID <= 0 || InProductID <= 0)
	{
		return INDEX_NONE;
	}

	UInteractableISMComponent* Comp = FindIISMComponent(InICID);
	if (!Comp)
	{
		Comp = GetOrCreateIISMComponent(InProductID);
	}
	if (!Comp)
	{
		return INDEX_NONE;
	}

	// Section 52: Check if saved ISMI exists and is free/empty on the component
	if (SavedISMI >= 0 && SavedISMI < Comp->GetInstanceCount())
	{
		const int32 ExistingISIDAtSlot = Comp->FindISID(SavedISMI);
		if (ExistingISIDAtSlot <= 0 || ExistingISIDAtSlot == InISID || Comp->IsEmptyInstance(SavedISMI))
		{
			Comp->RemoveEmptyInstance(SavedISMI);
			Comp->UpdateInstanceTransform(SavedISMI, InTransform, true, true, true);
			Comp->SetInstanceDataByISMI(SavedISMI, InPackedData);
			Comp->BindISIDToISMI(InISID, SavedISMI);
			return SavedISMI;
		}
	}

	// Otherwise reuse an available EmptyInstance or create a new ISM slot (Sections 51 & 52)
	return Comp->ReuseOrAddInstanceWithISID(InISID, InTransform, InPackedData, true);
}

bool APropManager::SerializeInstance(int32 InISID, FPropSerializedInstance& OutSerialized) const
{
	const FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record || Record->ISID <= 0) return false;

	// Section 50: Empty instances do not have an active logical ISID association and are not saved
	if (const UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
	{
		if (Comp->IsEmptyInstance(Record->ISMI))
		{
			return false;
		}
	}

	OutSerialized.ISID = Record->ISID;
	OutSerialized.ProductID = Record->ProductID;
	OutSerialized.IISMComponentID = Record->IISMComponentID;
	OutSerialized.ISMI = Record->ISMI;
	OutSerialized.Version = Record->Version;
	OutSerialized.State = Record->State;
	OutSerialized.Representation = Record->Representation;
	OutSerialized.ActiveTransform = GetTransform(InISID);
	OutSerialized.PackedRuntimeData = Record->PackedRuntimeData;
	OutSerialized.PackedRuntimeBits = static_cast<int64>(Record->PackedRuntimeData.PackToUint64());
	OutSerialized.OpenAmount = Record->RuntimeData.OpenAmount;
	OutSerialized.bActivated = Record->RuntimeData.bActivated;
	OutSerialized.Damage = Record->RuntimeData.Damage;
	OutSerialized.Durability = Record->RuntimeData.Durability;
	OutSerialized.RemainingFuel = Record->RuntimeData.RemainingFuel;
	OutSerialized.CustomFlags = Record->RuntimeData.CustomFlags;
	return true;
}

bool APropManager::DeserializeInstance(const FPropSerializedInstance& InSerialized, int32& OutISID)
{
	OutISID = 0;
	if (!ValidateLoadedInstance(InSerialized))
	{
		return false;
	}

	UInteractableISMComponent* Comp = FindIISMComponent(InSerialized.IISMComponentID);
	if (!Comp)
	{
		Comp = GetOrCreateIISMComponent(InSerialized.ProductID);
	}
	if (!Comp) return false;

	FPropPackedRuntimeData RestoredPacked = InSerialized.PackedRuntimeData;
	if (InSerialized.PackedRuntimeBits != 0)
	{
		RestoredPacked.UnpackFromUint64(static_cast<uint64>(InSerialized.PackedRuntimeBits));
	}

	const int32 NewISMI = RepairLoadedISMI(InSerialized.ISID, InSerialized.ProductID, Comp->IISMComponentID, InSerialized.ISMI, InSerialized.ActiveTransform, RestoredPacked);
	if (NewISMI == INDEX_NONE)
	{
		return false;
	}

	FISIDRuntimeIndex Record;
	Record.ISID = InSerialized.ISID;
	Record.ProductID = InSerialized.ProductID;
	Record.IISMComponentID = Comp->IISMComponentID;
	Record.ISMI = NewISMI;
	Record.Version = InSerialized.Version;
	Record.State = InSerialized.State;
	Record.Representation = EPropRepresentation::ISM; // Will promote in RestoreActiveActors if needed
	Record.ActiveTransform = InSerialized.ActiveTransform;
	Record.PackedRuntimeData = RestoredPacked;
	Record.RuntimeData.OpenAmount = InSerialized.OpenAmount;
	Record.RuntimeData.bActivated = InSerialized.bActivated;
	Record.RuntimeData.Damage = InSerialized.Damage;
	Record.RuntimeData.Durability = InSerialized.Durability;
	Record.RuntimeData.RemainingFuel = InSerialized.RemainingFuel;
	Record.RuntimeData.CustomFlags = InSerialized.CustomFlags;
	Record.RuntimeData.RemainingPortions = RestoredPacked.RemainingPortions;
	Record.RuntimeData.Temperature = RestoredPacked.Temperature;

	RegisterISID(Record);
	OutISID = InSerialized.ISID;
	return true;
}

void APropManager::RebuildISMMappings()
{
	for (TPair<int32, TObjectPtr<UInteractableISMComponent>>& Pair : ComponentsByICID)
	{
		if (Pair.Value)
		{
			RebuildISMIMap(Pair.Key);
		}
	}
}

void APropManager::FlushActiveActorState()
{
	if (ASMComponent)
	{
		ASMComponent->FlushActiveActorState();
	}
}

void APropManager::RestoreActiveActors()
{
	// Section 36: Activate ASMs only for props that require active representation
	for (TPair<int32, FISIDRuntimeIndex>& Pair : ISIDData)
	{
		const EPropState PropState = Pair.Value.State;
		if (PropState == EPropState::InMotion ||
			PropState == EPropState::Held ||
			PropState == EPropState::Settling ||
			PropState == EPropState::Unsettled)
		{
			AActivePropActor* OutASM = nullptr;
			PromoteInstanceToASM(Pair.Key, EPropActivationReason::LoadRestore, true, OutASM);
		}
	}
}

// =========================================================================
// Existing Registration & Mesh Query API (Delegated to ASMComponent & IISM)
// =========================================================================
void APropManager::RegisterProp(AActivePropActor* Prop)
{
	if (ASMComponent)
	{
		ASMComponent->RegisterProp(Prop);
	}
}

void APropManager::UnregisterProp(AActivePropActor* Prop)
{
	if (ASMComponent)
	{
		ASMComponent->UnregisterProp(Prop);
	}
}

void APropManager::RegisterISMComponent(UInteractableISMComponent* ISMComp)
{
	if (!ISMComp) return;
	RegisterIISMComponent(ISMComp, ISMComp->IISMComponentID);
}

void APropManager::UnregisterISMComponent(UInteractableISMComponent* ISMComp)
{
	if (!ISMComp) return;
	if (ISMComp->IISMComponentID > 0)
	{
		ComponentsByICID.Remove(ISMComp->IISMComponentID);
	}
	RegisteredISMComponents.Remove(ISMComp);
}

void APropManager::RegisterSettlingComponent(UStaticMeshPhysicsSimulationComponent* SimComp)
{
	if (ASMComponent)
	{
		ASMComponent->RegisterSettlingComponent(SimComp);
	}
}

void APropManager::UnregisterSettlingComponent(UStaticMeshPhysicsSimulationComponent* SimComp)
{
	if (ASMComponent)
	{
		ASMComponent->UnregisterSettlingComponent(SimComp);
	}
}

int32 APropManager::GetActiveSettlingCount() const
{
	return ASMComponent ? ASMComponent->GetActiveSettlingCount() : 0;
}

void APropManager::UpdateDistributedProps(float DeltaSeconds)
{
	if (ASMComponent)
	{
		ASMComponent->UpdateDistributedASMs(DeltaSeconds, AmbientTemperature, PropsUpdatedPerFrame);
	}
}

void APropManager::UpdateDistributedISM(float DeltaSeconds)
{
	RegisteredISMComponents.RemoveAll([](const TWeakObjectPtr<UInteractableISMComponent>& Ptr) { return !Ptr.IsValid(); });

	if (RegisteredISMComponents.Num() == 0) return;

	if (CurrentISMUpdateIndex >= RegisteredISMComponents.Num())
	{
		CurrentISMUpdateIndex = 0;
	}

	if (RegisteredISMComponents.IsValidIndex(CurrentISMUpdateIndex))
	{
		if (UInteractableISMComponent* ISM = RegisteredISMComponents[CurrentISMUpdateIndex].Get())
		{
			const int32 NumInstances = ISM->GetInstanceCount();
			if (NumInstances > 0)
			{
				if (CurrentISMInstanceIndex >= NumInstances)
				{
					CurrentISMInstanceIndex = 0;
				}

				// Section 40: Empty Instances are not gameplay-relevant and must not be updated
				if (!ISM->IsEmptyInstance(CurrentISMInstanceIndex))
				{
					const float EffectiveDelta = DeltaSeconds * static_cast<float>(NumInstances);
					ISM->UpdateInstanceTemperature(CurrentISMInstanceIndex, AmbientTemperature, EffectiveDelta);

					const int32 MappedISID = ISM->FindISID(CurrentISMInstanceIndex);
					if (MappedISID > 0)
					{
						if (FISIDRuntimeIndex* Record = FindISIDData(MappedISID))
						{
							FPropPackedRuntimeData UpdatedPacked;
							if (ISM->GetInstanceDataByISMI(CurrentISMInstanceIndex, UpdatedPacked))
							{
								Record->PackedRuntimeData.Temperature = UpdatedPacked.Temperature;
								Record->RuntimeData.Temperature = UpdatedPacked.Temperature;
							}
						}
					}
				}

				CurrentISMInstanceIndex++;
				if (CurrentISMInstanceIndex >= NumInstances)
				{
					CurrentISMInstanceIndex = 0;
					CurrentISMUpdateIndex = (CurrentISMUpdateIndex + 1) % RegisteredISMComponents.Num();
				}
			}
			else
			{
				CurrentISMUpdateIndex = (CurrentISMUpdateIndex + 1) % RegisteredISMComponents.Num();
			}
		}
		else
		{
			CurrentISMUpdateIndex = (CurrentISMUpdateIndex + 1) % RegisteredISMComponents.Num();
		}
	}
}

void APropManager::CheckSettlingQueue()
{
	if (ASMComponent)
	{
		ASMComponent->CheckSettlingQueue(bUseAsyncSettlingCheck);
	}
}

bool APropManager::CanInteractWithMesh(UPrimitiveComponent* TargetComponent, int32 InstanceIndex, AActor* InstigatorActor, bool& bOutIsGrabbable, FInteractablePropData& OutData)
{
	bOutIsGrabbable = false;
	if (!TargetComponent) return false;

	const int32 FoundISID = GetISID(TargetComponent, InstanceIndex);
	if (FoundISID > 0 && GetPropData(FoundISID, OutData))
	{
		bOutIsGrabbable = GetIsGrabbable(FoundISID);
		OutData.bIsGrabbable = bOutIsGrabbable;
		return true;
	}

	if (UInteractableISMComponent* ISM = Cast<UInteractableISMComponent>(TargetComponent))
	{
		if (ISM->GetPropDataForInstance(InstanceIndex, OutData))
		{
			bOutIsGrabbable = OutData.bIsGrabbable;
			return true;
		}
		return false;
	}

	if (AActivePropActor* Prop = Cast<AActivePropActor>(TargetComponent->GetOwner()))
	{
		if (!Prop->bIsActive) return false;

		OutData.ISID = Prop->ISID;
		OutData.ProductID = Prop->ProductID;
		OutData.ItemDisplayName = Prop->ItemDisplayName;
		OutData.ConsumableType = Prop->ConsumableType;
		OutData.ConsumableTemperature = Prop->ConsumableTemperature;
		OutData.CurrentTemperature = Prop->CurrentTemperature;
		OutData.bEnableTemperatureUpdate = Prop->bEnableTemperatureUpdate;
		OutData.TemperatureDopamineBonus = Prop->TemperatureDopamineBonus;
		OutData.ThermalExchangeRate = Prop->ThermalExchangeRate;
		OutData.Price = Prop->Price;
		OutData.NumOfPortions = Prop->NumOfPortions;
		OutData.RemainingPortions = Prop->RemainingPortions;
		OutData.QuantityLevel = Prop->QuantityLevel;
		OutData.bIsGrabbable = Prop->bIsGrabbable;
		OutData.SimulationState = Prop->PhysicsSimComponent ? Prop->PhysicsSimComponent->CurrentState : EPhysicsSimulationState::AtRest;
		OutData.VelocityTolerance = Prop->PhysicsSimComponent ? Prop->PhysicsSimComponent->VelocityTolerance : 10.0f;
		OutData.AngularVelocityTolerance = Prop->PhysicsSimComponent ? Prop->PhysicsSimComponent->AngularVelocityTolerance : 15.0f;
		OutData.bHasExpiry = Prop->bHasExpiry;
		OutData.ExpiryLifetimeMinutes = Prop->ExpiryLifetimeMinutes;
		OutData.AgeMinutes = Prop->AgeMinutes;
		OutData.SicknessLevel = Prop->SicknessLevel;
		OutData.ExpiredDopaminePenalty = Prop->ExpiredDopaminePenalty;
		OutData.ExpiryStimulusProfile = Prop->ExpiryStimulusProfile;
		OutData.StimuliPerPortion = Prop->StimuliPerPortion;
		OutData.TasteProfile = Prop->TasteProfile;

		bOutIsGrabbable = Prop->bIsGrabbable;
		return true;
	}

	return false;
}

bool APropManager::InteractWithMesh(UPrimitiveComponent* TargetComponent, int32 InstanceIndex, AActor* InstigatorActor, FName ActionName, int32 PortionsToConsume, FInteractablePropData& OutUpdatedData)
{
	if (!TargetComponent || !InstigatorActor) return false;

	const int32 FoundISID = GetISID(TargetComponent, InstanceIndex);
	if (FoundISID > 0)
	{
		if (ActionName == FName("Grab") || ActionName == FName("PickUp"))
		{
			const bool bGrabbed = ExecuteGrab(FoundISID, InstigatorActor, InstigatorActor->GetRootComponent(), NAME_None);
			GetPropData(FoundISID, OutUpdatedData);
			return bGrabbed;
		}
		if (ActionName == FName("Consume") || ActionName == FName("Drink") || ActionName == FName("Eat"))
		{
			const bool bConsumed = ApplyConsume(FoundISID, InstigatorActor, FMath::Max(1, PortionsToConsume));
			GetPropData(FoundISID, OutUpdatedData);
			return bConsumed;
		}
		if (ActionName == FName("Drop") || ActionName == FName("Release"))
		{
			const bool bReleased = DropProp(FoundISID, InstigatorActor);
			GetPropData(FoundISID, OutUpdatedData);
			return bReleased;
		}
	}

	if (UInteractableISMComponent* ISM = Cast<UInteractableISMComponent>(TargetComponent))
	{
		if (ActionName == FName("Consume") || ActionName == FName("Drink") || ActionName == FName("Eat"))
		{
			int32 Consumed = 0;
			const bool bSuccess = ISM->ConsumeInstancePortions(InstanceIndex, PortionsToConsume, InstigatorActor, Consumed);
			if (bSuccess)
			{
				ISM->GetPropDataForInstance(InstanceIndex, OutUpdatedData);
			}
			return bSuccess;
		}

		if (ActionName == FName("Grab") || ActionName == FName("PickUp"))
		{
			ISM->SetInstanceGrabbable(InstanceIndex, false);
			ISM->GetPropDataForInstance(InstanceIndex, OutUpdatedData);
			return true;
		}

		return ISM->GetPropDataForInstance(InstanceIndex, OutUpdatedData);
	}

	if (AActivePropActor* Prop = Cast<AActivePropActor>(TargetComponent->GetOwner()))
	{
		const float Ratio = (Prop->NumOfPortions > 0) ? static_cast<float>(PortionsToConsume) / static_cast<float>(Prop->NumOfPortions) : 1.0f;
		const bool bSuccess = Prop->ConsumePortion(InstigatorActor, Ratio);
		CanInteractWithMesh(TargetComponent, InstanceIndex, InstigatorActor, OutUpdatedData.bIsGrabbable, OutUpdatedData);
		return bSuccess;
	}

	return false;
}

void APropManager::ProcessActivePhysicsReplication(float DeltaSeconds)
{
	if (!HasAuthority()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	// Only stream if there are remote clients listening
	bool bHasRemoteClients = false;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (!PC->IsLocalController())
			{
				bHasRemoteClients = true;
				break;
			}
		}
	}
	if (!bHasRemoteClients) return;

	// Gather active ASMs that are currently in motion / unsettled / settling
	TArray<AActivePropActor*> SimulatingActors;
	SimulatingActors.Reserve(ActiveActorByISID.Num());

	for (const TPair<int32, TObjectPtr<AActivePropActor>>& Pair : ActiveActorByISID)
	{
		AActivePropActor* Actor = Pair.Value;
		if (IsValid(Actor) && Actor->bIsActive && Actor->ISID > 0)
		{
			if (Actor->State == EPropState::InMotion || Actor->State == EPropState::Unsettled || Actor->State == EPropState::Settling)
			{
				if (Actor->PropMesh && Actor->PropMesh->IsSimulatingPhysics())
				{
					SimulatingActors.Add(Actor);
				}
			}
		}
	}

	const int32 TotalSimulating = SimulatingActors.Num();
	if (TotalSimulating == 0)
	{
		NextPhysicsSyncIndex = 0;
		return;
	}

	const int32 Budget = FMath::Clamp(PhysicsSyncPropsPerFrame, 1, 30);
	const int32 CountToSend = FMath::Min(Budget, TotalSimulating);

	TArray<FActivePropPhysicsSync> Batch;
	Batch.Reserve(CountToSend);

	for (int32 i = 0; i < CountToSend; ++i)
	{
		const int32 CurrentIndex = (NextPhysicsSyncIndex + i) % TotalSimulating;
		AActivePropActor* TargetActor = SimulatingActors[CurrentIndex];
		if (!IsValid(TargetActor) || !TargetActor->PropMesh) continue;

		FActivePropPhysicsSync SyncItem;
		SyncItem.ISID = TargetActor->ISID;
		SyncItem.ASMID = TargetActor->ASMID;
		SyncItem.Location = TargetActor->PropMesh->GetComponentLocation();
		SyncItem.Rotation = TargetActor->PropMesh->GetComponentRotation();
		SyncItem.LinearVelocity = TargetActor->PropMesh->GetComponentVelocity();
		SyncItem.AngularVelocity = TargetActor->PropMesh->GetPhysicsAngularVelocityInDegrees();
		SyncItem.State = TargetActor->State;

		Batch.Add(SyncItem);
	}

	NextPhysicsSyncIndex = (NextPhysicsSyncIndex + CountToSend) % TotalSimulating;

	if (Batch.Num() > 0)
	{
		Multicast_SyncActivePhysicsBatch(Batch);
	}
}

void APropManager::Multicast_SyncActivePhysicsBatch_Implementation(const TArray<FActivePropPhysicsSync>& Batch)
{
	// Clients only
	if (HasAuthority()) return;

	for (const FActivePropPhysicsSync& Item : Batch)
	{
		AActivePropActor* ASM = GetActiveActorFromISID(Item.ISID);
		if (!ASM && Item.ASMID > 0)
		{
			ASM = GetActiveActorFromASMID(Item.ASMID);
		}

		if (ASM && ASM->bIsActive)
		{
			ASM->ApplyAuthoritativePhysicsSync(Item);
		}
	}
}

void APropManager::ProcessTimestampedThermalUpdates(float DeltaSeconds)
{
	if (!HasAuthority() || AllActiveISIDs.Num() == 0) return;

	const int32 CurrentMinutes = GetCurrentWorldMinutes();
	const int32 TotalISIDs = AllActiveISIDs.Num();
	const int32 CountToProcess = FMath::Clamp(ThermalUpdatesPerFrame, 5, 50);

	TArray<FPropRuntimeSyncItem> SyncBatch;

	for (int32 i = 0; i < CountToProcess; ++i)
	{
		if (NextThermalUpdateIndex >= TotalISIDs)
		{
			NextThermalUpdateIndex = 0;
		}

		const int32 CheckISID = AllActiveISIDs[NextThermalUpdateIndex++];
		FISIDRuntimeIndex* Record = FindISIDData(CheckISID);
		if (!Record) continue;

		bool bEnableThermal = false;
		float ProductThermalExchangeRate = 0.015f;
		if (const FPropProductDefinition* Def = ProductDefinitions.Find(Record->ProductID))
		{
			bEnableThermal = Def->TemperatureProfile.bEnableTemperatureUpdate;
			ProductThermalExchangeRate = Def->TemperatureProfile.ThermalExchangeRate;
		}

		if (!bEnableThermal) continue;

		const int32 ElapsedMinutes = CurrentMinutes - Record->PackedRuntimeData.LastUpdatedMinute;
		if (ElapsedMinutes >= 1)
		{
			const float CurrentTemp = Record->PackedRuntimeData.Temperature;
			if (FMath::Abs(CurrentTemp - AmbientTemperature) > 0.5f)
			{
				const float Alpha = FMath::Clamp(ProductThermalExchangeRate * static_cast<float>(ElapsedMinutes), 0.0f, 1.0f);
				const float NewTemp = FMath::Lerp(CurrentTemp, AmbientTemperature, Alpha);

				Record->PackedRuntimeData.Temperature = NewTemp;
				Record->PackedRuntimeData.LastUpdatedMinute = CurrentMinutes;

				if (UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
				{
					Comp->SetInstanceData(CheckISID, Record->PackedRuntimeData);
				}

				FPropRuntimeSyncItem Item;
				Item.ISID = CheckISID;
				Item.Temperature = NewTemp;
				Item.RemainingPortions = Record->PackedRuntimeData.RemainingPortions;
				Item.LastUpdatedMinute = CurrentMinutes;
				SyncBatch.Add(Item);
			}
			else
			{
				Record->PackedRuntimeData.LastUpdatedMinute = CurrentMinutes;
			}
		}
	}

	if (SyncBatch.Num() > 0)
	{
		Multicast_SyncRuntimeDataBatch(SyncBatch);
	}
}

void APropManager::Multicast_SyncRuntimeDataBatch_Implementation(const TArray<FPropRuntimeSyncItem>& Batch)
{
	if (HasAuthority()) return;

	for (const FPropRuntimeSyncItem& Item : Batch)
	{
		if (FISIDRuntimeIndex* Record = FindISIDData(Item.ISID))
		{
			Record->PackedRuntimeData.Temperature = Item.Temperature;
			Record->PackedRuntimeData.RemainingPortions = Item.RemainingPortions;
			Record->PackedRuntimeData.LastUpdatedMinute = Item.LastUpdatedMinute;

			if (UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
			{
				Comp->SetInstanceData(Item.ISID, Record->PackedRuntimeData);
			}
		}
	}
}

bool APropManager::Server_RequestTemperatureUpdate_Validate(int32 InISID, float NewTemperature)
{
	return InISID > 0;
}

void APropManager::Server_RequestTemperatureUpdate_Implementation(int32 InISID, float NewTemperature)
{
	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return;

	Record->PackedRuntimeData.Temperature = NewTemperature;
	Record->PackedRuntimeData.LastUpdatedMinute = GetCurrentWorldMinutes();

	if (UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
	{
		Comp->SetInstanceData(InISID, Record->PackedRuntimeData);
	}

	TArray<FPropRuntimeSyncItem> SingleItemBatch;
	FPropRuntimeSyncItem Item;
	Item.ISID = InISID;
	Item.Temperature = NewTemperature;
	Item.RemainingPortions = Record->PackedRuntimeData.RemainingPortions;
	Item.LastUpdatedMinute = Record->PackedRuntimeData.LastUpdatedMinute;
	SingleItemBatch.Add(Item);

	Multicast_SyncRuntimeDataBatch(SingleItemBatch);
}

bool APropManager::GetPackedRuntimeData(int32 InISID, FPropPackedRuntimeData& OutData) const
{
	if (const FISIDRuntimeIndex* Record = FindISIDData(InISID))
	{
		OutData = Record->PackedRuntimeData;
		return true;
	}
	return false;
}

bool APropManager::ConsumeInstancePortion(int32 InISID, AActor* ConsumerActor, float PortionRatio)
{
	if (InISID <= 0) return false;

	FISIDRuntimeIndex* Record = FindISIDData(InISID);
	if (!Record) return false;

	if (Record->PackedRuntimeData.RemainingPortions <= 0)
	{
		return false;
	}

	const int32 PortionsToConsume = FMath::Max(1, FMath::RoundToInt(PortionRatio));
	Record->PackedRuntimeData.RemainingPortions = FMath::Max(0, Record->PackedRuntimeData.RemainingPortions - PortionsToConsume);
	Record->PackedRuntimeData.LastUpdatedMinute = GetCurrentWorldMinutes();

	if (UInteractableISMComponent* Comp = FindIISMComponent(Record->IISMComponentID))
	{
		Comp->SetInstanceData(InISID, Record->PackedRuntimeData);
	}

	if (HasAuthority())
	{
		TArray<FPropRuntimeSyncItem> SingleItemBatch;
		FPropRuntimeSyncItem Item;
		Item.ISID = InISID;
		Item.Temperature = Record->PackedRuntimeData.Temperature;
		Item.RemainingPortions = Record->PackedRuntimeData.RemainingPortions;
		Item.LastUpdatedMinute = Record->PackedRuntimeData.LastUpdatedMinute;
		SingleItemBatch.Add(Item);

		Multicast_SyncRuntimeDataBatch(SingleItemBatch);
	}

	return true;
}
