#include "Props/FragmentDestructionActor.h"
#include "Props/InteractablePropManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"

AFragmentDestructionActor::AFragmentDestructionActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = false; // Manager deltas carry identities/transitions; bounded pose batches carry motion.
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AFragmentDestructionActor::BeginGroup(APropManager* Manager, FGuid InGroupID)
{
	ResetForPool();
	PropManager = Manager; GroupID = InGroupID;
	SetActorTransform(FTransform::Identity);
	SetActorHiddenInGame(false); SetActorEnableCollision(true);
	SetActorTickEnabled(GetNetMode() == NM_Client);
}

bool AFragmentDestructionActor::PreparePiece(int32 Index, const FPropProductDefinition& Product, const FTransform& Transform)
{
	if (Index < 0 || Index >= 64 || !Product.StaticMesh || Transform.ContainsNaN()) return false;
	while (Meshes.Num() <= Index)
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
		Mesh->SetupAttachment(GetRootComponent()); Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetGenerateOverlapEvents(false);
		Mesh->RegisterComponent(); Meshes.Add(Mesh);
		UStaticMeshPhysicsSimulationComponent* Probe = NewObject<UStaticMeshPhysicsSimulationComponent>(this);
		Probe->TargetMesh = Mesh; Probe->bUnsettleOnHit = false; Probe->SetIsReplicated(false);
		Probe->RegisterComponent(); Probe->SetComponentTickEnabled(false); SettlementProbes.Add(Probe);
	}
	UStaticMeshComponent* Mesh = Meshes[Index];
	Mesh->SetSimulatePhysics(false); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->EmptyOverrideMaterials(); Mesh->SetStaticMesh(Product.StaticMesh);
	for (int32 Material = 0; Material < Product.MaterialOverrides.Num(); ++Material) Mesh->SetMaterial(Material, Product.MaterialOverrides[Material]);
	Mesh->SetWorldTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	Mesh->SetLinearDamping(Product.PhysicsProfile.LinearDamping); Mesh->SetAngularDamping(Product.PhysicsProfile.AngularDamping);
	Mesh->SetEnableGravity(Product.PhysicsProfile.bEnableGravity);
	Mesh->SetVisibility(true); Mesh->SetHiddenInGame(false);
	SettlementProbes[Index]->VelocityTolerance = Product.PhysicsProfile.LinearVelocityTolerance;
	SettlementProbes[Index]->AngularVelocityTolerance = Product.PhysicsProfile.AngularVelocityTolerance;
	SettlementProbes[Index]->DefaultUnsettledDelay = Product.PhysicsProfile.UnsettledDelay;
	return true;
}

void AFragmentDestructionActor::ActivatePiece(int32 Index, int32 ISID, const FVector& Velocity, const FVector& AngularVelocity)
{
	if (!Meshes.IsValidIndex(Index)) return;
	PieceIDs.Add(ISID, Index); Elapsed.Add(ISID, 0); Stationary.Add(ISID, 0);
	UStaticMeshComponent* Mesh = Meshes[Index];
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetCollisionEnabled(GetNetMode() == NM_Client ? ECollisionEnabled::QueryOnly : ECollisionEnabled::QueryAndPhysics);
	if (GetNetMode() != NM_Client)
	{
		Mesh->SetSimulatePhysics(true);
		Mesh->SetPhysicsLinearVelocity(Velocity); Mesh->SetPhysicsAngularVelocityInDegrees(AngularVelocity);
	}
}

UStaticMeshComponent* AFragmentDestructionActor::FindPiece(int32 ISID) const
{
	const int32* Index = PieceIDs.Find(ISID);
	return Index && Meshes.IsValidIndex(*Index) ? Meshes[*Index].Get() : nullptr;
}

int32 AFragmentDestructionActor::FindISID(const UPrimitiveComponent* Component) const
{
	for (const auto& Piece : PieceIDs) if (Meshes[Piece.Value] == Component) return Piece.Key;
	return 0;
}

bool AFragmentDestructionActor::IsPieceSettled(int32 ISID) const
{
	const int32* Index = PieceIDs.Find(ISID);
	return Index && SettlementProbes[*Index]->CheckSettled(SettlementProbes[*Index]->VelocityTolerance, SettlementProbes[*Index]->AngularVelocityTolerance);
}

void AFragmentDestructionActor::CheckSettlement(float DeltaSeconds)
{
	if (!PropManager || !PropManager->HasAuthority() || GetNetMode() == NM_Client) return;
	TArray<int32> Settled;
	for (const auto& Piece : PieceIDs)
	{
		float& Age = Elapsed.FindChecked(Piece.Key); Age += DeltaSeconds;
		float& RestTime = Stationary.FindChecked(Piece.Key);
		RestTime = IsPieceSettled(Piece.Key) ? RestTime + DeltaSeconds : 0;
		if (Age >= SettlementProbes[Piece.Value]->DefaultUnsettledDelay && RestTime >= .2f) Settled.Add(Piece.Key);
	}
	for (int32 ISID : Settled) PropManager->SettleOriginalFragment(ISID);
}

void AFragmentDestructionActor::ReleasePiece(int32 ISID)
{
	int32 Index;
	if (!PieceIDs.RemoveAndCopyValue(ISID, Index)) return;
	UStaticMeshComponent* Mesh = Meshes[Index];
	Mesh->SetSimulatePhysics(false); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetHiddenInGame(true); Mesh->SetVisibility(false);
	Mesh->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	Mesh->SetWorldTransform(PropManager ? PropManager->GetResetTransformForSlot(Index) : FTransform(FVector(0, 0, -100000)), false, nullptr, ETeleportType::TeleportPhysics);
	Elapsed.Remove(ISID); Stationary.Remove(ISID); PoseTargets.Remove(ISID);
	if (PieceIDs.IsEmpty()) ResetForPool();
}

void AFragmentDestructionActor::ResetForPool()
{
	PieceIDs.Empty(); PoseTargets.Empty(); Elapsed.Empty(); Stationary.Empty();
	GroupID.Invalidate(); SetActorTickEnabled(false); SetActorEnableCollision(false); SetActorHiddenInGame(true);
	SetActorTransform(PropManager ? PropManager->DefaultResetTransform : FTransform(FVector(0, 0, -100000)));
	for (int32 I = 0; I < Meshes.Num(); ++I)
	{
		UStaticMeshComponent* Mesh = Meshes[I];
		Mesh->SetSimulatePhysics(false); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetHiddenInGame(true); Mesh->SetVisibility(false);
		Mesh->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
		Mesh->SetWorldTransform(PropManager ? PropManager->GetResetTransformForSlot(I) : FTransform(FVector(0, 0, -100000)), false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void AFragmentDestructionActor::SetPoseTarget(int32 ISID, const FTransform& Transform)
{
	if (FindPiece(ISID)) PoseTargets.Add(ISID, Transform);
}

void AFragmentDestructionActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	for (const auto& Target : PoseTargets) if (UStaticMeshComponent* Mesh = FindPiece(Target.Key))
	{
		FTransform Transform = Target.Value;
		Transform.SetLocation(FMath::VInterpTo(Mesh->GetComponentLocation(), Target.Value.GetLocation(), DeltaSeconds, 20));
		Transform.SetRotation(FMath::QInterpTo(Mesh->GetComponentQuat(), Target.Value.GetRotation(), DeltaSeconds, 20));
		Mesh->SetWorldTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	}
}
