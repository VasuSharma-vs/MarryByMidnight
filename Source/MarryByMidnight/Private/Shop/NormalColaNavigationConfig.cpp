#include "Shop/NormalColaNavigationConfig.h"
#include "NavMesh/RecastNavMesh.h"

ANormalColaNavigationData::ANormalColaNavigationData()
{
	RuntimeGeneration=ERuntimeGenerationType::Dynamic; bForceRebuildOnLoad=true;
}

void ANormalColaNavigationData::PostInitProperties()
{
	Super::PostInitProperties();
	// Recast loads inherited engine config after the constructor. The prototype
	// owns a dynamic world-local profile even when the original map uses Static.
	RuntimeGeneration=ERuntimeGenerationType::Dynamic; bForceRebuildOnLoad=true;
}

void ANormalColaNavigationData::PostLoad()
{
	Super::PostLoad();
	// Older prototype maps can contain the inherited static value serialized.
	RuntimeGeneration=ERuntimeGenerationType::Dynamic; bForceRebuildOnLoad=true;
}

UNormalColaNavigationConfig::UNormalColaNavigationConfig(const FObjectInitializer& Initializer):Super(Initializer)
{
	NavigationSystemClass=FSoftClassPath(UNormalColaNavigationSystem::StaticClass());
	bStrictlyStatic=false; bAutoSpawnMissingNavData=true; bCreateOnClient=false;
}

UNormalColaNavigationSystem::UNormalColaNavigationSystem()
{
	SetCustomerAgent(204,1056);
}

void UNormalColaNavigationSystem::SetCustomerAgent(float Radius,float Height)
{
	Radius=FMath::Max(1.f,Radius);
	SupportedAgents.Reset();
	auto& Giant=SupportedAgents.Emplace_GetRef(Radius,FMath::Max(Height,Radius*2));
	Giant.bCanWalk=true;
	Giant.Name=TEXT("NormalColaGiant");
	Giant.DefaultQueryExtent=FVector(Radius+150,Radius+150,200);
	Giant.SetNavDataClass(ANormalColaNavigationData::StaticClass());
}

void UNormalColaNavigationSystem::Configure(const UNavigationSystemConfig& Config)
{
	Super::Configure(Config);
	// OverrideSupportedAgents selects agents from the service CDO; it cannot introduce
	// a new world-local agent. Configure the owned list before navigation initialization.
	if (const auto* Prototype=Cast<UNormalColaNavigationConfig>(&Config))
		SetCustomerAgent(Prototype->CustomerRadius,Prototype->CustomerHeight);
}

void UNormalColaNavigationSystem::OnWorldInitDone(FNavigationSystemRunMode Mode)
{
	Super::OnWorldInitDone(Mode);
	if ((Mode!=FNavigationSystemRunMode::GameMode && Mode!=FNavigationSystemRunMode::PIEMode) || GetNavigationBounds().IsEmpty()) return;
	auto* Data=Cast<ANormalColaNavigationData>(GetDefaultNavDataInstance(FNavigationSystem::DontCreate));
	if (!Data) return;
	// PIE can duplicate a partially completed editor mesh. Finish a fresh build
	// against this world's registered geometry before admitting customer visits.
	const double Started=FPlatformTime::Seconds();
	Build(); Data->EnsureBuildCompletion();
	UE_LOG(LogTemp,Display,TEXT("NORMAL_COLA_NAVIGATION_BOOTSTRAP tiles=%d duration_ms=%.1f"),
		Data->GetNumActiveTiles(),(FPlatformTime::Seconds()-Started)*1000);
}
