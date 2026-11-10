#include "Props/InteractablePropManager.h"
#include "Props/FragmentDestructionActor.h"
#include "Props/FragmentAssemblyDataAsset.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodySetup.h"

AFragmentDestructionActor* APropManager::AcquireFragmentActor(FGuid GroupID)
{
	if (!GroupID.IsValid()) return nullptr;
	if (const auto* Existing = ActiveFragmentGroups.Find(GroupID)) return Existing->Get();
	AFragmentDestructionActor* Actor = nullptr;
	for (AFragmentDestructionActor* Candidate : FragmentActorPool)
		if (IsValid(Candidate) && !Candidate->GetGroupID().IsValid()) { Actor = Candidate; break; }
	if (!Actor)
	{
		for (int32 I = 0; I < FMath::Clamp(FragmentPoolGrowthBatchSize, 1, 16); ++I)
		{
			FActorSpawnParameters Params; Params.Owner = this; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AFragmentDestructionActor* Created = GetWorld()->SpawnActor<AFragmentDestructionActor>(AFragmentDestructionActor::StaticClass(), DefaultResetTransform, Params);
			if (!Created) break;
			Created->ResetForPool(); FragmentActorPool.Add(Created);
			if (!Actor) Actor = Created;
		}
	}
	if (Actor) { Actor->BeginGroup(this, GroupID); ActiveFragmentGroups.Add(GroupID, Actor); }
	return Actor;
}

bool APropManager::SpawnFragmentAssembly(UFragmentAssemblyDataAsset* Assembly, const FTransform& Transform, FVector Velocity, FVector AngularVelocity, TArray<int32>& OutISIDs)
{
	OutISIDs.Reset();
	TArray<FPropFractureFragmentEntry> Recipe;
	return Assembly && Assembly->BuildRecipe(this, Recipe) && CreateFragmentDestruction(Recipe, Transform, Velocity, AngularVelocity, OutISIDs);
}

bool APropManager::CreateFragmentDestruction(const TArray<FPropFractureFragmentEntry>& Recipe, const FTransform& Transform, const FVector& Velocity, const FVector& AngularVelocity, TArray<int32>& OutISIDs)
{
	OutISIDs.Reset();
	if (!HasAuthority() || GetNetMode() == NM_Client || Recipe.IsEmpty() || Recipe.Num() > 64 || Transform.ContainsNaN()
		|| Transform.GetScale3D().GetMin() <= 0 || Velocity.ContainsNaN() || AngularVelocity.ContainsNaN()) return false;
	for (const auto& Piece : Recipe)
	{
		const auto* Def = FindProductDefinition(Piece.FragmentProductID);
		const UBodySetup* Body = Def && Def->StaticMesh ? Def->StaticMesh->GetBodySetup() : nullptr;
		if (!Body || Body->AggGeom.GetElementCount() == 0 || Body->CollisionTraceFlag == CTF_UseComplexAsSimple
			|| !Def->CustomActiveActorClass || !Def->CustomActiveActorClass->IsChildOf(AFragmentPropActor::StaticClass())
			|| Piece.LocalSpawnTransform.ContainsNaN() || Piece.LocalSpawnTransform.GetScale3D().GetMin() <= 0 || Piece.LocalScatterDirection.ContainsNaN()
			|| !FMath::IsFinite(Piece.MinImpulseSpeed) || !FMath::IsFinite(Piece.MaxImpulseSpeed) || Piece.MinImpulseSpeed < 0 || Piece.MaxImpulseSpeed < Piece.MinImpulseSpeed
			|| !FMath::IsFinite(Piece.VelocityInheritance) || Piece.VelocityInheritance < 0 || Piece.VelocityInheritance > 1) return false;
	}
	EnsureLevelPropsInitialized();
	const FGuid GroupID = FGuid::NewGuid();
	AFragmentDestructionActor* Actor = AcquireFragmentActor(GroupID);
	if (!Actor) return false;
	// Prepare every original component and permanent slot before enabling any physics.
	TArray<int32> IDs;
	bool bPrepared = true;
	for (int32 I = 0; I < Recipe.Num(); ++I)
	{
		const FTransform WorldTransform = Recipe[I].LocalSpawnTransform * Transform;
		if (!Actor->PreparePiece(I, *FindProductDefinition(Recipe[I].FragmentProductID), WorldTransform)) { bPrepared = false; break; }
		const int32 ISID = GenerateProductInstance(Recipe[I].FragmentProductID, WorldTransform);
		if (ISID <= 0) { bPrepared = false; break; }
		IDs.Add(ISID);
		UInteractableISMComponent* Component = GetComponentFromISID(ISID);
		if (!Component || !Component->MoveInstanceToResetTransform(ISID)) { bPrepared = false; break; }
	}
	if (!bPrepared)
	{
		for (int32 ISID : IDs) ReleaseInstanceToEmpty(ISID);
		Actor->ResetForPool(); ActiveFragmentGroups.Remove(GroupID); return false;
	}
	for (int32 I = 0; I < IDs.Num(); ++I)
	{
		const auto& Piece = Recipe[I];
		FISIDRuntimeIndex* Record = FindISIDData(IDs[I]);
		Record->Representation = EPropRepresentation::CompositeFragment; Record->State = EPropState::Unsettled;
		Record->FragmentGroupID = GroupID; Record->FragmentPieceIndex = I; Record->FragmentPoseTime = WorldElapsedSeconds;
		Record->ASMID = 0; Record->ActiveActor = nullptr;
		++Record->Version;
		const FVector Center = Record->ActiveTransform.TransformPosition(FindProductDefinition(Record->ProductID)->StaticMesh->GetBounds().Origin);
		const FVector Offset = Center - Transform.GetLocation();
		const FVector Direction = Transform.TransformVectorNoScale(Piece.LocalScatterDirection).GetSafeNormal();
		const float Speed = FMath::FRandRange(Piece.MinImpulseSpeed, Piece.MaxImpulseSpeed);
		const FVector AngularRadians = FMath::DegreesToRadians(1.0f) * AngularVelocity;
		Actor->ActivatePiece(I, IDs[I], Piece.VelocityInheritance * (Velocity + FVector::CrossProduct(AngularRadians, Offset)) + Direction * Speed, AngularVelocity * Piece.VelocityInheritance);
		OriginalFragmentComponents.Add(IDs[I], Actor->FindPiece(IDs[I]));
		MarkInstanceDirty(IDs[I], EPropDirtyFlags::All);
		OnPropRepresentationChanged.Broadcast(IDs[I], EPropRepresentation::CompositeFragment, nullptr);
	}
	OutISIDs = MoveTemp(IDs);
	return true;
}

