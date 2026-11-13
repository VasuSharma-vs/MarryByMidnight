# Bottle grab/release and trace placement — 11 November 2026

The G/GG bottle fix is implemented and saved. Native build **11133** succeeds, and the full project automation suite passes **32 tests: 31 clean, one with the existing ABP_Unarmed warning, zero failed or unrun**. Saved production player/bottle callbacks are exercised with a real windowless camera projection; rendered keyboard interaction and remote-client network acceptance remain pending.

The user requested C++ for most implementation, with Blueprint only where necessary. The previous pause checkpoint is historical. Baseline is commit `f033721` on `codex/shop-checkout-integration`, with native module 11124 and 29 tests. No commit or push was performed in this continuation. Preserve the unrelated dirty `BP_ShopGameState` asset and the eleven excluded PSO assets.

## Findings and correction

The actual `IMC_Default.DefaultKeyMappings` maps G to `IA_Grab` through `InputTriggerHoldAndRelease`, threshold 0.4 seconds. The deprecated Mappings array is empty; its empty state was not the bug.

The bottle's interface attachment/detachment bypassed the native Prop Manager carry lifecycle, so detaching did not reliably clear the held flags and restore physics. Its `Move Actor To Location` interface event was absent from the real EventGraph. The player also had an unwired pickup-success execution branch and a delayed placement-release path. These are corrected without changing the existing input mapping, product data or interaction distance.

- Added native `FPropGrabGesture` / `UPropGrabInputComponent`: hold G for pickup/placement, two short G taps for drop, one action per press, duplicate ending-event suppression, and tap identity tied to the same logical held object. Default hold/double-tap windows are 0.4 seconds. No component Tick or separate inventory/physics implementation was introduced.
- Added `AActivePropActor::AttachForInteraction`, `DetachForInteraction` and `MoveHeldToLocation`. Managed server interactions reuse `ValidateGrab`, `BeginGrab`, `ReleaseGrab` and `UpdateHeldTransform`. Attachment records exclusive ownership; release clears it and restores the existing physics lifecycle. Repeated same-holder attachment is safe for the existing RepNotify adapter.
- Server placement preserves rotation, scale and ISID; rejects non-finite, out-of-range and obstructed destinations; and requires the actual active held registry record. The range tolerance follows the existing interaction validation. Client detach handles presentation while the existing server state/transform path remains authoritative.
- `BP_Cola` now calls these native adapters from its attach/detach interfaces and implements the missing move event.
- `BP_FirstPersonCharacter` now forwards all five Enhanced Input phases to the native component, dispatches its intent into the existing actor/component paths, guards the existing server attach/release events with native identity/ownership checks, routes successful pickup through the existing result branch, and releases placement immediately instead of through a delayed callback. Its `Move actor to Pocket` adapter attaches native props through `AttachForInteraction` and returns that result; other actors retain the original interface-move path. Existing non-prop component paths and unrelated event roots are retained. Old timing nodes are no longer reached by the grab input path.
- The existing camera helper was inspected: a hit returns impact point plus normalized surface normal times clearance radius; a miss returns TraceEnd. Its existing trace length is 150 cm and placement clearance is 50 cm. Those settings were retained.
- Changed the normal ASM conversion-batch diagnostic from Warning to Verbose. Genuine warnings/errors remain enabled. Test worlds now call EndPlay before destruction.

## Validation and evidence

Final results are parsed from `Saved/Automation/BottleGrabFull_11133/index.json`, not inferred from the runner's exit code. The selected module is `UnrealEditor-ColossalFizzCo-11133.dll` in `Binaries/Win64/UnrealEditor.modules`. The editor is closed; no claim is made that an interactive editor loaded it.

Three new tests extend the previous 29-test suite:

| Test | What it verifies |
| --- | --- |
| `ColossalFizzCo.Props.GrabGesture` | Hold pickup/place, GG release, no repeated action from Ongoing/Triggered/Completed, no dropping a different identity with an old tap, release fallback when Ongoing is missed. |
| `ColossalFizzCo.Props.AuthoredBottleGrabRelease` | Loads the real cola definition and `BP_Cola`; invokes its saved interfaces; verifies native ownership, repeat attachment, release flags/physics/gravity, placement at the requested location, unchanged scale/ISID, and rejection of invalid/range/occluded/stale placement. Over twelve engine frames the released body falls from Z 250.00 to 229.09 cm, velocity Z -191.805 cm/s, gravity -980 cm/s². |
| `ColossalFizzCo.Props.AuthoredGrabInputRouting` | Loads the saved `BP_FirstPersonCharacter` and its native SCS component, with an 800×600 windowless viewport. Confirms successful, nonzero screen-center deprojection and a real pickup hit on `BP_Cola`. Dispatches the exact compiled callbacks bound to IA_Grab: hold G picks up that bottle, GG clears carry state and restores physics, and hold G places at the production camera trace location and releases without a repeated action on completion. Invokes the actual server carry callbacks to verify another player's bottle cannot be stolen/released or retained as a false carry reference. |

