#include "Tests/FragmentNetworkTestActor.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/ColaPropActor.h"
#include "Props/FragmentDestructionActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

AFragmentNetworkTestActor::AFragmentNetworkTestActor()
{
	bReplicates = true; bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.TickInterval = .05f;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AFragmentNetworkTestActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFragmentNetworkTestActor, Stage); DOREPLIFETIME(AFragmentNetworkTestActor, SourceISID);
	DOREPLIFETIME(AFragmentNetworkTestActor, FragmentISIDs); DOREPLIFETIME(AFragmentNetworkTestActor, PreviousFragmentISIDs);
}

void AFragmentNetworkTestActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if WITH_EDITOR
	if (!Manager) Manager = APropManager::Get(this);
	if (!Manager) return;
	if (HasAuthority() && Command == 1)
	{
		Command = 0;
		UInteractablePropDataAsset* Product = LoadObject<UInteractablePropDataAsset>(nullptr, TEXT("/Game/Assest/DataAssets/DA_Cola.DA_Cola"));
		const int32 PID = Manager->RegisterProductFromDataAsset(Product);
		const FTransform Transform(GetActorRotation(), GetActorLocation() + FVector(0, 0, 100));
		SourceISID = Manager->GenerateProductInstance(PID, Transform);
		TSet<int32> Before; for (const auto& Pair : Manager->ISIDData) Before.Add(Pair.Key);
		AActivePropActor* Active = nullptr;
		if (!Manager->PromoteInstanceToASM(SourceISID, EPropActivationReason::Interaction, true, Active)) { LastFailure = TEXT("Source promotion failed"); return; }
		AColaPropActor* Bottle = Cast<AColaPropActor>(Active);
		FContainmentFailureRequest Failure; Failure.FailureType = EContainmentFailureType::BottleBreakage;
		if (!Bottle || !Bottle->SubmitFailureRequest(Failure)) { LastFailure = TEXT("Source fracture failed"); return; }
		PreviousFragmentISIDs = FragmentISIDs; FragmentISIDs.Reset();
		for (const auto& Pair : Manager->ISIDData) if (!Before.Contains(Pair.Key) && Pair.Value.Representation == EPropRepresentation::CompositeFragment) FragmentISIDs.Add(Pair.Key);
		FragmentISIDs.Sort(); Stage = 1; ForceNetUpdate();
	}
	if (HasAuthority() && Command == 3 && FragmentISIDs.Num() == 11)
	{
		Command = 0; AActivePropActor* Active = nullptr;
		if (!Manager->PromoteInstanceToASM(FragmentISIDs[0], EPropActivationReason::Interaction, true, Active) || !Active || !Active->IsA<AFragmentPropActor>())
		{ LastFailure = TEXT("Dedicated promotion failed"); return; }
		Active->SetInMotion(true); Stage = 3; ForceNetUpdate();
	}
	if (HasAuthority() && Command == 4 && !FragmentISIDs.IsEmpty())
	{
		Command = 0;
		if (AActivePropActor* Active = Manager->GetActiveActorFromISID(FragmentISIDs[0]))
		{
			Active->SetInMotion(false);
			if (Active->PhysicsSimComponent) Active->PhysicsSimComponent->SetStateUnsettled(.1f);
		}
		Stage = 4; ForceNetUpdate();
	}
	LocalRecordCount = LocalOriginalCount = LocalISMCount = LocalASMCount = LocalPreviousRestCount = 0;
	LocalVisibleOriginalCount = LocalSimulatingOriginalCount = LocalVisibleISMCount = LocalDedicatedActorCount = 0;
	LocalFragmentPositions.Reset();
	for (int32 ID : FragmentISIDs) if (const auto* Record = Manager->FindISIDData(ID))
	{
		++LocalRecordCount;
		if (const int32* Initial = InitialSlots.Find(ID)) { if (*Initial != Record->ISMI) ++LocalSlotChanges; }
		else InitialSlots.Add(ID, Record->ISMI);
		if (Record->Representation == EPropRepresentation::CompositeFragment)
		{
			++LocalOriginalCount;
			if (UStaticMeshComponent* Mesh = Manager->GetOriginalFragmentComponent(ID))
			{
				if (Mesh->IsVisible() && !Mesh->bHiddenInGame && Mesh->GetComponentScale().GetMin() > .005f) ++LocalVisibleOriginalCount;
				if (Mesh->IsSimulatingPhysics()) ++LocalSimulatingOriginalCount;
			}
		}
		else if (Record->Representation == EPropRepresentation::ISM)
		{
			++LocalISMCount;
			FTransform Transform;
			if (auto* Component = Manager->GetComponentFromISID(ID))
				if (Component->GetInstanceTransform(Record->ISMI, Transform, true) && Component->IsVisible() && Transform.Equals(Record->ActiveTransform, .1f) && Transform.GetScale3D().GetMin() > .005f) ++LocalVisibleISMCount;
		}
		else if (Record->Representation == EPropRepresentation::ASM)
		{
			++LocalASMCount;
			if (AActivePropActor* Active = Manager->GetActiveActorFromISID(ID))
				if (Active->IsA<AFragmentPropActor>() && Active->bIsActive && !Active->IsHidden() && Active->PropMesh && Active->PropMesh->IsVisible()) ++LocalDedicatedActorCount;
		}
		LocalFragmentPositions.Add(Manager->GetTransform(ID).GetLocation());
	}
	for (int32 ID : PreviousFragmentISIDs) if (const auto* Record = Manager->FindISIDData(ID)) if (Record->Representation == EPropRepresentation::ISM) ++LocalPreviousRestCount;
	bLocalSourcePresent = SourceISID > 0 && Manager->FindISIDData(SourceISID) != nullptr;
	LocalReleasedLiquidCount = Manager->ReleasedLiquids.Num(); LocalReleasedPortions = 0;
	for (const auto& Release : Manager->ReleasedLiquids) LocalReleasedPortions += Release.Portions;
	LocalPlayerCount = GetWorld()->GetGameState() ? GetWorld()->GetGameState()->PlayerArray.Num() : 0;
	if (HasAuthority() && Stage == 1 && LocalISMCount == 11) { Stage = 2; ForceNetUpdate(); }
#endif
}