UStaticMeshComponent* APropManager::GetOriginalFragmentComponent(int32 ISID) const
{
	const auto* Component = OriginalFragmentComponents.Find(ISID);
	return Component ? Component->Get() : nullptr;
}

void APropManager::ReleaseOriginalFragment(int32 ISID)
{
	UStaticMeshComponent* Component = GetOriginalFragmentComponent(ISID);
	OriginalFragmentComponents.Remove(ISID);
	if (Component) if (AFragmentDestructionActor* Actor = Cast<AFragmentDestructionActor>(Component->GetOwner()))
	{
		const FGuid GroupID = Actor->GetGroupID();
		Actor->ReleasePiece(ISID);
		if (Actor->GetActivePieceCount() == 0) ActiveFragmentGroups.Remove(GroupID);
	}
}

bool APropManager::SettleOriginalFragment(int32 ISID)
{
	if (!HasAuthority() || GetNetMode() == NM_Client) return false;
	FISIDRuntimeIndex* Record = FindISIDData(ISID);
	UStaticMeshComponent* Mesh = GetOriginalFragmentComponent(ISID);
	AFragmentDestructionActor* Actor = Mesh ? Cast<AFragmentDestructionActor>(Mesh->GetOwner()) : nullptr;
	UInteractableISMComponent* Component = GetComponentFromISID(ISID);
	if (!Record || Record->Representation != EPropRepresentation::CompositeFragment || !Actor || !Actor->IsPieceSettled(ISID)
		|| !Component || Component->FindISMI(ISID) != Record->ISMI || Mesh->GetComponentTransform().ContainsNaN()) return false;
	const FTransform FinalTransform = Mesh->GetComponentTransform();
	if (!Component->RestoreInstanceToGameplayTransform(ISID, FinalTransform)) return false;
	Record->ActiveTransform = FinalTransform; Record->Representation = EPropRepresentation::ISM; Record->State = EPropState::AtRest;
	Record->FragmentGroupID.Invalidate(); Record->FragmentPieceIndex = INDEX_NONE; ++Record->Version;
	ReleaseOriginalFragment(ISID);
	MarkInstanceDirty(ISID, EPropDirtyFlags::All);
	OnPropRepresentationChanged.Broadcast(ISID, EPropRepresentation::ISM, nullptr);
	return true;
}

