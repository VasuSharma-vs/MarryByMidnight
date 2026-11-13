#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Props/InteractablePropManager.h"
#include "Props/FragmentDestructionActor.h"
#include "Props/ColaPropActor.h"
#include "Components/ASMComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFragmentLifecycleTest, "ColossalFizzCo.Props.FragmentLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFragmentLifecycleTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	APropManager* Manager = World->SpawnActor<APropManager>();
	Manager->ASMComponent->MinASMCount = 1; Manager->ASMComponent->MaxASMCount = 1; Manager->ASMComponent->ExpandBatchSize = 2;
	Manager->bEnableAutosave = false;
	TArray<FPropFractureFragmentEntry> Recipe;
	for (int32 I = 1; I <= 11; ++I)
	{
		const FString Name = I == 9 ? TEXT("SM_Fragments_09") : FString::Printf(TEXT("SM_Fragments_%03d"), I);
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *(TEXT("/Game/Assest/meshs/Bottle_Fragments_Mesh/") + Name + TEXT(".") + Name));
		if (!TestNotNull(*Name, Mesh)) return false;
		FPropProductDefinition Product; Product.ProductID = 100 + I; Product.StaticMesh = Mesh; Product.CustomActiveActorClass = AFragmentPropActor::StaticClass();
		Product.ConsumableProfile.NumOfPortions = 0; Product.ExpiryProfile.bHasExpiry = false; Product.bEnableInstanceUniqueData = false;
		Manager->RegisterProductDefinition(Product);
		FPropFractureFragmentEntry Entry; Entry.FragmentProductID = Product.ProductID; Entry.MinImpulseSpeed = Entry.MaxImpulseSpeed = 0;
		Recipe.Add(Entry);
	}
	TArray<int32> IDs;
	const FTransform Origin(FRotator(0, 30, 0), FVector(200, 300, 400));
	TestTrue(TEXT("All eleven originals start together"), Manager->CreateFragmentDestruction(Recipe, Origin, FVector(100, 0, 0), FVector::ZeroVector, IDs));
	TSet<int32> UniqueIDs; UniqueIDs.Append(IDs);
	if (!TestEqual(TEXT("Each piece receives a unique identity"), UniqueIDs.Num(), 11)) return false;
	TArray<int32> Slots;
	for (int32 ID : IDs)
	{
		const auto* Record = Manager->FindISIDData(ID); Slots.Add(Record->ISMI);
		TestEqual(TEXT("Original component has explicit temporary representation"), Record->Representation, EPropRepresentation::CompositeFragment);
		TestEqual(TEXT("Original component does not share an ASMID"), Record->ASMID, 0);
		UStaticMeshComponent* Mesh = Manager->GetOriginalFragmentComponent(ID);
		TestNotNull(TEXT("Original component exists"), Mesh);
		TestEqual(TEXT("Hit identity resolves to the individual original"), Manager->GetISID(Mesh), ID);
		TestTrue(TEXT("Original uses showcase assembly transform"), Manager->GetTransform(ID).Equals(Origin));
	}
	UStaticMeshComponent* First = Manager->GetOriginalFragmentComponent(IDs[0]);
	AFragmentDestructionActor* OriginalActor = CastChecked<AFragmentDestructionActor>(First->GetOwner());
	const FGuid FirstGroup = OriginalActor->GetGroupID();
	auto DrainDeltaFor = [Manager](int32 ISID)
	{
		FPropInstanceDelta Selected; FPropBulkUpdateBatch Batch;
		while (Manager->BuildBulkUpdateBatch(64, Batch)) for (const auto& Delta : Batch.Records) if (Delta.ISID == ISID) Selected = Delta;
		return Selected;
	};
	const FPropInstanceDelta InitialDelta = DrainDeltaFor(IDs[1]);
	UWorld* ReplicaWorld = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!ReplicaWorld) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(ReplicaWorld);
	ON_SCOPE_EXIT { ReplicaWorld->DestroyWorld(false); GEngine->DestroyWorldContext(ReplicaWorld); };
	APropManager* Replica = ReplicaWorld->SpawnActor<APropManager>();
	Replica->ASMComponent->MinASMCount = 1; Replica->ASMComponent->MaxASMCount = 1;
	for (const auto& Pair : Manager->ProductDefinitions) Replica->RegisterProductDefinition(Pair.Value);
	Replica->ApplyInstanceDelta(InitialDelta);
	TestNotNull(TEXT("Late snapshot reconstructs the original component"), Replica->GetOriginalFragmentComponent(IDs[1]));
	FFragmentPhysicsSync Pose; Pose.ISID = IDs[1]; Pose.GroupID = InitialDelta.FragmentGroupID; Pose.Version = InitialDelta.Version;
	Pose.WorldTime = InitialDelta.FragmentPoseTime + 1; Pose.Transform = FTransform(FVector(500, 600, 700));
	Replica->ApplyFragmentPhysics(Pose);
	TestTrue(TEXT("Authoritative pose applies to matching generation"), Replica->FindISIDData(Pose.ISID)->ActiveTransform.Equals(Pose.Transform));
	Pose.Transform.SetLocation(FVector(1000)); Pose.WorldTime -= .5;
	Replica->ApplyFragmentPhysics(Pose);
	TestTrue(TEXT("Out-of-order pose is ignored"), Replica->FindISIDData(Pose.ISID)->ActiveTransform.GetLocation().Equals(FVector(500, 600, 700)));
	Pose.WorldTime += 2; Pose.GroupID = FGuid::NewGuid();
	Replica->ApplyFragmentPhysics(Pose);
	TestTrue(TEXT("Reused carrier cannot accept another group's pose"), Replica->FindISIDData(Pose.ISID)->ActiveTransform.GetLocation().Equals(FVector(500, 600, 700)));
	First->PutAllRigidBodiesToSleep();
	TestTrue(TEXT("Chaos-sleeping original is recognized as settled"), OriginalActor->IsPieceSettled(IDs[0]));
	TestFalse(TEXT("Settlement query does not wake a sleeping original"), First->RigidBodyIsAwake());
	First->SetSimulatePhysics(false);
	const FTransform Rest(FRotator(15, 90, 20), FVector(10, 20, 30)); First->SetWorldTransform(Rest);
	TestTrue(TEXT("One fragment settles independently"), Manager->SettleOriginalFragment(IDs[0]));
	TestEqual(TEXT("Other originals continue independently"), OriginalActor->GetActivePieceCount(), 10);
	TestEqual(TEXT("Permanent slot is preserved"), Manager->FindISIDData(IDs[0])->ISMI, Slots[0]);
	FTransform RestingISM; Manager->GetComponentFromISID(IDs[0])->GetInstanceTransform(Slots[0], RestingISM, true);
	TestTrue(TEXT("Existing ISM moves to final component transform"), RestingISM.Equals(Rest));
	for (int32 I = 1; I < IDs.Num(); ++I)
	{
		Manager->GetOriginalFragmentComponent(IDs[I])->SetSimulatePhysics(false);
		TestTrue(TEXT("Each remaining piece can finish separately"), Manager->SettleOriginalFragment(IDs[I]));
	}
	TestFalse(TEXT("Only fully converted carrier returns to pool"), OriginalActor->GetGroupID().IsValid());
	TestTrue(TEXT("Original component remains at its exact reset transform after carrier moves"), First->GetComponentTransform().Equals(Manager->DefaultResetTransform));
	TestFalse(TEXT("Pooled component no longer simulates"), First->IsSimulatingPhysics());
	TestEqual(TEXT("Pooled component has no collision"), First->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestTrue(TEXT("Carrier pool return leaves settled fragment unchanged"), Manager->GetTransform(IDs[0]).Equals(Rest));
	Replica->ApplyInstanceDelta(DrainDeltaFor(IDs[1]));
	TestNull(TEXT("Rest delta parks the replica's original component"), Replica->GetOriginalFragmentComponent(IDs[1]));
	Pose.GroupID = FirstGroup; Pose.WorldTime += 10; Replica->ApplyFragmentPhysics(Pose);
	TestEqual(TEXT("Delayed physics cannot reactivate settled original"), Replica->GetRepresentation(IDs[1]), EPropRepresentation::ISM);
	AActivePropActor* Active = nullptr;
	TestTrue(TEXT("Settled fragment promotes through normal allocator"), Manager->PromoteInstanceToASM(IDs[0], EPropActivationReason::Interaction, true, Active));
	if (!TestNotNull(TEXT("Dedicated fragment ASM exists"), Active)) return false;
	TestTrue(TEXT("Later interaction uses dedicated fragment class"), Active->IsA<AFragmentPropActor>());
	TestEqual(TEXT("Promotion retains logical identity"), Active->ISID, IDs[0]);
	TestEqual(TEXT("Promotion retains original permanent slot"), Manager->FindISIDData(IDs[0])->ISMI, Slots[0]);
	TestTrue(TEXT("Dedicated ASM returns to same ISM"), Manager->ConvertASMToISM(IDs[0], EPropState::AtRest, true, true));
	TArray<int32> NextIDs;
	TestTrue(TEXT("Original carrier can be reused"), Manager->CreateFragmentDestruction(Recipe, Origin, FVector(50, 0, 0), FVector(0, 0, 20), NextIDs));
	TestEqual(TEXT("Carrier is reused with a different generation"), Manager->GetOriginalFragmentComponent(NextIDs[0])->GetOwner(), static_cast<AActor*>(OriginalActor));
	TestTrue(TEXT("Generation changes on reuse"), OriginalActor->GetGroupID() != FirstGroup);
	FPropSerializedInstance Saved;
	TestTrue(TEXT("Active original serializes"), Manager->SerializeInstance(NextIDs[0], Saved));
	TestEqual(TEXT("Mid-destruction save restores through dedicated ASM path"), Saved.Representation, EPropRepresentation::ASM);
	TestTrue(TEXT("Save captures current physical transform"), Saved.ActiveTransform.Equals(Manager->GetTransform(NextIDs[0])));
	TestTrue(TEXT("Save captures physical velocity"), Saved.LinearVelocity.Equals(Manager->GetLinearVelocity(NextIDs[0])));
	const FString Slot = TEXT("Automation_Fragment_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { UGameplayStatics::DeleteGameInSlot(Slot, 0); };
	TestTrue(TEXT("Whole world saves mid-destruction"), Manager->SavePropDatabase(Slot));
	TestTrue(TEXT("Whole world restores mid-destruction"), Manager->LoadPropDatabase(Slot));
	TestTrue(TEXT("Loading does not replay original destruction carrier"), Manager->OriginalFragmentComponents.IsEmpty());
	for (int32 ID : NextIDs)
	{
		AActivePropActor* Restored = Manager->GetActiveActorFromISID(ID);
		TestTrue(TEXT("Every moving piece restores as dedicated ASM"), Restored && Restored->IsA<AFragmentPropActor>());
	}
	const int32 Count = Manager->ISIDData.Num(); Recipe.Last().FragmentProductID = 999999;
	TArray<int32> InvalidIDs;
	TestFalse(TEXT("Invalid recipe is rejected before allocation"), Manager->CreateFragmentDestruction(Recipe, Origin, FVector::ZeroVector, FVector::ZeroVector, InvalidIDs));
	TestEqual(TEXT("Invalid recipe creates no partial identities"), Manager->ISIDData.Num(), Count);
	Recipe.Last().FragmentProductID = 111;
	FPropContentDefinition Cola; Cola.ContentID = 7; Cola.ExpiryProfile.bHasExpiry = false;
	Manager->RegisterContentDefinition(Cola);
	FPropProductDefinition Bottle; Bottle.ProductID = 21; Bottle.StaticMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Assest/meshs/Cola1.Cola1"));
	Bottle.CustomActiveActorClass = AColaPropActor::StaticClass(); Bottle.bEnableInstanceUniqueData = true;
	Bottle.ConsumableProfile.NumOfPortions = 25; Bottle.DefaultUniqueData.EventData.ContentID = 7;
	Bottle.DefaultUniqueData.EventData.ContentLevel = .7f; Bottle.FractureFragments = Recipe;
	Manager->RegisterProductDefinition(Bottle);
	Manager->WorldElapsedSeconds = 100;
	const int32 BottleID = Manager->GenerateProductInstance(21, Origin);
	Manager->FindUniqueData(BottleID)->EventData.ContentCreationTime = 20;
	AActivePropActor* BottleASM = nullptr;
	TestTrue(TEXT("Filled bottle promotes before break"), Manager->PromoteInstanceToASM(BottleID, EPropActivationReason::Interaction, true, BottleASM));
	AColaPropActor* BottleActor = Cast<AColaPropActor>(BottleASM);
	if (!TestNotNull(TEXT("Bottle uses containment implementation"), BottleActor)) return false;
	const int32 BeforeBreak = Manager->ISIDData.Num();
	FContainmentFailureRequest Failure; Failure.FailureType = EContainmentFailureType::BottleBreakage;
	TestTrue(TEXT("Bottle break integrates the eleven-piece recipe"), BottleActor->SubmitFailureRequest(Failure));
	TestNull(TEXT("Source identity retires only after fragments commit"), Manager->FindISIDData(BottleID));
	TestEqual(TEXT("Eleven fragments replace exactly one parent"), Manager->ISIDData.Num(), BeforeBreak + 10);
	TestEqual(TEXT("Released liquid is accounted exactly once"), Manager->ReleasedLiquids.Num(), 1);
	TestTrue(TEXT("Fractional spilled portions are preserved"), FMath::IsNearlyEqual(Manager->ReleasedLiquids[0].Portions, 17.5, .0001));
	TestEqual(TEXT("Released liquid keeps its original content clock"), Manager->ReleasedLiquids[0].ContentCreationTime, 20.0);
	TestFalse(TEXT("Pooled bottle cannot replay destruction"), BottleActor->SubmitFailureRequest(Failure));
	TestEqual(TEXT("Replay cannot duplicate liquid"), Manager->ReleasedLiquids.Num(), 1);
	TestTrue(TEXT("Post-break world including released liquid saves"), Manager->SavePropDatabase(Slot));
	TestTrue(TEXT("Post-break world reloads"), Manager->LoadPropDatabase(Slot));
	TestEqual(TEXT("Liquid accounting survives save/load"), Manager->ReleasedLiquids.Num(), 1);
	TestTrue(TEXT("Loaded fractional quantity is unchanged"), FMath::IsNearlyEqual(Manager->ReleasedLiquids[0].Portions, 17.5, .0001));
	return true;
}
#endif
