#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Slate/SceneViewport.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DynamicBlueprintBinding.h"
#include "Components/PropGrabInputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Props/InteractableProp.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropGrabGestureTest, "ColossalFizzCo.Props.GrabGesture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropGrabGestureTest::RunTest(const FString& Parameters)
{
	using Phase = EPropGrabInputPhase; using Intent = EPropGrabIntent;
	FPropGrabGesture Gesture;
	auto Step = [&](Phase P, double Time, uint64 Token) { return Gesture.Process(P, Time, Token, .4, .4); };
	TestEqual(TEXT("Press alone does not place"), Step(Phase::Started, 0, 0), Intent::None);
	TestEqual(TEXT("Short empty press does not grab early"), Step(Phase::Ongoing, .2, 0), Intent::None);
	TestEqual(TEXT("Holding G requests one pickup"), Step(Phase::Ongoing, .41, 0), Intent::Grab);
	TestEqual(TEXT("Same press cannot place its newly grabbed bottle"), Step(Phase::Ongoing, .6, 101), Intent::None);
	TestEqual(TEXT("Hold-and-release trigger cannot pick up again"), Step(Phase::Triggered, .7, 101), Intent::None);
	TestEqual(TEXT("Completed after Triggered is consumed"), Step(Phase::Completed, .71, 101), Intent::None);
	Step(Phase::Started, 1, 101);
	TestEqual(TEXT("Holding an existing bottle requests trace placement"), Step(Phase::Ongoing, 1.41, 101), Intent::Place);
	TestEqual(TEXT("Placement is emitted only once"), Step(Phase::Ongoing, 1.6, 0), Intent::None);
	TestEqual(TEXT("Release after placement does not regrab"), Step(Phase::Triggered, 1.7, 0), Intent::None);
	Step(Phase::Started, 2, 101);
	TestEqual(TEXT("One short G tap keeps bottle held"), Step(Phase::Canceled, 2.05, 101), Intent::None);
	Step(Phase::Started, 2.15, 101);
	TestEqual(TEXT("Second short G tap drops the same bottle"), Step(Phase::Canceled, 2.2, 101), Intent::Drop);
	TestEqual(TEXT("Repeated end event cannot drop twice"), Step(Phase::Completed, 2.21, 101), Intent::None);
	Step(Phase::Started, 3, 101); Step(Phase::Canceled, 3.05, 101);
	Step(Phase::Started, 3.15, 102);
	TestEqual(TEXT("Old tap cannot drop another logical bottle"), Step(Phase::Canceled, 3.2, 102), Intent::None);
	Step(Phase::Started, 4, 101);
	TestEqual(TEXT("Missed Ongoing frame still recognizes hold on release"), Step(Phase::Triggered, 4.5, 101), Intent::Place);
	return true;
}

