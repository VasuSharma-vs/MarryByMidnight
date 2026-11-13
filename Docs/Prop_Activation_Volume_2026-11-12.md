# Prop activation volume — 12 November 2026

## Requested behavior and usage

The designer requested a component with a volume which promotes contained ISMs
to ASMs, keeps ASMs active while inside and permits normal settlement after exit.
Containment means collision geometry overlaps the box, not merely that the
actor's pivot is inside. Only registered Prop Manager identities participate;
the volume's owning actor is excluded.

Native `UPropActivationVolumeComponent` is an Add Component box named **Prop
Activation Volume**. Add it to an existing actor, or place the native
`APropActivationVolume` actor and resize its ActivationVolume box. No Blueprint
event wiring is required. It does not block players or props. The policy runs
only on the authority; the existing Prop Manager ASM/state/transform replication
remains responsible for clients.

- `Keep Props Active`: enabled by default. Use `SetKeepingPropsActive` at runtime.
- `Refresh Interval`: default 0.1 seconds, clamped to 0.02–5 seconds at BeginPlay.
- `Max Promotions Per Refresh`: default 8, clamped to 1–128 attempts. Existing
  active props are protected independently of this allocation budget.
- `RefreshProps`: optional immediate refresh after a scripted move/resize.
- `GetTrackedPropCount`: current server-side identity count, including pending
  promotions. It is not a replicated UI counter.

Promotions use zero impact impulse and the existing pool allocation/rollback
path. Failed allocations retry across subsequent refreshes. Normal free props
stay Unsettled without restarting a countdown; held props and explicit motion
retain their existing state. Chaos bodies may sleep while retaining an ASM;
the volume does not waste CPU by forcibly waking stationary bodies every frame.

Leaving all enabled volumes, disabling/deactivating one, or removing its
component releases that policy. Other overlapping volumes still apply.
Normal settling remains conditional on existing held/rocket/liquid/thermal
guards; exit does not force a prop to become an ISM immediately. Identities,
persistent ISM slots and unique runtime data are preserved. Constraints are
runtime geometry, not per-prop saved flags; load/reuse rediscovers current props.

## Integration

The manager has a weak, world-local registry of activation volumes. Demotion
checks evaluate live collision geometry, including rotated/scaled boxes, rather
than relying exclusively on the last overlap poll. The policy also protects
initial composite fragment components without changing their lifecycle.

Physical ISID discovery is shared in `Props/PropInstanceVolumeQuery`; the former
shop query forwards to it so scanner/shelf/tray behavior uses the same identity
resolution. No new Blueprint assets or changes to the prototype layout are
required. An oversized volume can deliberately retain many physics actors; keep
its bounds limited to gameplay areas which need them.

## Validation and final checkpoint

Editor **11206** builds and starts successfully. The final build log is
`Saved/Logs/PropActivationVolumeBuild_11206_Latent.log`. An early test compilation
error and a component-constructor startup error were corrected; intermediate
failed logs remain for traceability. The constructor now uses InitBoxExtent.

The full native suite completed: **53 clean passes, 3 warning-bearing passes,
1 failure, 0 not run** (56/57 passed). All four new tests pass cleanly:

| Test | Exercised behavior |
|---|---|
| ActivationVolume.Lifecycle | Multiple ISMs in one component, promotion budget, residency beyond the usual rest delay, blocked direct rest/state changes, automatic exit settlement, unchanged slots and unique physical history |
| ActivationVolume.OverlapAndCleanup | Overlapping policies, disable/reactivation, actor/component removal, moving volumes, rotated and nonuniformly scaled boxes |
| ActivationVolume.HeldAndMotion | Real manager grab/drop APIs; no forced release or interruption of explicit motion |
| ActivationVolume.FragmentCompatibility | Original component residency, settlement after release and later dedicated Fragment ASM promotion |

Timed tests tick their disposable worlds through latent commands on distinct
engine frames. Synchronous repeated world ticks were insufficient to exercise
fragment settlement reliably and were replaced in the test fixture. No fragment
behavior was relaxed to make the test pass.

The sole suite failure is the pre-existing
`ColossalFizzCo.Props.LiquidRepresentationContinuity` assertion requiring the
authored container height to equal exactly `186.58856`, at
`ColaLiquidIntegrationTests.cpp:69`. Liquid materials/dimensions and that test
were not changed by this feature. Full evidence is in
`Saved/Automation/PropActivationVolumeFull_11206/index.json` and
`Saved/Logs/PropActivationVolumeFull_11206.log`.

The editor was restarted and the native actor/component defaults were verified
through the live UE connection: extent `(250,250,250)`, enabled, interval 0.1 s,
budget 8. Evidence: `Saved/PropActivationVolume_20261112/EditorNativeComponentReady.json`.
Unreal is open on the designer's saved
`Lvl_NormalColaPrototype_Validation` map, **PIE stopped**. This feature has not
automatically placed any volumes or wired new Blueprint events. No runtime
graphics, packaged-build, measured-population performance or live remote-player
volume acceptance is claimed; the tests run authoritative disposable worlds.
The earlier full-map remote initialization overflow remains a separate issue.

The latest saves were captured again after the designer closed the editor, in
`Saved/PropActivationVolume_20261112/BeforeFullSuite`, to preserve any play progress
made during the build. All three saves were restored after the suite and their
SHA-256 hashes match, recorded in `RestoredSaveHashes.json`. The earlier `Before`
snapshot is historical; prefer `BeforeFullSuite` for this session's recovery.
No commit or push occurred. Preserve the extensive existing worktree.

## Manual editor check

1. Add **Prop Activation Volume** to an actor, or place the native actor of that
   name. Resize Box Extent around a small supported area containing managed props.
2. Press Play. Resting ISMs should promote over successive refreshes, respecting
   the budget, and free props should remain Unsettled ASMs inside the box.
3. Grab or move a prop until its complete collision shape exits every enabled
   volume. Drop it on a supporting surface and allow its normal settling guards
   to finish; its original ISM slot should become visible again.
4. Repeat with overlapping volumes, and disable or remove one. The remaining
   volume must keep its contained props active. Do not use a giant volume around
   the entire shop merely to solve a local interaction requirement.

## Modified runtime files

- `Public/Components/PropActivationVolumeComponent.h` and its private implementation
- `Public/Props/PropActivationVolume.h` and its private implementation
- `Public/Props/PropInstanceVolumeQuery.h` and its private implementation
- `Private/Props/PropActivationPolicy.cpp`
- `Public/Props/InteractablePropManager.h` and `Private/Props/InteractablePropManager.cpp`
- `Public/Props/InteractableProp.h`, `Private/Props/ActivePropState.cpp` and `Private/Props/InteractableProp.cpp`
- `Private/Components/StaticMeshPhysicsSimulationComponent.cpp`
- `Private/Props/PropFragmentLifecycle.cpp` and `Private/Props/FragmentDestructionActor.cpp`
- `Private/Shop/ShopInstanceVolumeQuery.cpp`
- `Private/Tests/PropActivationVolumeTests.cpp`
