# Development checkpoint — paused 10 November 2026, 23:49 IST

## Current instruction

The user explicitly requested documentation, a comprehensive checkpoint and a safe pause. **Do not continue development until the user resumes it.** This checkpoint supersedes earlier resume directions. The current bug investigation has made **no production code or Blueprint changes**; it remains unfixed.

Workspace: `C:/Users/PC/Documents/Unreal Projects/MarryByMidnight`. Branch: `codex/shop-checkout-integration`; last known HEAD `e99fdb3`. Work remains local and uncommitted. Do not discard user changes. The editor was last opened on native module **11124**. No PIE session, build, test or background development task was started during this bug investigation. The last inspected PIE state was false. Leave the user's editor open; closing it is not required to pause.

## Completed implementation before this bug report

Detailed record: [scanner/checkout continuation](Scanner_Checkout_Integration_Continuation_2026-11-10.md). Machine-readable evidence: [ATM integration](ATM_Integration_Evidence_2026-11-10.json). Earlier broad audit and historical checkpoints remain useful context, but their test/build and integration status is superseded here.

- Shared authoritative bank, cents-based accounting, locked checkout receipts, live/saved scanner entries, grouped quotes, physical tender/change, durable expiry penalties and compensation services exist in native code.
- Supplied coin meshes are linked through seven denomination product assets and interaction-compatible `BP_Coin`. Bank starts at $0; register starts empty. Sale cash stays physical until deposited; no duplicate sale credit to bank.
- Integrated the user's `SM_ATM` into `/Game/DirtyActors/BP_ATM`, placed beside the active checkout at `(9500,11330,2223.125)`. It uses native `UShopATMComponent`, separately tagged deposit/output components and the existing player terminal adapter. It is not a checkout-counter subclass.
- ATM withdrawal creates and activates all coins before committing a bank debit. Allocation/activation failure rolls back the new identities. Dispensed coins enter independent physics simulation and use the existing ASM/ISM pool. Deposits retire actual shop-owned coins and credit once. The source mesh itself was not rescaled or replaced; housing component scale is 0.5.
- Added native ShopActions to the real player, CustomerCash to the actual Basic NPC shopper, and scanner/register components plus separate tender/change boxes to the production scanner.
- Authored stock registration uses explicit `ShopInitialStock` tags and acquisition costs, separate from retail value. Explicitly tagged all **359 placed BP_Cola actors**, preserving other tags/costs, after PIE proved default-only tagging did not reach existing instances.
- The shopper now builds a budget/coin-limited plan from actual available cola stock. A fresh PIE session observed product 2, quantity 1 in its real shopping list. This did not prove pickup/arrival/payment success.
- Tagged one active checkout lane, moved its scanner within range, cached counter identity before docking, and wired native customer/scanner registration after bucket attachment with detachment on failure.
- Fixed bucket attachment reporting false after success, cleared stale attached-actor references on detach, added counter occupied/free events, replicated counter occupancy, and corrected incompatible collision modes on the bucket template and five placed overrides.
- World amount readout now reads native totals and the last scanned quote. Cents convert to double before division; compiled graph readback caught and corrected integer truncation. This is read-only presentation, not an independent wallet/calculation system.
- Saved assets/packages and restarted UE normally. Last module inspection confirmed `UnrealEditor-MarryByMidnight-11124.dll`.

## Verified tests and limits

- Build 11124 succeeded: `Saved/Logs/ShopATMPhysicsBuild.log`.
- Full suite: `Saved/Automation/ShopFull_11124/index.json` — **29 passed: 28 clean and one warning-bearing success, zero failed/unrun**. Existing warning: `ABP_Unarmed` calls Get Punching State from a potentially thread-unsafe animation update.
- Authored ATM integration tests load the saved Blueprint, resolve native/tagged components, reject a withdrawal at $0, create seven actual denominations with physics, debit the bank once, move/settle those coins into the real deposit volume, restore their exact value and prevent repeat credit.
- Build 11123 four-player listen-server PIE resolved the replicated ATM in worlds 0/1/2/3. This verifies presence/configuration, **not** remote monetary RPCs or a complete multiplayer sale.
- Main-level PIE startup and real shopper plan population were observed. Actual keyboard/mouse operation of the terminal and the complete checkout loop were not validated.
- `git diff --check` passed before the bug investigation. Production `ShopAutosave.sav` remained identical to its pre-PIE backup: SHA-256 `43F183F16F85592E067D5F9C5727F21B617B659CBE58823276F6DF2F1376F3DF`. NPC knowledge written by PIE was restored. Production autosave is enabled; original one-player listen-server settings were restored.
- **No new tests or fixes have been applied for the reported G/GG bug.** Prior passing tests do not establish that this input path works.

