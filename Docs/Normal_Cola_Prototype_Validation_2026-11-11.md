# Normal Cola prototype — resumed validation, 11 November 2026

**Scale acceptance superseded:** The designer rejected the reduced prototype prop/environment scale and approved the current NPC/player/Cola sizes in `Lvl_FirstPerson`. Follow [the latest scale/mass/contents record](Scale_and_Contents_Update_2026-11-11.md). The functional evidence below remains historical coverage; it does not accept the corrected environment. Module 11156 passes 39 tests, but its live pooling-warning check was interrupted before final acceptance.

Development is active following the user's explicit resume. Historical pause checkpoints remain evidence, not current instructions.

## Verified progress

- Builds **11149, 11151 and 11152** pass **39 automation tests**: 38 clean, one existing `ABP_Unarmed` thread-safety warning, zero failures/unrun. Latest report: `Saved/Automation/NormalColaResume_11152/index.json`.
- Both prototype maps saved the giant navigation profile. Python uses the reflected native class path because this engine does not export the expected subclass wrapper name.
- The original navigation config had no concrete service class. Moreover, `OverrideSupportedAgents` selects from the navigation service's default agents; it does not introduce a new agent. The prototype now uses its own native `UNormalColaNavigationSystem`, configured before world initialization. The actual NPC capsule remains radius 204 cm / height 1056 cm. Project-wide navigation and the original map are preserved.
- A real two-player listen-server run on **11150** passed two consecutive sales, customer departure, purchase retirement and counter reuse. The remote player sent four owned-component checkout requests. Both peers observed two retained receipts, **$68 physical cash**, **$0 bank**, and no remaining representations of the second customer's three purchased bottles. Evidence: `Saved/NormalColaResume_20261111/Live_11150_Result.json`, `Saved/Logs/NormalPrototypeEditor_11150.log` (`NORMAL_COLA_REMOTE_LOOP_PASSED`).
- The live driver was corrected to promote settled customer bottles through the normal Prop Manager path instead of assuming they remain ASMs. This was an instrumentation failure on 11149, not evidence of missing stock.

## Remote production and reinvestment

- NPC selection, claim and collection now revalidate content and sealing for registered shelf stock. The five-flavor integration test opens each previously registered physical bottle and verifies it disappears from the shopping list while retaining shop ownership.
- `NormalColaPrototypeTestActor`, validation map only, supports optional `Command=2`: run the existing two-sale check, stage actual proceeds in the ATM, purchase a bottle/cap/Mint cylinder using remote owned RPCs, fill, seal and register the finished physical bottle on a shelf. It uses earned funds and catalog costs. `Command=1` retains the original two-sale check.
- **11151 and 11152 passed the complete remote reinvestment driver.** On 11151, two sales earned $71, ATM deposit converted the physical proceeds once, supplies cost $24.50, and both peers reported $46.50 bank / $0 cash. Mint bottle 10362 was full/sealed with cap 10363 and cylinder 10364 at 0.9 content. Evidence: `Live_11151_ReinvestmentFinal.json` and matching success marker in `NormalPrototypeEditor_11151.log`.
- The repeat on 11152 earned $70 and ended with $45.50 bank; both peers agreed on full/sealed Mint bottle 10363, cap 10364 and cylinder 10365 at 0.9. Evidence: `Live_11152_ReinvestmentResult.json`. Live records reside in `Saved/NormalColaResume_20261111`; logs in `Saved/Logs`.
- Initial 11151 compilation required a missing complete data-asset include in the new test translation unit; the retry built successfully.

## Current UI and release correction

Rendered inspection found a clipped terminal at 480x360. Position assignment resets the viewport anchors, so the corrected native call order assigns position first, then centered anchors/alignment. **11153 visually passes at 480x360**: bank/cash summary, all seven supply buttons and Close are visible. A real mouse click bought one empty bottle, displayed Success and reduced both peers' bank from $49.50 to $45.50 exactly once. Escape closed the widget. Evidence: `Live_11153_RenderedPurchase.json` and the observed Sky window captures. Earlier 11152 centering remained clipped and is not a UI pass.

Release callbacks previously ran after `SetGrabber(nullptr)`, losing the original holder before collision-ignore cleanup. The current change retains the holder until representation release completes. The real saved-bottle test now simulates pocket-disabled collision and verifies restored collision and cleared movement ignores. **11153 passes all 39 tests** and repeats the full remote loop ($74 sales, $24.50 supplies, $49.50 final bank before the separate UI purchase). Three invalid-simulation warnings remain; do not claim these resolved by release ordering alone.

After those sales and the UI purchase, the remote client performed an actual `open 127.0.0.1:17777` reconnect using the in-game console. Server welcome and a second successful join are logged. The new client instance reports zero prior local requests and receives the same $45.50 bank, $0 cash, two receipts and full/sealed Mint bottle 10364 with cap 10365 and source 10366 at 0.9. Evidence: `Live_11153_Reconnected.json`, `NormalPrototypeEditor_11153.log`.

