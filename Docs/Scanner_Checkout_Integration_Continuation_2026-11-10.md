# Scanner/checkout integration continuation

**Paused by the user at 23:49 IST on 10 November 2026.** Read [the latest checkpoint](PAUSE_CHECKPOINT_2026-11-10_BOTTLE_GRAB.md) before resuming. It includes the new, unfixed G/GG bottle input bug. Do not treat the historical resume direction below as permission to continue during this pause.

## Active user direction

The user resumed Codex implementation after reviewing Gemini's read-only findings. The previous pause is historical. Continue the cola-shop scanner, checkout, physical cash and shared bank integration; do not resume the eleven excluded PSO systems.

New explicit answers:

- Keep the shared bank's new-game balance at **$0**. Do not seed $500 or another starting float.
- The user supplied `/Game/Assest/meshs/SM_ATM.SM_ATM` after initially reserving ATM art. Use this dedicated mesh. The shared bank still starts at $0.

Preserve authored cola capacities and prices. The $10,000 customer budget and physical coin limits are implementation/configuration limits, not permission to change nominal bottle volume or retail rates. Initial acquisition cost is separate from retail revenue. Only explicitly designated level inventory should receive fresh-start stock registration, and restored ownership must not be replaced.

## Verified baseline from Gemini

Read the reports directly:

- `Saved/Automation/CoinPhysicalVolume_11119/index.json`: 1 passed, no warning/error.
- `Saved/Automation/FullSuite_11119/index.json`: 25 passed (24 clean, one existing `ABP_Unarmed` Get Punching State thread-safety warning), zero failed/unrun.
- All 128 source files in the saved checkpoint's manifest matched their hashes before this continuation. These tests validate the 11119 baseline, not the changes below.

## Saved asset work in this continuation

- Backups: `Saved/ResumeScannerCash_20261110/Source` and `Assets`; Git status is recorded there. Existing source, user packages and unrelated map actors are preserved.
- Attached `ShopActions` (`UShopPlayerInteractionComponent`) to `BP_FirstPersonCharacter`.
- Attached `CustomerCash` (`UShopCustomerCashComponent`) to the actual `BP_Basic_NPC` shopper.
- Attached `ShopScanner` and `CashRegister` native components to `BP_ProductSceaner`; enabled actor replication.
- Added separate TenderVolume and ChangeVolume boxes, with native discovery tags, at X=-90/+90, Y=-90, Z=45; extents 70/70/20, Trigger profile. They are on the existing scanner platform; no substitute ATM housing was created.
- Created `/Game/Props/Currency/BP_Coin`, inheriting the established `BP_ASM_Actor` interaction interface. It overrides Can grab using the native physics component's grabbability and exposes the ISID for interaction. Enabled replication and assigned this class to all seven coin DataAssets.
- Saved the original scanner EventGraph DSL under `Saved/ResumeScannerCash_20261110/ScannerEventGraphBefore.dsl`. Added a **targeted** interface-interaction event that calls `UShopInteractionLibrary::OpenShopTerminal` with the actual interacting player and scanner actor. Existing BeginPlay/overlap roots remain preserved.
- Automatic approval review rejected a proposed whole-EventGraph deletion. That operation was not carried out. The narrower event insertion used supported node/pin APIs and preserved the rest of the graph. Do not retry the rejected broad deletion indirectly.

All listed packages were saved before the editor was gracefully closed for the next native build. Subsequent production stock, shopping-list, handoff, readout and ATM integration is recorded below; post-payment exit and end-to-end acceptance remain outstanding.

## Native stock/budget changes and cold-start loading correction

- `AActivePropActor::InitialShopPurchaseCents` adds an explicit acquisition cost for level actors tagged `ShopInitialStock`; zero is the default for authored inventory with no recorded purchase. Retail price is never substituted.
- `ConvertPlacedActorToISM` registers such tagged, unowned source actors as shop stock only after a valid logical ISID exists, before the source is pooled.
- A successful saved-level initialization clears pending original-actor conversions and pools redundant authored source actors, preventing duplicate level inventory over restored ISIDs/ownership.
- `GetAvailableColaStockISIDs` provides a **shop-specific** query over available stock with known liquid, actual nonzero content, drink type, consumability and nominal capacity. Generic Prop Manager registry/random APIs remain unchanged; food, empty bottles, coins and fragments remain registered but are excluded from the cola shopper query.
- `BuildShoppingPlan` and `GetPlannedQuantity` on the customer component group actual stock into a budget/coin-limited basket, conservatively budgeting normal full-bottle base prices. They do not change product capacities/prices or automatically prohibit expired stock.
- Terminal UI now closes when the player is no longer in valid reach and refreshes for changed item/candidate metadata and adjusted-revenue/debt summaries.
- Added `ColaCatalogAndBudget` and `AuthoredStockRestore` tests.

