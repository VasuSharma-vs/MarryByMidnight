# Midnight Fizz: Giant Thirst — Game Design Documentation (GDD)

**Bottle and checkout update — 12–13 November 2026:** [Implementation/checkpoint](Bottle_Systems_Update_2026-11-12.md),
Editor **11208**, **61/62 native tests passing**, 24/24 cold asset checks passing.
Canonical travel depletion, empty ASM/ISM persistence, approved 0.05 kg shell plus
liquid mass, pressure morphs and stable checkout widgets are saved. The existing
liquid-height assertion fails. Per-launch motion-history reset needs a follow-up;
rendered/apex/real-remote acceptance is pending. Screen control was stopped with
Escape; the original editor scene/settings and all 86 pre-test saves are restored.

**Current bottle rocket mode — 12 November 2026:** [Original BP_Cola flight](Original_Bottle_Rocket_2026-11-12.md)
is the designer-approved prototype default; the prior Chaos-thrust mode remains
optional. Final Editor **11207** builds; **57/58 native tests pass**, including a
clean original-flight regression. The existing liquid-height assertion is the
only failure. Visual/real-remote flight acceptance and packaging remain pending.
Editor reopened on the saved validation map, PIE stopped, settings and pre-test
saves restored. This mode decision supersedes older powered-flight wording below.

**Prop activation volume — 12 November 2026:** [Native component and guide](Prop_Activation_Volume_2026-11-12.md), Editor **11206**. Add a nonblocking box component or native placement actor to promote contained ISMs and retain ASMs until exit. Four new lifecycle tests pass; full suite **56/57 passes**, retaining the independent liquid-height failure. Editor restarted, saves restored, no automatic placement. Live remote-volume acceptance remains pending.

**Latest checkout repair — 12 November 2026:** [E interaction fix](Checkout_Interaction_Fix_2026-11-12.md), Editor **11205**. Actual keyboard E opens the checkout in standalone and on the listen-server host. Native suite: **52/53 pass**; an independent liquid-height assertion fails. Remote testing is blocked by a Prop Manager initialization `ReliableBufferOverflow` disconnect. PIE/settings/saves are restored. The older packaged candidate lacks this fix. Later real-level liquid feedback also leaves ASM/ISM visual acceptance pending despite the historical rendered comparison below.

**Latest liquid implementation — 12 November 2026:** [Fill and instancing correction](Liquid_Fill_Instancing_Fix_2026-11-12.md): Editor 11201, 51/51 native tests passing, SM6 compilation passing, and rendered local ASM/ISM orientation/fill comparison completed. Current world sizes and the reorganized material graph are preserved. This supersedes older liquid build/status notes; remote visual acceptance, profiling and packaging remain pending.

**Active implementation, 12 November 2026:** Read [the current checkpoint](Prototype_Progress_2026-11-12.md) and [play guide](Normal_Cola_Prototype_Play_Guide.md). Current Editor module 11194 passes 47 native tests (44 clean, 3 warning-bearing). Runtime Game/package 11193 builds and passes the instrumented packaged purchase/manufacture/stock/NPC/checkout/ATM loop, sustained stock, pressure flight and actual settled late join, both with no emulation and verified 75 ms outgoing lag / 2 percent loss per peer. Late-join position error is 0.5756 cm; all bounded test peers exit normally. All five real Normal recipes have native filling coverage, and all 756 saved configuration checks pass. Read First_Playable_Prototype_Report_2026-11-12.md for exact scope, files, evidence and limitations, plus the play guide for the candidate and Host/Client launcher. Hot-source hose/valve/cooling, thermal persistence and pressure during locked checkout need decisions in the questions DOCX. A full human hardware-input loop, active-flight join/interruption cases and measured performance remain unaccepted; desktop capture timed out at app approval. The complete first playable is not yet accepted. Original saves and PIE settings are restored and verified. Preserve current sizes, existing user work and historical evidence. No Git staging, commit or push occurred in this session.

