# Normal Cola prototype checkpoint — PAUSED

The user explicitly requested this checkpoint and a safe pause on 11 November 2026. **Do not resume implementation, builds, commandlets, automation or PIE until the user asks.** This supersedes the active implementation banners and older checkpoints. Work remains saved and uncommitted on `codex/shop-checkout-integration`, HEAD `f03372169e2e3792ea5802f685ac8eb7b688228f`.

## Current outcome and validation boundary

The Normal Cola foundation and an isolated prototype world are implemented. **The complete playable loop has not passed live acceptance.** Do not describe the prototype, giant navigation, remote cashier loop or new rounded-payment behavior as fully functional yet.

| Area | Verified status |
| --- | --- |
| Latest native build | **11144 succeeded**, `BuildId 55116800`; `Binaries/Win64/UnrealEditor.modules` selects `UnrealEditor-MarryByMidnight-11144.dll`. Build log: `Saved/Logs/NormalColaBuild_11144.log`. |
| Last complete automation | **11140: 38 passed — 37 clean, one existing ABP_Unarmed warning, zero failed/unrun**, `Saved/Automation/NormalColaCustomers_11140/index.json`. |
| Newer source validation | 11141–11144 compile successfully. **The full suite has not been rerun after these changes.** Rounded-tender assertions were added after the last passing suite. |
| Live multiplayer | Two real listen-server/client players connected on **11142**, with $0 bank and $0 cash. Customer routing failed before the first sale. No remote checkout/departure pass was obtained. |
| Giant navigation application | **Not applied.** The latest authoring commandlet failed because Python did not expose `unreal.NormalColaNavigationConfig`; details below. |
| Rendered UI / physical input | End-to-end UI and physical G/GG/hold-G acceptance remain pending. Windowless production callbacks and service tests are not equivalent to hardware or rendered multiplayer playtests. |
| Pause state | PIE stopped; interactive UE closed normally. No editor, commandlet, build/compiler or test process remained in the final process check. No development should restart automatically. |

The existing animation warning is `ABP_Unarmed` calling thread-unsafe `Get Punching State`. Do not suppress unrelated diagnostics to produce a clean result.

## Approved scope and decisions

- Only Normal Cola: Lemonade, Mint, Sweet, Spicy and Zero-Calorie. Core loop: buy supplies → fill → cap → shelf → customer shops/queues → manual cashier scans/bills/takes payment/gives change → bank proceeds at ATM → reinvest.
- Demo ingredients use pre-mixed cylinders. Full-game raw ingredient recipes are deferred. Configurable prototype supply costs are approved.
- Bank starts at $0. Existing starter stock is retained in quantity. Sales create physical register cash, **not another bank credit**; ATM deposits/withdrawals move value between cash and bank. Real finite coin denominations are $1/$2/$5/$10/$20/$50/$100.
- Fixed definition pricing; content clamped to normal full value; sequential temperature discount capped at 20%; detailed cents, final bill rounded to nearest $1. Existing expiry penalties, immutable receipts, compensation and debt rules remain authoritative.
- User approved native prototype customer-visit coordination using existing navigation, buckets, Prop Manager, scanner, checkout and cash services. The wider NPC simulation remains unchanged.
- User approved recycling NPCs and retiring purchased physical props **after exit bucket return and departure**, retaining receipts and ownership history.
- User approved rounded customer payments **only when the register physically holds exact change**. Pay exactly when change is unavailable, including the empty-register bootstrap.
- User approved a **world-local giant navigation profile**, capsule-based customer reach and size-derived queue spacing. Preserve the original character sizes and original map. The GDD explicitly requires giant patrons; “Normal Cola” does not mean human-sized NPCs.
- User saved personal changes and authorized saving the two reviewed repairs and restarting UE when necessary. Prefer C++; Blueprint work is limited to assets/adapters.
- All eleven previously excluded PSOs remain deferred and preserved. Do not restart food migration or their repairs.

## Completed implementation and assets

