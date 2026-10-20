#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "Curves/CurveFloat.h"
#include "Engine/EngineTypes.h"
#include "RocketProjectileComponent.generated.h"

class UPrimitiveComponent;

/**
 * Lifecycle states for the rocket projectile simulation.
 */
UENUM(BlueprintType)
enum class ERocketSimulationState : uint8
{
	Idle UMETA(DisplayName = "0. Idle"),
	TippingOver UMETA(DisplayName = "1. Tipping"),
	Dragging UMETA(DisplayName = "2. Dragging"),
	Rocketing UMETA(DisplayName = "3. Rocketing / Flying"),
	LowFuelSputter UMETA(DisplayName = "4. Low Fuel Thrust"),
	SpentFalling UMETA(DisplayName = "5. Spent Falling"),
	AtRest UMETA(DisplayName = "6. At Rest")
};

/**
 * Axis identifier for defining which local axis represents the nozzle / cap of the mesh.
 */
UENUM(BlueprintType)
enum class ERocketMeshAxis : uint8
{
	PositiveZ UMETA(DisplayName = "+Z (Up)"),
	NegativeZ UMETA(DisplayName = "-Z (Down)"),
	PositiveX UMETA(DisplayName = "+X (Forward)"),
	NegativeX UMETA(DisplayName = "-X (Backward)"),
	PositiveY UMETA(DisplayName = "+Y (Right)"),
	NegativeY UMETA(DisplayName = "-Y (Left)")
};

/**
 * Pivot compensation mode for rotating around the true Center of Mass rather than an arbitrary mesh pivot.
 */
UENUM(BlueprintType)
enum class ERocketPivotCompensationMode : uint8
{
	AutoDetectFromBounds UMETA(DisplayName = "Auto-Detect From Bounds"),
	ManualOffset UMETA(DisplayName = "Manual Offset"),
	None UMETA(DisplayName = "None (Use Raw Pivot)")
};

/**
 * Replicated network state struct for deterministic flight and bounce reconciliation.
 */
USTRUCT(BlueprintType)
struct FRocketFlightNetState
{
	GENERATED_BODY()

	UPROPERTY()
	ERocketSimulationState State = ERocketSimulationState::Idle;

	UPROPERTY()
	uint8 LaunchSeed = 0;

	UPROPERTY()
	uint8 BounceCount = 0;

	UPROPERTY()
	float FuelDuration = 12.0f;

	UPROPERTY()
	float Speed = 2400.0f;

	UPROPERTY()
	bool bPhysicsSimulating = false;

	UPROPERTY()
	FVector_NetQuantize LaunchLocation = FVector::ZeroVector;

	UPROPERTY()
	FVector_NetQuantizeNormal LaunchDirection = FVector::UpVector;

	UPROPERTY()
	FVector_NetQuantize LaunchTargetHitLocation = FVector::ZeroVector;

	UPROPERTY()
	FVector_NetQuantize LastBounceLocation = FVector::ZeroVector;

	UPROPERTY()
	FVector_NetQuantize LastBounceVelocity = FVector::ZeroVector;

	UPROPERTY()
	FVector_NetQuantize RestLocation = FVector::ZeroVector;

	UPROPERTY()
	FRotator RestRotation = FRotator::ZeroRotator;

	UPROPERTY()
	float ServerLaunchTime = 0.0f;
};

class URocketProjectileComponent;

/**
 * Ultra-compact payload item (~8 bytes) for batching multiple rocket launches into a single high-speed network packet.
 */
USTRUCT(BlueprintType)
struct FRocketBatchLaunchItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Rocket")
	TObjectPtr<AActor> BottleActor = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Rocket")
	FVector_NetQuantizeNormal LaunchDirection = FVector::UpVector;

	UPROPERTY(BlueprintReadWrite, Category = "Rocket")
	uint8 LaunchSeed = 0;
};

// Delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRocketLaunched);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRocketRicochet, const FHitResult&, HitResult, const FVector&, NewVelocity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRocketHitTarget, AActor*, HitActor, const FHitResult&, HitResult);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRocketFuelDepleted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRocketSettled);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRocketStateChanged, ERocketSimulationState, NewState, ERocketSimulationState, PrevState);

/**
 * Movement settings for State 1: Tipping (Images 1 to 3: pivoting from upright to flat on the ground).
 */
USTRUCT(BlueprintType)
struct FRocketTippingSettings
{
	GENERATED_BODY()

	/** If true, the bottle will tip over from upright onto the ground before skidding/flying. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Tipping")
	bool bEnableTipping = true;

	/** Duration in seconds of the tip-over transition until the bottle lies flat on the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Tipping", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float TippingDuration = 0.22f;

	/** If true, automatically detects the ground contact point / edge on the floor as the pivot point (Purple Pivot). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Tipping")
	bool bAutoDetectFloorPivot = true;

	/** Manual local offset for the tipping pivot point if bAutoDetectFloorPivot is false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Tipping", meta = (EditCondition = "!bAutoDetectFloorPivot"))
	FVector ManualPivotOffset = FVector::ZeroVector;

	/** Subtle spiral/precession radius in cm around the pivot point as the thruster vents while tipping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Tipping", meta = (ClampMin = "0.0", ClampMax = "25.0"))
	float PrecessionSpiralRadius = 4.0f;

	/** Frequency in Hz of the precession wobble while tipping over. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Tipping", meta = (ClampMin = "1.0", ClampMax = "20.0"))
	float PrecessionWobbleFrequency = 8.0f;
};

/**
 * Movement settings for State 2: Dragging (Images 4 to 5: ground skidding, floor edge falloff, accelerating towards takeoff).
 */
USTRUCT(BlueprintType)
struct FRocketDraggingSettings
{
	GENERATED_BODY()