namespace PropGrabTests
{
	struct FWorldOwner
	{
		UWorld* World;
		UGameInstance* GameInstance;
		explicit FWorldOwner(UWorld* InWorld, UGameInstance* InGameInstance = nullptr) : World(InWorld), GameInstance(InGameInstance) {}
		~FWorldOwner()
		{
			if (World->GetBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
			if (GameInstance) GameInstance->Shutdown();
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
		}
	};
	class FGravityCheck : public IAutomationLatentCommand
	{
		FAutomationTestBase* Test;
		TSharedPtr<FWorldOwner> Scope;
		TWeakObjectPtr<AActivePropActor> Bottle;
		double BeforeZ;
		int32 Frames = 0;
	public:
		FGravityCheck(FAutomationTestBase* InTest, TSharedPtr<FWorldOwner> InScope, AActivePropActor* InBottle)
			: Test(InTest), Scope(InScope), Bottle(InBottle), BeforeZ(InBottle->GetActorLocation().Z) {}
		bool Update() override
		{
			// Physics scheduling uses engine frame numbers. Repeated synchronous Tick calls are not distinct frames.
			Scope->World->Tick(LEVELTICK_All, 1.f/60.f);
			if (++Frames < 12) return false;
			if (Test->TestTrue(TEXT("Released bottle remains valid during fall"), Bottle.IsValid()))
			{
				Test->AddInfo(FString::Printf(TEXT("Drop Z %.2f -> %.2f; velocity %s; world gravity %.1f; root is mesh %d"), BeforeZ, Bottle->GetActorLocation().Z,
					*Bottle->PropMesh->GetPhysicsLinearVelocity().ToString(), Scope->World->GetGravityZ(), Bottle->GetRootComponent() == Bottle->PropMesh));
				Test->TestTrue(TEXT("Released bottle actually falls under gravity"), Bottle->GetActorLocation().Z < BeforeZ - 1);
			}
			return true;
		}
	};
	bool Attach(AActivePropActor* Prop, UPrimitiveComponent* Parent)
	{
		UFunction* Function = Prop->FindFunction(TEXT("Request Attach to Component"));
		if (!Function) return false;
		FStructOnScope Params(Function);
		auto* Component = FindFProperty<FObjectPropertyBase>(Function, TEXT("Component"));
		if (!Component) return false;
		Component->SetObjectPropertyValue_InContainer(Params.GetStructMemory(), Parent);
		Prop->ProcessEvent(Function, Params.GetStructMemory());
		for (TFieldIterator<FBoolProperty> It(Function); It; ++It)
			if (It->HasAnyPropertyFlags(CPF_OutParm)) return It->GetPropertyValue_InContainer(Params.GetStructMemory());
		return false;
	}
	bool Detach(AActivePropActor* Prop)
	{
		UFunction* Function = Prop->FindFunction(TEXT("Request Detach from Component"));
		if (!Function) return false;
		FStructOnScope Params(Function); Prop->ProcessEvent(Function, Params.GetStructMemory());
		for (TFieldIterator<FBoolProperty> It(Function); It; ++It)
			if (It->HasAnyPropertyFlags(CPF_OutParm)) return It->GetPropertyValue_InContainer(Params.GetStructMemory());
		return false;
	}
	bool Move(AActivePropActor* Prop, FVector Location)
	{
		UFunction* Function = Prop->FindFunction(TEXT("Move Actor To Location"));
		if (!Function) return false;
		FStructOnScope Params(Function);
		auto* Value = FindFProperty<FStructProperty>(Function, TEXT("Location"));
		if (!Value || Value->Struct != TBaseStructure<FVector>::Get()) return false;
		*Value->ContainerPtrToValuePtr<FVector>(Params.GetStructMemory()) = Location;
		Prop->ProcessEvent(Function, Params.GetStructMemory());
		return Prop->GetActorLocation().Equals(Location, .01);
	}
	bool FireGrabPhase(ACharacter* Player, const FString& Phase)
	{
		// Dispatch the exact compiled callback Enhanced Input binds, without desktop input or a viewport.
		UClass* BindingClass = LoadClass<UDynamicBlueprintBinding>(nullptr, TEXT("/Script/EnhancedInput.EnhancedInputActionDelegateBinding"));
		auto* Binding = BindingClass ? UBlueprintGeneratedClass::GetDynamicBindingObject(Player->GetClass(), BindingClass) : nullptr;
		if (!Binding) return false;
		auto* Bindings = FindFProperty<FArrayProperty>(Binding->GetClass(), TEXT("InputActionDelegateBindings"));
		auto* Row = Bindings ? CastField<FStructProperty>(Bindings->Inner) : nullptr;
		if (!Row) return false;
		auto* ActionField = FindFProperty<FObjectPropertyBase>(Row->Struct, TEXT("InputAction"));
		auto* PhaseField = FindFProperty<FEnumProperty>(Row->Struct, TEXT("TriggerEvent"));
		auto* FunctionField = FindFProperty<FNameProperty>(Row->Struct, TEXT("FunctionNameToBind"));
		if (!ActionField || !PhaseField || !FunctionField) return false;
		FScriptArrayHelper Array(Bindings, Bindings->ContainerPtrToValuePtr<void>(Binding));
		for (int32 Index = 0; Index < Array.Num(); ++Index)
		{
			void* Data = Array.GetRawPtr(Index);
			UObject* Action = ActionField->GetObjectPropertyValue_InContainer(Data);
			const int64 Value = PhaseField->GetUnderlyingProperty()->GetSignedIntPropertyValue(PhaseField->ContainerPtrToValuePtr<void>(Data));
			if (!Action || Action->GetName() != TEXT("IA_Grab") || PhaseField->GetEnum()->GetNameStringByValue(Value) != Phase) continue;
			UFunction* Function = Player->FindFunction(FunctionField->GetPropertyValue_InContainer(Data));
			if (!Function) return false;
			FStructOnScope Params(Function);
			if (auto* Source = FindFProperty<FObjectPropertyBase>(Function, TEXT("SourceAction"))) Source->SetObjectPropertyValue_InContainer(Params.GetStructMemory(), Action);
			Player->ProcessEvent(Function, Params.GetStructMemory());
			return true;
		}
		return false;
	}
	bool InvokeCarryRPC(ACharacter* Player, const FString& Name, AActor* Target)
	{
		for (TFieldIterator<UFunction> It(Player->GetClass()); It; ++It)
		{
			if (!It->GetName().Replace(TEXT(" "), TEXT("")).Equals(Name, ESearchCase::IgnoreCase)) continue;
			if (!It->HasAnyFunctionFlags(FUNC_NetServer)) return false;
			auto* Parameter = FindFProperty<FObjectPropertyBase>(*It, TEXT("Target"));
			if (!Parameter) return false;
			FStructOnScope Params(*It);
			Parameter->SetObjectPropertyValue_InContainer(Params.GetStructMemory(), Target);
			Player->ProcessEvent(*It, Params.GetStructMemory());
			return true;
		}
		return false;
	}
	bool GetPlacementLocation(ACharacter* Player, FVector& Location)
	{
		for (TFieldIterator<UFunction> It(Player->GetClass()); It; ++It)
		{
			if (!It->GetName().Replace(TEXT(" "), TEXT("")).Equals(TEXT("getcameratracesafelocation"), ESearchCase::IgnoreCase)) continue;
			FStructOnScope Params(*It);
			if (auto* Radius = FindFProperty<FFloatProperty>(*It, TEXT("Radius"))) Radius->SetPropertyValue_InContainer(Params.GetStructMemory(), 50.f);
			else if (auto* RadiusDouble = FindFProperty<FDoubleProperty>(*It, TEXT("Radius"))) RadiusDouble->SetPropertyValue_InContainer(Params.GetStructMemory(), 50.);
			else return false;
			Player->ProcessEvent(*It, Params.GetStructMemory());
			for (TFieldIterator<FStructProperty> Value(*It); Value; ++Value)
				if (Value->HasAnyPropertyFlags(CPF_OutParm) && Value->Struct == TBaseStructure<FVector>::Get())
				{
					Location = *Value->ContainerPtrToValuePtr<FVector>(Params.GetStructMemory());
					return true;
				}
		}
		return false;
	}
	bool GetCameraHit(ACharacter* Player, FHitResult& Hit)
	{
		for (TFieldIterator<UFunction> It(Player->GetClass()); It; ++It)
		{
			if (!It->GetName().Replace(TEXT(" "), TEXT("")).Equals(TEXT("PlayerHitLineTrace"), ESearchCase::IgnoreCase)) continue;
			FStructOnScope Params(*It);
			Player->ProcessEvent(*It, Params.GetStructMemory());
			for (TFieldIterator<FStructProperty> Value(*It); Value; ++Value)
				if (Value->HasAnyPropertyFlags(CPF_OutParm) && Value->Struct == FHitResult::StaticStruct())
				{
					Hit = *Value->ContainerPtrToValuePtr<FHitResult>(Params.GetStructMemory());
					return true;
				}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBottleGrabAdapterTest, "ColossalFizzCo.Props.AuthoredBottleGrabRelease", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBottleGrabAdapterTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto WorldScope = MakeShared<PropGrabTests::FWorldOwner>(World);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager = World->SpawnActor<APropManager>(); Manager->bEnableAutosave = false; Manager->InitializePropManager();
	auto* Product = LoadObject<UInteractablePropDataAsset>(nullptr, TEXT("/Game/Assest/DataAssets/DA_BasicCola.DA_BasicCola"));
	if (!TestNotNull(TEXT("Production bottle definition"), Product)) return false;
	const int32 PID = Manager->RegisterProductFromDataAsset(Product);
	const int32 ID = Manager->GenerateProductInstance(PID, FTransform(FRotator::ZeroRotator, FVector(100,0,250), FVector(.15)));
	AActivePropActor* Bottle = nullptr;
	if (!TestTrue(TEXT("Production bottle promotes"), Manager->PromoteInstanceToASM(ID, EPropActivationReason::Interaction, false, Bottle)) || !Bottle) return false;
	TestEqual(TEXT("Actual production Blueprint used"), Bottle->GetClass()->GetPathName(), FString(TEXT("/Game/Props/Colas/BP_Cola.BP_Cola_C")));
	for (TFieldIterator<UFunction> It(Bottle->GetClass()); It; ++It)
		if (It->GetName().Contains(TEXT("Request")) || It->GetName().Contains(TEXT("Move Actor")))
		{
			FString Signature = It->GetName();
			for (TFieldIterator<FProperty> Param(*It); Param; ++Param) if (Param->HasAnyPropertyFlags(CPF_Parm)) Signature += TEXT(" | ") + Param->GetName() + TEXT(":") + Param->GetClass()->GetName();
			AddInfo(Signature);
		}
	Manager->FindISIDData(ID)->RuntimeData.LastInteractionTime = -100;
	auto* Pawn = World->SpawnActor<ACharacter>(); Pawn->SetActorLocation(FVector(0,0,100));
	auto* Input = NewObject<UPropGrabInputComponent>(Pawn); Pawn->AddInstanceComponent(Input); Input->RegisterComponent();
	TestTrue(TEXT("Owned RPC may grab an available bottle"), Input->CanGrabTarget(Bottle));
	TestTrue(TEXT("Production attach interface succeeds"), PropGrabTests::Attach(Bottle, Pawn->GetMesh()));
	TestTrue(TEXT("Attach records exclusive holder in native registry"), Manager->FindISIDData(ID)->RuntimeData.InteractionOwner.Get() == Pawn);
	TestTrue(TEXT("Owned RPC recognizes the real holder"), Input->OwnsHeldTarget(Bottle));
	auto* OtherPawn = World->SpawnActor<ACharacter>();
	auto* OtherInput = NewObject<UPropGrabInputComponent>(OtherPawn); OtherPawn->AddInstanceComponent(OtherInput); OtherInput->RegisterComponent();
	TestFalse(TEXT("Another player's RPC cannot release the bottle"), OtherInput->OwnsHeldTarget(Bottle));
	TestFalse(TEXT("Another player cannot steal the held bottle"), OtherInput->CanGrabTarget(Bottle));
	TestTrue(TEXT("Holding Actor RepNotify may safely repeat attach"), PropGrabTests::Attach(Bottle, Pawn->GetMesh()));
	TestFalse(TEXT("Held bottle has physics disabled"), Bottle->PropMesh->IsSimulatingPhysics());
	Bottle->PropMesh->IgnoreActorWhenMoving(Pawn,true);
	Bottle->PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TestTrue(TEXT("Production detach interface succeeds"), PropGrabTests::Detach(Bottle));
	TestFalse(TEXT("Release callback retains holder long enough to clear collision ignores"),Bottle->PropMesh->GetMoveIgnoreActors().Contains(Pawn));
	TestEqual(TEXT("Release restores collision suppressed by a customer pocket"),Bottle->PropMesh->GetCollisionEnabled(),ECollisionEnabled::QueryAndPhysics);
	TestNull(TEXT("Actor is detached, not just a mesh subcomponent"), Bottle->GetAttachParentActor());
	TestFalse(TEXT("Drop clears held state"), Bottle->bIsHeld);
	TestTrue(TEXT("Drop enables actual bottle physics"), Bottle->PropMesh->IsSimulatingPhysics());
	TestTrue(TEXT("Dropped bottle retains gravity"), Bottle->PropMesh->IsGravityEnabled());
	TestTrue(TEXT("Drop releases native ownership"), !Manager->FindISIDData(ID)->RuntimeData.InteractionOwner.IsValid());
	TestTrue(TEXT("Bottle can be grabbed after release"), PropGrabTests::Attach(Bottle, Pawn->GetMesh()));
	const FVector Scale = Bottle->GetActorScale3D();
	const FVector Destination(180,50,250);
	TestFalse(TEXT("Held placement cannot teleport outside interaction reach"), Bottle->MoveHeldToLocation(FVector(10000,0,250)));
	auto* Wall = World->SpawnActor<AActor>();
	auto* WallBox = NewObject<UBoxComponent>(Wall); Wall->SetRootComponent(WallBox); WallBox->SetBoxExtent(FVector(10,100,200));
	WallBox->SetCollisionProfileName(TEXT("BlockAll")); WallBox->RegisterComponent(); Wall->SetActorLocation(FVector(90,25,200));
	TestFalse(TEXT("Held placement cannot pass through a blocking wall"), Bottle->MoveHeldToLocation(Destination));
	Wall->Destroy();
	TestTrue(TEXT("Production move interface reaches trace location"), PropGrabTests::Move(Bottle, Destination));
	TestTrue(TEXT("Moving preserves scale"), Bottle->GetActorScale3D().Equals(Scale));
	TestTrue(TEXT("Moving updates the same ISID transform"), Manager->GetTransform(ID).GetLocation().Equals(Destination, .01));
	TestFalse(TEXT("Non-finite placement is rejected"), Bottle->MoveHeldToLocation(FVector(std::numeric_limits<double>::infinity(),0,0)));
	TestTrue(TEXT("Placement releases through production interface"), PropGrabTests::Detach(Bottle));
	TestFalse(TEXT("Released bottle cannot be moved by stale held placement"), Bottle->MoveHeldToLocation(Destination));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new PropGrabTests::FGravityCheck(this, WorldScope, Bottle)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGrabBlueprintInputTest, "ColossalFizzCo.Props.AuthoredGrabInputRouting", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGrabBlueprintInputTest::RunTest(const FString& Parameters)
{
	// A windowless viewport gives the real Blueprint's screen-center deprojection valid projection data.
	// No Slate window is created or activated and no desktop input is injected.
	auto* GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* World = GameInstance->GetWorld();
	if (!World) return false;
	auto WorldScope = MakeShared<PropGrabTests::FWorldOwner>(World, GameInstance);
	auto* ViewportClient = NewObject<UGameViewportClient>(GEngine);
	auto& Context = *GameInstance->GetWorldContext();
	Context.GameViewport = ViewportClient;
	ViewportClient->Init(Context, GameInstance, false);
	auto Viewport = FSceneViewport::Create(TStrongPtrVariant<FViewportClient>(ViewportClient), TSharedPtr<SViewport>());
	Viewport->SetInitialSize(FIntPoint(800, 600));
	ViewportClient->Viewport = &Viewport.Get();
	ON_SCOPE_EXIT { ViewportClient->Viewport = nullptr; Context.GameViewport = nullptr; };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager = World->SpawnActor<APropManager>(); Manager->bEnableAutosave = false; Manager->InitializePropManager();
	auto* Controller = World->SpawnActor<APlayerController>();
	UClass* PlayerClass = LoadClass<ACharacter>(nullptr, TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	if (!TestNotNull(TEXT("Production player Blueprint"), PlayerClass)) return false;
	// Input routing is exercised in isolation from the player's HUD/viewport BeginPlay setup.
	World->SetBegunPlay(false);
	auto* Player = World->SpawnActor<ACharacter>(PlayerClass, FVector(0,0,100), FRotator::ZeroRotator);
	World->SetBegunPlay(true); Controller->Possess(Player);
	Player->GetCharacterMovement()->DisableMovement(); // Empty fixture has no ground; keep the camera stationary.
	auto* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	GameInstance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
	LocalPlayer->ViewportClient = ViewportClient;
	LocalPlayer->PlayerController = Controller; Controller->Player = LocalPlayer;
	LocalPlayer->Size = FVector2D(1,1);
	Controller->SetViewTarget(Player); Controller->UpdateCameraManager(0.f);
	FVector RayOrigin, RayDirection;
	if (!TestTrue(TEXT("Windowless player has real screen-center deprojection"), Controller->DeprojectScreenPositionToWorld(400,300,RayOrigin,RayDirection))) return false;
	TestTrue(TEXT("Screen-center direction is normalized and nonzero"), RayDirection.IsNormalized());
	auto* Input = Player->FindComponentByClass<UPropGrabInputComponent>();
	if (!TestNotNull(TEXT("Saved player Blueprint contains native grab component"), Input)) return false;
	auto* Product = LoadObject<UInteractablePropDataAsset>(nullptr, TEXT("/Game/Assest/DataAssets/DA_BasicCola.DA_BasicCola"));
	if (!Product) return false;
	const int32 PID = Manager->RegisterProductFromDataAsset(Product);
	const int32 ID = Manager->GenerateProductInstance(PID, FTransform(FRotator::ZeroRotator, FVector(100,0,200), FVector(.15)));
	AActivePropActor* Bottle = nullptr;
	if (!Manager->PromoteInstanceToASM(ID, EPropActivationReason::Interaction, false, Bottle) || !Bottle) return false;
	Manager->FindISIDData(ID)->RuntimeData.LastInteractionTime = -100;
	auto* HoldingActor = FindFProperty<FObjectPropertyBase>(PlayerClass, TEXT("Holding Actor"));
	auto* HoldingState = FindFProperty<FByteProperty>(PlayerClass, TEXT("Holding State"));
	if (!TestNotNull(TEXT("Player carry compatibility reference"), HoldingActor) || !TestNotNull(TEXT("Player carry compatibility state"), HoldingState)) return false;
	auto* OtherPlayer = World->SpawnActor<ACharacter>();
	if (!TestTrue(TEXT("Other player exclusively holds bottle"), Bottle->AttachForInteraction(OtherPlayer->GetMesh()))) return false;
	TestTrue(TEXT("Saved server pickup RPC exists"), PropGrabTests::InvokeCarryRPC(Player, TEXT("RequestServertoAttachActortoComponent"), Bottle));
	TestTrue(TEXT("Actual pickup RPC preserves other player's ownership"), Manager->FindISIDData(ID)->RuntimeData.InteractionOwner.Get() == OtherPlayer);
	TestNull(TEXT("Rejected pickup RPC does not retain a false held reference"), HoldingActor->GetObjectPropertyValue_InContainer(Player));
	TestTrue(TEXT("Saved server release RPC exists"), PropGrabTests::InvokeCarryRPC(Player, TEXT("RequestServertoDetachActortoComponent"), Bottle));
	TestTrue(TEXT("Actual release RPC cannot detach another player's bottle"), Bottle->bIsHeld && Bottle->GetAttachParentActor() == OtherPlayer);
	Bottle->DetachForInteraction();
	Manager->FindISIDData(ID)->RuntimeData.LastInteractionTime = -100;
	if (!TestTrue(TEXT("Native lifecycle prepares a held bottle"), Bottle->AttachForInteraction(Player->GetMesh(), TEXT("Holding object")))) return false;
	HoldingActor->SetObjectPropertyValue_InContainer(Player, Bottle); HoldingState->SetPropertyValue_InContainer(Player, 1);
	TestTrue(TEXT("Owner's saved release RPC executes"), PropGrabTests::InvokeCarryRPC(Player, TEXT("RequestServertoDetachActortoComponent"), Bottle));
	TestFalse(TEXT("Owner's saved release RPC actually releases"), Bottle->bIsHeld);
	TestTrue(TEXT("Owner's saved release RPC restores physics"), Bottle->PropMesh->IsSimulatingPhysics());
	TestNull(TEXT("Owner's saved release RPC clears carry reference"), HoldingActor->GetObjectPropertyValue_InContainer(Player));
	Manager->FindISIDData(ID)->RuntimeData.LastInteractionTime = -100;
	if (!TestTrue(TEXT("Regrab after owner's server release"), Bottle->AttachForInteraction(Player->GetMesh(), TEXT("Holding object")))) return false;
	HoldingActor->SetObjectPropertyValue_InContainer(Player, Bottle); HoldingState->SetPropertyValue_InContainer(Player, 1);
	TestTrue(TEXT("Actual compiled G Started callback exists"), PropGrabTests::FireGrabPhase(Player, TEXT("Started")));
	World->Tick(LEVELTICK_All, .05f);
	TestTrue(TEXT("Actual compiled G Canceled callback exists"), PropGrabTests::FireGrabPhase(Player, TEXT("Canceled")));
	TestTrue(TEXT("First tap leaves bottle held"), Bottle->bIsHeld);
	World->Tick(LEVELTICK_All, .1f);
	TestTrue(TEXT("Second G press reaches saved native input adapter"), PropGrabTests::FireGrabPhase(Player, TEXT("Started")));
	World->Tick(LEVELTICK_All, .05f);
	TestTrue(TEXT("Second G release callback executes"), PropGrabTests::FireGrabPhase(Player, TEXT("Canceled")));
	TestEqual(TEXT("Compiled Blueprint reads the native double-tap intent"), Input->LastIntent, EPropGrabIntent::Drop);
	TestFalse(TEXT("Actual GG callback releases the bottle"), Bottle->bIsHeld);
	TestTrue(TEXT("Actual GG callback restores physics"), Bottle->PropMesh->IsSimulatingPhysics());
	TestNull(TEXT("Actual GG callback clears legacy held actor"), HoldingActor->GetObjectPropertyValue_InContainer(Player));
	TestEqual(TEXT("Actual GG callback clears legacy held state"), HoldingState->GetPropertyValue_InContainer(Player), uint8(0));
	Manager->FindISIDData(ID)->RuntimeData.LastInteractionTime = -100;
	if (!TestTrue(TEXT("Bottle can be held for placement"), Bottle->AttachForInteraction(Player->GetMesh(), TEXT("Holding object")))) return false;
	HoldingActor->SetObjectPropertyValue_InContainer(Player, Bottle); HoldingState->SetPropertyValue_InContainer(Player, 1);
	TestTrue(TEXT("Hold placement reaches actual input Started callback"), PropGrabTests::FireGrabPhase(Player, TEXT("Started")));
	World->Tick(LEVELTICK_All, .45f);
	FVector ExpectedLocation;
	if (!TestTrue(TEXT("Production camera trace returns a placement location"), PropGrabTests::GetPlacementLocation(Player, ExpectedLocation))) return false;
	TestFalse(TEXT("Trace placement is not a default zero vector"), ExpectedLocation.IsNearlyZero());
	AddInfo(FString::Printf(TEXT("Production placement trace: ray origin %s direction %s; destination %s"), *RayOrigin.ToString(), *RayDirection.ToString(), *ExpectedLocation.ToString()));
	TestTrue(TEXT("Hold placement reaches actual input Ongoing callback"), PropGrabTests::FireGrabPhase(Player, TEXT("Ongoing")));
	TestEqual(TEXT("Compiled Blueprint reads native hold placement intent"), Input->LastIntent, EPropGrabIntent::Place);
	TestTrue(TEXT("Actual G hold places bottle at camera trace location"), Bottle->GetActorLocation().Equals(ExpectedLocation, .01));
	TestFalse(TEXT("Actual G hold releases the placed bottle"), Bottle->bIsHeld);
	TestTrue(TEXT("Actual G hold restores placed bottle physics"), Bottle->PropMesh->IsSimulatingPhysics());
	TestNull(TEXT("Actual G hold clears carry reference"), HoldingActor->GetObjectPropertyValue_InContainer(Player));
	TestTrue(TEXT("Hold release reaches actual Triggered callback"), PropGrabTests::FireGrabPhase(Player, TEXT("Triggered")));
	TestTrue(TEXT("Hold completion reaches actual Completed callback"), PropGrabTests::FireGrabPhase(Player, TEXT("Completed")));
	TestEqual(TEXT("Hold release emits no second interaction"), Input->LastIntent, EPropGrabIntent::None);
	if (!TestTrue(TEXT("Pickup ray has valid projection data"), Controller->DeprojectScreenPositionToWorld(400,300,RayOrigin,RayDirection))) return false;
	Bottle->PropMesh->SetSimulatePhysics(false);
	const FVector BoundsOffset = Bottle->PropMesh->Bounds.Origin - Bottle->GetActorLocation();
	Bottle->SetActorLocation(RayOrigin + RayDirection * 80 - BoundsOffset, false, nullptr, ETeleportType::TeleportPhysics);
	Manager->FindISIDData(ID)->ActiveTransform = Bottle->GetActorTransform();
	Manager->FindISIDData(ID)->RuntimeData.LastInteractionTime = -100;
	FHitResult PickupHit;
	if (!TestTrue(TEXT("Production pickup ray query executes"), PropGrabTests::GetCameraHit(Player, PickupHit))) return false;
	AddInfo(FString::Printf(TEXT("Pickup ray hit %s at %s; target bounds %s extent %s"), *GetNameSafe(PickupHit.GetActor()), *PickupHit.ImpactPoint.ToString(), *Bottle->PropMesh->Bounds.Origin.ToString(), *Bottle->PropMesh->Bounds.BoxExtent.ToString()));
	if (!TestTrue(TEXT("Actual pickup ray hits the production bottle"), PickupHit.GetActor() == Bottle)) return false;
	// The isolated input fixture omits character BeginPlay/Tick; seed its existing tick cache from the real query.
	auto* TickHit = FindFProperty<FStructProperty>(PlayerClass, TEXT("Hit Result On tick"));
	if (!TestTrue(TEXT("Player trace compatibility cache exists"), TickHit && TickHit->Struct == FHitResult::StaticStruct())) return false;
	// Start off target, then aim at the bottle during the hold. A press-start cached trace must not win.
	*TickHit->ContainerPtrToValuePtr<FHitResult>(Player) = FHitResult();
	TestTrue(TEXT("Pickup reaches actual G Started callback"), PropGrabTests::FireGrabPhase(Player, TEXT("Started")));
	World->Tick(LEVELTICK_All, .45f);
	*TickHit->ContainerPtrToValuePtr<FHitResult>(Player) = PickupHit;
	TestTrue(TEXT("Pickup reaches actual G Ongoing callback"), PropGrabTests::FireGrabPhase(Player, TEXT("Ongoing")));
	TestEqual(TEXT("Compiled Blueprint reads native hold pickup intent"), Input->LastIntent, EPropGrabIntent::Grab);
	TestTrue(TEXT("Actual G hold picks up the traced production bottle"), HoldingActor->GetObjectPropertyValue_InContainer(Player) == Bottle && Bottle->bIsHeld);
	TestTrue(TEXT("Actual pickup assigns native ownership"), Manager->FindISIDData(ID)->RuntimeData.InteractionOwner.Get() == Player);
	TestTrue(TEXT("Pickup release reaches actual Triggered callback"), PropGrabTests::FireGrabPhase(Player, TEXT("Triggered")));
	TestTrue(TEXT("Pickup completion reaches actual Completed callback"), PropGrabTests::FireGrabPhase(Player, TEXT("Completed")));
	TestTrue(TEXT("Same press cannot place its newly picked up bottle"), Bottle->bIsHeld);
	return true;
}
#endif