**Latest equipment design — 11 November 2026:** The complete designer-supplied [Equipment Design Specification](Equipment_Design_Specification.md) is now part of the GDD. Only the **Manual Wheel Rotator** from its five-machine catalog is in the demo: two players cooperate using G-press downward hold with declining effectiveness and cap rotation, without electricity. The Cap Gun, Automatic Fill and Cap Machine, Pressure Regulator and Automatic Bottle Cleaner remain full-game-only. Existing bulk source/hose/dispenser/filling infrastructure stays in scope. [Equipment integration and conflicts](Equipment_Design_Integration_2026-11-11.md) flags G/grab input contention, carrier transport, mass/tuning and operator-session policy. Current player/NPC/bottle sizes and plastic/Normal/manual-twist scope are preserved. This is documentation, not completed implementation or testing.

**Latest prototype decision — 11 November 2026:** **Plastic Bottles + five Normal Colas + Normal Caps (Manual Twist) only.** [Plastic Bottle Pressure System](Prototype_Plastic_Bottle_Pressure_2026-11-11.md) requires hit/shake/drop/impact pressure buildup and a physics-driven cola rocket when internal pressure exceeds bottle tolerance, with motion/rotation affected by orientation and applied forces. Other bottle materials, cap types, advanced cola categories and fragmentation are excluded from prototype acceptance; preserve their full-game assets. Preserve current player/NPC/bottle sizes. The earlier glass/twist-cap design choice is resolved to plastic, with runtime alignment pending. Read [the master GDD](GDD_Master.md) and [conflict register](Chat_Design_Decisions_2026-11-11.md). Existing hot bulk-source/cooling, cash/ATM and Normal price rules remain. Pressure tuning and sealed-cap/held/empty/uncapped release details remain open. This documentation update does not implement or test the new subsystem; historical 11156 results do not accept it.

**Earlier functional evidence — 11 November 2026.** Read [the validation report](Normal_Cola_Prototype_Validation_2026-11-11.md) and [play guide](Normal_Cola_Prototype_Play_Guide.md). Giant navigation, repeated remote checkout/departure, ATM banking, remote procurement/fill/cap/restock, rendered supply purchase, client reconnect, prolonged queue waiting and real disk save/load have passing evidence on the earlier reduced-scale prototype. The latest completed native suite is 39 passing tests on 11156. Live pooling-warning verification was interrupted by the designer's scale correction; corrected-scale acceptance and the new hot-discharge/cooling workflow remain pending alongside hardware input, adverse-network/pressure edges, packaging and measured performance. Historical pause banners below have been superseded by the user's explicit resume.

**Development PAUSED — 11 November 2026, Normal Cola prototype.** The user requested a full checkpoint and safe pause. Read [the latest prototype pause checkpoint](PAUSE_CHECKPOINT_2026-11-11_NORMAL_COLA_PROTOTYPE.md) before doing any work. Module **11144 builds**; the last full suite on **11140 passes 38 tests**. Live two-player navigation and full-loop acceptance remain unfinished; the latest navigation authoring script failed. UE/build/tests are stopped, settings and save baselines restored. **Do not resume until explicitly asked.** All resume banners below are historical.

**Normal Cola prototype implementation resumed on 11 November 2026.** The latest user request supersedes the historical pause below. Read [the active prototype record](Normal_Cola_Prototype_2026-11-11.md): complete the buy/fill/cap/stock/NPC/checkout/reinvest loop for Lemonade, Mint, Sweet, Spicy and Zero-Calorie, using pre-mixed cylinders for the demo and configurable supply costs. Full-game raw-ingredient mixing and advanced cola mechanics remain deferred.

**Development PAUSED on 11 November 2026.** The user asked to pause work and log everything done. **Do not resume edits, builds, tests, or PIE until asked.** Read [the 11 November pause checkpoint](PAUSE_CHECKPOINT_2026-11-11.md) first: module **11133** (32 baseline automation tests) remains unchanged on disk, **live 2-player `PIE_ListenServer` Step 1 bottle validation failed** (premature bottle shatter on Client 1, Host `SetTimer`/aim miss, `Move actor to` / NavMesh / overlap / material / `BP_Bucket` collision root causes documented), checkout integration (`Steps 2–5`) was only inspected read-only (`Saved/InspectCheckoutLifecycle/`), PIE was stopped, `PlaySettings` and `Saved/SaveGames/` were restored and hash-verified, and 2 narrow Blueprint edits remain unsaved in open `UnrealEditor` (`PID 13032`) memory. The `11133` resumption note below is now superseded by this pause checkpoint.