Build 11120 initially caught a shadowed UI member name; that was fixed and the build succeeded. Its full automation attempt then failed during cold startup, before tests ran: constructor-time hard loading of a Coin DataAsset recursively pulled in the Coin Blueprint, player/shop interface classes and the checkout-counter CDO. The log is `Saved/Logs/ShopStockIntegrationTests_11120.log`; no passing test result may be attributed to that run.

Build 11121 changes currency recipe products to soft asset references and removes constructor-time asset loading. Currency operations load/validate assets on demand, retaining strong references during the commit and holding the mutation guard while loading. This preserves editable recipes and removes the recursive native-CDO dependency rather than removing the interaction-compatible Coin Blueprint. The build is in progress; validation remains pending.

The editor initially remained open after a graceful close request. UE MCP subsequently waited, and desktop inspection could not initialize (`failed to write kernel assets`, repeated after reset). The user has now explicitly closed UE. No force termination was used, and no UI automation input was sent. Reconnect MCP after a fresh editor launch; do not reuse the blocked connection or assume old window state.

## Next steps

1. Verify the editor loaded build 11124, then validate actual player interaction with the ATM and scanner, including opening/closing the terminal, coin pickup/drop and remote-player RPCs. Native tests and four-world actor presence do not establish this.
2. Follow the actual shopper through pickup, queue navigation, bucket docking, scanner association and physical payment. Its production shopping list now populates, but arrival/payment has not passed end to end. Handle occupied-lane retries and failed/aborted movement without leaving reservations or buckets stranded.
3. Complete post-payment customer exit only after required compensation choices and replacement transfers are handled. Preserve unpaid bank obligations and committed receipts; do not delete sold stock/cash while still needed in the transaction.
4. Exercise live/saved scans, grouped quotes, fractional-price readouts, immutable bill locking, correct change, fresh replacements and repeated expiry interactions through the rendered terminal.
5. Validate remote-player continuation, late joins, disconnects and save/load with physical coins and completed/uncompleted transactions. Use isolated slots/backups; keep production starting bank at $0 and preserve explicit stock ownership/acquisition costs.
6. Review reachable legacy Blueprint interaction/credit paths and profile physics/timers/navigation under realistic load. Excluded PSO warnings are historical, outside this task's repair scope.

## Subsequent verified progress (builds 11121–11123)

- Build 11121 compiled and completed 27 native tests (24 clean, three warning-bearing tests), with zero failed/unrun. Soft currency references resolved the cold-start crash.
- Build 11122 compiled and completed **28 tests (27 clean, one existing ABP_Unarmed thread-safety warning), zero failed/unrun**. Evidence: `Saved/Automation/ShopIntegration_11122/index.json`. This includes a new expiry-during-payment test: committed expiry metadata refreshes the scanner without changing locked prices. Proper test world contexts removed the other two warning sources.
- Build 11123 compiled successfully. `Saved/Automation/ShopAuthoredATM_11123/index.json` records **AuthoredATMIntegration passed** (one warning-bearing success, zero failed/unrun; the same existing ABP_Unarmed thread-safety warning). It loads the saved production ATM Blueprint, resolves tagged components, checks replication and supplied mesh, rejects withdrawals at $0, withdraws seven real denominations using isolated test funds, verifies output does not auto-deposit, promotes/moves/settles those identities into the real deposit volume, restores exactly $188, and proves subsequent polling cannot double-credit. Test-only funding does not change production starting money.
- `BP_Cola` now explicitly tags authored starting bottles `ShopInitialStock`, with acquisition cost zero unless an actor explicitly supplies a cost. The shopper controller now waits for valid stock and builds its grouped shopping list through `CustomerCash.BuildShoppingPlan` / `GetPlannedQuantity`, retaining authored pricing/capacity and budget constraints.
- The checkout-queue task restricts selection to the tagged `ShopActiveCheckout` counter. It caches the selected counter before docking changes availability, then calls `RegisterCustomerAtCounter` with the real controlled shopper. Failed registration detaches the bucket and fails the task, releasing the lane rather than reporting success.
- Fixed `BP_Bucket.Request Attach to Component` returning false after successful docking. Detachment now clears `AttachedActor` after notifying it. Added individual `AddPockets` / `RemovePockets` events to the counter to set lane occupancy; unrelated event roots remain intact. Enabled bucket/counter replication. Fixed the bucket's incompatible QueryOnly/physics collision setup to QueryAndPhysics / PhysicsBody, retaining a Pawn-ignore response.
- Automatic approval review rejected a graph-wide counter DSL edit because it could prune existing roots. It was not executed. Individual event nodes and pin connections provided the narrower integration; no broad deletion/rewrite was used.
- Created independent Actor Blueprint `/Game/DirtyActors/BP_ATM` from the existing counter interaction adapter, with native `ShopATMComponent`, supplied housing at 0.5 component scale, a tagged deposit box, and a separate tagged output point. It is not a counter subclass and does not enter counter searches. Docking is disabled; interaction opens the native ATM terminal through the actual player's ShopActions component.
- ATM deposit center is local `(0,65,30)` with half-extents `(24,12,14)`; output is `(0,90,65)`, outside the deposit box. Housing uses the supplied convex collision. Original mesh geometry/scale is unchanged. The output currently drops coins in front of the housing; no substitute tray artwork was created.
- Placed the ATM at `(9500,11330,2223.125)`, beside the active scanner/counter. The active scanner is at `(8835,11330,2193)`; active counter is the existing `BP_CheckoutCounter3` at approximately `(8701,11329,2218)`. Rendered editor capture verified the housing/slot placement, not player interaction or navigation.
- Saved all dirty assets/packages successfully. `SceneTools.save_actor` failed for external-actor packages because its AssetTools.exists check does not find them; supported `save_assets([])` correctly saved dirty map/external packages. Verified the ATM uasset and new external actor package on disk.

