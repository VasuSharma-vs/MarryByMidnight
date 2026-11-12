#pragma once

#include "CoreMinimal.h"
#include "Props/InteractableProp.h"
#include "Components/RocketProjectileComponent.h"
#include "Props/ColaPricingLibrary.h"
#include "Props/ColaLiquidMotion.h"
#include "ColaPropActor.generated.h"

class UStaticMeshComponent;
class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class URocketProjectileComponent;
class UShopProductionStationComponent;

USTRUCT(BlueprintType)
struct FColaRocketLaunchConfiguration
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 Seed = 0;
	UPROPERTY(BlueprintReadOnly) float Distance = 0;
	UPROPERTY(BlueprintReadOnly) float Speed = 0;
	UPROPERTY(BlueprintReadOnly) float Acceleration = 0;
	UPROPERTY(BlueprintReadOnly) FVector Direction = FVector::UpVector;
};

UENUM(BlueprintType)
enum class ECapRemovalOperation : uint8
{
	EjectPhysics UMETA(DisplayName = "Eject with Physics Impulse"),
	DropQuietly  UMETA(DisplayName = "Drop Quietly")
};

/**
 * Native C++ Base Class for Cola Bottles (BP_Cola, BP_SpicyCola, BP_SugerCrushCola).
 * Handles portion tracking, liquid material parameter driving, foam pressure & agitation physics,
 * quadratic underfill penalty, cap ejection, and erratic rocket launches.
 */
UCLASS(Blueprintable, ClassGroup = (Props))
class MARRYBYMIDNIGHT_API AColaPropActor : public AActivePropActor
{
	GENERATED_BODY()

public:
	AColaPropActor();
	virtual void OnApplySharedDefinitions(const FPropProductDefinition* ProductDef, const FPropContentDefinition* ContentDef) override;
	virtual void ApplyUnsettlingImpulse(const FVector& Impulse) override;
	/** Fallback for bottle definitions with invalid empty mass. Never changes mesh scale. */
	UPROPERTY(EditAnywhere, Category="Cola|Physics", meta=(ClampMin="0.001", Units="kg")) float DefaultBottleMassKg = .05f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cola|Physics") float BottleMass = .05f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cola|Physics") float ContentMass = 0;
	void UpdateBottleMass();
	/** Impact agitation was tuned against a 5 kg body; preserve that response when mass changes. */
	UPROPERTY(EditAnywhere, Category="Cola|Physics", meta=(ClampMin="0.1", Units="kg")) float ImpactReferenceMassKg = 5.f;
	FVector NormalizePhysicalContactImpulse(const FVector& Impulse) const;
	FColaLiquidMotion LiquidMotion;
	void AdvanceLiquidVisuals(float DeltaSeconds, const FTransform& Transform);
	virtual bool CanDemoteToISM() const override;
	UFUNCTION(BlueprintPure, Category="Cola|Economy") FColaPriceQuote GetSaleQuote() const;
	virtual void ResetTransientPoolState() override;
	virtual void ResetForInstanceBinding() override;
	virtual void OnApplyEventData(const FPropEventUpdateData& Data, uint8 DirtyMask = 0xFF) override;
	virtual void OnApplyContinuousData(const FPropContinuousUpdateData& Data, float NewStrength=1, bool bStrengthChanged=false) override;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure", meta=(ClampMin="0", ClampMax="1")) float PressureMorphScale = .65f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure", meta=(ClampMin="0.1")) float PressureMorphInterpolationSpeed = 6.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cola|Pressure") float SmoothedPressureExpansion = 0;
	virtual void ApplyInitialImpactFromPromotion(FVector Location, const FVector& Impulse, AActor* InstigatorActor, int32 EventID = 0) override;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cola|Content") bool SetBottleContentType(int32 NewContentID);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cola|Liquid") float FillBottleAtRate(float Rate, float DeltaSeconds);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cola|Content") void RemoveBottleContent();
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cola|Failure") bool SubmitFailureRequest(const FContainmentFailureRequest& Request);
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Cola|Failure") EContainmentFailureType AcceptedFailureType = EContainmentFailureType::None;
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Cola|Pressure") float InteractionPressure = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cola|Pressure") float PressureDecayRate = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cola|Pressure") float AtmosphereVentRate = 4.0f;
	FTimerHandle CapDebrisTimer;
	UPROPERTY(EditAnywhere, Category="Cola|Debug") bool bDebugBottleImpacts = false;
	TSet<int32> ProcessedImpactIDs;
	double LastImpactTime = -1;
	void ApplyBottleImpact(const FVector& Impulse, int32 EventID);
	bool UsesPlasticPressure() const;
	float GetBottlePressure() const;
	float GetBottlePressureTolerance() const;
	void AddAgitationPressure(float Amount);
	void UpdatePlasticPressure(float DeltaSeconds);
	bool StartPlasticPressureRelease();
	/** Current prototype choice: reuse BP_Cola's original projectile flight. False selects Chaos thrust. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cola|Pressure|Rocket")
	bool bUseOriginalRocketFlight = true;
	bool LaunchOriginalBottleRocket(bool bHighAccuracy = false);
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Rocket", meta=(ClampMin="1", Units="cm")) float RocketMinDistance = 2000.f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Rocket", meta=(ClampMin="1", Units="cm")) float RocketMaxDistance = 8000.f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Rocket", meta=(ClampMin="1")) float RocketMinSpeed = 1500.f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Rocket", meta=(ClampMin="1")) float RocketMaxSpeed = 4500.f;
	/** Zero chooses a new server seed per event; a positive seed repeats the same configuration. */
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Rocket", meta=(ClampMin="0")) int32 RocketRandomSeed = 0;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Rocket", meta=(ClampMin="0")) float RocketAcceleration = 1600.f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Rocket", meta=(ClampMin="0", ClampMax="89", Units="Degrees")) float RocketDirectionVariation = 25.f;
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Cola|Pressure|Rocket") FColaRocketLaunchConfiguration RocketLaunchConfiguration;
	void UpdateOriginalRocketDischarge(float DeltaSeconds);
	TWeakObjectPtr<UShopProductionStationComponent> CapHoldingStation;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure") float ImpactPressureGain = .4f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure") float ShakePressureGain = .4f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure") float JetLiquidLevelPerSecond = .15f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure") float JetAccelerationPerPressure = 1600.f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure") float JetPressureVentRate = .4f;


protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	// -------------------------------------------------------------
	// Components
	// -------------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<URocketProjectileComponent> RocketComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CapMeshComponent;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Rendering")
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterialInstance;

	// -------------------------------------------------------------
	// Liquid & Portions
	// -------------------------------------------------------------
	/** Current fill level (0.0 = empty, 1.0 = standard full, > 1.0 = overfilled up to 2.0) */
	UPROPERTY(ReplicatedUsing = OnRep_ColaLevel, EditAnywhere, BlueprintReadWrite, Category = "Cola|Liquid")
	float ColaLevel = 1.0f;

	UFUNCTION()
	virtual void OnRep_ColaLevel();

	/** Stability tolerance threshold before bottle detonates into a rocket (default 2.0, SugarCrush 1.8, Spicy 1.6) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cola|Liquid")
	float StabilityTolerance = 2.0f;

	/** Base price for selling a full bottle */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cola|Economy")
	float BasePrice = 10.0f;

	// -------------------------------------------------------------
	// Agitation & Foam
	// -------------------------------------------------------------
	/** Current foam pressure from shaking / dropping (0.0 to 10.0) */
	UPROPERTY(ReplicatedUsing = OnRep_FoamPressure, VisibleAnywhere, BlueprintReadOnly, Category = "Cola|Foam")
	float FoamPressure = 0.0f;

	UFUNCTION()
	virtual void OnRep_FoamPressure();

	/** G-force jerk threshold to register bottle shaking/agitation while held */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cola|Foam")
	float AgitationJerkThreshold = 1.5f;

	/** Multiplier applied to foam surge when bottle is overfilled (> 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cola|Foam")
	float OverfillSurgeMultiplier = 3.0f;

	/** Rate of foam dissipation when stationary (FInterpTo speed) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cola|Foam")
	float FoamDecayRate = 1.2f;

	/** Impulse threshold on OnComponentHit to trigger impact agitation surge */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cola|Foam")
	float DropImpactThreshold = 2500.0f;

	// -------------------------------------------------------------
	// Gameplay Functions
	// -------------------------------------------------------------
	virtual bool OnPlayerGrab_Implementation(USceneComponent* AttachToParent, FName SocketName = NAME_None) override;

	/** Adds liquid to bottle, updates morph targets and checks for detonation */
	UFUNCTION(BlueprintCallable, Category = "Cola|Liquid")
	virtual void FillBottle(float FillStrength);

	/** Computes agitation from velocity jerk / delta acceleration, updates foam & morph targets */
	UFUNCTION(BlueprintCallable, Category = "Cola|Foam")
	virtual void UpdateFoamPressure(float DeltaSeconds);

	/** Launches bottle into erratic zigzag rocket flight */
	UFUNCTION(BlueprintCallable, Category = "Cola|Rocket")
	virtual void LaunchBottle();

	/** Attaches bottle cap mesh (PID determines style/material) */
	UFUNCTION(BlueprintCallable, Category = "Cola|Cap")
	virtual void AddCap(int32 InProductID);

	/** Detaches cap, adds physical impulse upwards and destroys after delay */
	UFUNCTION(BlueprintCallable, Category = "Cola|Cap")
	virtual void RemoveCap(float RemovingStrength = 1.0f, ECapRemovalOperation Operation = ECapRemovalOperation::EjectPhysics);

	/** Compatibility query returning the difference from the configured linear target-volume quote. */
	UFUNCTION(BlueprintPure, Category = "Cola|Economy")
	virtual float CalculateUnderfillPenalty(float TargetFill = 1.0f) const;

	/** Returns the configured portion-price quote with temperature and expiry rules. */
	UFUNCTION(BlueprintPure, Category = "Cola|Economy")
	virtual float CalculateSalePayout(float TargetFill = 1.0f) const;

	/** Safely updates dynamic material instance on material slot 1 of SkeletalMeshComponent */
	UFUNCTION(BlueprintCallable, Category = "Cola|Rendering")
	virtual void UpdateDynamicMaterial(bool bUpdateVelocity);

	/** Updates SkeletalMesh morph targets ("EmptyLevel" and "OverfullLevel") */
	UFUNCTION(BlueprintCallable, Category = "Cola|Rendering")
	virtual void UpdateMorphTargets();

	UFUNCTION()
	virtual void HandleOnBecomeActive(AActivePropActor* Actor, int32 InISID, int32 InProductID, FTransform ActiveTransform);

	UFUNCTION()
	virtual void HandleOnBeforeReturnToRest(AActivePropActor* Actor, int32 InISID, int32 InProductID, FTransform RestTransform);