## Current bottle bug — actual findings

User report: after grabbing a bottle, pressing GG detaches it but it does not fall; holding G does not send it to the traced location.

Backups and captured graph data: `Saved/BottleGrabBug_20261110/`. Includes original `BP_Cola.uasset`, `BP_FirstPersonCharacter.uasset`, `GrabInputGraph.json`, `DetachHoldingMacro.json`, `TraceMoveMacro.json`, and `HoldGateGraph.json`. These are inspection snapshots; no graph mutation has occurred.

### Confirmed release lifecycle gap

`/Game/Props/Colas/BP_Cola:Request Detach from Component` currently:

1. Calls `PhysicsSimComponent.SetStateUnsettled(1.5)`.
2. Detaches **PropMesh** from its component, keeping world transform.
3. Sets component grabbability true and returns success/self.

It does not use `APropManager::ReleaseGrab` / `AActivePropActor::OnPlayerRelease`, clear the held/interaction lock, or explicitly restore physics. `UStaticMeshPhysicsSimulationComponent::SetStateUnsettled` returns early while its active-prop owner has `bIsHeld` or `bIsInMotion`. This explains why detachment alone can leave the bottle suspended.

`BP_Cola:Request Attach to Component` similarly calls the component's SetStateHeld and attaches the actor directly. It bypasses the manager's normal BeginGrab/SetGrabber ownership path. Preserve its socket, collision-ignore and success handling when integrating the native lifecycle.

Native paths already exist:

- `InteractablePropManager.cpp`: ExecuteGrab around 4840, UpdateHeldTransform around 4919, ReleaseGrab around 4944.
- `InteractableProp.cpp`: OnPlayerGrab around 2064; OnPlayerRelease around 2100. Native release detaches the actor, clears held/owner state, restores collision/physics and starts unsettled/motion processing.
- `StaticMeshPhysicsSimulationComponent.cpp`: SetStateUnsettled around 536 rejects a still-held owner.

The player's existing server and client detach wrappers both call the Blueprint interface. A fix must keep server authority and client presentation synchronized; do not simply enable client-side authoritative physics or issue duplicate manager mutations.

### Confirmed missing move implementation and remaining input diagnosis

`BlueprintTools.list_events(BP_Cola)` reports interface **Move Actor To Location** with `bIsImplemented:false`. There is no corresponding event node in its actual EventGraph. Reading a generated graph under `:Move Actor To Location` shows a compiler-generated persistent-frame/Ubergraph stub; it is **not** evidence of an editable working implementation. Use the actual interface event adapter if needed; do not edit compiler-generated intermediate graphs.

Player macro `BP_FirstPersonCharacter:NewMacro` calls the held actor's MoveActorToLocation interface, waits 0.1 seconds, then chooses actor/component release. Its location input is wired from `get camera trace safe location`.

Input graph findings (all in `BP_FirstPersonCharacter:Grab and Intract`):