	/** If true, the bottle performs a floor drag/acceleration stage before liftoff. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Dragging")
	bool bEnableDragging = true;

	/** Minimum duration in seconds of the ground skid/drag phase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Dragging", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float MinDragDuration = 0.35f;

	/** Maximum duration in seconds of the ground skid/drag phase (seed-derived so identical on client and server). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Dragging", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float MaxDragDuration = 0.65f;

	/** Initial forward acceleration (cm/s^2) applied as the bottle begins skidding on the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Dragging", meta = (ClampMin = "100.0", ClampMax = "10000.0"))
	float InitialDragAcceleration = 800.0f;

	/** Maximum forward acceleration (cm/s^2) as thrust ramps up towards takeoff. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Dragging", meta = (ClampMin = "500.0", ClampMax = "20000.0"))
	float MaxDragAcceleration = 3500.0f;

	/** Exponent governing how thrust builds up during drag (1.0 = linear, 2.0 = quadratic curve). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Dragging", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float ThrustRampExponent = 1.4f;

	/** Distance in cm along the launch direction to the takeoff waypoint (Blue Circle in Image 4/5). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Dragging", meta = (ClampMin = "20.0", ClampMax = "500.0"))
	float TakeoffDistance = 140.0f;

	/** Sliding friction coefficient against the floor during drag (reduces lateral slip). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Dragging", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GroundFriction = 0.15f;

	/** Gravity scale applied if the bottle runs off the edge of a shelf/table with no floor underneath (Image 5). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Dragging", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float GravityScaleWithoutFloor = 1.0f;
};

/**
 * Movement settings for State 3: Rocketing / Flying.
 */
USTRUCT(BlueprintType)
struct FRocketFlightSettings
{
	GENERATED_BODY()

	/** Maximum speed of the rocket in cm/s when thrust curve evaluates to 1.0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Flight", meta = (ClampMin = "100.0", ClampMax = "10000.0"))
	float BaseSpeed = 2400.0f;

	/** Total duration of powered rocket flight in seconds before fuel is exhausted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Flight", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float MaxFuelDuration = 12.0f;

	/** Speed curve over flight time (X: normalized 0-1 time, Y: speed multiplier). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Flight")
	TObjectPtr<UCurveFloat> FuelThrustCurve;

	/** Gravity acceleration applied during flight in cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Flight")
	float GravityZ = -980.0f;

	/** Gravity scale while rocket thrust is active (0.0 = straight rocket, 1.0 = full gravity arc). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Flight", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float InFlightGravityScale = 0.15f;

	/** If true, flight performs a line trace and uses LaunchSeed to generate erratic offset waypoints (Image 1) so it does not fly in a straight line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Flight")
	bool bEnableErraticPath = true;

	/** Spacing in cm between random offset waypoints along the baseline flight ray (Yellow Circles in Image 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Flight", meta = (EditCondition = "bEnableErraticPath", ClampMin = "100.0", ClampMax = "800.0"))
	float WaypointSpacing = 300.0f;

	/** Minimum lateral deviation in cm away from the straight baseline ray. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Flight", meta = (EditCondition = "bEnableErraticPath", ClampMin = "5.0", ClampMax = "200.0"))
	float MinPathDeviation = 35.0f;

	/** Maximum lateral deviation in cm away from the straight baseline ray (Yellow Circles in Image 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Flight", meta = (EditCondition = "bEnableErraticPath", ClampMin = "10.0", ClampMax = "300.0"))
	float MaxPathDeviation = 90.0f;

	/** Frequency of cosmetic corkscrew wobble in Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Flight")
	float CosmeticWobbleFrequency = 20.0f;

	/** Amplitude of cosmetic corkscrew wobble in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Flight")
	float CosmeticWobbleDegrees = 4.0f;

	/** If true, aligns mesh rotation to velocity heading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Flight")
	bool bAlignRotationToVelocity = true;
};

/**
 * Movement settings for State 4: Low Fuel / Sputter (almost empty thrust upward struggle).
 */
USTRUCT(BlueprintType)
struct FRocketLowFuelSettings
{
	GENERATED_BODY()

	/** If true, near the end of fuel duration the bottle pitches upward and sputters as remaining fizz vents before falling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|LowFuel")
	bool bEnableLowFuelSputter = true;

	/** Duration in seconds at the end of flight where dying fizz sputter occurs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|LowFuel", meta = (ClampMin = "0.2", ClampMax = "5.0"))
	float LowFuelPhaseDuration = 1.8f;

	/** Maximum pitch-up angle in degrees away from horizontal during the dying fizz arc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|LowFuel", meta = (ClampMin = "15.0", ClampMax = "88.0"))
	float PitchUpAngle = 70.0f;

	/** Upward thrust impulse speed (cm/s) trying to lift the bottle as fizz vents. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|LowFuel", meta = (ClampMin = "0.0", ClampMax = "1200.0"))
	float UpwardThrustImpulse = 450.0f;

	/** Multiplier applied to cosmetic wobble during low-fuel sputter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|LowFuel", meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float WobbleMultiplier = 2.5f;
};

/**
 * Movement settings for State 5: Falling (Spent ballistic drop and ricochets).
 */
USTRUCT(BlueprintType)
struct FRocketFallingSettings
{
	GENERATED_BODY()

