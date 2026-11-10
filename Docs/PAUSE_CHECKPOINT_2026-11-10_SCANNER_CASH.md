# Paused checkpoint — scanner, checkout, bank and physical coins

**User instruction: stop implementation after documenting the current state. Do not resume development until the user explicitly asks.** This checkpoint supersedes earlier resume/pause instructions. Recorded on 10 November 2026, approximately 19:20 IST. The final already-running build result is recorded below when it exits; no additional test run or editor session is to be started during this pause.

## Workspace and preservation

- Project: `C:/Users/PC/Documents/Unreal Projects/MarryByMidnight`, UE 5.8, Windows PowerShell.
- Branch: `codex/shop-checkout-integration`. Git HEAD: `e99fdb3` (`Checkpoint bottle fragment integration and paused food migration`). Today's scanner/cash work is **uncommitted and not pushed**. Do not confuse the existing commit with this checkpoint.
- All source changes are saved on disk. Source before the scanner changes is copied in `Saved/CheckoutBaseline_20261110_174616/Source`. Older audit/resume baselines also remain under `Saved`.
- Many existing Blueprint/package changes predate this scanner implementation. Preserve the complete working tree, including untracked user reference images and coin meshes. Do not reset or wholesale overwrite assets based on Git status.
- The user authorized saving and gracefully restarting UE. `AssetTools.save_assets([])` returned true; the project editor was then closed through `CloseMainWindow`, with no force kill. No interactive editor or PIE remained during the pause inventory. No production Blueprint rewiring was performed in this scanner/cash session. The saved old scanner graph is still present.
- The authorized save-all also saved previously dirty open assets; `PSO_FoodContainer` now appears modified in Git status. This does **not** mean the deferred migration was resumed. Preserve that package and the other user changes.
- The UE MCP plugin is available as `mcp__ue5`. Prefer its tools for supported editor operations. After a future editor launch, rediscover the current state; do not assume previous object paths or an old MCP connection remain valid.
- No CUA session or sub-agent delegation was used. Do not spawn agents proactively under the current instructions.

## Approved decisions — do not ask these again

1. Live scanner volume adds bottles automatically and removes unsaved bottles when they leave. Save retains individual ISIDs after they leave. Entries may be removed before billing. Group identical products for display while preserving each bottle's data.
2. Normal full-bottle base price uses the product's nominal portions and configured per-portion rate. Apply `Clamp(ContentLevel, 0, 1)` and then the temperature discount, capped at 20%. Overfill adds no sale value and does not increase the expiry penalty. Keep detailed values in cents and round only the aggregate bill to the nearest $1, half-dollar ties upward.
3. Expired bottles are allowed in an invoice. Before billing players may inspect/remove/replace them. Finalizing locks the original invoice; after successful payment it stays immutable.
4. Each expired sold bottle creates one unavoidable deduction equal to its original normal full-bottle base price, ignoring previous discounts. Then choose either a full, fresh, same-cola/same-nominal-capacity free replacement, or another original-base-price deduction. Choices and adjustment history are separate from the original receipt. Unpaid deductions persist as debt. Recognize the revenue loss immediately even when the bank cannot pay.
5. Any teammate may continue the shared transaction. Server authority validates identity, pricing, stock, payment, penalties, replacements, wallet and state transitions.
6. Coins are $1, $2, $5, $10, $20, $50 and $100. The register starts empty and has finite physical coins. The user supplied all seven meshes under `/Game/Assest/meshs/Coins`, then added collision. No substitute coin meshes are needed or permitted.
7. **The shared wallet is the shop bank account. Cash sales stay in the register until ATM deposit. Do not credit the bank on sale and then credit it again on deposit.** Withdrawals debit bank funds and create real coins; deposits retire accepted coin identities and credit the bank once. Expiry deductions debit the bank or remain owed. This accounting was explicitly approved in `ATM_Cash_Accounting_Proposal.md`.
8. Existing approvals remain: any player may purchase/take loans; incorrect change blocks completion; canceled checkout stock returns to shop inventory; 60-second autosave; unfinished checkouts canceled/excluded from restore; persistent bottle pressure/cap links/content age; configurable ASM pool growth; no old-save migration required.
9. Eleven legacy PSO assets are excluded from the current game. **Do not resume food-container migration or repair**: PSO_BaseMesh, PSO_FoodChopper, PSO_FoodContainer, PSO_FoodDrum, PSO_GasCylender, PSO_HeavyMetalDesk, PSO_Patatos, PSO_Plate, PSO_Sink, PSO_Stove and PSO_StovePlate. See `Current_Development_Scope.md`.