- `K2Node_EnhancedInputAction_0` is IA_Grab. Triggered output 0 → `K2Node_ExecutionSequence_0`; Started output 1 caches the traced actor; Ongoing output 2 → elapsed counter; Canceled/Completed outputs 3/4 → reset/double-tap logic.
- Ongoing checks `can enter >= 0.4` using `K2Node_PromotableOperator_1`, then `K2Node_MacroInstance_5` (IsGrabable).
- `K2Node_MacroInstance_19` is the placement Gate. Enter input 0 comes from IsGrabable's Holding Something output. Open input 1 comes from Triggered Sequence_0 then_1. Close input 2 comes from Ongoing pickup Sequence_1 then_1. It starts closed.
- Its Exit goes to the authority split, then `NewMacro` for the host or `SendactorlocationtoServer` for a remote player. Whether Ongoing/Triggered ordering starves the Gate must be tested against the actual mapping triggers. **Do not assume that adding another Gate.Enter connection is sufficient or preserves first-grab behavior.**
- Double-tap release uses `K2Node_MultiGate_2`; its first output starts a 0.4-second RetriggerableDelay (`K2Node_CallFunction_6`) that resets the gate; its second output switches holding state and releases through existing wrappers.
- The action asset `/Game/Input/Actions/IA_Grab` has no action-level triggers. The initial read of IMC_Default.Mappings returned empty, but **that property is deprecated in UE5.8**. Actual mappings are in `DefaultKeyMappings.mappings` (and possible `MappingProfileOverrides`). Inspect the G mapping and its trigger objects there on resume. Empty deprecated Mappings does not mean the game has no input mappings.

## Recommended resume order

1. Read this checkpoint and confirm the user has resumed work. Review current dirty/editor state without overwriting user work.
2. Inspect `IMC_Default.DefaultKeyMappings` and actual G trigger thresholds. Finish the input-event/Gate diagnosis before editing that wiring.
3. Add meaningful regression coverage using the **saved production BP_Cola** and its actual interface functions: grab → GG-style release clears attachment/held lock and restores gravity; held bottle moves to a finite traced world location while preserving scale/identity; release there falls and can settle/demote. Consider a failing baseline first. Native-only happy-path tests would miss the broken adapter.
4. Surgically adapt bottle attach/release/move to the existing native manager lifecycle. Preserve actor/component input modes, double-tap timing, sockets, client presentation and unrelated graphs. Use individual nodes/connections where possible; do not delete whole EventGraphs. Previous automatic approval review rejected broad graph rewrites, and narrower event insertion succeeded.
5. Verify long-hold placement does not immediately drop a newly grabbed bottle, single/double taps still behave correctly, trace misses are handled, and a delayed hold callback cannot affect a newly pooled/reused ISID. Check host plus remote player and dropped-bottle physics.
6. Save assets, build only if native changes are needed, run focused regressions and the full suite, and distinguish automated interface tests from actual rendered input tests. Desktop-control initialization previously failed; MCP supplies snapshots/state but no key input.
7. Return to pending checkout work after the grab regression is fixed: actual shopper pickup/queue/docking/payment, compensation-aware customer departure/cleanup, player-driven multiplayer sale, late joins/disconnect/save-load and profiling.

## Persistent constraints and unresolved work

- No legacy food migration or repair: the eleven excluded PSO systems remain outside scope, including PSO_FoodContainer. Their map-load warnings are known and were not repaired here.
- Preserve server authority, existing ISIDs, pools and stock ownership; reuse native services. Starting bank remains $0. Fixed prices, sequential content/temperature discounts, whole-dollar final-bill rounding, immutable receipts and original-base expiry penalties remain approved.
- The full game and checkout loop are **not complete**. Native service tests, asset wiring, replicated actor presence and fully playable multiplayer behavior are distinct milestones.
- No commit/push was made in this continuation or bug investigation. Save/checkpoint does not mean changes are committed.

## Recovery files

- Current pause documentation: this file; linked from `Docs/README.md` and `Docs/IMPLEMENTATION_HANDOFF.md`.
- Full working-file recovery snapshot: `Saved/Checkpoints/BottleGrab_20261110_2349/` (Files, manifest, Git status and binary diff; verify manifest before restoration).
- Earlier integration backups: `Saved/ResumeScannerCash_20261110/` and `Saved/Checkpoints/ScannerCash_20261110_1920/`.
- Current bug's pre-edit Blueprints and graph snapshots: `Saved/BottleGrabBug_20261110/`.

Development is paused at the user's request. No bug fix is claimed.
