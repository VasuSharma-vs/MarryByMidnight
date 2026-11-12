# Gemini handoff — next implementation and validation

Prepared 11 November 2026. This is a bounded next task, not an instruction to rebuild the entire game or replace existing systems.

## Objective

Finish and validate the playable **customer → scanner → physical payment/change → completed sale → customer departure → next customer** loop. First verify the newly saved bottle controls in production multiplayer play. Prefer C++; use Blueprint only for necessary asset references, behavior-tree/blackboard adapters, interaction hooks and presentation.

The next useful implementation is customer checkout lifecycle integration and counter reuse. Scanner, checkout, wallet, cash, ATM and pricing services already exist. Extend them; do not create competing money, inventory or transaction authorities.

## Read first and preserve

Project root: `C:/Users/PC/Documents/Unreal Projects/MarryByMidnight`, UE 5.8.

Read these files relative to that root, in this order:

1. `Docs/README.md` and the newest section of `Docs/IMPLEMENTATION_HANDOFF.md`.
2. `Docs/Bottle_Grab_Fix_2026-11-11.md` and `Docs/Bottle_Grab_Validation_2026-11-11.json`.
3. `Docs/Current_Development_Scope.md`.
4. `Docs/Scanner_Checkout_Design_Update_2026-11-10.md` and `Docs/ATM_Cash_Accounting_Proposal.md`.
5. `Docs/Scanner_Checkout_Integration_Continuation_2026-11-10.md`.
6. `Docs/Core_Design_Decisions_Update_2026-11-10.md`, `Docs/Implementation_Approvals_2026-11-10.md`, and the architecture documents indexed by README as needed.

Older pause and status sections are historical where superseded by the 11 November work. The user resumed development. The complete game is unfinished.

Current branch is `codex/shop-checkout-integration`; baseline commit is `f033721`. There are important uncommitted source and Blueprint changes. Inspect `git status` and preserve them. In particular, `Content/FirstPerson/Blueprints/BP_ShopGameState.uasset` was already dirty before the bottle work: do not revert or overwrite it as cleanup. Do not reset the tree or regenerate assets from old checkpoints.

Current selected native module is **11133**. Its full report is `Saved/Automation/BottleGrabFull_11133/index.json`: **32 passed, 31 clean and one pre-existing ABP_Unarmed warning, zero failed/unrun**. This is not proof of full rendered or remote-client gameplay. The editor was closed at handoff; inspect current process/module state before assuming it is still closed.

Do not rerun `Saved/BottleGrabResume_20261111/ApplyGrabAdapters.py` or `ApplyPickupAdapter.py` blindly: both have already saved nodes/components. The final host adapter attaches native props before checking pickup success. Restoring its former pre-attach move check breaks pickup.

All eleven excluded PSOs remain deferred: PSO_BaseMesh, PSO_FoodChopper, PSO_FoodContainer, PSO_FoodDrum, PSO_GasCylender, PSO_HeavyMetalDesk, PSO_Patatos, PSO_Plate, PSO_Sink, PSO_Stove, PSO_StovePlate. Preserve their assets and references; do not migrate food containers.

## Approved gameplay rules

- Shared bank starts at **$0**. The register starts empty. Any teammate can operate the same transaction.
- Product definitions determine fixed prices. Charge for clamped content level 0–1; overfill adds no value. Apply temperature discount sequentially, capped at 20%. Keep detailed cents; round only the aggregate bill to the nearest $1, with nonnegative half-dollar ties upward.
- Physical denominations: $1, $2, $5, $10, $20, $50, $100. Use the supplied configurable coin assets and ATM art; no substitute currency meshes or artificial production funds.
- Sale coins stay in the register. A successful sale does **not** also credit the bank. ATM withdrawal debits bank and creates the corresponding physical coins atomically. ATM deposit credits the bank once and makes the same identities unavailable for another deposit/spend.
- Incorrect change prevents completion. Coin amounts come from real server-validated identities and custody, not client amounts or unlimited denomination spawning.
- Live scanner entries leave with their bottles; Save retains individual identities. Duplicate ISIDs are prohibited. The original invoice locks before payment completion.
- Expired bottles may be sold. Each expired bottle triggers one normal-full-base-price penalty after sale, regardless of discounts. Then require a player choice: one full, fresh, same-cola/same-capacity replacement for free, or another normal-base-price deduction. Original receipt remains immutable; adjustments are separate. Insufficient bank/stock must not erase obligations.
- Incomplete checkouts cancel rather than resume after load. Completed sales, physical team cash, bank balance, receipts and outstanding obligations persist according to existing save architecture.
- Server owns prices, identities, reservations, money, compensation, fulfillment and replicated state. Keep player requests on the owned-player RPC boundary.