## Implemented and previously validated native work

### Scanner, pricing and checkout

- Canonical `GetProductScanData`/`BuildQuote` reuse one pricing path. Expired bottles retain their discounted invoice value. Zero known-content levels can be quoted; unknown empty-content identities remain invalid.
- `FColaPriceQuote` and individual receipt snapshots persist base price, content/temperature discounts, sale amount and expiry information. `RoundBillToWholeDollar` rounds only the bill.
- The scanner maintains live/saved/suppressed sets, groups products, marks unavailable saved stock, prevents duplicate ISIDs and rotates a session GUID when the cart resets. Polling queries both ISM and ASM identity through the Prop Manager.
- Checkout locks canonical snapshots, reserves stock, validates identity/content/event sequence at completion, transfers ownership, records an immutable receipt and cancels unfinished reservations. Reserved bottles cannot be consumed through the guarded manager path.
- Owned-player scanner RPCs validate possession, same world, distance, monotonically increasing command sequence, current scanner session and compensation case. Replacement requests also require the actual candidate to be inside the scanner volume.

### Post-sale expiry handling

- One persistent case per expired sold bottle, separate automatic and chosen monetary deduction accounting, free replacement transfer and bounded replicated display/history caches.
- Original bill is never rewritten. Adjusted revenue includes mandatory obligations immediately; paid deductions and outstanding debt are tracked separately.
- Bank credit callbacks schedule deferred debt collection. Restore avoids collecting old-world cases during the wallet-before-checkout restore sequence. Reentry guards prevent duplicate adjustment/payment mutations.
- Save validation checks receipt totals/rounding, stock ownership, missing/duplicate expiry cases, duplicate adjustments, replacement ownership, paid totals and choice consistency.

### Coins, finite payment and ATM services

- Saved seven product definitions: `/Game/Props/Currency/DA_Coin_1`, `_2`, `_5`, `_10`, `_20`, `_50`, `_100`. They reference the matching supplied `SM_{dollars}DCoin` mesh and disable consumption, expiry, temperature updates and shell damage. Source meshes were not edited by the assistant.
- Recipes remain editable on `UShopCheckoutComponent`; default mesh scale is 0.04. Import diameters of roughly 68–148 cm become about 2.7–5.9 cm in gameplay. Native constructors reference the seven saved DAs.
- UE MCP inspection confirmed the user's updated collision: two convex shapes per mesh, including a hull covering the visible diameter, default trace mode.
- A server registry assigns each monetary coin a real PID/ISID, denomination, origin operation, owner and custody. Unregistered visual duplicates have no monetary value.
- Ordered operations record issuance, withdrawals, deposits and completed checkout transfers. Validation replays ownership transitions, prohibits reused retired IDs, checks denomination/product identity and requires every surviving team coin in the save.
- Withdrawal preflights denomination/recipe/collision, batch limits, bank funds and outstanding debt before creating coins. Bank/resource commit is atomic; a failed bank commit releases generated props. Duplicate operation IDs reuse the original result.
- Deposit validates actual coin ownership and absence of checkout escrow, retires identities through `ReleaseInstanceToEmpty`, and credits the bank once. Repeated deposit operations cannot double credit. Deposits schedule collection of owed expiry deductions.
- Checkout requires real tender and change IDs. It transfers customer tender to the team and team change to the customer. **The native automatic bank credit on sale has been removed.** Old amount-only `ReceiveTender`/`ReturnChange` APIs remain compatibility entry points that deliberately return `InvalidRequest`; do not reconnect them as payment.
- `UShopCashRegisterComponent` polls configured tender/change boxes on the server, reconciles current IDs and invalidates stale/ambiguous counts. Scanner completion refreshes physical trays immediately before committing.
- `UShopATMComponent` supports automatic drop deposits and configurable output. It rejects an output inside the deposit mouth. Owned-player withdrawal RPCs validate possession/world/range/sequence and accept denomination requests rather than client monetary totals.
- `UShopCustomerCashComponent` provides a per-visit server identity and configurable finite budget. It joins a scanner and issues a locked invoice's physical payment at most once. Default budget is configurable; current customer payment chooses an exact denomination breakdown.
- Physical team cash has a replicated display total. Only team-owned coins persist; unfinished escrow is cleared and customer-owned cash is excluded from world snapshots. World load cross-checks ATM bank receipts, coin product/instance identity and the saved ISID allocator.
- Save schema is now 5 in both manager and world-save defaults. Earlier unreleased schema 5 snapshots do not contain the final cash extensions; no migration was requested.

