#include "Props/FragmentAssemblyDataAsset.h"
#include "Props/FragmentDestructionActor.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractablePropManager.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"

bool UFragmentAssemblyDataAsset::BuildRecipe(APropManager* Manager, TArray<FPropFractureFragmentEntry>& Recipe) const
{
	Recipe.Reset();
	if (!Manager || !Manager->HasAuthority() || Manager->GetNetMode() == NM_Client || Pieces.IsEmpty() || Pieces.Num() > 64) return false;
	// Validate the complete assembly before registering any product.
	for (const auto& Piece : Pieces)
	{
		const UBodySetup* Body = Piece.Product && Piece.Product->StaticMesh ? Piece.Product->StaticMesh->GetBodySetup() : nullptr;
		if (!Body || Body->AggGeom.GetElementCount() == 0 || Body->CollisionTraceFlag == CTF_UseComplexAsSimple
			|| !Piece.Product->CustomActiveActorClass || !Piece.Product->CustomActiveActorClass->IsChildOf(AFragmentPropActor::StaticClass())
			|| Piece.LocalTransform.ContainsNaN() || Piece.ScatterDirection.ContainsNaN()
			|| !FMath::IsFinite(Piece.MinSpeed) || !FMath::IsFinite(Piece.MaxSpeed) || Piece.MinSpeed < 0 || Piece.MaxSpeed < Piece.MinSpeed
			|| !FMath::IsFinite(Piece.VelocityInheritance) || Piece.VelocityInheritance < 0 || Piece.VelocityInheritance > 1) return false;
	}
	for (const auto& Piece : Pieces)
	{
		FPropFractureFragmentEntry Entry;
		Entry.FragmentProductID = Manager->RegisterProductFromDataAsset(Piece.Product);
		if (Entry.FragmentProductID <= 0) { Recipe.Reset(); return false; }
		Entry.LocalSpawnTransform = Piece.LocalTransform; Entry.LocalScatterDirection = Piece.ScatterDirection;
		Entry.MinImpulseSpeed = Piece.MinSpeed; Entry.MaxImpulseSpeed = Piece.MaxSpeed; Entry.VelocityInheritance = Piece.VelocityInheritance;
		Recipe.Add(Entry);
	}
	return true;
}