## Actual starting points in code and assets

Inspect all paths below before editing; names describe existing code, not proposed replacements.

| Existing class/file | Reuse |
| --- | --- |
| `Source/MarryByMidnight/Public/Shop/ShopCustomerCashComponent.h` and private counterpart | `CustomerID`, `JoinScanner`, `PlacePayment`, budget and issued-payment bookkeeping. |
| `Shop/ShopInteractionLibrary.h/.cpp` | `RegisterCustomerAtCounter`; currently chooses a scanner near the counter and calls `JoinScanner`. |
| `Shop/ShopScannerComponent.h/.cpp` | `AssignCustomer`, `BeginCheckout`, `CompleteCheckout`, `ClearCart`, `SaveProducts`, replicated `FShopScannerState`, `OnScannerStateChanged`. |
| `Shop/ShopCheckoutComponent.h/.cpp` | Native sale/cash/stock services, `OnSaleCompleted`, `OnCheckoutsChanged`, `OnAdjustmentsChanged`, immutable receipts, expiry cases, reservation release and committed save state. |
| `Shop/ShopCashRegisterComponent.h/.cpp` | Real tender/change volume queries and `RefreshPayment`. |
| `Shop/ShopPlayerInteractionComponent.h/.cpp` | `RequestScannerAction`, expected session, ordered requests, distance/access validation and owned RPCs. |
| `Shop/ShopATMComponent.h/.cpp` | `Withdraw`, `CollectDeposits`, configured deposit/output components. |
| `UI/ShopTerminalWidget.h/.cpp` | Existing terminal presentation and player action requests. |

These header/private paths are under `Source/MarryByMidnight/Public` and `Source/MarryByMidnight/Private` respectively. Reuse `APropManager`, `USharedTeamWalletComponent`, cash/checkout types and canonical scan/pricing queries.

Inspect the production `BP_ProductSceaner`, `BP_CheckoutCounter`, `BP_Bucket`, customer/controller assets, `Content/AI/BasicNPC/BT_ShopingCloas.uasset`, `BB_BasicNPC`, and `TempColaShoes/BTTask_MoveInCheckOutLine.uasset`. The current active counter/scanner are identified by the existing integration and `ShopActiveCheckout` tag. Confirm asset references and actual graph bindings in Unreal; filenames/text searches cannot establish all Blueprint behavior.

Verified native findings to investigate, not permission to duplicate behavior:

- `UShopCheckoutComponent::CompleteCheckout` commits stock/cash, creates cases, publishes state and broadcasts `OnSaleCompleted`. A native search found the declaration/broadcast but no native subscriber; inspect Blueprint delegate subscriptions before adding one.
- `UShopCustomerCashComponent` has private `Station`, `IssuedPayments` and `SpentCents`, but its public header has no visit-finish/cancel interface. Audit lifecycle cleanup, payment retry and budget behavior against actual cash custody.
- `UShopScannerComponent::ClearCart` already rotates SessionID, handles cancellation and rejects clearing a completed bill with an unselected compensation. Reuse its rules. Do not bypass that gate by zeroing scanner structs externally.
- Sale delegates currently fire while the checkout component's `bCommitting` guard is active. A listener must not perform forbidden reentrant transaction mutations. Reconcile after commit with a guarded event/next-tick continuation if needed; validate the customer/checkout/session again when it runs. Do not introduce arbitrary gameplay delays or a per-frame search loop.

## Implementation sequence

1. **Baseline and bottle acceptance.** Confirm the selected DLL and preserve the existing 32-test baseline. With a fresh editor test G hold pickup, GG drop, hold G placement on surfaces/free space and immediate regrab. Repeat with a remote player, simultaneous contention, settlement to ISM and late join. Fix reproducible failures through existing native carry/Prop Manager paths; do not redesign the controls.

2. **Trace one existing customer through the real production loop.** Record its CustomerID, selected counter/scanner, scanner SessionID and CheckoutID. Find the actual behavior-tree state, bucket docking/undocking and exit path. Verify that checkout completion reaches that customer and that the next customer can use the counter. Identify missing bindings versus logic failures before changing code.