	/**
	 * Falling simulation mode (Image 2):
	 * If true: Full Chaos physics simulation with physics gravity and tumbling handoff.
	 * If false (default): Kinematic ballistic projectile trace with visual tumble and smooth ground proximity landing interpolation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Falling")
	bool bUsePhysicsSimulation = false;

	/** Distance above the floor (in cm) at which the falling bottle begins smoothly interpolating towards its deterministic landing rotation (Image 2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Falling", meta = (EditCondition = "!bUsePhysicsSimulation", ClampMin = "50.0", ClampMax = "600.0"))
	float GroundInterpDistance = 250.0f;

	/** Rotational tumble speed in degrees/second while falling high in the air. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Falling", meta = (EditCondition = "!bUsePhysicsSimulation", ClampMin = "30.0", ClampMax = "720.0"))
	float TumbleRate = 180.0f;

	/** If true, automatically switches to full Chaos physics simulation when fuel runs out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Falling")
	bool bHandoffToPhysicsOnDepleted = false;

	/** Gravity scale applied while falling spent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Falling", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float SpentGravityScale = 1.0f;

	/** If true, allows the bottle to bounce when spent until coming to rest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Falling")
	bool bBounceAfterFuelDepleted = true;

	/** Coefficient of restitution when spent so the bottle clatters and settles quickly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Falling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpentBounciness = 0.28f;

	/** Friction applied to horizontal sliding velocity on each bounce when spent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Falling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpentFriction = 0.5f;

	/** Maximum random deflection angle in degrees on bounces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Falling", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float RicochetRandomDeflection = 10.0f;

	/** Cooldown in seconds before another bounce on the same surface can register. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Falling", meta = (ClampMin = "0.01", ClampMax = "0.2"))
	float RicochetDebounceTime = 0.05f;
};

/**
 * Movement settings for State 6: At Rest (Settled).
 */
USTRUCT(BlueprintType)
struct FRocketAtRestSettings
{
	GENERATED_BODY()

