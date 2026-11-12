# First playable prototype development report

Updated 12 November 2026. The **core-loop candidate works in instrumented packaged multiplayer**, including settled-state late join and one moderate packet-loss profile. **The complete first playable is not yet accepted.** The specified physical hot-source hose/valve/cooling workflow and thermal persistence policy need designer decisions, and a full human keyboard/mouse playthrough remains untested.

## Candidate and scope

- Game build: **11193**, Win64 Development, Unreal Engine 5.8. Both Editor and Game targets build; the coherent cook/archive succeeds.
- Latest Editor module: **11194**. Its only addition after the Game build is an editor-only five-flavor filling integration test.
- Package: `Saved/PrototypeResume_20261112/Packaged_11193/PackageArchive/Windows/MarryByMidnight.exe`.
- Start the player-facing map through [the play guide](Normal_Cola_Prototype_Play_Guide.md) and `Scripts/StartNormalColaPrototype.ps1`. Close the earlier smoke-test window before launching this candidate.
- Plastic bottles, Lemonade/Mint/Sweet/Spicy/Zero Calories, Normal manual twist caps, premixed bulk supplies and two distinct cap operators. Existing player, giant customer and bottle dimensions are preserved.
- No Toxic, Black Magic, radioactive/explosive cola, other bottle materials, advanced caps, powered equipment or raw-ingredient mixing was added to demo scope.

## Implemented functionality and acceptance

| System | Current functionality | Evidence and limits |
|---|---|---|
| Procurement | Server-owned catalog/costs, bank debit, mesh-sized separated delivery, occupancy preflight, reusable delivery slots | Native tests and actual remote packaged purchases. Bank starts at zero; starter stock funds purchases. |
| Five-flavor filling | Shared source/filler transfers real portions, selects the matching filled product and preserves bottle physical history | Native integration exercises all five real recipes. Full remote packaged manufacture exercises Mint. Physical hose and hot-liquid exchange remain incomplete. |
| Manual cap closing | Physical cap ISID, compatible attachment, exclusive holding/rotation roles, contextual G/R, rate/session/range checks, declining hold efficiency and accounted leakage | Native checks plus real owned client requests produce a truly full, sealed bottle. Complete human manufacture is pending. Station is currently preinstalled and fixed. |
| Factory cap identities | Sealed starter products receive independent cap identities through existing creation/load services; pooling and promotion retain them | Native tests cover idempotence, no identity allocation during promotion, physical links, pooling, disk roundtrip and no regeneration after opening. |
| Shelf stock | Finished sealed Normal bottles become shop stock in shelf volumes | Real packaged manufacture is placed on a clear supported shelf and becomes selectable stock. The fixture searches all configured shelves. Human placement remains pending. |
| Customer visits | Giant navigation, bucket collection, product search/claim, queue, checkout handover, bucket return, departure and recycling | Real packaged NPC visits complete two transactions. Native prototype coordinator reuses existing navigation, prop and checkout services. |
| Scanner and checkout | Unique bottle detection, live/saved entries, grouping, fixed content/temperature pricing, immutable locked invoice, physical payment and validated change | Native suite covers the services; packaged clients finalize real customer purchases. The driver stages physical items and invokes owned RPCs; this does not replace hardware-input acceptance. |
| Cash, ATM and bank | Real finite denomination coins, physical register proceeds, ATM deposits/withdrawals, shared server-authoritative bank and idempotent records | Both packaged peers and late joiner agree on cash, bank and receipts. Sales do not also credit the bank. Native tests cover additional financial cases. |
| Expired-product compensation | Locked original-price penalty and tracked replacement/additional-deduction cases | Native coverage; not separately exercised in the current packaged happy-path run. Pressure release during reserved/customer-owned checkout needs a policy decision. |
| Plastic pressure | Persistent agitation pressure, impact buildup, strict tolerance, same-cap ejection, orientation-dependent Chaos propulsion, conserved liquid loss, gravity settlement | Real staged-impact packaged flight rises about 9.45 m and ends at content 0.715. Human shaking/dropping and saving during powered flight remain unaccepted. |
| Replication and pooling | Existing canonical ISIDs and ASM/ISM services; received rocket snapshots survive client binding; full pool return resets old flight | Native receiving-client Rocketing/AtRest cases and real settled late join pass. No new wire representation, duplicate state owner or save-schema field was added for this repair. |
| Persistence and lifetime | Existing save/cash rules, pressure and cap data; GC-tracked initialized catalog; absent-world-safe shop teardown | Forced-GC full suite passes. Global original saves restored/hash-verified. Full packaged autosave interruption scenarios remain pending. |

## Validation results

| Check | Result |
|---|---|
| Native suite on Editor 11194 | **47 pass: 44 clean, 3 with warnings; 0 failed/unrun.** |
| Saved configuration/geometry audit | **756 pass** on 11182; later gameplay fixes do not alter the audited sizes/layout. |
| Coherent 11193 cook/archive | **Pass**, 120.8 seconds. |
| Packaged main-map startup | **Pass**, NullRHI smoke, 80 navigation tiles in 26 ms, normal engine quit/exit 0. This is startup coverage, not a human playthrough. |
| Packaged two-peer full loop plus settled late join | **All checks pass**. Bank 5450 cents, physical cash 0, two receipts; full/sealed manufacture, pressure flight and sustained 350 available stock. |
| Packet emulation | **All checks pass with engine-confirmed 75 ms outgoing lag and 2% outgoing loss on each peer**, plus settled late join. Bank 4750 cents, cash 0, two receipts. |
| Late-join transform/state | AtRest state, contents/cap/finances correct; **0.5756 cm** position error in both profiles. All test peers exit normally. |
| Human hardware input | Earlier desktop role/G/R routing was exercised, but **no complete player-driven core loop is accepted**. Current screen inspection timed out at the desktop app approval prompt. |
| DOCX layout | Questions file is saved and structurally readable; **visual pagination QA is blocked because the bundled renderer lacks LibreOffice**. |