**Historical (pre-pause) note — 11 November 2026:** The G/GG bottle pickup, release and trace-placement fix is saved, primarily in C++; module **11133** builds and all **32 automated tests pass** (one existing animation warning), including real windowless camera traces through saved player callbacks. Physical keyboard and remote-client acceptance remain pending. Read the [bottle-grab fix record](Bottle_Grab_Fix_2026-11-11.md). The [previous checkpoint](PAUSE_CHECKPOINT_2026-11-10_BOTTLE_GRAB.md) is historical; its pause instruction was superseded by the user's resume request.

The user requested a Gemini handoff. [Next-task implementation and test instructions](Gemini_Next_Task_2026-11-11.md) cover bottle multiplayer acceptance and completion of the existing customer checkout/departure loop, with approved rules, current APIs and pass criteria.

Welcome to the central design repository for **Midnight Fizz: Giant Thirst** (Working Title / Project `ColossalFizzCo`).

Current review: [Development progress report — 10 November 2026](<C:/Users/PC/Documents/Unreal Projects/ColossalFizzCo/Docs/Development_Progress_Report_2026-11-10.md>), with a [native class and Blueprint inventory](<C:/Users/PC/Documents/Unreal Projects/ColossalFizzCo/Docs/Development_Class_and_Blueprint_Inventory_2026-11-10.md>) and [audit evidence](<C:/Users/PC/Documents/Unreal Projects/ColossalFizzCo/Docs/Development_Audit_Evidence_2026-11-10.json>). This review distinguishes implemented code, production integration and tested behavior; earlier audit/progress documents remain historical records.

This documentation suite serves as the authoritative blueprint for all gameplay systems, artistic specifications, mechanics design, item databases, and technical implementation tasks in Unreal Engine 5.

## Primary design requirements and precedence

[Equipment Design Specification](Equipment_Design_Specification.md) is the designer's current five-type catalog; its Section 10 explicitly limits demo equipment from this catalog to the Manual Wheel Rotator while retaining essential filling infrastructure. [Equipment integration](Equipment_Design_Integration_2026-11-11.md) records the new cooperative closure requirement and remaining details. It supersedes older single-action closure acceptance and full-game F1-gun assumptions for the demo without restoring a mandatory bottle-eruption timer.

