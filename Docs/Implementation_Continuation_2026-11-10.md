# Resumed implementation — 10 November 2026

This continues the original implementation request after the user's stop/checkpoint and explicit resume. It is a progress report, not a claim that all documented game systems are complete.

Current status: paused at the user's explicit request. PAUSE_CHECKPOINT_2026-11-10.md supersedes any ongoing-work wording below. The user approved modern food-container migration after this report was drafted; it remains unapplied. A generic placed-quantity initialization hook compiled in module 11111, but only the earlier 11110 build has the complete validation described below.

## Source control and user work

The earlier complete checkpoint was committed and pushed to main as e3be512c2a780904ce409f361f2676bda10a2a3d. Current implementation is on codex/shop-checkout-integration. The scanner asset already had user changes when work resumed; its recorded SHA-256 is 6F883206717F374BC2A59D80260DFC7658A52000516EC6693A719E53AEAA8452. The assistant has not rewritten that Blueprint.

The supplied source meshes and BP_bottel_Fragments_Visioul_Socase remain reference assets. The configuration script fingerprints them and refuses unexpected target classes or different existing assemblies. Existing bottle product packages are backed up under Saved/BottleFragmentAssetBackup before saving.

## Implemented in this continuation

- A canonical product scan query reads resting ISMs and active props without promotion or unique-state allocation. Scanner and checkout use the same configured quantity, temperature range and content age. Failed reads clear output; expired items remain inspectable but cannot enter checkout.
- UShopScannerComponent provides a replicated server cart, duplicate-scan replacement, cached-price removal, customer identity assignment, reservation, tender/change, commit and cancellation. It reuses UShopCheckoutComponent and the existing shared wallet. Client echoes cannot mutate the cart. The existing scanner Blueprint and customer/physical cash flow still need integration.
- AFragmentDestructionActor carries independent original static-mesh components. Every piece receives a unique ISID and permanent parked ISM slot before physics begins. Each settles independently into the same slot. The actor parks only after all its original pieces have transitioned.
- AFragmentPropActor provides the distinct class-specific pool used for subsequent individual interactions. Original carrier reuse cannot move settled fragment ISMs.
- CompositeFragment records carry group IDs and component indices through reliable manager deltas and snapshots. Bounded unreliable pose batches include group ID, version and sample time. Client replicas interpolate authoritative poses and reject stale or mismatched packets.
- Queries, transform/velocity access, deletion, initial-component impulses, client rendering and batch locations understand the original component phase. Grabbing is available after first settlement through the dedicated ASM path; original moving components continue to receive physics contacts and impulses.
- Mid-destruction saves normalize original moving pieces to dedicated ASM restoration, preserving each ISID, transform and linear/angular velocities. Loading does not replay the initial scatter.
- Bottle breakage allocates its complete configured assembly before retiring the source. Allocation/release failure preserves the source. Remaining liquid is transferred to persistent, replicated release records with fractional portions, temperature and original content timestamp; it is not silently discarded. Linked physical caps are unlinked and released independently. Puddle rendering, variant hazard effects and cleanup are not completed by the release ledger.
- Eleven fragment product assets and DA_GlassBottleFragmentAssembly are created from the supplied showcase geometry/transforms. The assembly is assigned to DA_BasicCola, DA_Cola, DA_EmptyBottel, DA_GoodCola, DA_NormalCola, DA_SpicyCola and DA_SweetCola. Every supplied mesh has one convex collision element.
- Corrected DA_EmptyBottel: zero content now retains the nominal capacity of the matching filled Cola1 bottle, and all seven bottle assets enable persistent instance history. Empty registration no longer invents a liquid ContentID. A regression test loads the actual empty/filled assets and refills while preserving creation time and damage.
- Save schema is now 4 for velocity and released-liquid persistence. Older saves are rejected; the approved plan does not require legacy migration.
- Routine verbose prop diagnostics no longer emit warnings on successful conversion/registration paths.
- Corrected carrier reset ordering so moving the pooled actor does not displace its components from their configured reset transforms. The shared settlement helper now recognizes Chaos-sleeping bodies and does not repeatedly wake stationary pieces when a support trace misses an overhang/stack.
- Added the missing historical NPCConsumableProp → ActivePropActor class redirect, following the rename chain in commits b8d53c4 and de3bda7. This restores nine legacy prop Blueprints without rewriting their packages.

## Validation

Native Win64 Development Editor build succeeded with module suffix 11110, also confirmed loaded by the interactive editor. All 15 native automation tests passed with zero failures in Saved/Automation/FragmentFinal/index.json. Fourteen passed without warnings; the actual-asset refill test additionally loads the existing ABP_Unarmed and reports its preexisting Get Punching State thread-safety warning. That warning remains open. Coverage includes the eleven real meshes, independent settlement, exact component reset positions, stable slots, carrier reuse, dedicated ASM promotion/demotion, late-snapshot reconstruction, stale pose rejection, mid-destruction save/load, filled-bottle fracture, liquid persistence and actual configured bottle refill. Scanner coverage includes duplicate scans, competing reservations, incorrect change, cancellation, idempotent payment and client rejection.

Scripts/ConfigureBottleFragmentAssets.py completed successfully after the user authorized graceful editor restart to release locked asset files. All seven bottle links and twelve new configuration assets saved. The supplied showcase and meshes were fingerprinted and preserved; the user's scanner hash is unchanged from resume.

The isolated four-player listen-server test passed all seven phases. Every world observed eleven visible original pieces; only the server simulated those originals (11 server, 0 on each client). All pieces settled into visible ISMs. Reactivation used a visible dedicated AFragmentPropActor on every client, then returned to the existing slot. A second destruction reused the original carrier while preserving the earlier eleven settled fragments. Final settled positions agreed within 0.5 cm, all worlds recorded the same 270 released portions from two configured 135-portion bottles, and no slot changed. Observations are local fixture properties, not replicated server counters. Evidence: Saved/Automation/FragmentFourPlayer.json and Docs/Fragment_Validation_2026-11-10.json.

The stronger test initially exposed one second-group piece that stayed awake. Its failure trace is retained under Saved/Automation/FragmentFourPlayer_BeforeSettlementFix.json. The sleeping-body/support-trace correction above was then tested successfully; the acceptance timeout was not relaxed.

Cleanup passed: PIE stopped, /Game/FirstPerson/Lvl_FirstPerson restored, captured network/window settings restored, editor left open. Autosave is disabled only on the isolated test manager. The new test fixture and test map are development instrumentation.

The broader read-only Blueprint check inspected 50 assets: 49 compile, and every checked package hash remained unchanged. PSO_FoodContainer has no parent/generated class and remains broken; twelve external level actors reference it. A simple reparenting trial on an isolated copy still failed because its Widget/Food Mesh/delegate contract is missing. See Legacy_Food_Container_Repair_Proposal.md; the user has been asked to choose modern migration or legacy interaction restoration before changing those placements.

## Remaining work

The broader unfinished domains listed in Implementation_Progress_2026-11-10.md remain open except for the fragment/scanner backend portions described here. In particular: production scanner/UI/customer/cash integration, nozzle integration, complete caps/pressure/thermal/pouring, spill visuals and hazards, compensation obligations, business systems, NPC shopping, tools/stations, vehicles, progression, content tuning and performance acceptance. Concrete fragment meshes have not been supplied. Authored fragment break sound/VFX, the orphaned legacy food container and the existing animation thread-safety warning remain open. The completed four-player check does not replace dedicated-server, packet-loss, late-join-over-network or large-load profiling acceptance.