protected:
	/** Hit event handler for drop impacts */
	UFUNCTION()
	virtual void HandleBottleComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	/** Rocket projectile delegate handlers */
	UFUNCTION()
	virtual void HandleRocketHitTarget(AActor* HitActor, const FHitResult& HitResult);

	UFUNCTION()
	virtual void HandleRocketFuelDepleted();

	UFUNCTION()
	virtual void HandleRocketSettled();

private:
	float LiquidHeightCm = 150.f;
	float LiquidRadiusCm = 35.f;
	float LiquidResponseGain = 1.f;
	float LiquidPlaneOffset = 0;
	FVector LastLiquidLocalNormal = FVector::ZeroVector;
	float PortionVolumeMl = 20.f;
	float ContentDensityGPerMl = 1.04f;
	float LastLiquidFill = -1;
	bool bMorphInitialized = false;
	FGuid PressureReleaseEpisode;
	float PressurePublishElapsed = 0;
	float LastPublishedPressure = 0;
	float RocketInitialContentLevel = 0;
	float RocketInitialPressure = 0;
	FVector PrevLocation = FVector::ZeroVector;
	FVector PrevVelocity = FVector::ZeroVector;
	FRotator PrevRotation = FRotator::ZeroRotator;
	float PrevGForce = 0.0f;
	int32 WarmupFramesRemaining = 0;
	float HeldGracePeriod = 0.0f;
	bool bIsFirstActiveFrame = true;
	bool bWasHeldLastFrame = false;
};
