# Original bottle rocket flight — 12 November 2026

## Current designer decision

The designer explicitly chose **the original BP_Cola rocket flight for now**,
after clarification that the existing pressure launch already used Rocket
Projectile Component's separate Chaos-thrust mode. This is a temporary prototype
flight-mode change. Normal-only contents, plastic bottles/manual caps, current
world sizes, canonical pressure/content accounting and server authority remain.
The earlier physics-driven-flight requirement remains a later option rather
than the prototype's current powered movement path.

## Implementation checkpoint

`AColaPropActor::bUseOriginalRocketFlight` defaults true and selects the shared
original launcher. Pressure buildup still gates on strict tolerance exceedance;
the existing managed cap is ejected once and keeps its identity. The launcher
uses the canonical ASM root, the existing tipping/dragging/erratic-flight code
and the component's launch/flight-plan replication. Clearing the option retains
the previous pressure-physics mode. No Blueprint event rewiring is needed.

Both powered modes consume canonical liquid and vent pressure through the same
release ledger. Original flight does not also receive Chaos jet forces. If
liquid or agitation pressure is exhausted before the component finishes its
path, its existing momentum-preserving physics handoff ends powered movement;
the falling body then uses authoritative root replication. Held/active flight,
ISM identity, volume residency, settlement and pool-reset guards remain.

Original launch clears previous pressure-physics flags on server and receiving
clients. Snapshot fallback now passes the component's replicated distance in
centimetres, rather than accidentally treating fuel duration in seconds as a
path length. No replicated schema extension was added.

The new regression exposed an existing movement conflict: setting the prop In
Motion re-enabled Chaos during original projectile launch. The launcher now
disables simulation/gravity after this state change, matching its client path.
The component owns powered movement without competing physics integration.
Settlement restores the product's gravity setting for ASMs retained by an
activation volume. Collision reporting uses cached body mass while the original
flight is kinematic, avoiding an invalid simulated-mass query.

## Validation on final Editor build 11207

- Native Editor build succeeds. Final build log:
  `Saved/Logs/OriginalBottleRocketBuild_11207_MassFix.log`.
- Full native suite: **57/58 pass** (53 clean, four warning-bearing), one failed,
  zero not run. Report: `Saved/Automation/OriginalBottleRocketFinal_11207/index.json`;
  log: `Saved/Logs/OriginalBottleRocketFinal_11207.log`.
- **OriginalBottleRocket passes with zero warnings**. It exercises a real
  prototype lemonade bottle and its physical cap, switching from prior physics
  flight without pooling, root movement, deterministic launch parameters,
  canonical content conservation, empty-fuel physics handoff, stable ISID/ISM
  slot, receiving snapshot binding, client authority rejection and pool reset.
- **PlasticPressure passes** with its two existing collision/simulation warnings;
  it explicitly selects the retained Chaos mode. All four activation-volume
  lifecycle tests still pass.
- The only failure is the existing **LiquidRepresentationContinuity** assertion
  at `Private/Tests/ColaLiquidIntegrationTests.cpp:69`, which expects an exact
  artist-configurable liquid body height of 186.58856. No liquid material,
  dimensions or assertion was changed in this task.
- Other warning-bearing tests are AuthoredBottleGrabRelease (existing animation
  thread-safety warning), BottlePawnPhysics and ManualWheelRotator (existing
  collision/simulation warnings).

The receiving snapshot fixture swaps actor roles within a native test world;
it is not a real network connection. The optional prototype pressure driver now
observes the selected mode, but was not run this task. Visual flight feel, actual
remote peers, active-flight late join, adverse networking and packaging remain
pending. The separately known full-map remote startup `ReliableBufferOverflow`
still blocks remote acceptance. Older physical-flight results do not accept the
new default. The packaged 11193 candidate has not been rebuilt.

## Saved state and use

Unreal was closed normally after the designer saved and paused input. The final
11207 editor was reopened on the same saved
`/Game/NormalColaPrototype/Lvl_NormalColaPrototype_Validation`, with PIE stopped.
The live BP_Cola default **bUseOriginalRocketFlight = true** is verified. Play
settings remain two players, listen server, one process, no automatic mouse
capture. Verification: `Saved/OriginalBottleRocket_20261112/EditorOriginalRocketReady.json`.

All 80 pre-test `.sav` files from `BeforeTests` were restored before reopening,
with zero hash mismatches in `RestoredSaveHashes.json`. Backups, scripts and
editor-state records are in `Saved/OriginalBottleRocket_20261112`. The user's
saved vehicle-material edits and existing worktree are preserved. No Blueprint
graph rewiring, map layout edit, Git commit or push occurred in this task.

In a bottle actor's defaults, **Cola > Pressure > Rocket > Use Original Rocket
Flight** selects the current behavior. Clearing it selects the previous Chaos
pressure-thrust mode. Strict pressure tolerance and cap requirements still apply;
this setting does not directly trigger a launch.

## Modified source and documentation

- `Public/Props/ColaPropActor.h`: editable mode selector and shared launcher.
- `Private/Props/ColaPropActor.cpp`: original launch, canonical root/content,
  settlement gravity and kinematic collision mass reads.
- `Private/Props/ColaPressure.cpp`: mode selection and shared discharge accounting.
- `Public/Components/RocketProjectileComponent.h`: shared powered-phase query.
- `Private/Components/RocketProjectileComponent.cpp`: mode reuse, movement
  ownership and replicated-distance fallback correction.
- `Private/Components/RocketPressurePhysics.cpp`: shared end-of-propulsion handoff.
- `Private/Tests/OriginalBottleRocketTests.cpp`: new integration regression.
- `Private/Tests/PlasticPressureTests.cpp`: retained physical-mode coverage.
- `Private/Tests/NormalColaPrototypePressure.cpp`: mode-aware optional test driver.

Source paths above are relative to `Source/ColossalFizzCo`. Documentation is
updated in this record, `GDD_Master.md`,
`Prototype_Plastic_Bottle_Pressure_2026-11-11.md`,
`Normal_Cola_Prototype_Play_Guide.md`, `IMPLEMENTATION_HANDOFF.md` and `README.md`.
