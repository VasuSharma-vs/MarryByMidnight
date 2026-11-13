# Bottle systems update — 12–13 November 2026

## Active work and approved scope

The native change set is saved and builds as Editor **11208**. Final full suite:
**61/62 pass** (56 clean, five warning-bearing), one pre-existing liquid-height
assertion failure, zero not run. Screen control was stopped by the designer's
physical Escape key during the final rendered check. No further Computer Use or
implementation followed; only checkpointing and restoration. Preserve current
world scales and the extensive existing worktree. No Git commit/push occurred.

Tasks: canonical liquid depletion and empty visibility in ASM/ISM; independent
empty/content mass; checkout open/refresh flicker; projectile-to-physics handoff
at apex or distance limit; server-seeded launch configuration and travel-based
depletion; pressure-driven interpolated plastic morphs. Default portion volume
will be 20 ml, editable, with per-content density. The designer approved 0.05 kg
empty plastic bottle mass, superseding the earlier 80 kg tuning. Contact behavior
must still be protected; this does not authorize resizing any actor.

Initial findings: jet release currently drains at a fixed rate independently of
travel; material has both legacy ColaLevel and canonical content inputs; original
flight plan can include scripted falling/bounces; repeated OpenTerminal calls
always close/recreate the widget. Existing pressure morph uses fill plus foam,
not actual pressure/tolerance. Root causes and final evidence will be recorded
as each fix is verified.

Recovery files and live inspection: `Saved/BottleSystemsUpdate_20261112`.
Source snapshots are under `Before/Source`; preserve the user's editor edits.
The original baseline was 11207 / 57 of 58 native tests passing. The status and
evidence below supersede that baseline for the files modified in this task.

## Implemented changes

| Request | Implementation and remaining acceptance |
| --- | --- |
| Rocket liquid depletion and empty display | Original pressure flight uses canonical travel progress instead of a fixed drain rate. Each projectile movement sub-step releases the required portions through the existing conservation ledger. The configured distance produces exact zero. The ASM material reads canonical content, including cached plane fill; pool return no longer writes stale ColaLevel back into canonical state. Empty ISM custom data and reactivation remain zero. GPU mask already explicitly clips zero fill; its saved graph was preserved. |
| Separate shell/liquid mass | Product definitions/assets expose BottleMass (approved 0.05 kg) and PortionVolumeMl (20 ml default). Content definitions expose DensityGPerMl (1.04 default), sourced from each Normal product's editable LiquidDensityGPerMl. ContentMass = fill × capacity portions × ml per portion × density / 1000. Physics uses BottleMass + ContentMass. Fractional releases, fills, consumption/event callbacks and receiving clients update the existing body without an impulse or recreation. |
| Brief next-customer view | Existing customer lifecycle clears/releases the completed visit and permits the next shared cart. This expected automatic transition was retained, as the designer explicitly allowed. A new widget is no longer created merely because the same station receives another open callback. |
| Checkout flicker | Opening the same visible station is idempotent. NativeConstruct populates its first visible frame. Changing prices, temperature, tender/change and settlement text updates existing rows; buttons/focus targets are retained until the cart structure or phase changes. The widget test uses real customer coin identities. |
| Projectile/physics handoff | Plastic original flight opts into an authoritative apex/distance cutoff. A positive-to-nonpositive vertical velocity after ascent, the existing plan's falling phase, or the configured distance requests the shared physics handoff. Position/rotation stay in place; measured projectile angular velocity replaces random tumble injection for this policy, and linear velocity is retained. Physics falling uses the existing authoritative root replication and settling system. Distance handoff is tested; a rendered apex-specific acceptance pass remains pending. |
| Seeded launch configuration | Editable RocketMinDistance, RocketMaxDistance, RocketMinSpeed, RocketMaxSpeed, RocketRandomSeed, RocketAcceleration and RocketDirectionVariation feed one server-generated configuration per launch. Zero seed selects a new server seed; a positive seed repeats the selection. The selected configuration and existing component flight parameters are replicated. The original erratic path is reused; only plastic bottles opt into the new cutoff policy. |
| Pressure morphs | Actual agitation above resting carbonation, normalized toward the strength-adjusted tolerance, drives OverfullLevel with local interpolation. Existing fill/overfill morphs and durability data are retained. Pressure release relaxes the expansion; no per-frame morph replication is added. Resting baseline carbonation does not keep every stocked bottle in ASM. Pooling waits for pressure expansion to relax and resets cosmetic state. |

The approved 0.05 kg shell supersedes the previous 80 kg bottle tuning. The
generic GameplayMassKg property remains for other prop classes; bottle actors
derive their mass from the new physical fields. Four full 20 ml portions at the
default density add **0.0832 kg**, producing **0.1332 kg total**. An empty bottle
weighs **0.05 kg**. Densities are configurable prototype defaults, not finalized
flavor balance. These gameplay quantities do not resize giant meshes.

The player's passive CharacterMovement push/touch/repulsion factors were reduced
and use mass scaling. The lightweight contact regression measures a maximum
**5.103 cm/s** during walking. The explicit heavy-contact fixture still verifies
mass-aware impulses and substantial response to stronger forces.

## Verified evidence

- Final successful build: `Saved/Logs/BottleSystemsBuild_11208_MorphAsset.log`.
- Final full native run:
  `Saved/Automation/BottleSystemsVerified_11208/index.json` and
  `Saved/Logs/BottleSystemsVerified_11208.log` — 61/62 pass.
- New **BottleRocketTravel** passes cleanly: gradual fractional depletion, exact
  empty state at 600 cm, conservation ledger, shell-only mass, preserved root
  pose, preserved linear/angular velocity and physics handoff.