3. **Implement authoritative lifecycle coordination in C++.** Extend an appropriate existing component where practical. Track the current visit's references/IDs, observe existing committed receipt/scanner/adjustment events, and expose only small adapters for the behavior tree. Do not store another copy of transaction prices or cash balances. Match events by CustomerID and CheckoutID; reject stale visit/session callbacks. Bind once and unbind on cancellation, visit completion and EndPlay. Use weak actor references and safe teardown.

4. **Finish a successful visit exactly once.** Keep the immutable receipt and outstanding debt/case records. Respect the existing compensation-choice gate before clearing the station. Once eligible, coordinate existing bucket undocking, counter availability, customer station/AI bookkeeping and movement into the existing exit behavior. Allow the next customer only when resources are actually released. A duplicate event must not undock twice, reset a newer customer's scanner, delete a receipt or pay again. Treat lifecycle states as coordination, not a second financial authority.

5. **Handle cancellation/failure and retries.** Before successful completion, use the existing cancellation/reservation/cash-release APIs. Audit issued customer coins, SpentCents and IssuedPayments so cancel/retry cannot mint duplicate tender, lose a customer's budget or reset budget while old coins remain spendable. Preserve sold stock ownership and completed financial records after sale. Destruction/abort must not leave a counter locked forever or access dead objects. If post-sale destruction/exit policy is not documented, present that specific design question instead of silently deleting purchases or obligations.

6. **Complete physical integration and presentation.** Verify the actual scanner, tender, change, ATM deposit and output components/volumes using the supplied art. Both resting ISMs and active physics coins must be detected by the established identity query. Keep volumes distinct so output does not immediately redeposit. Update UI from native replicated state, never from a parallel Blueprint wallet/total. Make only targeted Blueprint pins/events/component-reference edits; preserve unrelated graph roots.

7. **Acceptance and cleanup.** Complete the native tests and production multiplayer scenarios below. Restore original play settings/map after tests and keep test funding/saves isolated. Record the exact build, tests, evidence and unresolved policies. Ask the user before desktop mouse/keyboard control, as requested in the latest handoff.

This sequence is an implementation recommendation within the approved architecture. Any major schema/architecture change, new gameplay policy, or choice about customer exit/despawning without an existing design must be presented for approval. Continue independent, already specified work while clarification is pending.

## Test plan and pass criteria

Add meaningful regression tests around the changed lifecycle and its integration. Existing coverage lives in `Private/Tests/ShopScannerTests.cpp`, `ShopCheckoutTests.cpp`, `ShopExpiryAdjustmentTests.cpp`, `ShopPhysicalCashTests.cpp`, `ShopPlayerInteractionTests.cpp`, `ShopStockIntegrationTests.cpp`, `SharedWalletTests.cpp` and `PropGrabTests.cpp`. Reuse helpers such as `ShopCashTestHelpers.h`; do not duplicate pricing tests just to increase the count.

| Scenario | Required result |
| --- | --- |
| New game | Production bank $0, empty register, valid existing stock. Funding used by tests exists only in isolated fixtures. |
| Live/save/remove | Unsaved bottles leave the cart when physically removed; saved bottles remain; individual remove works; grouped rows keep unique ISIDs and individual prices. |
| Rounding/locking | Fixture totals $10.49 → $10, $10.50 → $11; detailed cents retained. After lock, prices/items cannot be edited. |
| Exact physical payment | One immutable receipt; the purchased ISIDs become customer-owned; actual tender becomes register cash; bank gains nothing from the sale itself. Customer leaves through existing behavior and next customer can check out. |
| Insufficient/wrong change | Completion rejected; no receipt, sale ownership or team cash is committed prematurely; customer remains waiting. Correct finite drawer coins permit completion once. |
| Two consecutive customers | First bucket/counter/session released correctly; second uses a fresh session and its own identity; no stale cart/tender/budget or late callback from customer one affects customer two. |
| Retry/race | Repeat Complete/Cancel/payment/visit-finished callbacks and concurrent teammate requests. No duplicate receipt, coin issuance, stock transfer, departure or clearing of a newer session. Use existing expected-session and sequence checks. |
| Cancel and retry | Before sale, reservations release and stock returns according to existing cancellation rules; escrow is released correctly; same visit can retry without duplicated coins or permanently consuming canceled-purchase budget. No bank/sale credit. |
| Expired sale | Three fixture bottles at $100 base with 20% temperature discount yield $240 locked sale. One expired bottle creates exactly $100 mandatory penalty. Full fresh replacement gives adjusted revenue $140; additional $100 cash compensation gives $40. Invoice stays $240; register retains the paid $240 before deposits. Bank deductions/debt are separate from that cash. Wrong cola/capacity, underfilled or expired replacement rejected. |
| Insolvency and lifecycle | At a $0 bank, required penalties remain owed. Customer/counter cleanup cannot erase durable cases/debt. Unselected compensation is not silently auto-selected. Follow existing ClearCart eligibility; clarify any unspecified exit condition. |
| ATM conservation | Withdrawal: bank −X, physical team cash +X. Deposit: physical team cash −X, bank +X before any legitimately triggered debt collection. Repeated overlaps/requests cannot double-credit. Failed allocation cannot debit bank or leave new spendable identities. |
| Save/load | Save incomplete payment, completed sale, physical drawer coins and expiry debt in isolated slots. Reload cancels only unfinished checkout state; committed history, identities/custody, bank/cash and debt survive without duplicate tender, payments or sold-stock restoration. Reconcile actual world objects and accounting. |
| Abort/EndPlay | Waiting customer or station removed, task aborts, level ends. No dangling delegate/timer access or permanent lane lock; incomplete work cancels safely and successful sale/cases remain committed. |

