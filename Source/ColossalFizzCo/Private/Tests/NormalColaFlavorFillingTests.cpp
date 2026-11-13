#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/ShopSupplyCatalog.h"
#include "Shop/BottleFillingComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNormalColaFlavorFillingTest,"ColossalFizzCo.Shop.AllNormalColaFilling",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNormalColaFlavorFillingTest::RunTest(const FString& Parameters)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	if (!TestTrue(TEXT("Real Normal catalog initializes"),Manager->ShopSupplies->InitializeCatalog())) return false;
	const auto* Catalog=Manager->ShopSupplies->Catalog.Get();
	auto* Empty=Catalog->EmptyBottleProduct.LoadSynchronous();
	if (!TestNotNull(TEXT("Configured empty plastic bottle"),Empty)) return false;
	TSet<int32> FilledContents;
	int32 Tested=0;
	for (const auto& Offer:Catalog->Offers)
	{
		if (Offer.Kind!=EShopSupplyKind::ColaCylinder) continue;
		const int32 Index=int32(Offer.Flavor);
		if (!Catalog->FinishedBottleProducts.IsValidIndex(Index)) return false;
		auto* SourceAsset=Offer.Product.LoadSynchronous();
		auto* Finished=Catalog->FinishedBottleProducts[Index].LoadSynchronous();
		if (!TestNotNull(TEXT("Authored bulk recipe exists"),SourceAsset) || !TestNotNull(TEXT("Matching finished flavor exists"),Finished)) return false;
		const FVector Origin(Index*1000,0,400);
		Manager->WorldElapsedSeconds=10+Index*3;
		const int32 Source=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(SourceAsset),FTransform(Origin+FVector(0,100,0)));
		const int32 Bottle=Manager->GenerateProductInstance(Manager->RegisterProductFromDataAsset(Empty),FTransform(Origin));
		if (!TestTrue(TEXT("Real source and bottle identities created"),Source>0 && Bottle>0)) return false;
		const auto History=Manager->FindUniqueData(Bottle)->EventData;
		auto* Equipment=World->SpawnActor<AActor>();
		auto* Root=NewObject<USceneComponent>(Equipment); Equipment->SetRootComponent(Root); Root->RegisterComponent(); Equipment->SetActorLocation(Origin);
		auto* Filler=NewObject<UBottleFillingComponent>(Equipment); Equipment->AddInstanceComponent(Filler);
		Filler->MaxConnectionDistance=250; Filler->TargetContentLevel=1; Filler->PortionsPerSecond=4; Filler->RegisterComponent();
		if (!TestTrue(*FString::Printf(TEXT("%s connects to the shared filler"),*Offer.OfferID.ToString()),Filler->StartFilling(Source,Bottle))) return false;
		for (int32 Step=0;Step<100 && Filler->bIsFilling;++Step) Filler->TickComponent(.05f,LEVELTICK_All,nullptr);
		const auto* Data=Manager->FindUniqueData(Bottle); const auto* SourceData=Manager->FindUniqueData(Source);
		if (!TestNotNull(TEXT("Filled bottle data remains present"),Data) || !TestNotNull(TEXT("Bulk source remains present"),SourceData)) return false;
		TestFalse(TEXT("Filler stops at normal capacity"),Filler->bIsFilling);
		TestEqual(TEXT("Correct flavor enters bottle"),Data->EventData.ContentID,Finished->DefaultContentID);
		TestEqual(TEXT("Normal full content level"),Data->EventData.ContentLevel,1.f);
		TestTrue(TEXT("Four portions consume one tenth of real forty-portion source"),FMath::IsNearlyEqual(SourceData->EventData.ContentLevel,.9f,1.e-5f));
		TestEqual(TEXT("Flavor conversion preserves bottle strength"),Data->EventData.Strength,History.Strength);
		TestEqual(TEXT("Flavor conversion preserves bottle physical age"),Data->EventData.CreationTime,History.CreationTime);
		const auto* Record=Manager->FindISIDData(Bottle); const auto* Product=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
		TestTrue(TEXT("Filled representation selects authored flavor product"),Product && Product->SourceDataAsset==Finished);
		FilledContents.Add(Data->EventData.ContentID); ++Tested;
	}
	TestEqual(TEXT("All five Normal bulk recipes are exercised"),Tested,5);
	TestEqual(TEXT("Each recipe retains a distinct content identity"),FilledContents.Num(),5);
	return true;
}
#endif
