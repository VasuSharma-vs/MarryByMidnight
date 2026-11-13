#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractableProp.h"
#include "Shop/ShopProcurementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDefaultBottleCapTest,"ColossalFizzCo.Shop.DefaultBottleCapIdentity",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDefaultBottleCapTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	if (!TestTrue(TEXT("Five-flavor catalog initializes"),Manager->ShopSupplies->InitializeCatalog())) return false;
	auto* Product=LoadObject<UInteractablePropDataAsset>(nullptr,TEXT("/Game/NormalColaPrototype/DA_NormalMint.DA_NormalMint"));
	if (!TestNotNull(TEXT("Real sealed starter bottle"),Product)) return false;
	const int32 Bottle=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(Product),FTransform(FVector(0,0,300)));
	if (!TestTrue(TEXT("Starter bottle identity is generated"),Bottle>0)) return false;
	FPropEventSyncDelta History; History.ISID=Bottle; History.SetFlag(EPropEventDirtyMask::Strength); History.Strength=.83f;
	History.SetFlag(EPropEventDirtyMask::CreationTime); History.CreationTime=17;
	if (!TestTrue(TEXT("Valid normalized bottle history is accepted"),Manager->SubmitInstanceEventUpdate(Bottle,History))) return false;
	const auto Before=Manager->FindUniqueData(Bottle)->EventData;
	if (!TestTrue(TEXT("Factory cap identity exists before any promotion"),Before.AttachedCapISID>0)) return false;
	const int32 BeforePromotionCount=Manager->ISIDData.Num();
	AActivePropActor* Actor=nullptr;
	if (!TestTrue(TEXT("Starter bottle promotes"),Manager->PromoteInstanceToASM(Bottle,EPropActivationReason::Interaction,false,Actor))) return false;
	const int32 Cap=Manager->FindUniqueData(Bottle)->EventData.AttachedCapISID;
	TestEqual(TEXT("Promotion creates no logical identities"),Manager->ISIDData.Num(),BeforePromotionCount);
	if (!TestTrue(TEXT("Default cap receives an independent identity"),Cap>0 && Cap!=Bottle)) return false;
	TestEqual(TEXT("Factory cap links back to bottle"),Manager->FindUniqueData(Cap)->EventData.AttachedBottleISID,Bottle);
	TestEqual(TEXT("Initialization preserves bottle damage"),Manager->FindUniqueData(Bottle)->EventData.Strength,Before.Strength);
	TestEqual(TEXT("Initialization preserves bottle creation time"),Manager->FindUniqueData(Bottle)->EventData.CreationTime,Before.CreationTime);
	TestEqual(TEXT("Initialization preserves liquid"),Manager->FindUniqueData(Bottle)->EventData.ContentLevel,Before.ContentLevel);
	TestEqual(TEXT("Initialization preserves liquid expiry origin"),Manager->FindUniqueData(Bottle)->EventData.ContentCreationTime,Before.ContentCreationTime);
	TestEqual(TEXT("Initialization preserves seal"),Manager->FindUniqueData(Bottle)->EventData.CapSealed,Before.CapSealed);
	auto* CapActor=Manager->GetActiveActorFromISID(Cap);
	TestTrue(TEXT("Default cap is a separate attached actor"),CapActor && CapActor->GetAttachParentActor()==Actor);
	const int32 Count=Manager->ISIDData.Num();
	TestTrue(TEXT("Repeated default-cap initialization is idempotent"),Manager->EnsureDefaultPhysicalCap(Bottle));
	TestEqual(TEXT("No additional cap is allocated"),Manager->ISIDData.Num(),Count);
	if (!TestTrue(TEXT("Starter assembly pools"),Manager->ConvertASMToISM(Bottle,EPropState::AtRest,true,true))) return false;
	const FString Slot=TEXT("Automation_DefaultCap_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (!TestTrue(TEXT("Factory cap identity saves"),Manager->SavePropDatabase(Slot,0))
		|| !TestTrue(TEXT("Factory cap identity loads"),Manager->LoadPropDatabase(Slot,0))) return false;
	TestEqual(TEXT("Reload keeps the same cap ISID"),Manager->FindUniqueData(Bottle)->EventData.AttachedCapISID,Cap);
	if (!TestTrue(TEXT("Reloaded bottle promotes"),Manager->PromoteInstanceToASM(Bottle,EPropActivationReason::Interaction,false,Actor))) return false;
	TestEqual(TEXT("Repromotion does not allocate another cap"),Manager->FindUniqueData(Bottle)->EventData.AttachedCapISID,Cap);
	TestTrue(TEXT("Factory cap can be physically removed"),Manager->DetachPhysicalCap(Bottle));
	TestTrue(TEXT("Open bottle does not regenerate a cap"),Manager->EnsureDefaultPhysicalCap(Bottle));
	TestEqual(TEXT("Removed cap remains detached"),Manager->FindUniqueData(Bottle)->EventData.AttachedCapISID,0);
	TestNotNull(TEXT("Removed physical cap remains in manager"),Manager->FindISIDData(Cap));
	return true;
}
#endif
