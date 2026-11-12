# Checkout E interaction — 12 November 2026

## Current implementation checkpoint

The designer reported that pressing E anywhere around the Normal Cola checkout
did not open its terminal. Read-only live inspection found the editor on
`Lvl_NormalColaPrototype_Validation` with PIE stopped. The same checkout Blueprint
is used by the player-facing map. A non-playing editor viewport cannot exercise
the gameplay E action; validation must use a possessed PIE player.

E is correctly mapped to IA_Intract (HoldAndRelease trigger), and its Started
execution is wired. The legacy route uses HitResultOnTick from a **150 cm** prop
trace. The giant counter has full blocking housing collision, but legitimate
station reach is **350 cm** from the pawn to its tagged front control point.
This mismatch prevents otherwise reachable desk surfaces from being targeted.
The former service tests call OpenTerminal directly and did not establish that
the real E input works.

Native `UShopPlayerInteractionComponent::TryOpenTerminalFromView` now traces from
the actual player camera, adding its offset to the existing station reach budget.
Only the first blocking hit can be selected, it must have a supported shop
component, and the existing possession, world and tagged-control reach checks
still apply. Scanner, ATM and production terminals share this query. Opening the
menu is local; financial/production actions retain their existing server RPC
validation. The generic prop/grab trace is unchanged.

`UShopInteractionLibrary::TryOpenShopTerminalFromView` is the small Blueprint
adapter. `Scripts/FixCheckoutInteraction.py` inserted two nodes in the existing
player's IA_Intract Started path: native lookup, then a handled branch. A
successful shop open ends that chain. Failure continues the original interaction
route. Hold/release, grab and other original event connections were preserved.
The player Blueprint compiles and is saved.

Editor module **11205 builds successfully** with build acceleration disabled.
`MarryByMidnight.Shop.CheckoutViewInteraction` reproduces the 150 cm miss using
giant counter collision, then checks the new query, looking away, invalid direction,
distant corners, an obstructing object and lost possession. This test passes.

## Validation and remaining limits

- Editor module **11205** built successfully; the saved player Blueprint compiles.
- The full native suite completed: **49 clean passes, 3 warning-bearing passes,
  1 failure, 0 not run** (52/53 passed). The checkout view regression passes.
- The failure is `MarryByMidnight.Props.LiquidRepresentationContinuity`, at the
  assertion requiring the artist-configurable body height to equal exactly
  `186.58856`. No liquid material, body dimensions or liquid test was changed
  for this checkout repair. Investigate the authored dimensions and that assertion
  in the liquid task; do not change the designer's material merely to pass it.
- Actual mouse/keyboard **E** opened the menu in a possessed standalone PIE player
  in `/Game/NormalColaPrototype/Lvl_NormalColaPrototype`.
- Actual **E** also opened it in the **Server 0** window of a two-player
  listen-server PIE session. Both checks displayed the scanner totals and
  Save / Finalize / Clear / Close buttons. They did not use a direct OpenTerminal
  call or an automated gameplay driver.
- Remote **Client 1** validation is **blocked**, not passed. The client joined,
  then disconnected during initial prop synchronization before keyboard testing
  could exercise the live checkout. `CheckoutLive_11205.log` records
  `ReliableBufferOverflow` and failed
  `BP_PropManager_C::Multicast_ReceivePropInitializationBatch` at
  `2026.11.12-12.37.58`. The detached client window cannot validate gameplay.
  Existing `SendInitialPropSnapshot` sends all initialization batches immediately;
  `SendPropInitializationBatch` multicasts each batch even when given a particular
  client. This is a separate synchronization issue requiring investigation and
  a bounded delivery/backpressure repair, followed by remote E and RPC checks.
  That code was not changed in this task.

To use the checkout, enter **Play**, climb its front player access deck, face the
counter near the front control position and press **E**. The pawn must remain
within the existing 350 cm control reach; the entire giant counter is not an
unlimited interaction area. Looking at the floor or another blocking object
does not open it. At the test pose, pawn `(350,-80,558.4)` and approximately
`pitch=-30, yaw=90` reach the counter. An earlier, steeper downward test correctly
missed the counter and did not open the menu.

Only the Editor module was rebuilt. The older packaged **11193** candidate does
not contain this repair. No complete new sale/manufacturing loop, remote E,
packaging or performance acceptance is claimed by these tests.

## Files and recovery

- `ShopPlayerInteractionComponent.h/.cpp`
- `ShopInteractionLibrary.h/.cpp`
- `Private/Tests/ShopTerminalInputTests.cpp`
- `Content/FirstPerson/Blueprints/BP_FirstPersonCharacter.uasset`
- `Scripts/FixCheckoutInteraction.py`

The user’s editor contents were saved before closing normally. The original
player Blueprint and current ShopAutosave, NormalColaPrototypeAutosave and
NPC_CulturalKnowledge saves are backed up under
`Saved/CheckoutInteraction_20261112/Before`, with hashes in `BeforeHashes.json`.
Live graph/collision inspection and before/after input connections are beside
that directory. Restore only this task’s snapshots after disposable validation;
older liquid-test backups precede the user's latest play progress.

Build and wiring logs: `Saved/Logs/CheckoutInteractionBuild_11205.log` and
`CheckoutInputWiring_11205.log`. Full native report target:
`Saved/Automation/CheckoutInteractionFull_11205/index.json`.

Keyboard-test evidence is in `Saved/CheckoutInteraction_20261112/`:
`E_Standalone_Opened.png`, `E_ListenServer_Opened.png` and
`LiveValidationResult.json`. PIE was stopped afterward. Original two-player
listen-server play settings were restored. All three gameplay saves were restored
from this task's Before snapshots and their hashes match `BeforeHashes.json`.
Unreal remains open on the player-facing prototype map. No commit or push was
performed; the extensive existing worktree is preserved.