1. **Reviewed Blueprint repairs:** verified and saved the owner-release cast/Target wiring in `BP_FirstPersonCharacter` and duplicate EQS-event cleanup in `BTTask_MoveInCheckOutLine`. Preserved the remaining unique disconnected event. Positive owner-RPC regression covers the missing Target wire. Evidence/before-copies: `Saved/NormalColaPrototype_20261111/BeforeReviewedRepairs`, `LiveRepairs.json`, `ReviewedRepairsSaved.json`.
2. **Carry adapters:** generic `BP_ASM_Actor` attach/detach/move interfaces now call native carry functions and return real success; its grabability getter uses the native field. The player refreshes the pickup trace at the hold decision rather than caching only press-start aim. A stronger aim-change-during-hold test passes under 11140. Bottle ordinary-hit processing now observes activation warmup/grace and ignores its carrier's contacts; explicit promotion impacts still apply. Native input suppresses unfinished passive hand-punch overlaps in worlds tagged `NormalColaPrototype`; full-game combat is preserved.
3. **Procurement:** `ShopSupplyCatalog` and `ShopProcurementComponent` use canonical product generation and wallet atomic resource commits. Quantities/costs/offer IDs are server-owned; partial delivery rolls back; ledger IDs prevent duplicate delivery/debit across replay/save-load. Delivered supplies start physics. No parallel wallet or inventory authority was created.
4. **Physical caps:** product definitions include cap role and local attachment transform. `PropCapAssembly.cpp` validates compatibility, reciprocal identities, stock ownership/reservations, seal/torque and distance; uses actual cap props; preserves bottle/cap ISIDs and ISM slots across promotion/demotion; returns the same cap to physics when opened. Core lifecycle is automated-tested. Network, complete save/load, pressure opening/failure and destructive edge cases remain outstanding.
5. **Production:** native station, existing terminal UI and owned-player RPC boundary support purchases, physical cylinder/bottle/cap inputs, filling, stop, attach, close and open. Requests validate sequence/session/distance and real input volumes; source/target removal stops filling. Normal workbench fills stop at 1.0. The general filler retains optional physical-capacity filling. Refilling preserves bottle history and starts the new content lifecycle through existing Prop Manager transfer/product mappings.
6. **Catalog/stock:** five content IDs **901–905**, four portions per bottle, forty per cylinder. Empty↔filled mappings use the same permanent bottle identity. Shopping filters to these Normal contents when the catalog is active. `ShopShelfStockComponent` registers finished sealed stock from its volume. Scanner/cash/equipment reuse `ShopInstanceVolumeQuery` for both ASMs and ISMs.
7. **Customer lifecycle:** `ShopCustomerFlow` and `ShopCustomerVisitComponent` implement bounded arrivals, original bucket pockets, exclusive shopping claims, collection, queue, cashier handoff, flavor/capacity invoice checks, matching committed receipt lookup, physical purchase collection, compensation gate, station release, bucket return, departure and retirement. Existing services still own stock/money/history. Queue/claims and cash lifecycle have passing tests; full movement and fulfillment do not yet have live acceptance.
8. **Cash cleanup:** canceled tender is reclaimed before rebilling and spend budget restored without creating team money. Station release rejects stale customer/session requests and honors compensation gates. Reused visitors get fresh IDs after cleanup; completed cash/receipts survive. New rounded-tender code searches only physically present, unallocated team coins for exact change; its expanded test is compiled but unrun.
9. **Persistence profile:** `APropManager::InitialSaveSlot` optionally loads a map-specific slot. Empty retains existing explicit initialization behavior. Prototype uses `NormalColaPrototypeAutosave`; validation disables load/autosave. No new save schema was added for visit coordination.
10. **Approved giant support, built but not applied/tested:** `NormalColaNavigationConfig` uses engine `OverrideSupportedAgents`, with a dedicated dynamic Recast data class scoped to the prototype world. Defaults match the existing capsule: radius 204 cm, height 1056 cm. `ShopInteractionGeometry` measures capsule-surface distance; visit arrival uses feet/floor coordinates, movement stops by center tolerance, queue spacing derives from diameter, and the flow waits for navigation readiness. NPC shopping uses the trusted native grab lifecycle after authority/stock/lock/proximity checks.

New assets under `/Game/NormalColaPrototype`:

- `DA_NormalEmptyBottle`, `DA_NormalTwistCap`, `DA_NormalColaSupplyCatalog`.
- `DA_NormalLemonade`, `DA_NormalMint`, `DA_NormalSweet`, `DA_NormalSpicy`, `DA_NormalZeroCalorie`.
- Five `DA_Cylinder<Flavor>` assets.
- `BP_NormalColaWorkbench`, `BP_NormalColaCheckout`, `BP_NormalColaCustomer`, `BP_NormalShoppingBucket`, `BP_NormalColaGameMode`.
- `Lvl_NormalColaPrototype` and `Lvl_NormalColaPrototype_Validation`.

Prototype prices: empty bottle $4, cap $0.50, forty-portion cylinder $20. Full-bottle base prices before temperature/content adjustments: $10/$12/$13/$18/$9 in flavor order above. These apply to new prototype definitions; the original seven product definitions are preserved. The new map contains **356 starter bottles**, matching the actual counted source-map bottle actors, distributed across the five new definitions. Ten shelves, three buckets, checkout, supplied-art ATM, workbench and route points are saved.

The actual player Blueprint is reused. A duplicate player failed compilation because existing interfaces require `BP_FirstPersonCharacter` specifically; that duplicate was not saved. Original user assets/maps were not replaced by a prototype copy.