void APropManager::RestoreOriginalFragmentRepresentation(int32 ISID)
{
	FISIDRuntimeIndex* Record = FindISIDData(ISID);
	if (!Record || Record->Representation != EPropRepresentation::CompositeFragment || !Record->FragmentGroupID.IsValid()) return;
	const auto* Def = FindProductDefinition(Record->ProductID);
	if (!Def) return;
	if (UStaticMeshComponent* Mesh = GetOriginalFragmentComponent(ISID))
	{
		if (auto* Actor = Cast<AFragmentDestructionActor>(Mesh->GetOwner()))
			if (Actor->GetGroupID() == Record->FragmentGroupID) { Actor->SetPoseTarget(ISID, Record->ActiveTransform); return; }
		ReleaseOriginalFragment(ISID);
	}
	AFragmentDestructionActor* Actor = AcquireFragmentActor(Record->FragmentGroupID);
	if (!Actor || !Actor->PreparePiece(Record->FragmentPieceIndex, *Def, Record->ActiveTransform)) return;
	Actor->ActivatePiece(Record->FragmentPieceIndex, ISID, FVector::ZeroVector, FVector::ZeroVector);
	OriginalFragmentComponents.Add(ISID, Actor->FindPiece(ISID));
}

void APropManager::ResetFragmentRuntime(bool bDestroyActors)
{
	OriginalFragmentComponents.Empty(); ActiveFragmentGroups.Empty();
	for (AFragmentDestructionActor* Actor : FragmentActorPool) if (IsValid(Actor))
	{
		Actor->ResetForPool(); if (bDestroyActors) Actor->Destroy();
	}
	if (bDestroyActors) FragmentActorPool.Empty();
	FragmentPhysicsAccumulator = 0; FragmentSyncCursor = 0;
}

void APropManager::ProcessFragmentPhysics(float DeltaSeconds)
{
	if (OriginalFragmentComponents.IsEmpty()) { FragmentPhysicsAccumulator = 0; return; }
	FragmentPhysicsAccumulator += DeltaSeconds;
	if (FragmentPhysicsAccumulator < .05f) return;
	const float Elapsed = FragmentPhysicsAccumulator; FragmentPhysicsAccumulator = 0;
	TArray<TObjectPtr<AFragmentDestructionActor>> Actors; ActiveFragmentGroups.GenerateValueArray(Actors);
	for (AFragmentDestructionActor* Actor : Actors) if (IsValid(Actor)) Actor->CheckSettlement(Elapsed);
	TArray<int32> IDs; OriginalFragmentComponents.GetKeys(IDs); IDs.Sort();
	if (IDs.IsEmpty()) return;
	TArray<FFragmentPhysicsSync> Batch;
	const int32 Count = FMath::Min(IDs.Num(), FMath::Clamp(PhysicsSyncPropsPerFrame, 1, 30));
	for (int32 I = 0; I < Count; ++I)
	{
		FragmentSyncCursor %= IDs.Num(); const int32 ISID = IDs[FragmentSyncCursor++];
		FISIDRuntimeIndex* Record = FindISIDData(ISID); UStaticMeshComponent* Mesh = GetOriginalFragmentComponent(ISID);
		if (!Record || !Mesh) continue;
		Record->ActiveTransform = Mesh->GetComponentTransform(); Record->FragmentPoseTime = WorldElapsedSeconds;
		FFragmentPhysicsSync Update; Update.ISID = ISID; Update.Version = Record->Version; Update.GroupID = Record->FragmentGroupID;
		Update.WorldTime = WorldElapsedSeconds; Update.Transform = Record->ActiveTransform; Batch.Add(Update);
	}
	if (!Batch.IsEmpty()) Multicast_FragmentPhysics(Batch);
}

void APropManager::ApplyFragmentPhysics(const FFragmentPhysicsSync& Update)
{
	FISIDRuntimeIndex* Record = FindISIDData(Update.ISID);
	if (!Record || Record->Representation != EPropRepresentation::CompositeFragment || Record->Version != Update.Version
		|| Record->FragmentGroupID != Update.GroupID || !FMath::IsFinite(Update.WorldTime) || Update.WorldTime <= Record->FragmentPoseTime || Update.Transform.ContainsNaN()) return;
	Record->FragmentPoseTime = Update.WorldTime; Record->ActiveTransform = Update.Transform;
	if (UStaticMeshComponent* Mesh = GetOriginalFragmentComponent(Update.ISID))
		if (auto* Actor = Cast<AFragmentDestructionActor>(Mesh->GetOwner())) Actor->SetPoseTarget(Update.ISID, Update.Transform);
}

void APropManager::Multicast_FragmentPhysics_Implementation(const TArray<FFragmentPhysicsSync>& Batch)
{
	if (HasAuthority() || Batch.Num() > 30) return;
	for (const auto& Update : Batch) ApplyFragmentPhysics(Update);
}