## Latest changes under build — not yet tested

Immediately before the user's pause request, implementation began on the actual terminal UI and integration adapters. The following changes are saved but have **not** passed automation or in-game validation:

- `UShopTerminalWidget` and `UShopTerminalButton`: native UMG checkout/ATM interface, grouped prices/discounts, individual bottle expiry/quantity/temperature, save/remove/lock/complete/clear actions, compensation choices, replacement candidates, bank/physical-cash summary and denomination withdrawal buttons.
- `UShopPlayerInteractionComponent::OpenTerminal` and an owned client RPC: open the native widget for the interacting player's controller, with target/range checks and input focus/close handling.
- `UShopInteractionLibrary::OpenShopTerminal` and `RegisterCustomerAtCounter`: Blueprint adapters for existing interaction and NPC checkout handoff.
- Scanner state now includes replicated full/fresh physical replacement candidates after completion. The scanner's lock path finds the authoritative matching customer component and requests physical payment.
- The failing coin lifecycle test was changed to promote with `EPropActivationReason::Drop` (Unsettled) before Settling/demotion. Earlier attempts used Grab/Held and PhysicsImpulse/InMotion, which are intentionally blocked from immediate demotion by the existing architecture. The final correction is not yet rerun. Do not declare this check passed.

These additions are **not attached to production Blueprints**. No ATM actor has been placed. No production scanner/AI graph was rewritten. Native UI code existing on disk is not proof of a playable checkout loop.

## Build and test evidence

| Artifact / native module | Result and limits |
| --- | --- |
| `Saved/Automation/ScannerCheckoutStage1Final/index.json`, 11114 | 20 passed: 19 clean, one existing ABP_Unarmed warning. This predates the physical cash changes and still used the older sale-bank-credit model. |
| `Saved/Logs/PhysicalCashBuild.log` | Initial build failed on the required `OutLifetimeProps` variable name for DOREPLIFETIME. Fixed before the successful retry. |
| `Saved/Logs/PhysicalCashBuildRetry.log`, 11115 | Build succeeded. First full test run crashed when an expiry fixture indexed a missing case after failed payment. Root cause: fixture registered logical stock before lazy manager initialization; creating the first coin initialized/cleared that test world. |
| `Saved/Automation/PhysicalCashStage1Retry/index.json`, 11116 | **23 passed, zero failed**: 22 clean, one existing ABP_Unarmed warning. Fixtures now initialize the manager before registering test stock. Includes supplied-coin ATM conservation, duplicate operations, actual world SaveGame/load, atomic rollback, missing recipe rejection, player boundary and checkout/expiry regressions. |
| `Saved/Automation/PhysicalCashValidation/index.json`, 11117 | 25 performed: 22 clean, two with warnings, one failed. CoinPhysicalVolume detected all seven ISMs, but demotion failed after an intentionally Held/InMotion promotion. An unrelated HTTP connectivity warning was captured in FiniteDrawerCashExchange; ABP_Unarmed warning also remained. |
| `Saved/Automation/PhysicalCashValidationFinal/index.json`, 11118 | **24 passed, one failed**: 23 clean, one ABP_Unarmed warning. CoinPhysicalVolume still failed demotion after PhysicsImpulse/InMotion; all other tests including finite-drawer cancellation/exchange/deposit passed. |
| `Saved/Logs/ShopTerminalBuild.log`, 11119 | **Build succeeded**, all 32 actions, 147.43 seconds. Includes the new terminal UI/adapters and final coin test correction. **No test run has been started for this module.** |

Do not describe the latest source as having all tests passing. The strongest fully green physical-cash baseline is module 11116 with 23 tests. The latest full suite ran on 11118 and had one failure; the test correction and terminal changes came afterwards. All current tests are native editor automation, not a new four-player checkout acceptance run.