- New **BottleContentDynamics** passes cleanly: real Normal product mass, changing
  density, fractional/zero ASM material values, actual expansion morph existence
  and response, pressure relaxation, permanent ISM slot/zero fill, stale mirror
  rejection, reactivation, receiving-role mass/material/morph callbacks and client
  pressure-authority rejection. The role swap is not a real remote connection.
- New **TerminalWidgetStability** passes cleanly with a standalone game instance
  and actual UMG/Slate tree: first-frame controls, stable repeated refresh,
  intentional phase replacement and stable buttons after physical tender changes.
- **BottlePawnPhysics.Heavy** and **.Lightweight** pass. Both retain the existing
  fixture's collision/simulation warning type. Other warning-bearing tests are
  AuthoredBottleGrabRelease, PlasticPressure and ManualWheelRotator. No new
  bottle-content, travel or UI test warnings were emitted.
- The sole failure remains **LiquidRepresentationContinuity**, line 69 of
  ColaLiquidIntegrationTests.cpp: the old test requires an exact 186.58856 cm
  material body height, while the designer's saved graph now uses 150 cm. This
  task did not alter the artist's dimensions or weaken that assertion.
- Cold asset check: **24/24 pass** in
  `Saved/BottleSystemsUpdate_20261112/AssetChecks.json`. BP_Cola,
  BP_FirstPersonCharacter, BP_CheckoutCounter and BP_ProductSceaner compile to
  BS_UP_TO_DATE. Existing M_BaseLiquid recompiles on PCD3D_SM6; its empty mask and
  retained wave/vortex code are verified. The source material asset hash is
  unchanged. Export: `VerifiedLiquidMaterial.t3d` in the same directory.

## Remaining work and limits

1. Review and reset ascent/angular history explicitly at each original launch,
   then test two consecutive launches on the same ASM retained by an activation
   volume. The new fields currently reset on pool reset; they are not explicitly
   reset at the beginning of every LaunchRocket call. A stale ascent flag could
   affect a retained actor's later launch. This was identified during final
   review after screen control was stopped; it has not been corrected or tested.
2. Complete the rendered empty/half/full comparison and an apex-specific flight
   test. The first isolated fixture recorded exact 0/0.5/1 ASM material values,
   but its editor invocation exited before visual capture. The second invocation
   failed to resolve the relative Python path. No final screenshot or visual
   acceptance is claimed. Use the script's absolute path if resuming this check.
3. Validate real remote peers, flight interruption/late join and adverse-network
   timing. The known full-map Prop Manager initialization ReliableBufferOverflow
   remains a separate blocker. Native receiving-role fixtures do not accept it.
4. Hardware E/click behavior still needs a live recheck. Native widget identity,
   phase and text refresh tests pass; this is not a full player-input playtest.
5. Rebuild the packaged candidate after these changes are accepted. Packaged
   11193 does not contain this work. Population/GPU performance is not measured.
6. The earlier decision about pressure changes during a locked checkout remains
   open in the existing prototype questions; financial/identity guards were not
   bypassed here.

## Safe checkpoint and restoration

Computer Use reported a physical Escape stop; no further screen-control call
was made. The final temporary editor was not in Play. Through UE MCP, the saved
`/Game/NormalColaPrototype/Lvl_NormalColaPrototype_Validation` was restored, with
two players, listen server, one process and no automatic mouse capture. Evidence:
`Saved/BottleSystemsUpdate_20261112/RestoredEditor.json`.

All **86** pre-test `.sav` files were restored with **zero hash mismatches** in
`RestoredSaveHashes.json`. The user's saved assets and vehicle materials are
preserved. The temporary visual scene was not saved into production content.
No Blueprint graph or liquid material asset edit was made by this task.

## Files changed

Paths below are relative to `Source/ColossalFizzCo`:

- `Public/Props/InteractablePropDataAsset.h`, `Public/Props/InteractablePropDatabase.h`,
  `Private/Props/InteractablePropDatabase.cpp`: physical product/content fields.
- `Private/Shop/ShopProcurementComponent.cpp`, `Private/Props/PropManagerState.cpp`:
  per-content density registration and validation.
- `Public/Props/ColaPropActor.h`, `Private/Props/ColaPropActor.cpp`,
  `Private/Props/ColaPressure.cpp`, `Private/Props/ColaContainment.cpp`: canonical
  visuals, mass, pressure morphs, launch configuration, discharge and pooling.
- `Public/Components/RocketProjectileComponent.h`,
  `Private/Components/RocketProjectileComponent.cpp`,
  `Private/Components/RocketPressurePhysics.cpp`: movement progress, cutoff,
  acceleration, angular handoff and replication state.
- `Private/Components/PropGrabInputComponent.cpp`: lightweight contact protection.
- `Private/Shop/ShopPlayerInteractionComponent.cpp`, `Public/UI/ShopTerminalWidget.h`,
  `Private/UI/ShopTerminalWidget.cpp`: idempotent opening and stable text updates.
- `Private/Tests/BottleContentDynamicsTests.cpp`,
  `Private/Tests/BottleRocketTravelTests.cpp`,
  `Private/Tests/ShopTerminalWidgetStabilityTests.cpp`: new integration coverage.
- `Private/Tests/OriginalBottleRocketTests.cpp`,
  `Private/Tests/BottlePawnPhysicsTests.cpp`: updated launch and contact coverage.

Also added `Scripts/ValidateBottleSystemsAssets.py`; this record and the current
GDD/handoff/readme banners document approvals, evidence and unfinished acceptance.