An extended idle checkout exposed queued customers incorrectly hitting the movement timeout when obstructed by another customer. Queued visits now use bounded path retries while waiting instead of route-failure cancellation, and explicitly disable held mesh physics before disabling pocket collision. **11155 repeats the full remote loop and passes a queue wait exceeding two minutes**: same three customers waiting/queuing on both peers, no route failures or visit abortions, no duplicate receipts or financial changes. It earned $66, spent $24.50 on supplies and ended at $41.50 bank / $0 cash. Evidence: `Live_11155_QueueStart.json`, `Live_11155_QueueWaiting.json`; success marker 12:51:12 UTC and observation 12:53:34 UTC in `NormalPrototypeEditor_11155.log`.

## Real disk save/load

The expanded five-flavor integration test saves actual manufactured bottle/cap assemblies and an unfinished checkout into a unique `Automation_NormalCola_<GUID>` slot, then loads it using a fresh world's Prop Manager. Build 11154 exposed a genuine failure: the pending bottle returned to its stock position while its physical cap retained the old location. `GetCanceledStockTransform` now also resolves reciprocal cap companions using the bottle's cap attachment transform. Incremental saves normalize cached companions even when the cap itself is not dirty.

**11155 passes all 39 tests**, including the real disk round trip. Verified: earned bank less procurement, deposited cash not recreated, committed receipt retained, incomplete checkout discarded, returned stock available, all five flavor identities, full contents, closed seals, reciprocal cap IDs, aligned cap transforms, separate container/content timestamps, nonzero pressure, and 0.9 cylinder content. Retained success evidence slot: `Saved/SaveGames/Automation_NormalCola_0BEAD4E74AADC7D510E280BD2A4C2B43.sav`; report `Saved/Automation/NormalColaResume_11155/index.json`. The failing 11154 slot/report remain historical evidence.

The remaining three live physics warnings were narrowed to the inactive branch of `OnRep_IsActive`: disabling actor collision updated a still-simulating body before its simulation stopped. Build 11156 moves actor collision shutdown after the mesh simulation shutdown. Final regression/live verification is in progress; warning elimination is not yet claimed.

## Files changed during this resume

| Area | Files |
| --- | --- |
| World-local navigation | `Public/Shop/NormalColaNavigationConfig.h`, `Private/Shop/NormalColaNavigationConfig.cpp`, `Private/Tests/NormalColaNavigationTests.cpp`, `Scripts/ConfigureNormalColaNavigation.py`; two prototype maps |
| Fresh shelf eligibility and queue lifecycle | `Public/Shop/ShopCheckoutComponent.h`, `Private/Shop/ShopStockQueries.cpp`, `Private/Shop/ShopCustomerFlow.cpp`, `Private/Shop/ShopCustomerVisitComponent.cpp` |
| Release/collision and canceled assembly persistence | `Private/Props/InteractableProp.cpp`, `Private/Props/InteractablePropManager.cpp`, `Private/Shop/ShopCheckoutComponent.cpp` |
| Native terminal centering | `Private/Shop/ShopPlayerInteractionComponent.cpp` |
| Integration/regression coverage | `Public/Tests/NormalColaPrototypeTestActor.h`, `Private/Tests/NormalColaPrototypeTestActor.cpp`, new `Private/Tests/NormalColaPrototypeProduction.cpp`, `Private/Tests/NormalColaLoopTests.cpp`, `Private/Tests/PropGrabTests.cpp` |
| Documentation | This record, active prototype record, play guide, README/handoff status |

C++ paths above are relative to `Source/MarryByMidnight`. The overall Git worktree also contains user/Gemini work and earlier implementation; this table does not attribute every repository change to this resume.

## Acceptance limits and next checks

The repeated remote checkout/departure, Mint manufacture/reinvestment, prolonged queue waiting, rendered supply purchase/close, client reconnect and real disk save/load are accepted for their exercised paths. All-five-flavor manufacture and reinvestment have passed native integration tests. Hardware G/GG and a complete player-driven production cycle, every terminal action, adverse-network/pressure-failure edges, packaged builds and measured performance remain unaccepted. Existing `ABP_Unarmed` thread-safety and bottle-material MorphTargets warnings remain; advanced cola mechanics and raw-ingredient mixing are intentionally outside the prototype.

Finish the current pooling-order regression/live check, restore original save baselines and one-client PIE settings, then perform the hands-on play guide. The global NPC knowledge subsystem writes its save during teardown even when shop autosave is disabled; preserve the changed test copy before restoring the baseline from `Saved/NormalColaResume_20261111/SaveGamesBeforeResume`.

No commits, resets or pushes have been performed during this resume. Preserve the existing user/Gemini work and all historical checkpoint snapshots.