- The scanner world readout now uses `RefreshNativeAmounts` to read native ScannerState totals and last scanned quote in dollars, guarded against an invalid widget. It initializes after the existing widget setup and refreshes on a 0.2-second read-only timer on each peer. The old EndOverlap float/map adjustment is disconnected in favor of this readout; it no longer overwrites server totals. Existing BeginPlay setup and other event roots are preserved. A future event-bound presentation adapter can replace polling if profiling warrants it.
- Saved game files were copied to `Saved/ResumeScannerCash_20261110/BeforeATMPlaySaveGames/SaveGames` before runtime acceptance. Keep gameplay validation from overwriting the user's persistent save.
- Full regression on build 11123: **29 passed (28 clean, one ABP_Unarmed warning-bearing success), zero failed/unrun**, verified in `Saved/Automation/ShopFull_11123/index.json`.
- Restarted the editor normally; module inspection confirmed `UnrealEditor-ColossalFizzCo-11123.dll`. Main-map PIE startup succeeded. Four-player listen-server PIE resolved the replicated ATM actor in worlds 0/1/2/3. This verifies actor presence/configuration, not remote withdrawal RPCs or a full multiplayer sale. The original one-player listen-server settings and production autosave setting were restored after stopping PIE.
- PIE exposed five placed bucket component overrides retaining CharacterMesh / QueryOnly collision despite the corrected Blueprint template. Updated those specific instance collision fields to PhysicsActor / QueryAndPhysics / PhysicsBody and saved their external packages. Subsequent session logs contained no new bucket collision-mode warnings; earlier session warnings remain in the log.
- PIE also exposed a starting-stock integration failure: **zero** placed bottles inherited the CDO's new tag, so the real shopper's plan was empty. Explicitly appended `ShopInitialStock` on all **359 authored BP_Cola instances**, preserving existing tags/costs. Saved the external actor packages. A fresh PIE session then produced a real shopper list with product 2, quantity 1, within its unchanged budget. This checks catalog/plan integration, not successful pickup or completed checkout.
- Build 11124 compiled successfully (`Saved/Logs/ShopATMPhysicsBuild.log`). ATM withdrawal now requests active physics during the existing atomic coin creation phase. If allocation/activation fails, all new identities are released before any bank debit/receipt is committed. Ordinary cash service calls/customer tray tender retain their existing resting representations. The authored ATM test now requires every dispensed coin to simulate independently.
- Final full regression on **11124: 29 passed (28 clean, one existing ABP_Unarmed warning-bearing success), zero failed/unrun**. Parsed `Saved/Automation/ShopFull_11124/index.json`, not just the process exit code. `git diff --check` passed. The production `ShopAutosave.sav` SHA-256 is unchanged from the pre-PIE backup (`43F183F16F85592E067D5F9C5727F21B617B659CBE58823276F6DF2F1376F3DF`); NPC cultural knowledge written during PIE was restored from its backup.
- Counter `CanAttach` now replicates. The readout explicitly converts cents to double before dividing by 100; a graph readback caught and corrected integer-division truncation of individual fractional-dollar prices. The corrected Blueprint compiles and is saved.

No player key/mouse interaction or full terminal operation was validated in rendered PIE. Desktop-control initialization remains unavailable; MCP provided captures and state inspection. Do not label the complete checkout loop or remote-player input as tested.

Outstanding: actual PIE/multiplayer acceptance, customer post-payment departure and transaction cleanup, scanner/cash volume placement under real physics, late joins/save-load, and optimization. A rendered editor image or native automation pass is not evidence that those gameplay flows work end to end.

This continuation is work in progress. Native compilation, Blueprint compilation, rendered playability and multiplayer acceptance are separate milestones.