## Exact latest blocker and partial authoring state

`Scripts/ConfigureNormalColaNavigation.py` ran under selected module 11144 and failed at line 24:

```text
AttributeError: module 'unreal' has no attribute 'NormalColaNavigationConfig'
already_applied=isinstance(settings.get_editor_property('NavigationSystemConfig'),unreal.NormalColaNavigationConfig)
```

Log: `Saved/Logs/ConfigureNormalGiantNavigation.log`; runner: `Saved/NormalColaPrototype_20261111/GiantNavigationRunner.log`. **No `NORMAL_COLA_GIANT_PROFILE_SAVED` marker or `GiantNavigationProfile.json` was produced.** Do not infer that the C++ class failed to compile: build 11144 succeeded. On resume, verify the reflected class by its `/Script/MarryByMidnight.NormalColaNavigationConfig` path and inspect Python export/type metadata; decide whether to load by reflected class path or expose the type appropriately. This remains a diagnosis task, not a verified cause.

Before failing, the script **saved** the prototype customer asset with `TenderRoundUpDollars=10` and RVO avoidance enabled. Neither map received the intended navigation config, raised bounds, route marker heights or widened shelf layout. A manual earlier editor change made the validation Recast actor Dynamic/force-rebuild, but runtime agent dimensions still reset to 35/144 from the default supported-agent profile. The main map's navigation remains the original inadequate setup. The script is guarded against shifting bottle X coordinates twice once the custom config exists; review idempotence before rerunning after a partial application.

Live failure evidence: `LiveLoopObservation_NavDynamic.json` shows two peers, zero sales/receipts/cash/bank and a blocked customer. The live capsule root stood around Z530; body radius/half-height read back as 204/528. The default 35/144 agent and initial bounds top Z650 cannot represent this giant. Source anchors: GDD scale requirements in `Docs/GDD_Master.md` lines 14–20 and 70 onward. Preserve scale.

## Tests and evidence

| Build/report | Result / coverage |
| --- | --- |
| `NormalColaFoundation_11134` | 33 clean tests: procurement, owner release, prior baseline. |
| `NormalColaCaps_11135` | 34 passed, one existing warning: real cap association, seal, slots, pool/re-promotion/removal. |
| `NormalColaStations_11136`, `NormalColaAssets_11137` | 35 passed, one warning: physical station inputs and request guards. |
| `NormalColaReinvestment_11138` | 35 passed / 1 failed: overly strict float comparison in new integration test. Corrected to explicit 1e-5 tolerance after observing actual values. Historical failure retained. |
| `NormalColaReinvestment_11139` | 36 passed, one warning: isolated starter sale → actual currency query → ATM deposit → reinvest in all five flavors → fill/cap/shelf. Each 40-portion cylinder retained 0.900000095 after one four-portion fill. |
| `NormalColaCustomers_11140` | **38 passed**, one warning: cancellation/rebill/visit reset/station ID guards; claim contention and queue progression. Claims fixture explicitly has no navigation. |
| 11141–11144 | Built. Full regression/expanded rounded-tender test and all newer live paths remain unrun. |
| `NormalPrototypeEditor_11142.log` | Live two-player connection, blocked giant route; no successful sale. |

Native `NormalColaPrototypeTestActor` is placed **only in the validation map**. `Command=1` opts into two consecutive remote-player checkout/departure cycles. It moves real bottle representations through native carry/staging for physical scanner queries, sends LockBill/Complete through the remote player's existing owned RPC component, verifies actual cash/bank/history, and waits for retirement/counter reuse. It is instrumentation, not production gameplay and not hardware input or UI acceptance. Its stage-10 change path can stage finite existing team coins when rounded tender is issued.

## Saved state, backups and preservation