For physical tray tests, move real registered coin/bottle ISIDs into the configured volumes; do not merely set TenderCents or fabricated overlap flags. For customer departure, test actual production behavior-tree/bucket/counter assets, not just a helper returning true. The native customer currently issues exact denominations for its bill; test overpayment/change with valid physical fixtures rather than changing production tender policy without approval.

Run production PIE with at least one host and one remote client. Each teammate must be able to continue the same transaction. Check shared scanner/receipt/adjustment views, counter availability and cash/bank state on both peers, then late join. Test out-of-range requests, stale SessionID and attempts involving another customer's reserved items. A standalone ProcessEvent test is not evidence of RPC transport or replication. Record actual observations and IDs per world.

Back up existing SaveGames and use isolated test slots/maps where possible. Avoid overwriting the user's gameplay autosave or NPC knowledge while validating. Stop PIE and restore captured play settings/map afterward; preserve any user edits made during testing.

## Build and automation commands

Save dirty user work and close/restart UE normally when changing native reflected types or locked assets. Verify the loaded module after restarting. Select a fresh unused numeric suffix; 11133 is the known passing handoff module. Do not edit reflected headers while the build runs.

Example PowerShell (11134 is illustrative; confirm it is unused first):

```powershell
$projectRoot = 'C:/Users/PC/Documents/Unreal Projects/MarryByMidnight'
$ueRoot = 'C:/Program Files/Epic Games/UE_5.8'
& "$ueRoot/Engine/Build/BatchFiles/Build.bat" MarryByMidnightEditor Win64 Development "-Project=$projectRoot/MarryByMidnight.uproject" -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=2 '-ModuleWithSuffix=MarryByMidnight,11134'
```

After a successful build, run tests in a separate headless process, with its MCP auto-start disabled to avoid confusing it with the interactive editor:

```powershell
& "$ueRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$projectRoot/MarryByMidnight.uproject" /Engine/Maps/Entry -Unattended -NullRHI -NoSound -NoSplash -NoLiveCoding -stdout '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False' '-ExecCmds=Automation RunTests MarryByMidnight' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$projectRoot/Saved/Automation/GeminiCheckoutLifecycle" "-abslog=$projectRoot/Saved/Logs/GeminiCheckoutLifecycle.log"
$lifecycleReport = Get-Content -LiteralPath "$projectRoot/Saved/Automation/GeminiCheckoutLifecycle/index.json" -Raw | ConvertFrom-Json
$lifecycleReport | Select-Object succeeded, succeededWithWarnings, failed, notRun
```

Preserve reports by using a fresh report directory for distinct validation revisions. Inspect failures and warning entries, not just process exit code or total tests found. Require the existing 32 tests plus the meaningful new tests to pass, zero failed/unrun. The known ABP_Unarmed thread-safety warning must remain explicitly identified; do not suppress unrelated diagnostics to manufacture a clean run. Run `git diff --check` for text changes.

## Deliverables

Provide the actual implemented behavior, files changed, build/module identifier, automation counts/report paths, multiplayer observations, remaining bugs and unresolved policy questions. Separate **implemented**, **automated-tested**, **production-playtested** and **remote-multiplayer-tested**. Save a continuation record in Docs and update the current handoff section. Do not label the whole game complete because this loop or the native suite passes.

Only after this loop and carry regression are accepted should work expand to the next active cola-shop station, such as connecting the existing native filling service to the real nozzle/tank path. Wider hazards, vehicles and excluded legacy PSO repairs are not this task.