The [documents skill](C:/Users/PC/.codex/plugins/cache/openai-primary-runtime/documents/26.1110.11258/skills/documents/SKILL.md) requires “Run `render_docx.py` to produce `page-<N>.png` images”. Its render attempt cannot find `soffice`, so the questions DOCX is a saved checkpoint with visual layout QA pending.

Different final bank balances reflect different customer shopping plans. Within each run, the independent peers agree exactly and the supply costs/accounting checks pass.

The driver reuses real services and owned RPCs, but stages existing bottles/coins/input positions. The pressure fixture inverts the actual manufactured bottle and calls its impact API; it does not inject a scripted trajectory. Native five-flavor filling coverage does not establish five separate human multiplayer playthroughs.

## Defects corrected during integration

1. Giant-size delivery spacing, station reach, queue spacing, bucket carrying and initial navigation were aligned to the preserved reference geometry.
2. Cooperative cap controls now claim the available role and contribute in the same authoritative request, eliminating an unavoidable pre-hold leakage gap.
3. Plastic pressure uses the existing persistent channel; the existing rocket component supplies Chaos thrust from the actual cap axis and settles using mesh bounds.
4. The bottle Blueprint rocket template incorrectly carried `RF_ClassDefaultObject` and used ActorComponent's CDO as archetype. An explicit backed-up in-place repair preserves authored settings and fixes cooked loading.
5. Native soft-reference supply/currency content was omitted by map-only cooking. Explicit bounded prototype cook rules now include it.
6. The initialized soft catalog was collected after about two minutes, making all Normal stock unrecognized. A transient strong reference fixes this; forced-GC and sustained packaged checks pass.
7. GC exposed timer cleanup through absent worlds in the shop components. Teardown now handles that lifetime correctly without changing live-world release behavior.
8. Client binding cleared an already received rocket snapshot, leaving late joiners at Idle. Binding preserves/reapplies incoming state; actual pool return still clears old flight.

## Changed files and recovery records

The complete Source/Scripts change manifest against this session's initial backup is `Saved/PrototypeResume_20261112/SessionChangedFiles.csv`. It distinguishes added files from edits and preserves pre-existing user work. Important groups are:

- Props and lifecycle: `ColaPropActor`, `ColaContainment`, `ColaPressure`, `PropCapAssembly`, `ActivePropState`, `InteractableProp`, `InteractablePropManager`, shared definitions/data assets and gameplay mass handling.
- Physics: `RocketProjectileComponent`, `RocketPressurePhysics`.
- Shop: procurement/catalog, production/manual capping, filling, customer flow/visits/cash, scanner/register/ATM/shelf components, owned-player actions and shared interaction geometry.
- UI: existing native shop terminal/production controls, stable button lifetimes and scrollable content.
- Navigation: scoped `NormalColaNavigationConfig` runtime bootstrap.
- Validation: native pressure/cap/mass/procurement/navigation/manual-capping/loop tests, new five-flavor filling test and the opt-in validation actor.
- Build and tooling: `PackageNormalColaPrototype.ps1`, `TestPackagedNormalCola.ps1`, `StartNormalColaPrototype.ps1`, scoped authoring/audit scripts and `RefreshBottleRocketBlueprint.py`.
- Configuration/content: prototype startup maps, explicit prototype cook directory, scoped prototype assets and necessary shared bottle template/material compatibility repairs. The original reference world geometry and excluded PSOs were not deliberately reauthored.

Read [the chronological checkpoint](Prototype_Progress_2026-11-12.md) for exact failed attempts and build transitions. Source/script/config/doc snapshots and SHA256 manifests are retained under `Saved/PrototypeResume_20261112`. No Git reset, staging, commit or push occurred in this session.

## Remaining work in priority order

1. **Designer decisions:** physical hot bulk-source hose/nozzle/valve and cooling workflow; separate shell/liquid temperature persistence; physical pressure failure during locked/customer-owned checkout. Questions and proposed options are in `Prototype_Decisions_and_Questions_2026-11-12.docx`.
2. **Human playthrough:** purchase, real G/hold-G/GG carrying and placement, fill, two-player seal, shelf stock, scan, payment/change, deposit and reinvest. Screen/input access needs to be available for this acceptance.
3. **Remaining interruption/network cases:** disconnect during filling/capping/checkout, active-flight late join, save/load during active pressure release, and real packaged autosave interruption. One moderate profile is not exhaustive network testing.
4. **Performance and warnings:** profile CPU/GPU/physics/replication/memory with intended customer/prop loads. Remaining warnings include ABP_Unarmed's thread-unsafe getter, fixture simulate/collision warnings, legacy NPC Arrow-parent fixup, client SetReplicates role warnings, and material/engine warnings in runtime logs. No zero-warning claim is made.
5. **Deferred content:** raw ingredient mixing, transport/carrier behavior beyond the fixed station assumption and full-game advanced systems remain outside this implementation pass.

An older isolated 11179 smoke-test instance was left open when desktop approval timed out. Its saves are separate from this candidate. All subsequent bounded test games close normally; do not use that older window as the current build.