[Browser-chat decisions and conflicts — 11 November 2026](Chat_Design_Decisions_2026-11-11.md) records direct designer decisions separately from assistant recaps, superseded requirements and open questions. [GDD Section 2](GDD_Master.md#2-the-core-gameplay-loop) is the active demo loop. No old capping countdown, 125% detonation rule, PSI table, quadratic bottle rebate, plastic-disposable assumption or food migration plan overrides these decisions. The current request authorizes documentation reconciliation; it does not establish runtime implementation or acceptance of the new requirements.

[Scanner/checkout design update](<C:/Users/PC/Documents/Unreal Projects/ColossalFizzCo/Docs/Scanner_Checkout_Design_Update_2026-11-10.md>) records the latest approved live/saved scanning, locked invoices, post-sale expiry penalties, physical cash and rounding rules. It supersedes earlier expired-sale rejection, zero expired invoice value, and proportional overfill pricing/penalties.

[Current development scope](<C:/Users/PC/Documents/Unreal Projects/ColossalFizzCo/Docs/Current_Development_Scope.md>) records the user's exclusion of eleven legacy PSO assets. Their repair and food-container migration are deferred; older resume instructions do not override this scope.

[Core Design Decisions — Update of 10 November 2026](Core_Design_Decisions_Update_2026-11-10.md) contains the latest decisions on the shared wallet, fixed pricing, refill expiry, reuse, autosaving and pool expansion. It takes precedence over conflicting earlier documents.

[Core Gameplay & Bottle System Design Decisions](Core_Gameplay_Design_Decisions.md) contains the underlying primary requirements supplied on 9 November 2026. Follow it where the latest update does not replace a rule. Configurable values and example balance figures are not final hardcoded constants.

[Implementation alignment notes](Core_Design_Implementation_Alignment.md) record the superseded requirements, structural consequences, and remaining design details. The earlier [implementation audit](Documentation_Implementation_Audit.md) remains a historical assessment against the earlier specifications; its counts are not a completion assessment against the new requirements.

The user subsequently approved the temporary initial-component fragment representation in [Fragment actor integration](Fragment_Actor_Integration_Proposal.md). The supplied glass bottle now has eleven configured fragments; older eight-piece diagrams remain examples. [Continuation notes](Implementation_Continuation_2026-11-10.md) track the resumed implementation and validation.

---

## 📚 Document Index

1. [**Master Game Design Document (GDD_Master.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/ColossalFizzCo/Docs/GDD_Master.md)
   - High Concept, Game Pillars & The Scale Twist (Tiny Bartenders vs. Giant Customers).
   - Core Demo Loop (Buy → Fill → Seal/Cool → Stock → NPC Shop/Queue → Scan/Pay/Change → ATM Deposit → Reinvest).
   - Co-op Multiplayer Architecture (Shared Shop, Shared Wallet, Shared Inventory).
   - Day & Shift Cycles, Customer Satisfaction, and Shop Progression.

2. [**Mechanics, Tools & Stations (Mechanics_and_Tools.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/ColossalFizzCo/Docs/Mechanics_and_Tools.md)
   - **Station 1: Normal Bulk Cola Container & Filling Equipment** (Hot high-pressure supply → hose/dispenser → equipment → bottle; final tuning open; no demo electricity/self-heating).
   - **Station 2: Manual Wheel Rotator** (Demo: two-player downward hold/rotation on Normal twist caps and plastic bottles; other closures/F1 gun remain full-game reference).
   - **Station 3: Temperature & Chilling System** (Consumable temperature, thermal decay, ice baths).
   - **Station 4: Checkout Scanner & Pricing Engine** (Clamped fractional content, up-to-20% temperature discount, locked invoices, physical cash and ATM accounting).
   - **Station 5: Bottle Cleaning & Decontamination Station** (Reusability conditions, brush scrubbers, hazard sanitation).
   - **Station 6: Shop Vehicles & Heavy Machinery** (Fizz-Mule flatbed, Micro-Forklift cylinder hoister, Mobile Capper Buggy for drive-by bottle capping).

3. [**Items, Colas & Hazards Catalog (Items_Catalog_and_Hazards.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/ColossalFizzCo/Docs/Items_Catalog_and_Hazards.md)
   - **Current Material/Cap Authority:** Core decisions and chat reconciliation; tables with PSI/disposable plastic/indestructible metal/Crown Cork/Bolt-Lock/Rune Seal are marked historical proposals.
   - **Current Contents:** Five Normal demo flavors from the 21-type working catalog; legacy formula names/effects do not expand scope.
   - **Shaking & Agitation Mishaps:** Rocket launches, Fragile shatter, Toxic gas clouds, Spider swarms, Dizziness.
   - **Customer Demographics:** Giants, Golems, Witches, Goblins (height differences, scale challenges, preferences).
   - **Vehicle Fleet & Logistics Catalog:** Fizz-Mule Flatbed, Micro-Forklift, Mobile Capper Buggy (specs, fuel/battery, and roll-over hazards).

4. [**Technical Implementation & Unreal Engine 5 Architecture (Technical_Implementation_Plan.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/ColossalFizzCo/Docs/Technical_Implementation_Plan.md)
   - Integration map between workstation blueprints (`BP_ColaTank`, `BP_CappingStation`, `BP_Chiller`, `BP_CheckoutScanner`), `AShopVehiclePawn`, and UI/HUD specs *(legacy 64-bit packing details superseded by item 6 below)*.

5. [**Cola Types, Supernatural Hazards & Customer Compendium (Cola_Types_and_Patrons.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/ColossalFizzCo/Docs/Cola_Types_and_Patrons.md)
   - Dedicated encyclopedia for standard & supernatural colas (Poison, Spider, Radioactive, Ghost, Blood).
   - Catastrophic disaster reactions (Poison clouds, spider swarms, ionizing radiation contagion, sensory hallucinations).
   - Player countermeasure tools & safety gear (Gas masks, air-scrubbing blowers, bug swatters, lead barrels, holy salt, exorcism bell).
   - Supernatural patron roster (Golems, Werewolves, Witches, Vampires, Mountain Giants, Banshees) and wrath tantrums.
   - Standardized developer data templates for easily authoring new colas and monster patrons.

6. [**ISM Data Architecture & Zero-Copy Shared / Unique Split (ISM_Data_Architecture.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/ColossalFizzCo/Docs/ISM_Data_Architecture.md)
   - Part 1 baseline audit of all 17 `FPropProductDefinition` members, legacy `PropDatabase` / `IISMDB` duplication, 64-bit `FPropPackedRuntimeData`, and 3-float GPU data.
   - Part 2 zero-copy Category 1 dual shared registries (`ProductDefinitions` + `ContentDefinitions`), Category 2 split (`Continuous` vs `Event`), feature flags (`bEnableInstanceUniqueData`, `bEnableContinuousUpdateData`), 4-float GPU pipeline, `SchemaVersion = 2`, and proposed C++ header contracts (`[Needs Approval]`).

7. [**Active Prop Actor & Cola ASM Runtime Architecture (ActivePropActor_ASM_Architecture.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/ColossalFizzCo/Docs/ActivePropActor_ASM_Architecture.md)
   - 3-tier ASM state separation (Persistent Event mirror, Persistent Continuous mirror, Transient ASM-only state) eliminating per-actor shared-array copies.
   - Complete mapping of the Bottle Containment & Failure System onto `AActivePropActor` and `AColaPropActor`, `CanDemoteToISM()` gates, `PoolGeneration` timer safety, verified Phase B bug fixes, and `[Pre-Authorized]` C++ contracts.

8. [**Prop Manager & ISM <-> ASM Subsystem Architecture (PropManager_ISM_ASM_Architecture.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/ColossalFizzCo/Docs/PropManager_ISM_ASM_Architecture.md)
   - Orchestration across `APropManager`, `UASMComponent`, `UInteractableISMComponent`, and unified `UPropBatchUpdateComponent`.
   - Split replication channels (`Registry`, `Event` ordered by `EventSequence`, hot `Continuous`), monotonic `double WorldElapsedSeconds` clock, `EPropProductResetMode` conversions, safe `CustomActiveActorClass` swap, and `[Needs Approval]` C++ contracts.

9. [**BP_Cola & AColaPropActor Technical Documentation (BP_Cola_Technical_Documentation.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/ColossalFizzCo/Docs/BP_Cola_Technical_Documentation.md)
   - Bottle simulation and pooling reference; newer core/scanner decisions supersede its quadratic underfill economy and any conflicting closure/pressure claims. Existing source/tests establish implementation separately from this design reference.

10. [**Bottle Damage, Product Transformation & Fragment Lifecycle (Bottle_Damage_Transformation_and_Fragments.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/ColossalFizzCo/Docs/Bottle_Damage_Transformation_and_Fragments.md)
    - GPU-driven damage visuals (Glass crack `Lerp` vs Plastic WPO Vertex Texture via `Slot 3`), ASM <-> ISM persistence (`CapType` + `CapSealed` + `Strength`), 1-to-Many `Broken Glass Bottle` -> `Glass Fragment 1..N` independent lifecycles, `Melted Glass` / `Burst Bottle` transformations, and property inheritance matrix.

---

## 🛠️ Design Philosophy
Every feature in this game is designed to enforce:
1. **Physical Comedy & High Chaos:** Bottling high-pressure soda should be as volatile as handling live munitions.
2. **Cooperative Interdependence:** You cannot do it alone. Players must synchronize filling, capping, chilling, and serving.
3. **Skill-Based Timing & Precision:** Mechanical depth inspired by F1 pit stops and physics simulations.
4. **David vs. Goliath Wonder:** Physical scale contrast makes everyday tasks feel heroic and perilous.