- No UE/editor/commandlet/build/test remains running. Interactive editor PID 28440 was closed normally with the authorized restart workflow. MCP session IDs saved in evidence are stale after closure; initialize a fresh session after restart.
- PIE settings were captured in `BeforePlaySettings_11142.json`: listen server, one client, one process, 480×360, position (1434,0), no centered window, port17777, mouse control false. Testing used two clients. **Restored `PlayNumberOfClients=1` in the closed editor's saved settings**; remaining captured values match.
- ShopAutosave is unchanged: 1,686,367 bytes, SHA-256 `07D0EDCF91025C540D2E0D33D069A114920878EFA749529C6ECA28A7B939E1D3`.
- The global NPC knowledge subsystem wrote `NPC_CulturalKnowledge.sav` during commandlet/world teardown despite the validation manager's disabled shop autosave. The pause copy (1,999 bytes; hash `91DE90D4C3C95EC65E64042373459EEA3C4EEB3E518D37EA3376F0F55592363A`) is preserved in `PauseSnapshot/SaveGamesAtPause`. **Restored the verified pre-test file** from `Saved/Backup_SaveGames_20261111_Session`, SHA-256 `8108801B59C1B869A5AFAB9C2D73CA692DC5ED6FAA0C5192D07764FA8719A07E`. Back up/restore this global slot around future validation, not just the shop slot.
- User's saved content before integration is backed up in `AfterUserSaveBeforeIntegration` (17 tracked packages plus the untracked external actor copied separately). Earlier adapter before-copies are in `BeforePrototypeAdapters`; reviewed-repair copies in `BeforeReviewedRepairs`.
- `Saved/NormalColaPrototype_20261111/PauseSnapshot` contains the pause Git status, binary tracked diff, restored settings, save copies, changed-file copies and SHA-256 manifest. Saved is ignored by Git; the checkpoint in Docs is the tracked-worktree resumption entry.
- Many modified original `.uasset` files were already changed by the user/Gemini or saved by the user during this work. **Do not attribute every Git delta to this implementation, reset the tree, or overwrite `BP_ShopGameState`.** Preserve the two user external-actor packages and untracked `B/SH` package.
- No commit, reset, staging, merge or push was performed during this continuation. No new approval is needed to continue the already approved policies after the user resumes.

## Known issues and remaining work

1. Fix reflected/Python navigation-config access, apply the profile to **both** prototype maps and verify the saved config/agent dimensions/bounds after a fresh load. No character rescaling.
2. Run the full suite on 11144/current source, including new finite-change rounding assertions. Repair actual failures before live validation.
3. Finish giant routing, reach, queue clearance, stock collection and physical bucket docking. Check that the widened layout is applied and that multiple giants can pass without locking the aisle. The global default NavMesh settings must remain unchanged.
4. Pass the two-player driver: two sales through real owned-player RPCs, exact/rounded payments with physical change, immutable receipts, $0 automatic bank credit, customer purchase collection, exit bucket return, retirement and reuse.
5. Verify manufactured physical-cap bottles and cylinder/filling/procurement requests over the network, client late join, cap attachment ordering, save/load and persistent content age/pressure/history. Full physical-cap pressure opening/failure/destruction paths need review; one core test does not establish these edges.
6. Validate real G hold pickup, GG gravity drop, trace placement, repeated grabbing, contention and settlement on host/remote. Passive punch suppression is scoped to the prototype; the full-game unfinished attack window and Gemini's old timer warning are not globally repaired. The claimed NavMesh rejection of placement is **not established**; core `FAISystem::IsValidLocation` is numerical coordinate validation.
7. Check rendered terminal interaction, labels/trays, affordability/reinvestment and a whole player-driven production cycle. Visual/layout measurements are provisional until observed in UE. Do not substitute server-only function calls for user-flow validation.
8. Review destructive/aborted post-sale paths, player-held sold bottles, missing purchased goods, cap retirement and repeated session cleanup under load. Current visitors wait for a cashier to release held purchases and for compensation choices; outstanding debt persists. A missing paid bottle reports a blocked collection state rather than inventing a refund policy.
9. Revalidate stock eligibility after registration, including a bottle opened or changed after it was first placed on a shelf; initial sealed-stock registration alone does not prove every later shopping query still enforces the finished-product requirements.
10. Performance validation is pending. Timers/queries and customer count are bounded; there is no measured performance/scalability claim yet.

## Resume order

1. Wait for the user's explicit resume. Read this checkpoint, active prototype record, approvals/architecture and current Git state. Preserve all work.
2. Inspect `NormalColaNavigationConfig.h/.cpp`, script failure, module selection and class reflection; fix the authoring access problem. Avoid blindly rerunning `ConfigureNormalColaPrototype.py`: its Blueprint node creation is not fully idempotent. Map creation scripts intentionally refuse to overwrite existing worlds.
3. Apply `ConfigureNormalColaNavigation.py` and validate exact saved metadata. It should configure only prototype copies. Record any partial save before retrying.
4. Build with a fresh unused suffix if C++ changes; keep UE closed for reflected changes. Never edit reflected headers while UBT runs. Run the full `MarryByMidnight` suite with MCP auto-start disabled in the test process.
5. Back up both save slots. Reopen the validation map, initialize local UE MCP, capture settings, configure two-client listen-server PIE and observe fixture states in both worlds. Reconnect tools via fresh initialize/notifications; do not reuse a stale MCP session. Existing tool schemas/client helper are in the evidence folder.
6. Resolve live failures using specific phase/proximity/path evidence, then complete production, physical input, late-join and save/load checks. Restore saves/settings and stop PIE after testing. Update implementation/test claims only from observed evidence.

**Development is paused. No automatic continuation is authorized.**
