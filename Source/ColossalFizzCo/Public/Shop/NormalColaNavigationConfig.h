#pragma once
#include "CoreMinimal.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "NormalColaNavigationConfig.generated.h"

UCLASS(NotPlaceable)
class COLOSSALFIZZCO_API ANormalColaNavigationData:public ARecastNavMesh
{
	GENERATED_BODY()
public:
	ANormalColaNavigationData();
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
};

/** Owns the prototype's agents instead of filtering the project's default agent list. */
UCLASS()
class COLOSSALFIZZCO_API UNormalColaNavigationSystem:public UNavigationSystemV1
{
	GENERATED_BODY()
public:
	UNormalColaNavigationSystem();
	virtual void Configure(const UNavigationSystemConfig& Config) override;
	virtual void OnWorldInitDone(FNavigationSystemRunMode Mode) override;
private:
	void SetCustomerAgent(float Radius,float Height);
};

/** World-local agent profile for the existing giant customer; does not change project navigation settings. */
UCLASS(EditInlineNew)
class COLOSSALFIZZCO_API UNormalColaNavigationConfig:public UNavigationSystemModuleConfig
{
	GENERATED_BODY()
public:
	UNormalColaNavigationConfig(const FObjectInitializer& Initializer=FObjectInitializer::Get());
	UPROPERTY(EditAnywhere,Category="Prototype") float CustomerRadius=204;
	UPROPERTY(EditAnywhere,Category="Prototype") float CustomerHeight=1056;
};