The input test isolates callbacks from the player's HUD/BeginPlay/Tick setup; its normal cached hit is seeded from the actual camera ray. Its viewport has no Slate window and does not take desktop focus. In the final run the placement destination is `(169.484, 2.521, 165.234)` cm, and the subsequent pickup ray hits the production bottle at `(93.909, 2.521, 165.234)` cm. This is not a zero-vector commandlet trace. The test does not inject physical keyboard events, evaluate Enhanced Input hardware timing, create a remote connection or prove RPC transport. The callback ownership checks run in an authoritative standalone test world.

The one remaining warning is pre-existing: `ABP_Unarmed` Two Way Blend calls potentially thread-unsafe `Get Punching State`. It is outside this bottle fix. No new grab-test cleanup or conversion-batch warnings remain. `git diff --check` passes.

Historical evidence is retained:

- `Saved/Automation/BottleGrabReproduction_11126/index.json`: corrected begun-play fixture against the original saved bottle adapter, reproducing missing ownership/release/move/fall behavior. Earlier 11125 fixture attempts did not begin their world correctly and are not definitive reproduction evidence.
- `Saved/Automation/BottleGravity_11128/index.json`: isolated real-body fall passes after switching synchronous ticks to distinct engine-frame latent ticks.
- `Saved/Automation/GrabInputRouting_11129/index.json`: first compiled GG input integration pass.
- `Saved/Automation/BottleGrabFull_11129/index.json`: all 32 pass before fixture cleanup and the expanded actual placement/RPC tests.
- `Saved/Automation/BottleGrabFull_11130/index.json`: all 32 pass with expanded placement/RPC callback checks, but no viewport; this did not establish nonzero camera deprojection.
- `Saved/Automation/GrabInputViewport_11131/index.json`: windowless viewport proves successful camera deprojection and placement at a real location.
- `Saved/Automation/BottleGrabFull_11132/index.json` and `Saved/Automation/GrabInputViewport_11133/index.json`: stronger pickup tests fail. The isolated fixture initially lacked the normal tick hit cache; after correcting it, the real pickup ray hit the bottle and exposed an ordering bug introduced by checking the old pre-attach move result. The native move correctly requires a held object, so host pickup now attaches through the native lifecycle before reporting success. The original non-prop move adapter is retained on cast failure. The final 11133 full-suite report passes with this saved correction.
- `Saved/Logs/BottleGrabBuild_11133.log`, `Saved/Logs/BottleGrabFull_11133.log`, and `Docs/Bottle_Grab_Validation_2026-11-11.json`: final build, full test run and hashed evidence manifest.

## Modified files

- `Source/ColossalFizzCo/Public/Components/PropGrabInputComponent.h` (new)
- `Source/ColossalFizzCo/Private/Components/PropGrabInputComponent.cpp` (new)
- `Source/ColossalFizzCo/Public/Props/InteractableProp.h`
- `Source/ColossalFizzCo/Private/Props/InteractableProp.cpp`
- `Source/ColossalFizzCo/Private/Props/InteractablePropManager.cpp` (normal diagnostic severity only)
- `Source/ColossalFizzCo/Private/Tests/PropGrabTests.cpp` (new)
- `Content/Props/Colas/BP_Cola.uasset`
- `Content/FirstPerson/Blueprints/BP_FirstPersonCharacter.uasset`
- This record, its validation JSON, and the current-direction sections of `Docs/README.md` / `Docs/IMPLEMENTATION_HANDOFF.md`.

`BP_ShopGameState` was already dirty and was not changed by this work: its SHA-256 still matches the fresh before-copy (`BDC5663A6A22B53274D887758800DE9AEFDCBD3B6D85D536D8DAF7BDE4398670`). Do not revert it as part of bottle cleanup.

## Recovery and remaining acceptance

Before-copies, input/graph inspection, narrow adapter scripts and runner logs are under `Saved/BottleGrabResume_20261111`. Successful save markers are `BOTTLE_GRAB_ADAPTERS_SAVED` in `Saved/Logs/BottleGrabAdaptersSaved.log` and `PICKUP_ADAPTER_SAVED` in `Saved/Logs/GrabPickupAdapterSaved.log`. Do not rerun `ApplyGrabAdapters.py` or `ApplyPickupAdapter.py` blindly: they create nodes/components and have already saved the assets. Earlier failed script attempts saved nothing. UE MCP tools were unavailable in this session; supported local Unreal Python/editor APIs performed the narrow graph edits. The editor was saved and closed normally before native replacement and headless asset work.

Next acceptance steps:

1. With a fresh 11133 editor, test physical G hold pickup, GG drop, hold G placement on surfaces and into free space, followed by immediate regrab, in the production map.
2. Repeat on a remote client with two players contending for the same bottle; observe attachment, authoritative release/fall, placement and eventual ASM-to-ISM settlement on both peers, then check a late join. These network paths are not yet acceptance-tested by this continuation.
3. Keep testing non-prop actor and component carrying through the retained compatibility paths. No new dedicated runtime acceptance was performed for them.
4. Resume scanner/checkout/ATM/customer acceptance and the remaining active cola-shop backlog after the reported carry bug is accepted. Excluded PSO repair/migration remains deferred.

The user is watching a movie and asked that desktop access be requested before taking mouse/keyboard control again. Continue background C++ work/tests freely; obtain their reply before bringing an editor window forward or using desktop input. No desktop control is needed to read these artifacts. This record is not a new user-requested pause.