Historical four-player fragment and wallet evidence remains valid for its original builds and scenarios. It does not establish current cash/ATM/UI multiplayer acceptance. See `Fragment_Validation_2026-11-10.json` and the older wallet evidence with its recorded initialization-barrier limitation.

## Files changed or added for this work

All paths below are under `Source/MarryByMidnight` unless otherwise stated. The machine-readable checkpoint manifest records exact hashes and Git status, including user changes outside this list.

- Pricing/identity/save: `Public/Props/ColaPricingLibrary.h`, `Private/Props/ColaPricingLibrary.cpp`, `Public/Props/InteractablePropDatabase.h`, `Public/Props/InteractablePropManager.h`, `Private/Props/InteractablePropManager.cpp`, `Private/Shop/ShopProductQuery.cpp`.
- Bank/atomic commit: `Public/Shop/SharedWalletTypes.h`, `Private/Shop/SharedWalletTypes.cpp`, `Public/Shop/SharedTeamWalletComponent.h`, `Private/Shop/SharedTeamWalletComponent.cpp`.
- Checkout/cash/adjustments: `Public/Shop/ShopCheckoutTypes.h`, `Public/Shop/ShopCashTypes.h`, `Public/Shop/ShopCheckoutComponent.h`, `Private/Shop/ShopCheckoutComponent.cpp`, `Private/Shop/ShopPhysicalCash.cpp`, `Private/Shop/ShopPostSaleAdjustments.cpp`.
- Scanner: `Public/Shop/ShopScannerComponent.h`, `Private/Shop/ShopScannerComponent.cpp`, `Private/Shop/ShopScannerVolume.cpp`.
- Register/ATM/customer/player: matching headers and CPPs for `ShopCashRegisterComponent`, `ShopATMComponent`, `ShopCustomerCashComponent`, `ShopPlayerInteractionComponent`, `ShopInteractionLibrary`.
- UI: `Public/UI/ShopTerminalWidget.h`, `Private/UI/ShopTerminalWidget.cpp`.
- Tests: updated `ColaPricingTests.cpp`, `ShopProductQueryTests.cpp`, `ShopCheckoutTests.cpp`, `ShopScannerTests.cpp`; added `ShopExpiryAdjustmentTests.cpp`, `ShopPlayerInteractionTests.cpp`, `ShopPhysicalCashTests.cpp`, `ShopCashTestHelpers.h`.
- Assets: seven new `Content/Props/Currency/DA_Coin_*.uasset` files; seven user-supplied `Content/Assest/meshs/Coins/SM_*DCoin.uasset` files. Other already-modified packages must be preserved.
- Documentation: `Scanner_Checkout_Design_Update_2026-11-10.md`, `ATM_Cash_Accounting_Proposal.md`, `Coin_Integration_2026-11-10.md`, this checkpoint, README and IMPLEMENTATION_HANDOFF. Earlier audit files and `Scripts/AuditProjectReadOnly.py` remain in the working tree.

## Known issues and pending integration