	/** Velocity threshold (cm/s) below which a spent bottle settles to At Rest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|AtRest", meta = (ClampMin = "5.0", ClampMax = "200.0"))
	float RestVelocityThreshold = 45.0f;

	/** Time in seconds velocity must remain below threshold before locking at rest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|AtRest", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float SettlingLockDuration = 0.15f;

	/** If true, aligns the bottle flat against the ground normal when coming to rest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|AtRest")
	bool bAlignToGroundOnSettle = true;
};

/**
 * High-performance, multiplayer-replicated movement component for erratic rocket projectiles (such as shaken soda bottles).
 * Supports client-side prediction, deterministic launch seeds, fuel/thrust curves, and sweep-based ricochets.
 */
UCLASS(ClassGroup = (Movement), meta = (BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API URocketProjectileComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URocketProjectileComponent();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Deactivate() override;

public:
	// -------------------------------------------------------------------------
	// Target Component
	// -------------------------------------------------------------------------

	/** The primitive component (mesh or collision) that this component moves. Auto-resolves to Owner's RootComponent if null. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Setup")
	TObjectPtr<UPrimitiveComponent> TargetPrimitive;

	// -------------------------------------------------------------------------
	// Organized Movement Settings By State
	// -------------------------------------------------------------------------

	/** Comprehensive movement settings for State 1: Tipping (Images 1 to 3: pivoting from upright to flat on the ground). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 1 - Tipping")
	FRocketTippingSettings TippingSettings;

	/** Comprehensive movement settings for State 2: Dragging (Images 4 to 5: ground skidding, floor edge falloff, accelerating towards takeoff). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 2 - Dragging")
	FRocketDraggingSettings DraggingSettings;

	/** Comprehensive movement settings for State 3: Rocketing (in-flight thrust, base speed, fuel curve). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 3 - Rocketing")
	FRocketFlightSettings FlightSettings;

	/** Comprehensive movement settings for State 4: Low Fuel Thrust (exhaust sputter and upward arc). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 4 - Low Fuel Thrust")
	FRocketLowFuelSettings LowFuelSettings;

	/** Comprehensive movement settings for State 5: Falling (spent ballistic drop, Chaos physics handoff, ricochets). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 5 - Falling")
	FRocketFallingSettings FallingSettings;

	/** Comprehensive movement settings for State 6: At Rest (settling and locking flush on floor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 6 - At Rest")
	FRocketAtRestSettings AtRestSettings;

	// -------------------------------------------------------------------------
	// Setup & Backward-Compatible Properties
	// -------------------------------------------------------------------------

	/** If true, a standing bottle will perform an asymmetric kick/tilt so it doesn't thrust straight down into a shelf. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 1 - Tipping")
	bool bTipOverToFly = true;

	/** If true, calling ConfigureRocket() will automatically fire the rocket immediately on initialization (All-In-One mode). Default is false (Two-Stage launch). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | Setup")
	bool bLaunchOnInitialized = false;

	/** If true, launches will bundle Location, Direction, Seed, Fuel, and Speed in an All-In-One high-accuracy packet (~22 bytes).
	 * If false (default), uses the ultra-compact Two-Stage launch (7 bytes, only Time & Direction, no mid-air location snapping).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | Setup")
	bool bLaunchWithHighAccuracy = false;

	/** Maximum speed of the rocket in cm/s when FuelThrustCurve evaluates to 1.0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Rocket | State 3 - Rocketing", meta = (ClampMin = "100.0", ClampMax = "10000.0"))
	float BaseSpeed = 2400.0f;

	/** Total duration of rocket thrust in seconds before fizz is depleted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Rocket | State 3 - Rocketing", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float MaxFuelDuration = 12.0f;

	/** Gravity acceleration applied during flight (cm/s^2). Default is normal Unreal gravity (980 cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 3 - Rocketing")
	float GravityZ = -980.0f;

	/** Speed curve over flight time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 3 - Rocketing")
	TObjectPtr<UCurveFloat> FuelThrustCurve;

	/** Gravity multiplier while rocket thrust is active (0.0 = pure straight rocket, 1.0 = heavy arc). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 3 - Rocketing", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float InFlightGravityScale = 0.15f;

	/** If true, automatically switches to full Chaos physics simulation when fuel runs out rather than kinematic falling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 5 - Falling")
	bool bHandoffToPhysicsOnDepleted = false;

	/** If true, near the end of fuel duration the bottle pitches upward and sputters as remaining fizz vents before falling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 4 - Low Fuel Thrust")
	bool bExhaustSputterNearEnd = true;

	/** Duration in seconds at the end of flight where dying fizz sputter and upward arc occur. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 4 - Low Fuel Thrust", meta = (EditCondition = "bExhaustSputterNearEnd", ClampMin = "0.2", ClampMax = "5.0"))
	float ExhaustPhaseDuration = 1.8f;

	/** Maximum pitch-up angle in degrees away from horizontal during the dying fizz arc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 4 - Low Fuel Thrust", meta = (EditCondition = "bExhaustSputterNearEnd", ClampMin = "15.0", ClampMax = "88.0"))
	float ExhaustPitchUpAngle = 70.0f;

	/** Upward thrust impulse speed (cm/s) trying to lift the bottle as fizz vents. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 4 - Low Fuel Thrust", meta = (EditCondition = "bExhaustSputterNearEnd", ClampMin = "0.0", ClampMax = "1200.0"))
	float ExhaustUpwardThrust = 450.0f;

	/** Multiplier applied to cosmetic wobble frequency and amplitude during the exhaust sputter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 4 - Low Fuel Thrust", meta = (EditCondition = "bExhaustSputterNearEnd", ClampMin = "1.0", ClampMax = "5.0"))
	float ExhaustWobbleMultiplier = 2.5f;

	/** If true, the bottle will drop to the ground and skid in an expanding circle/spiral during the tilt phase before blasting off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 2 - Dragging", meta = (EditCondition = "bTipOverToFly"))
	bool bEnableGroundDragSpiral = true;

	/** Minimum duration in seconds of the ground skid/drag phase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 2 - Dragging", meta = (EditCondition = "bTipOverToFly && bEnableGroundDragSpiral", ClampMin = "0.1", ClampMax = "2.0"))
	float MinDragDuration = 0.35f;

	/** Maximum duration in seconds of the ground skid/drag phase. Seed-derived so it is identical on server and client. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 2 - Dragging", meta = (EditCondition = "bTipOverToFly && bEnableGroundDragSpiral", ClampMin = "0.1", ClampMax = "3.0"))
	float MaxDragDuration = 0.65f;

	/** Maximum radius in cm of the expanding circle as the bottle drags across the floor before taking off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 2 - Dragging", meta = (EditCondition = "bTipOverToFly && bEnableGroundDragSpiral", ClampMin = "5.0", ClampMax = "150.0"))
	float MaxDragRadius = 35.0f;

	/** Number of full revolutions the bottle spins around the ground circle during the drag phase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 2 - Dragging", meta = (EditCondition = "bTipOverToFly && bEnableGroundDragSpiral", ClampMin = "1.0", ClampMax = "12.0"))
	float DragSpinRevolutions = 4.0f;

	/** Duration in seconds of the initial tip-over / hop transition before full forward thrust engages. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 1 - Tipping", meta = (EditCondition = "bTipOverToFly", ClampMin = "0.01", ClampMax = "0.5"))
	float TipOverDuration = 0.08f;

	/** Upward impulse speed (cm/s in +Z) during the tip-over hop to clear the shelf/floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 1 - Tipping", meta = (EditCondition = "bTipOverToFly"))
	float TipOverHopSpeed = 150.0f;

	/** Minimum tilt angle in degrees away from vertical (Z-up) during the kick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 1 - Tipping", meta = (EditCondition = "bTipOverToFly", ClampMin = "10.0", ClampMax = "85.0"))
	float TipOverAngleMin = 35.0f;

	/** Maximum tilt angle in degrees away from vertical (Z-up) during the kick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket | State 1 - Tipping", meta = (EditCondition = "bTipOverToFly", ClampMin = "10.0", ClampMax = "85.0"))
	float TipOverAngleMax = 70.0f;

	// -------------------------------------------------------------------------
	// Mesh Orientation & Cosmetic Wobble
	// -------------------------------------------------------------------------

	/** Which local axis on the mesh corresponds to the cap / nozzle opening. Default is +Z (standard upright bottle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Orientation")
	ERocketMeshAxis NozzleAxis = ERocketMeshAxis::PositiveZ;

	/** If true, turns the target primitive so the nozzle points backwards (opposite to movement). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Orientation")
	bool bAlignRotationToVelocity = true;

	/** Frequency of the erratic cosmetic corkscrew wobble in Hz. (Client-side cosmetic only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Orientation")
	float CosmeticWobbleFrequency = 20.0f;

	/** Amplitude of the cosmetic corkscrew wobble in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Orientation")
	float CosmeticWobbleDegrees = 4.0f;

	// -------------------------------------------------------------------------
	// Pivot & Center of Mass Compensation
	// -------------------------------------------------------------------------

	/**
	 * Determines how the component handles mesh pivot offsets.
	 * AutoDetectFromBounds: Automatically queries the mesh bounding box to find the exact geometric center,
	 * ensuring the bottle rotates around its middle rather than a bottom pivot.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Pivot")
	ERocketPivotCompensationMode PivotCompensationMode = ERocketPivotCompensationMode::AutoDetectFromBounds;

	/** Custom local center of mass offset when PivotCompensationMode is set to ManualOffset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Pivot", meta = (EditCondition = "PivotCompensationMode == ERocketPivotCompensationMode::ManualOffset"))
	FVector ManualCenterOffset = FVector::ZeroVector;

	/** If true, dynamically adjusts height during the initial tip-over so no part of the mesh penetrates below the starting surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Pivot")
	bool bPreventGroundPenetrationOnTip = true;

	// -------------------------------------------------------------------------
	// Collision & Bouncing
	// -------------------------------------------------------------------------

	/** Collision channel used to sweep the rocket forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Collision")
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_WorldDynamic;

	/** Radius of the sphere sweep in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Collision", meta = (ClampMin = "1.0", ClampMax = "50.0"))
	float SweepRadius = 6.0f;

	/** Coefficient of restitution (bounciness) while rocketing. 1.0 = perfect elastic bounce, 0.0 = zero bounce. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Collision", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Bounciness = 0.75f;

	/** If false, the bottle will not bounce once fuel is depleted—it will stop dead on the very next surface it hits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Collision")
	bool bBounceAfterFuelDepleted = true;

	/** Lower coefficient of restitution when out of fuel so the bottle clatters and settles quickly rather than bouncing like rubber. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Collision", meta = (EditCondition = "bBounceAfterFuelDepleted", ClampMin = "0.0", ClampMax = "1.0"))
	float SpentBounciness = 0.28f;

	/** Friction applied to horizontal sliding velocity on each bounce when spent (0.0 = high friction/instant stop, 1.0 = frictionless). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Collision", meta = (EditCondition = "bBounceAfterFuelDepleted", ClampMin = "0.0", ClampMax = "1.0"))
	float SpentFriction = 0.5f;

	/** Maximum chaotic angle variation in degrees added when ricocheting off a surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Collision", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float RicochetRandomDeflection = 10.0f;

	/** Maximum number of ricochets before fizz/momentum is forced to exhaust (only enforced if bLimitFlightByMaxBounces is true). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Collision")
	int32 MaxBounces = 25;

	/** If true, reaching MaxBounces forces the rocket to cut fuel and transition to SpentFalling. If false, fuel duration strictly governs flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Collision")
	bool bLimitFlightByMaxBounces = false;

	/** Cooldown in seconds before another ricochet can register against the exact same surface (prevents multi-hit micro-bounces). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Collision", meta = (ClampMin = "0.01", ClampMax = "0.2"))
	float RicochetDebounceTime = 0.05f;

	/** Velocity threshold (cm/s) below which a spent bottle settles to AtRest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Collision")
	float RestVelocityThreshold = 45.0f;

	// -------------------------------------------------------------------------
	// Debug & Diagnostics
	// -------------------------------------------------------------------------

	/** If true, logs detailed diagnostic events (launches, bounces, state changes, replication, settling) to Output Log. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Debug")
	bool bEnableDebugLogs = true;

	/** If true, prints real-time status messages directly onto the screen (Server in Green, Client in Cyan). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Debug")
	bool bShowOnScreenDebug = true;

	/** If true, draws real-time 3D flight paths, waypoints, and target hit locations in the viewport (Server in Green, Client in Cyan). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rocket|Debug")
	bool bDrawDebugTrajectory = true;

	// -------------------------------------------------------------------------
	// Public API
	// -------------------------------------------------------------------------

	/**
	 * Configures rocket flight parameters (Fuel, Speed, Seed) prior to launch without launching (Two-Stage setup).
	 * Sets and replicates the parameters so subsequent lightweight launches know their fuel/speed/seed.
	 * If bLaunchOnInitialized is true, calling this will also automatically launch the rocket immediately!
	 */
	UFUNCTION(BlueprintCallable, Category = "Rocket")
	void ConfigureRocket(float InFuelDuration = -1.0f, float InSpeed = -1.0f, uint8 InSeed = 0, FVector InLaunchDirection = FVector::ZeroVector);

	/**
	 * Launches the rocket in the specified direction.
	 *
	 * @param InDirection Flight heading vector.
	 * @param bHighAccuracy If true, uses All-In-One instant launch (bundles Location, Direction, Seed, Fuel, Speed in one packet, ~22 bytes).
	 *                     If false (default), uses the ultra-efficient Two-Stage launch (Direction & Time only, ~7 bytes, no mid-air location snapping).
	 * @param InSeed Optional custom seed override (0 = use pre-configured seed).
	 * @param InFuelDuration Optional custom fuel duration override (-1 = use pre-configured fuel).
	 * @param InSpeed Optional custom base speed override (-1 = use pre-configured speed).
	 */
	UFUNCTION(BlueprintCallable, Category = "Rocket")
	void LaunchRocket(
		FVector InDirection,
		bool bHighAccuracy = false,
		uint8 InSeed = 0,
		float InFuelDuration = -1.0f,
		float InSpeed = -1.0f);

	/** Explicit helper to launch with All-In-One high accuracy (includes initial Location, Direction, Seed, Fuel, and Speed). */
	UFUNCTION(BlueprintCallable, Category = "Rocket")
	void LaunchRocketHighAccuracy(
		FVector InDirection,
		uint8 InSeed = 0,
		float InFuelDuration = -1.0f,
		float InSpeed = -1.0f);

	/**
	 * Stops the rocket.
	 * If bImmediate is true: instantly cancels all velocity, stops bouncing, and puts the bottle At Rest.
	 * If bImmediate is false: cuts thrust and lets the bottle drop under gravity with realistic spent bouncing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rocket")
	void StopRocket(bool bImmediate = false);

	/**
	 * Immediately switches the mesh to Simulate Physics and injects the rocket's current velocity.
	 * Fully replicated across all multiplayer clients so the bottle tumbles realistically as a physics prop.
	 *
	 * @param AdditionalImpulse Optional extra velocity impulse (e.g. from an explosion or hit).
	 * @param AngularVelocityInDegrees Optional rotational tumble velocity (if zero, applies a natural random tumble).
	 */
	UFUNCTION(BlueprintCallable, Category = "Rocket")
	void HandoffToPhysicsSimulation(FVector AdditionalImpulse = FVector::ZeroVector, FVector AngularVelocityInDegrees = FVector::ZeroVector);

	/** Gets the current state of the rocket simulation. */
	UFUNCTION(BlueprintPure, Category = "Rocket")
	ERocketSimulationState GetSimulationState() const { return CurrentState; }

	/** Gets the current linear velocity vector of the rocket. */
	UFUNCTION(BlueprintPure, Category = "Rocket")
	FVector GetRocketVelocity() const { return CurrentVelocity; }

	/** Gets remaining fuel as a normalized ratio (1.0 = Full, 0.0 = Empty). */
	UFUNCTION(BlueprintPure, Category = "Rocket")
	float GetNormalizedRemainingFuel() const;

	/** Gets elapsed powered rocket flight time in seconds (stops counting when fuel is depleted). */
	UFUNCTION(BlueprintPure, Category = "Rocket")
	float GetElapsedFlightTime() const { return ElapsedFlightTime; }

	/** Gets total air time in seconds since rocket launch (continues counting through falling until at rest). */
	UFUNCTION(BlueprintPure, Category = "Rocket")
	float GetTotalAirTime() const { return TotalAirTime; }

	/** Gets the total number of physical ricochets/bounces counted so far. */
	UFUNCTION(BlueprintPure, Category = "Rocket")
	int32 GetBounceCount() const { return CurrentBounceCount; }

	/** Resets all internal simulation variables, velocities, timers, and stops ticking so this component is clean when pooled or reused. */
	UFUNCTION(BlueprintCallable, Category = "Rocket")
	void ResetRocketSimulation();

	/** Prints comprehensive debug diagnostics to the Output Log and on-screen. */
	UFUNCTION(BlueprintCallable, Category = "Rocket|Debug")
	void PrintRocketDebugInfo() const;

	/** Replicated random seed assigned once at spawn/initialization for deterministic simulation without MTU overhead. -1 = uninitialized. */
	UPROPERTY(ReplicatedUsing = OnRep_LaunchSeed, EditAnywhere, BlueprintReadWrite, Category = "Rocket|Network")
	int32 LaunchSeed = -1;

	UFUNCTION()
	void OnRep_LaunchSeed();

	/** Launches multiple rockets simultaneously in a single network batch. */
	UFUNCTION(BlueprintCallable, Category = "Rocket")
	static void LaunchRocketBatch(const TArray<URocketProjectileComponent*>& InRockets, const TArray<FVector>& InDirections, float InFuelDuration = -1.0f);

	/** Applies client launch initialization without visual popping/snapping, using speed catch-up. */
	void ApplyClientLaunch(
		float InServerLaunchTime,
		const FVector& InLaunchDirection,
		bool bSetStartingLocation = false,
		const FVector& InLaunchLocation = FVector::ZeroVector,
		uint8 InSeed = 0,
		float InFuelDuration = -1.0f,
		float InSpeed = -1.0f);

	/** Returns network-synchronized server world time in seconds. */
	UFUNCTION(BlueprintPure, Category = "Rocket")
	float GetSynchronizedServerTime() const;

	// -------------------------------------------------------------------------
	// Event Delegates
	// -------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Rocket|Events")
	FOnRocketLaunched OnRocketLaunched;

	UPROPERTY(BlueprintAssignable, Category = "Rocket|Events")
	FOnRocketRicochet OnRocketRicochet;

	UPROPERTY(BlueprintAssignable, Category = "Rocket|Events")
	FOnRocketHitTarget OnRocketHitTarget;

	UPROPERTY(BlueprintAssignable, Category = "Rocket|Events")
	FOnRocketFuelDepleted OnRocketFuelDepleted;

	UPROPERTY(BlueprintAssignable, Category = "Rocket|Events")
	FOnRocketSettled OnRocketSettled;

	UPROPERTY(BlueprintAssignable, Category = "Rocket|Events")
	FOnRocketStateChanged OnRocketStateChanged;

protected:
	// -------------------------------------------------------------------------
	// Internal State & Simulation
	// -------------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket|State")
	ERocketSimulationState CurrentState = ERocketSimulationState::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket|State")
	FVector CurrentVelocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket|State")
	float ElapsedFlightTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket|State")
	float TotalAirTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rocket|State")
	int32 CurrentBounceCount = 0;

	/** Replicated state for client reconciliation */
	UPROPERTY(ReplicatedUsing = OnRep_FlightNetState)
	FRocketFlightNetState FlightNetState;

	UFUNCTION()
	void OnRep_FlightNetState();

	void SetSimulationState(ERocketSimulationState NewState);
	void PerformSimulationStep(float DeltaTime);
	void ProcessTippingStep(float DeltaTime);
	void ProcessDraggingStep(float DeltaTime);
	void ProcessRocketStep(float DeltaTime);
	void ProcessSpentFallingStep(float DeltaTime);

	/** Legacy alias for backward compatibility */
	void ProcessTipOverStep(float DeltaTime) { ProcessTippingStep(DeltaTime); }

	void InitializeTipping(const FVector& InLaunchHeading);
	void InitializeDragging(float InServerLaunchTime, int32 InSeed);
	void InitializeFlightPath(const FVector& InStartLoc, const FVector& InFlightDir, int32 InSeed);
	void EvaluateFlightSpline(float Progress, FVector& OutLocation, FVector& OutTangent) const;
	FRotator ComputeGroundRestRotation(const FVector& GroundNormal, float InSeedYaw) const;
	void AdvanceToNextFlightLeg();
	void TransitionToRocketing(const FVector& InLiftoffCenter);
	void DrawRocketDebugVisuals(float DeltaTime);

	bool SweepAndMove(const FVector& MoveDelta, FHitResult& OutHit);
	void HandleRicochet(const FHitResult& Hit);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_HandoffToPhysicsSimulation(FVector FinalLinearVelocity, FVector FinalAngularVelocity);

	void ApplyPhysicsHandoff(const FVector& FinalLinearVelocity, const FVector& FinalAngularVelocity);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SettleRocket(FVector InRestLocation, FRotator InRestRotation);

	/** Direct, reliable multicast RPC that launches this individual rocket on all clients (Two-Stage: ~8 bytes, Time, Direction, Seed, no location snapping). */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_LaunchRocket(float InServerLaunchTime, FVector_NetQuantizeNormal InLaunchDirection, uint8 InSeed);

	/** All-In-One High-Accuracy launch multicast RPC (~22 bytes). Transmits Location, Direction, Seed, Fuel, and Speed. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_LaunchRocketFull(
		float InServerLaunchTime,
		FVector_NetQuantize InLaunchLocation,
		FVector_NetQuantizeNormal InLaunchDirection,
		uint8 InSeed,
		float InFuelDuration,
		float InSpeed);

	/** Multicast to configure rocket parameters on clients ahead of time (Two-Stage setup). */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ConfigureRocket(float InFuelDuration, float InSpeed, uint8 InSeed);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_BatchLaunchRockets(float InServerLaunchTime, const TArray<FRocketBatchLaunchItem>& BatchItems);

	/** Secondary reliable multicast RPC sending the server's pre-traced multi-bounce flight plan (8-16 rounded integer waypoints). Sent as normal data, not in MTU launch packet. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SendFlightPlan(const TArray<FVector_NetQuantize>& InFlightNodes);

	/** Pre-calculates 8-16 line-trace bounce and trace-end waypoints on the server, rounded to integer coordinates. */
	void GenerateServerFlightPlan(TArray<FVector_NetQuantize>& OutNodes);

	/** Stores and activates the authoritative rounded waypoint array for rocketing on both server and client. */
	void SetAuthoritativeFlightPlan(const TArray<FVector_NetQuantize>& InFlightNodes);

	void LogDebug(const FString& Message, bool bOnScreen = false) const;

	FRotator ComputeTargetRotation(const FVector& FlightDirection, float DeltaTime) const;
	FVector GetNozzleLocalDirection() const;

	void AutoResolveTargetPrimitive();
	void CacheMeshBoundsAndCenter();
	FVector GetEffectiveCenterOffset() const;
	FVector GetWorldCenterLocation() const;
	void SetTargetTransformAroundCenter(const FVector& NewCenterLocation, const FRotator& NewRotation);

	/** Computes the oriented bounding extent along any world direction (SAT theorem) to detect the true leading edge. */
	float GetProjectedExtentAlongDirection(const FVector& Direction) const;

private:
	FRandomStream RandomStream;
	FVector TipOverLaunchHeading = FVector::ForwardVector;
	float TipOverElapsedTime = 0.0f;

	// Pivot & center-of-mass tracking
	FVector CalculatedCenterOffset = FVector::ZeroVector;
	FVector MeshBoundsExtent = FVector::ZeroVector;
	float InitialGroundZ = 0.0f;

	// Ricochet debounce & same-surface micro-bounce tracking
	TWeakObjectPtr<UPrimitiveComponent> LastHitComponent = nullptr;
	FVector LastHitNormal = FVector::ZeroVector;
	float TimeSinceLastBounce = 100.0f;

	// Diagnostics & settling tracking
	float DebugHeartbeatTimer = 0.0f;
	float TravelHistoryLogTimer = 0.0f;
	float PhysicsSettledTimeAccumulated = 0.0f;

	// Client-side visual smoothing / interpolation
	FVector ClientReconciliationTarget = FVector::ZeroVector;
	bool bClientReconciling = false;

	// Server replication tracking & periodic straight-flight sync
	float LastReplicatedServerLaunchTime = -1.0f;
	float NetSyncTimer = 0.0f;

	// Cached settings to restore when simulation ends
	bool bHasSavedSettings = false;
	bool bSavedSimulatePhysics = false;
	bool bSavedEnableGravity = false;
	EComponentMobility::Type SavedMobility = EComponentMobility::Movable;
	FName SavedCollisionProfileName = NAME_None;
	ECollisionEnabled::Type SavedCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	FCollisionResponseContainer SavedCollisionResponses;
	bool bSavedReplicateMovement = true;

	void SavePrimitiveSettings();
	void RestorePrimitiveSettings();

	// Batch launch buffering
	static void QueueBatchLaunchItem(URocketProjectileComponent* Component, const FRocketBatchLaunchItem& Item);
	static void FlushBatchLaunchForWorld(TWeakObjectPtr<UWorld> WeakWorld);

	// Smooth client catch-up without visual snapping
	float ClientCatchupDuration = 0.0f;
	float ClientCatchupTimeRemaining = 0.0f;
	float ClientCatchupSpeedMultiplier = 1.0f;
	FVector ClientReconciliationOffset = FVector::ZeroVector;

	// Fixed time-step accumulator for deterministic 60Hz physics sub-stepping
	float SimulationTimeAccumulator = 0.0f;

	// Deferred client launch if RPC arrives before initial LaunchSeed property replicates
	bool bPendingLaunch = false;
	float PendingServerLaunchTime = 0.0f;
	FVector PendingLaunchDirection = FVector::ForwardVector;

	// State 1: Tipping tracking (Images 1 to 3)
	FVector TippingPivotPoint = FVector::ZeroVector;
	FVector TippingStartCenter = FVector::ZeroVector;
	FRotator TippingStartRotation = FRotator::ZeroRotator;
	FVector TippingTargetCenter = FVector::ZeroVector;
	FRotator TippingTargetRotation = FRotator::ZeroRotator;
	FVector TippingFloorHeading = FVector::ForwardVector;
	FVector TippingFloorNormal = FVector::UpVector;
	float TippingElapsedTime = 0.0f;
	float TotalTippingDuration = 0.22f;

	// State 2: Dragging tracking (Images 4 to 5)
	void InitializeGroundDrag(float InServerLaunchTime, int32 InSeed) { InitializeDragging(InServerLaunchTime, InSeed); }
	FVector DragTakeoffPoint = FVector::ZeroVector;
	FVector DragStartCenter = FVector::ZeroVector;
	FVector DragCenterLocation = FVector::ZeroVector;
	FVector DragSurfaceNormal = FVector::UpVector;
	FVector DragTangentU = FVector::ForwardVector;
	FVector DragTangentV = FVector::RightVector;
	float TotalDragDuration = 0.45f;
	float DragElapsedTime = 0.0f;
	float DragStartAngleRad = 0.0f;
	float InitialGroundZOffset = 6.0f;
	float DragCurrentSpeed = 0.0f;

	// State 3: Flight path tracking (Image 1)
	FVector FlightStartLocation = FVector::ZeroVector;
	FVector FlightBaselineDir = FVector::ForwardVector;
	float FlightBaselineLength = 2400.0f;
	float FlightDistanceTraveled = 0.0f;
	TArray<FVector> FlightWaypoints;
	FVector LaunchTargetHitLocation = FVector::ZeroVector;
	bool bHasLaunchTargetHit = false;
	int32 FlightLegIndex = 0;

	// Pre-calculated authoritative multi-bounce flight plan (8-16 rounded integer waypoints)
	TArray<FVector> AuthoritativeFlightNodes;
	TArray<float> CumulativeFlightPlanDistances;
	int32 CurrentFlightPlanNodeIndex = 0;

	// State 5: Falling tracking (Image 2)
	float FallingTumbleAngle = 0.0f;
	bool bTargetRestRotationInitialized = false;
	FRotator DeterministicRestRotation = FRotator::ZeroRotator;

	// Trajectory debug visualization
	FVector LastDebugTrailLocation = FVector::ZeroVector;
};