1. Latest terminal build/test status must be read before opening/saving assets with its native types. If compilation fails, preserve the current source and resume by fixing the exact logged error after user authorization to continue.
2. The production `BP_ProductSceaner` still has old float/maps/expiry logic and writes directly to the GameState wallet mirror on its Add-to-List interaction. Replace that path with the native shared scanner/terminal path; never leave two money authorities. Back up/export its current graphs first.
3. Attach native scanner/register components, bind the existing scanner volume and add separate physical tender/change trays. Set the owning station to replicate. Confirm meshes/collision/scale with rendered gameplay; no current rendered screenshot proves the new coin loop.
4. Attach owned-player action components to the actual player Blueprint, and customer cash components to the **actual BasicNPC shopper** Blueprint. Wire interaction events through `OpenShopTerminal`. Wire the checkout-line arrival/attach success through `RegisterCustomerAtCounter`. Do not assume the separate native NPC family is used by the current shopper.
5. Initial placed cola stock is not automatically registered in `UShopCheckoutComponent` yet. Add a deliberate authority-side registration point after actual ISIDs exist, preserving saved ownership and preventing customer items from being resold. Do not use unguarded scan-time registration as a workaround.
6. Author a usable ATM actor/placement with distinct deposit volume and output, bind the shared native UI and validate physical pickup/return. No dedicated ATM mesh exists in the project inventory; existing authored assets may be reused if appropriate. Coin appearance is now supplied, so do not ask for it again.
7. Review the new UI's refresh/focus/scroll behavior in rendered UE. Receipt/debt updates, changing candidate metadata with stable IDs, destroyed/out-of-range station behavior, customer cancellation/budget reuse and long bills need integration review. UI code is not production-certified.
8. The latest physical-volume test correction must be run. Real bank/cash conservation and pooling need a full multiplayer sequence including simultaneous teammates, duplicate/delayed commands, moving a counted coin away, competing stations, insufficient float, empty bank, late join and save/load during an unfinished checkout.
9. Product balancing remains authored data: existing bottles may have high nominal-portion prices. Do not silently change those rates to suit the default configurable NPC budget. Current customer exact-payment behavior does not exercise randomized overpayment/change in normal play; cash/change services support it, but authored customer tender behavior needs consideration.
10. Existing ABP_Unarmed `Get Punching State` thread-safety warning is still unresolved. The excluded orphan PSO_FoodContainer still has its historical compile issue; this is not an instruction to repair it.
11. Wider game systems remain incomplete as detailed in the development report: production filling/nozzle connection, caps/pressure/thermal/spill breadth, puddle rendering/hazards, concrete fragment content, procurement/loans/rent/recycling, NPC shopping/bucket completion, progression/UI/content/vehicles and wider acceptance/performance checks. This cash milestone is not completion of the full GDD.

## Exact resume order

1. Wait for an explicit user request to resume. Read this checkpoint, `Current_Development_Scope.md`, the scanner design update and ATM accounting approval. Inspect Git status and source hashes; preserve concurrent user edits.
2. Read the final 11119 build outcome and selected module manifest. Resolve any compile failure in the new terminal code. Run the coin lifecycle check first, then the full native suite once it passes; inspect JSON results rather than relying only on process exit code.
3. Launch one fresh interactive UE instance with the selected build and confirm the loaded native module. Discover editor state through MCP. Do not mistake headless automation for an interactive restart.
4. Back up/export relevant Blueprint graphs and packages. Wire player, scanner/register, actual shopper and checkout-counter handoff; remove the legacy sale-credit path and add safe initial shop-stock registration. Keep all excluded PSO assets untouched.
5. Create/configure the ATM, validate the native UI and physical trays, and test one full rendered cash sale plus withdrawal/deposit and both expiry-compensation choices.
6. Run actual multiplayer acceptance and save/load interruption scenarios, then update durable evidence and the progress report. Commit/push only when requested/authorized for the intended checkpoint, clearly separating preserved user changes from implementation evidence.

Build template: UE5.8 `Build.bat MarryByMidnightEditor Win64 Development -Project=<absolute uproject> -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=2 '-ModuleWithSuffix=MarryByMidnight,<fresh suffix>'`. Quote the comma-bearing module argument in PowerShell.

Automation template: UE5.8 `UnrealEditor-Cmd.exe <uproject> /Engine/Maps/Entry -Unattended -NullRHI -NoSound -NoSplash -NoLiveCoding -stdout -FullStdOutLogOutput` with `-ExecCmds=Automation RunTests MarryByMidnight`, `-TestExit=Automation Test Queue Empty`, unique report/log paths, and `-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False`. The override avoids an editor MCP port conflict.

## Final stop-state verification

The already-running build completed successfully. `Binaries/Win64/UnrealEditor.modules` selects `UnrealEditor-MarryByMidnight-11119.dll`, BuildId `55116800`. The current UI/adapters compile; they remain untested. No tests were started after the user's pause request, and UE was not reopened.

Durable evidence is saved in `Docs/Scanner_Cash_Checkpoint_Evidence_2026-11-10.json`. A local recovery copy is under `Saved/Checkpoints/ScannerCash_20261110_1920`, with source/docs/config/scripts, changed/untracked content, the Git diff/status and build/test evidence. Its manifest records SHA-256 hashes and the verified stop state. These are local files; no commit or push was performed.

The final process inventory is recorded in the evidence. No project editor, headless test process or project build remains running. Development is paused until the user asks to resume.
