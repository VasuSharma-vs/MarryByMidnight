# Documentation-to-Implementation Audit

Audit date: **9 November 2026 (Asia/Calcutta)**. Project: **ColossalFizzCo / Midnight Fizz: Giant Thirst**, Unreal Engine association **5.8**.

## Assessment

The project contains a substantial legacy ISM/ASM prop framework, a native cola/rocket prototype, a cable-interaction prototype, and early checkout/economy Blueprints. The newer documented bottle containment, shared-content, damage, and fracture architecture is **an incomplete refactor**, not an integrated implementation. Several recently edited consumers depend on APIs that do not exist in the current provider headers/implementations. The full cooperative workshop loop is not established by the inspected implementation.

The highest-priority work is to restore source/header consistency, connect scanner execution, and finish authoritative persistent bottle state before building additional stations or hazards.

The matrices classify **102 consolidated requirements: 16 Implemented, 45 Partial, and 41 Missing**. These are equally counted audit rows, not a completion percentage: a schema definition and a complete vehicle system differ greatly in effort and scope. Several Implemented rows describe isolated deliverables inside the currently inconsistent source tree.

## Scope, method, and status definitions

All **11 original Markdown files in Docs** were read, including proposed contracts, revision histories, supersession notices, and open decisions. Requirements repeated across documents are consolidated in the matrices below. C++ declarations were checked against implementations and call sites; Content was inventoried; UE5 MCP was used to inspect live Blueprint parents, graph lists, graph DSL, individual node pins, class defaults, variable replication, data assets, and selected material expressions.

- **Implemented:** The specific behavior or narrow deliverable exists in inspected implementation. This does not certify the entire project builds or the gameplay passes runtime tests.
- **Partial:** Some code, schema, asset, or graph exists, but an integration, execution, configuration, or specification gap remains. A declaration or enum alone never earns an implemented gameplay status.
- **Missing:** No implementation of the documented behavior was found in the searched source and relevant project asset inventory. This is scoped negative evidence, not a claim that every imported asset or arbitrary Blueprint graph was exhaustively inspected.

This is a **static implementation audit with live editor inspection**, not a build, packaged-game, performance, multiplayer, or playtest certification. No C++ or gameplay assets were edited, compiled, saved, or played for this audit. Existing local modifications were preserved. The running editor's loaded native classes may come from an earlier build and must not be assumed to match the source on disk.

Baseline Git HEAD: `f133d1d6c60e765c2c8dfd63c245143ddf15fcec`. The working tree already contained modified prop-system files, modified assets, untracked cola sources, and untracked Docs. Findings apply to the inspected working tree, not just HEAD. See [source/file manifest](Audit_Manifest_2026-11-09.json) for hashes and [MCP evidence](Audit_Evidence_2026-11-09.json) for captured graph/default data.

### Evidence key

| Key | Source / evidence |
|---|---|
| DB | [InteractablePropDatabase.h](../Source/ColossalFizzCo/Public/Props/InteractablePropDatabase.h), [InteractablePropDatabase.cpp](../Source/ColossalFizzCo/Private/Props/InteractablePropDatabase.cpp) |
| DA | [InteractablePropDataAsset.h](../Source/ColossalFizzCo/Public/Props/InteractablePropDataAsset.h), [InteractablePropDataAsset.cpp](../Source/ColossalFizzCo/Private/Props/InteractablePropDataAsset.cpp) |
| PM | [InteractablePropManager.h](../Source/ColossalFizzCo/Public/Props/InteractablePropManager.h), [InteractablePropManager.cpp](../Source/ColossalFizzCo/Private/Props/InteractablePropManager.cpp) |
| BASE | [InteractableProp.h](../Source/ColossalFizzCo/Public/Props/InteractableProp.h), [InteractableProp.cpp](../Source/ColossalFizzCo/Private/Props/InteractableProp.cpp) |
| COLA | [ColaPropActor.h](../Source/ColossalFizzCo/Public/Props/ColaPropActor.h), [ColaPropActor.cpp](../Source/ColossalFizzCo/Private/Props/ColaPropActor.cpp) |
| ISM | [InteractableInstancedStaticMeshComponent.h](../Source/ColossalFizzCo/Public/Components/InteractableInstancedStaticMeshComponent.h), [implementation](../Source/ColossalFizzCo/Private/Components/InteractableInstancedStaticMeshComponent.cpp) |
| POOL | [ASMComponent.h](../Source/ColossalFizzCo/Public/Components/ASMComponent.h), [ASMComponent.cpp](../Source/ColossalFizzCo/Private/Components/ASMComponent.cpp) |
| BATCH | [PropBatchUpdateComponent.cpp](../Source/ColossalFizzCo/Private/Components/PropBatchUpdateComponent.cpp) |
| ROCKET | [RocketProjectileComponent.h](../Source/ColossalFizzCo/Public/Components/RocketProjectileComponent.h), [implementation](../Source/ColossalFizzCo/Private/Components/RocketProjectileComponent.cpp) |
| BP | [Audit_Evidence_2026-11-09.json](Audit_Evidence_2026-11-09.json): exact Unreal object/graph paths, node identifiers, pins, defaults, and material channel properties |
| INVENTORY | [Audit_Manifest_2026-11-09.json](Audit_Manifest_2026-11-09.json): original Docs/source/config hashes and Content asset paths |

Line references below are one-based references to the inspected source snapshot. Symbols remain the more stable reference after subsequent edits.

## Critical integration findings

### A. Source providers and consumers do not agree

This is direct static evidence of build blockers, not a compiler result:

1. `UPropBatchUpdateComponent::ProcessBatchUpdate` and related setters read `Manager->WorldElapsedSeconds`, call `FindUniqueData`, `FindContentDefinition`, `Multicast_SyncContinuousBatch`, `ChangePropProductWithMode`, and `SubmitInstanceEventUpdate`. These members are absent from the current `APropManager` header and implementation. See BATCH lines **55, 90, 337, 341, 460**, and PM's current class declaration.
2. BATCH calls `AColaPropActor::SubmitFailureRequest` at lines **337 and 359**. COLA declares and defines no such method. The failure request struct exists in DB, which does not implement the caller's behavior.
3. BASE's header declares a new five-argument `InitializeFromISIDRecord`, defines the three-argument form inline as a wrapper, and declares unique-data-aware write-back. Its `.cpp` still defines the old three-argument initialization and one-argument write-back. The new callbacks/getters, including `OnApplyContinuousData`, `OnApplyEventData`, `CanDemoteToISM`, and `ResetTransientPoolState`, have declarations without matching definitions in that `.cpp`. Legacy fields used extensively by BASE/COLA `.cpp` have also been replaced in the header.
4. ISM's header removes `PropDatabase` and `IISMDB`, but PM still accesses them, including `InComponent->IISMDB` around **948–984** and `CompPair.Value->PropDatabase.Empty()` around **7767**.

**Consequence:** New schema, ISM, batch, and pool work cannot be classified as a functioning end-to-end system. A running editor does not disprove these source inconsistencies.

### B. Scanner pricing is present but its execution entry is disconnected

Live object: `/Game/DirtyActors/BP_ProductSceaner.BP_ProductSceaner:EventGraph`.

- The overlap entry `K2Node_ComponentBoundEvent_0` executes `K2Node_Message_1`, then `K2Node_CallFunction_78` (Print String). That print node's `then` pin has **no links**.
- The intended pricing chain starts at `K2Node_Message_3` (CanIntract), whose `execute` pin has **no links**. Its downstream branch, bill/penalty setters, and temporary-map insertion therefore have no execution path from that overlap entry.
- Portion, temperature, multiplier, and quadratic-rebate arithmetic nodes exist. Node/pin inspection was necessary because graph DSL excludes disconnected execution chains.
- The sale interaction updates the shop GameState wallet, but depends on the temporary map being populated. This graph does not establish a successful scan-to-sale path.
- End-overlap removes the map entry before reading it through `K2Node_CallFunction_160` (Map Find), so the subsequent subtraction cannot reliably recover the removed price.
- The scanner's loaded default `bReplicates` is **false**, and its inspected sale chain has no authority switch/server RPC gate. Replicated GameState variables alone do not establish server-authoritative checkout.

### C. Resting and active meshes use different GPU channel layouts

ISM now writes `[ContentLevel, ExpiryAge, TemperatureCelsius, StrengthRatio]` in `UpdateInstanceCustomDataDirect` (**ISM .cpp:720**). BASE still writes `[PortionRatio, FrostFactor, ExpiryAge]` with no fourth channel (**BASE .cpp:497**). This can change appearance on promotion/demotion even before damage visuals are implemented.

Selected live masters reinforce the incomplete migration: `M_BaseGlass` has direct PerInstanceCustomData reads at indices **0, 1, 2**; the inspected `ColaMaterials/Liquied/M_BaseLiquid` reads index **0**, with scalar parameters not enabled for Custom Primitive Data. Neither inspected master supplies evidence of the specified direct Slot-3 strength pipeline. Nested material functions and all possible material variants were not recursively proven; glass crack and plastic VAT behavior remain unverified/partial rather than assumed complete.

### D. Live tuning differs materially from the docs

| Inspected asset/default | Observed | Documented expectation |
|---|---|---|
| DA_BasicCola | Price **25**, portions **135**, ideal temperature **5°C** | $2.50/portion, commonly 4 portions, 4°C |
| DA_SweetCola | Price **50**, portions **135**, ideal temperature **5°C** | $3.25/portion, 3°C |
| DA_SpicyCola | Price **70**, portions **135**, ideal temperature **5°C** | $4.50/portion, 8°C |
| DA_EmptyBottel | Price **3**, portions **0**, temperature **5°C** | Scanner example assumes empty glass cost $4; physical material-specific costs need definition |
| BP_Cola / BP_SpicyCola / BP_SugerCrushCola / BP_PlastickBottel native defaults | All: tolerance **2**, BasePrice **10**, jerk threshold **1.5**, overfill multiplier **3** | Spicy tolerance 1.6 / jerk 1 / surge 4.5; Sugar tolerance 1.8 / jerk 1.2 / surge 4 |
| BP_Cola PropDataAsset | **DA_Cola** | Comparison table identifies DA_BasicCola / CID 1 |
| Scanner defaults | Bottle cost **4**, fallback max portions **4** | Content assets currently define **135** portions |

These are defaults read from the live editor, not a claim about every placed instance override. The Blueprint-local legacy variables are separate from the inspected native properties; duplicate state is itself a migration risk.

## 1. Shared data, persistence, rendering, and networking

Primary specifications: ISM Data §§1–2; Prop Manager §§1–9; Technical Plan §§1–2 and 4 (respecting its supersession notice).

| ID | Documented feature/specification | Status | Evidence and remaining gap |
|---|---|---|---|
| A01 | Permanent ISID identity; reusable ISMI slots; parking instead of deleting on promotion | Implemented | PM registration/promotion/conversion/release paths and ISM identity maps/EmptyInstances implement the legacy lifecycle. New state preservation remains separate below. |
| A02 | Central shared product definitions and data-asset registration | Implemented | PM `ProductDefinitions`, `RegisterProductFromDataAsset`, `FindProductDefinition`, `InitializeIISMDB`; DB `FromDataAsset`. |
| A03 | Central shared liquid registry with registration, lookup, initial/on-register replication | Partial | DB defines `FPropContentDefinition` and a snapshot array, but PM lacks `ContentDefinitions`, its lookup/registration APIs, and corresponding replication. |
| A04 | Sparse per-ISID unique data, zero allocation when feature disabled | Partial | DB types and BASE `LocalUniqueData` exist; PM has no `UniqueDataByISID` owner or `FindUniqueData`. Static zero-cost runtime allocation is not established. |
| A05 | Continuous/Event struct split and CapType persistence field | Implemented | DB `FPropContinuousUpdateData`, `FPropEventUpdateData`, `FPropInstanceUniqueData` include the documented fields. This status is for schema presence only. |
| A06 | Product/data-asset capacity, strength, thermal, cap, conversion, and fracture configuration fields | Implemented | DA header, DB product definition, and `FromDataAsset` copy these settings. Runtime execution is not implied. |
| A07 | Feature toggles and invariant: continuous requires unique state | Partial | Fields and `FromDataAsset` coercion exist. Coercion also forces unique data for expiry or any positive portion count, beyond explicit opt-in. PM sparse storage/registration validation is missing. |
| A08 | Remove per-slot PropDatabase and component IISMDB duplication | Partial | Removed from ISM header/replication and local query code changed; PM still references removed members. Refactor cannot integrate as written. |
| A09 | Zero shared-data copies on pooled actors; on-demand shared/content resolution | Partial | BASE header introduces getters and a full `CachedFallbackProductDef`; `.cpp` still copies arrays/profiles and uses removed legacy fields (`ApplyProductDefinition`:451–473). No completed manager/content resolver path. |
| A10 | On-demand legacy query synthesis from product + content + unique state | Partial | ISM `GetPropDataForInstance` calls updated `SyncFromISIDRecord`; DB supports synthesis. Required manager content/unique APIs and PM's own legacy bridge are incomplete. |
| A11 | Four-float ISM data writer with raw temperature and normalized strength | Implemented | ISM constructor/OnRegister set four floats; `UpdateInstanceCustomDataDirect`:720 writes correct layout and derives age. Provider/integration blockers still apply. |
| A12 | Same four-float layout on active static and skeletal meshes | Partial | Header documents new layout; BASE `.cpp:497` still writes old three-float layout. |
| A13 | Immutable creation time and persistent monotonic double world clock | Partial | DB adds double timestamps/save field; PM still uses world-relative minute clock (`GetCurrentWorldMinutes`:326), has no double clock member/tick/restore. |
| A14 | Schema 2 save/load with conditional unique state and clock persistence | Partial | DB world-save defaults to 2 and serialized instances include UniqueData; PM `SchemaVersion` defaults to **1**, serializers at 7892/7925 omit UniqueData, save/load omit clock. |
| A15 | Automatic schema 1-to-2 migration | Missing | No version-specific conversion branch in PM load/deserialize. A schema default change is not a migration. |
| A16 | Existing logical save/load, destroyed-ID list, slot repair, actor restoration | Implemented | PM `SavePropDatabase`, `LoadPropDatabase`, `SerializeInstance`, `DeserializeInstance`, `RepairLoadedISMI`, `RestoreActiveActors` implement legacy persistence. |
| A17 | NetSerialize for compact Continuous items and dirty-field Event deltas | Implemented | DB `.cpp:489,508` writes optional thermal strength and mask-selected event fields. Live transport is separate below. |
| A18 | Reliable Event transport, unreliable Continuous transport, sequence/stale-time rejection | Partial | Payloads and client guard fields declared; BATCH invokes new transport, but PM has no sync RPCs/handlers or event-submit API. BASE callbacks also lack definitions. |
| A19 | Registry sync only on join/registration; recurring deltas contain no full definitions | Missing | PM `BuildBulkUpdateBatch`:7041 and `SendInstanceDelta`:7106 still append ProductDefinitions to recurring batches. |
| A20 | Client-predicted promotion, reconciliation without transform snapping | Partial | Legacy prediction/physics correction exists; POOL sets `bPendingServerPromoteAck`. New EventSequence/LastDiscreteEventTime reconciliation path is not implemented in PM/BASE. |
| A21 | Exact 48-byte unique state, 12–16-byte wire items, zero-heap performance claims | Partial | Structs/serializers support the intended layout direction, but exact ABI sizes, map overhead, packet framing, and allocation/bandwidth claims were not measured or asserted. Serializer adds a presence bit beyond its 12/16-byte scalar payloads. |

Contract detail: the principal capacity, pressure-threshold, deformation-flag, thermal, cap, transformation, content-viscosity, taste and expiry fields exist in DB/DA. The documented `FailurePriorityOverrides` configuration was not found; there is no failure manager to consume it. Hazard enums currently cover FoamRocket, FirePatch, ToxicCloud, SpiderSwarm, StickySlow and Radiation; the additional Cryo/Luxury/Ghost/Blood/Hangover behaviors are not implemented merely by this enum. Early Technical Plan `FBottleRuntimeState` and legacy cap/cleanliness packing are explicitly superseded; their exact names are not required to be recreated, although active cleaning requirements still need a persistence representation.

## 2. Pooling, thermal updates, conversion, and fracture

Primary specifications: Prop Manager §§4–7; Active Prop §§3–5 and 7; Bottle Damage §§1–8.

| ID | Documented feature/specification | Status | Evidence and remaining gap |
|---|---|---|---|
| B01 | Dynamic actor pool, minimum 100 / maximum 500, reuse and settling queues | Implemented | POOL configuration, acquisition, pool expansion/trimming, active/empty mappings and settling implementations exist. No scale benchmark was run. |
| B02 | Hydrate and write back exact float content/cap/strength/time through promotion/demotion | Partial | POOL now passes new five-argument initialization and unique-data write-back; BASE implementations remain old signatures and packed integer/minute state. |
| B03 | Generation-token-safe lifecycle reset and cleanup | Partial | BASE declares `PoolGeneration` and reset hooks, but `.cpp` does not implement the new reset contract; COLA still has raw-this cap timer. |
| B04 | Demotion gate for pressure, liquid/shell temperatures, leaks, closing timers and rocket state | Partial | POOL settling and physics component call `CanDemoteToISM`; BASE declaration lacks implementation, COLA has no override, direct PM conversion only checks legacy held/motion/rocket conditions. |
| B05 | Unified exponential background temperature/age/thermal-strength sweeper | Partial | BATCH has exponential integration, timestamp advance, GPU refresh, thermal wear, deferred failure and active-source delegation. PM clock/registry/transport and BASE callbacks are missing. POOL's older thermal method is now a no-op. |
| B06 | Ambient/thermal-zone temperatures and external temperature setters | Partial | BATCH ambient calculation, batch delegates, and setters exist. Its integrated solver uses Manager AmbientTemperature; a complete registered-zone resolver and configured ice/fire/freezer stations were not found. |
| B07 | Multi-source active thermal solver; separate ContentTemperature; safe equilibrium demotion | Missing | FPropThermalSource schema and BASE HasActiveThermalSources declaration exist. No native active dual-temperature solver or COLA implementation/override was found. |
| B08 | Background extreme-temperature failure with configurable silent conversion/promotion | Partial | DA/DB policy and BATCH deferred branches exist, but they call missing PM mode conversion / COLA SubmitFailureRequest APIs. Branches use ContentReset rather than documented PreserveUniqueState behavior. |
| B09 | PreserveUniqueState / ContentReset / FullReset conversion semantics | Partial | Enum exists; PM still exposes bool reset API and resets packed creation minute when true. No `ChangePropProductWithMode` implementation. |
| B10 | Safe active subclass swap preserving hold socket and velocities | Partial | POOL `SwapActiveActorClass`:1079 captures state/socket/velocities and reacquires actor. PM `ChangePropProduct`:2423 never calls it; it applies new visuals to the existing class. |
| B11 | Initial ISM impact uses unclamped effective impulse, bypasses warmup for damage | Partial | BASE declares an impact hook; PM's hit method only forwards to SetMeshInstanceToUnsettled. Physics impulse still clamps to 220, COLA ignores warmup impacts; no damage hook call/definition. |
| B12 | Product transformation and fracture recipe/context schemas | Implemented | DB contains EPropTransformationType, FPropFractureFragmentEntry, FPropTransformationContext, target IDs and hull policy. |
| B13 | Central one-to-many request retires original ISID and invokes broken-bottle orchestrator | Missing | No PM RequestProductTransformation or ABrokenBottlePropActor implementation/class in Source or matching relevant Blueprint asset in inventory. |
| B14 | Independent shard IDs, inherited time/temp, fresh strength, deterministic scatter | Missing | No SpawnActiveFragmentInstance or fragment orchestration implementation. General prop IDs/physics are reusable foundations, not a fracture implementation. |
| B15 | Pool-full grounded ISM fallback; independent shard cleanup; optional broken hull | Missing | Fields/recipes describe policy only. No fracture spawn loop, grounding fallback or hull completion logic found. |
| B16 | Melted/burst product conversion with property-transfer matrix and recursion guards | Partial | Target IDs and legacy generic PID conversion exist; native failure-to-transform route, inheritance matrix execution, residue/cap handling and fracture recursion guards are absent. |
| B17 | Persistent glass crack Lerp and plastic VAT/WPO dents driven by Slot 3 | Partial | Strength field and ISM Slot 3 producer exist. No damage accumulation in COLA; active layout still old; inspected material masters do not establish the new strength channel. No verified plastic VAT pipeline. |

## 3. Bottle behavior and native cola API

Primary specifications: BP_Cola Technical §§3–6; Active Prop §§5–7; Bottle Damage §§2–4; GDD §5.

| ID | Documented feature/specification | Status | Evidence and remaining gap |
|---|---|---|---|
| C01 | Native BP_Cola inheritance with plastic, spicy, sugar children | Implemented | MCP confirms BP_Cola parent ColaPropActor; three children parent BP_Cola. Their inspected main logic graphs are empty or only parent event calls. |
| C02 | Liquid EmptyLevel/OverfullLevel morphs, foam ratio, smooth interpolation | Implemented | COLA `UpdateMorphTargets`:491 uses fill + foam, clamps morph weights, snaps warmup, interpolates at 12. |
| C03 | Slot-1 dynamic liquid level and velocity/angular-velocity slosh parameters | Implemented | COLA `UpdateDynamicMaterial`:458 writes Liquid Level, Use Dynamic Values, Object Velocity and Object Angular Velocity. Live liquid master has corresponding liquid/slosh infrastructure. |
| C04 | Lazy liquid material keyed by ContentID; pressure/temp/age/strength shader inputs; pool restoration | Partial | Current MID creation depends only on material availability; no ContentID gating or pressure/temp/age/strength parameter writes; no completed reset hook restoring original material. |
| C05 | Content selection and supported-liquid checks on empty bottles | Missing | SupportedContentIDs/FPropContentDefinition exist; COLA has no SetBottleContentType implementation/API. |
| C06 | Frame-independent FillBottle(rate, dt) returning accepted fractional portions with slowdown | Partial | COLA `.cpp:318` exposes void FillBottle(float), directly increments level and clamps to 2. No dt, accepted-volume return, slowdown curve, content/failure validation or tank accounting. |
| C07 | Independent overfill warning/hard cap versus pressure-failure thresholds | Partial | DA/DB fields exist; COLA still uses StabilityTolerance on fill/foam total. Basic native tolerance 2 with level clamped to 2 means fill-only `>` check cannot fire at the default. |
| C08 | Content draining and fractional consumption; reusable empty PID conversion; sealed-consume gate | Partial | Legacy ConsumePortion exists but decrements one integer regardless of fractional PortionRatio, ignores CapSealed, and can release/destroy empty objects. No RemoveBottleContent or EmptyBottleProductID route. |
| C09 | Jerk-based held agitation, impact foam, idle foam decay | Implemented | COLA UpdateFoamPressure/HandleBottleComponentHit implement prototype behavior with held/attachment/teleport guards. Documented numerical 0–100 agitation model is separate below. |
| C10 | Distinct base/interaction pressure, content/resistance multipliers, impulse-ID dedup and two vent modes | Partial | Enums/shared settings exist; COLA uses ColaLevel + mapped FoamPressure. No AccumulatedInteractionPressure, event dedup sets, cooldown-based impulse bookkeeping, or ReleasePressure API. |
| C11 | Monotonic structural Strength wear from pressure/heat/hits; temporary deformation; gated repair | Missing | Strength storage/configuration and BATCH thermal branch exist, but COLA has no structural solver, permanent/temporary deformation split or RepairBottle. No complete authoritative damage behavior. |
| C12 | Four-stage cap compatibility/attachment/closing/validation with Auto/Event trigger | Partial | COLA AddCap only creates/attaches a component; InProductID is unused to choose mesh/style. No four-stage state/torque/validation or persistent seal updates. |
| C13 | Twist/Pop/Break/Pull/Cut removal, safe-open containment and cap identity | Partial | Five-mode enum exists in DB; COLA RemoveCap only accepts strength plus EjectPhysics/DropQuietly. No hardware/pressure/environment check or CapSealed/CapType event update. |
| C14 | Central failure priority/dedup/lockout and single hazard/transformation dispatch | Missing | Failure enums/request struct exist; SubmitFailureRequest/ContainmentFailureManager is absent from COLA; direct LaunchBottle bypasses that design. |
| C15 | Erratic rocket launch, cone, speed/distance limits, 8 flight and 4 falling bounces | Implemented | COLA LaunchBottle:337 configures Random3D, spacing 350–650, deviation 35–90°, 25° launch cone and documented clamps. ROCKET implements flight and settling. |
| C16 | Deterministic replicated rocket state, seed and launch/reset plumbing | Implemented | ROCKET FRocketFlightNetState, Multicast_LaunchRocket/Full, DOREPLIFETIME and ResetRocketSimulation exist. Network correctness was not playtested. |
| C17 | Quadratic native underfill penalty and payout helper | Implemented | COLA CalculateUnderfillPenalty/CalculateSalePayout:440–456 match BasePrice − BasePrice×underfill². No temperature factor/content price resolution in these helpers. |
| C18 | Prices resolve shared liquid metadata and scanner uses a single agreed formula | Partial | Native BasePrice is fixed/defaulted; scanner contains different rebate-based math. No integration to ContentDef BasePortionPrice or a common pricing implementation. |
| C19 | CapState/FailureState replication and one-shot FX by event ID | Missing | COLA only registers ColaLevel and FoamPressure for native replication. Specified cap/failure state properties/OnRep handlers are absent. |
| C20 | Preserved fractional/overfilled liquid and cap state across ISM/ASM | Partial | COLA promotion reconstructs fill/cap from integer portions; pre-return floors level×portions and clamps to MaxPortions (536–609). No persistent cap/seal restoration. |

## 4. Stations, shop loop, vehicles, and HUD

Primary specifications: GDD §§1–7; Mechanics §§1–6; Technical Plan §§3–5; Items §6.

| ID | Documented feature/specification | Status | Evidence and remaining gap |
|---|---|---|---|
| D01 | Bulk cylinder/nozzle, cable attachment, tension and player movement feedback | Partial | BP_ColaTank has cable attachment, replicated interaction/tick functions and tension/gravity/braking graph logic. No verified 100kg transport restriction, 350-unit release/trip rule or manifold supply accounting. |
| D02 | Nozzle trace/use input fills at 25%/sec, deducts accepted tank volume, works on mobile tanks | Missing | Inspected BP_ColaTank EventGraph contains cable interaction but no FillBottle call or depletion logic. Native FillBottle is available but no complete dispenser path was found. |
| D03 | 85–100% optimal / 101–125% danger / >125% detonation and specified portion bands | Partial | Fill/foam thresholds exist as a different prototype; no documented zone mapping. New target architecture also changes max-overfill semantics, requiring reconciliation. |
| D04 | Stationary F1 capper, 5/3/2.5-second urgency, torque windows and failure outcomes | Missing | No BP_CappingStation, torque timer/validation gameplay, WBP_TorqueDial or Server_SubmitTorqueResult implementation found. Basic AddCap does not fulfill the station. |
| D05 | Ice trough: 6 bottles, ice supply, −1.5°C/sec | Missing | Generic temperature hooks exist; no corresponding station implementation/configured asset found. |
| D06 | Blast freezer: 2°C in 3 sec; >10 sec freeze expansion/shatter | Missing | No freezer station/timer/expansion failure path found. |
| D07 | Portion/temp scanner, quadratic bottle rebate, total accumulation | Partial | BP_ProductSceaner contains arithmetic and maps, but scan execution is disconnected; defaults/tuning differ and map removal lookup is ordered incorrectly. See finding B. |
| D08 | Shared replicated wallet and sale/loss counters | Partial | BP_ShopGameState fields are Replicated; BP_FirstPersonGameMode selects it; scanner has credit/counter nodes. Complete authoritative scan-to-sale execution is unproven/broken as inspected. |
| D09 | Shared procurement/inventory, rent, quota, shop upgrades and progression | Missing | No integrated purchase/debit/stock/quota/rent/upgrade system found. Wallet fields alone do not fulfill this requirement. |
| D10 | Customer pickup pedestal, giant drink animation and dirty return chute | Missing | Generic shopping/NPC/checkout assets exist, but no specified giant-delivery/return lifecycle found. |
| D11 | Dirt/infestation states, scanner contamination rules, sink scrub 2.5 sec, drying rack | Missing | PSO_Sink inspected graph only forwards parent events; no bottle sanitation implementation established. No persistent cleanliness field or documented scrub/contamination workflow found. |
| D12 | Material-dependent reuse, single-use plastic, metal/stone cleansing, two-player heavy lift | Partial | Material enum, empty-conversion fields and plastic Blueprint exist. Native empty consumption still follows legacy destroy/release; no completed washing/cooperative carrying rules. |
| D13 | Fizz-Mule: 750cm/s, 12 bottles, battery dock, passenger and magnetic cargo lock | Missing | No named vehicle/gameplay implementation found. VehicleTemplate and BP_ChargoTruck do not provide these mechanics; truck inspected graph only opens a door morph. |
| D14 | Micro-Forklift: 400cm/s, 200kg lift, Q/E mast, manifold docking and steam hazards | Missing | No forklift/mast/docking/steam loop found. |
| D15 | Mobile Capper Buggy: 550cm/s, 30-cap hopper, 0.4-sec drive-by press | Missing | No specialized buggy or capper implementation found. |
| D16 | Vehicle bottle agitation, drift/crash impulses, rollovers, battery stalls and overheating | Missing | Generic Chaos vehicle templates exist but no documented cargo-to-bottle agitation/power/steam mechanics found. |
| D17 | Tiny 25cm team, giant countertop routes/ramps/lifts and scale interactions | Partial | First-person map and generic traversal/template assets exist; player/level/customer scale and authored production layout were not validated. No end-to-end tiny-worker/giant-customer implementation established. |
| D18 | Scanner HUD: portions, temperature, bill and flashing underfill warning | Partial | WBP_AmountDisplay/amount update calls and PrintString warnings exist; complete specified portion/temp/warning display not established. |
| D19 | Giant order-ticket UI, patience/anger meter and order queue | Missing | No WBP_GiantOrderTicket or integrated fantasy order/patience/reputation workflow found. Generic NPC shopping does not establish these features. |
| D20 | Shift timer/quota, 1–5 stars, bankruptcy/rent/destruction game-over | Missing | No documented shift/rating/win/loss implementation found in native sources or relevant Blueprint inventory/inspected GameMode/GameState. |
| D21 | Predicted dispensing/torque input with server validation | Missing | Legacy prop prediction exists; documented station input/torque RPC and timestamp validation are absent. |
| D22 | PC/Steam/console delivery and cooperative session flow | Partial | UE project/input/replication foundations exist. No packaged platform verification, Steam/session/lobby integration or complete co-op shop flow was established. |

## 5. Catalog, hazards, and fantasy patrons

Primary specifications: Items §§1–5; Cola Types §§1–6; GDD §§3 and 5; BP_Cola §6.

| ID | Documented feature/specification | Status | Evidence and remaining gap |
|---|---|---|---|
| E01 | Four bottle materials with exact weight/speed, pressure, break/melt and thermal distinctions | Partial | Material enum/shared flags exist; plastic Blueprint is present. No verified glass/metal/stone gameplay set or exact behavior/tuning matrix. |
| E02 | Twist/Crown/Bolt/Rune caps with pressure ratings, timing widths and rune suppression | Partial | Cap enums/mask/default-mesh/torque fields exist, plus basic cap mesh assets; hardware-specific execution/ratings/magic suppression are absent. |
| E03 | Basic, Sugar and Spicy bottle/content variants | Partial | Named assets and child Blueprints exist, but live native behavior settings are identical and data-asset price/portion/temp values diverge. No central CID mapping. |
| E04 | Fire/Spicy persistent flames, crate ignition and sprinkler reactions | Missing | Spicy child exists; inspected graph contains only parent events. No fire hazard behavior or dedicated BP_FireCola found. |
| E05 | Sugar sticky floor geyser, 50% slow and mop removal | Missing | Sugar child exists without specified hazard logic; StickySlow enum is not a puddle/cleanup implementation. |
| E06 | Toxic cloud: 4m, 20 sec, coughing/damage/drop, gas mask and fans | Missing | ToxicCloud enum only; no DA_ToxicCola or corresponding gameplay/countermeasure assets/implementation found. |
| E07 | Spider swarm: cap theft, webs, 70% vehicle slowdown, bites; swatter/sprayer | Missing | SpiderSwarm enum only; no spawning/AI/web/cleanup implementation or DA_SpiderCola found. |
| E08 | Hangover vapor: 8 sec input inversion, blur and ragdoll sway | Missing | Generic NPC dizziness/ragdoll systems exist, but no cola-driven player inversion/haze implementation or DA_HangoverCola found. |
| E09 | Cryo blast: frozen puddles/player encasement | Missing | No DA_CryoCola or specified flash-freeze gameplay found. |
| E10 | Luxury golden flash: 4 sec blinding | Missing | No DA_LuxuryCola or cola flashbang implementation found. |
| E11 | Radiation field: bottle contagion, sprint disable, Geiger/static, lead tools and chemical cleanup | Missing | Radiation enum exists; no DA_RadioactiveCola, contagion, protection or decontamination loop found. |
| E12 | Ghost cola: fake orders/audio, 10 sec inversion, floating props, salt and exorcism bell | Missing | No DA_GhostCola or specified hallucination/poltergeist/countermeasure implementation found. |
| E13 | Blood cola: bats/darkness, werewolf patience halving, UV/garlic/mop | Missing | No DA_BloodCola or described hazard/reaction/tool implementation found. Imported blood decals are unrelated evidence. |
| E14 | Exact 0–100 agitation additions, 30/65/90 thresholds, decay and presentation cues | Partial | Native jerk/impulse foam prototype uses 0–10 foam plus fill; no unified documented A model or complete cues. The docs themselves disagree on additions. |
| E15 | Golem tastes/temperature rejection and EMP affecting scanner/vehicles/bottles | Missing | Generic NPC taste/temp simulation exists; no golem archetype/EMP shop integration found. |
| E16 | Werewolf short patience, blood reaction, leap/slash/howl rampage | Missing | No specified archetype/reactions found. |
| E17 | Witch poison/spider orders, dirt preference, reverse conveyor/pumpkin/ice-fire curses | Missing | No specified archetype or curses found. |
| E18 | Vampire pristine-only service and shadow fog/lantern relight | Missing | No specified cleanliness evaluation/archetype/shadow event found. |
| E19 | Mountain giant 3–4-keg orders, counter slam and +40 landing agitation | Missing | Generic physics impulses exist; no giant ordering/table-slam gameplay found. |
| E20 | Phantom/Banshee ghost orders and scream opening loose seals | Missing | No archetype, loose-seal broadcast or scream behavior found. |
| E21 | Pixie/Goblin sugar orders, counter traversal and cap theft | Missing | No specified archetype/behavior found. |
| E22 | Data-driven addition templates for colas and patrons | Partial | Generic data assets and NPC simulation provide foundations. Documented ReactionTimeLimit/FailureHazardActor/tool protection and patron Patience/FavoriteColas/Wrath configuration are not a complete authoring schema. |

## 6. Explicit Phase B bug-fix checklist verification

These correspond to ActivePropActor_ASM_Architecture §6; they are not new requirements added by this audit.

| Documented fix | Current result |
|---|---|
| Remove shared per-actor copies and separate definition/event/continuous application | **Unfinished:** new header API, old copying implementation; source/header mismatch. |
| Four-float CPD and stable exponential thermal updates | **Partial:** ISM/BATCH new code, BASE old three-float/Euler implementation. |
| Fractional, seal-aware consumption and reusable empty conversion | **Unfinished:** integer decrement and legacy empty destruction remain. |
| Do not overwrite FoamPressure during warmup/grab | **Unfixed:** COLA Tick still assigns zero during warmup/grace/transit at line 163. |
| Remove duplicate bWasHeldLastFrame overwrite | **Unfixed:** OnPlayerGrab still immediately assigns true after the computed value. |
| Guard delayed cap destruction against pool reuse | **Unfixed:** RemoveCap still captures raw this in an untracked 5-sec timer. |
| Restore cap from persistent seal; preserve fractional/overfilled liquid; clean cap/MIDs | **Unfixed:** cap inferred from positive portions; pre-return floors/clamps liquid; no completed reset contract. |
| FillBottle(dt) returns exact accepted portions; warns/slows/routes failure centrally | **Unfixed:** void FillBottle(float), direct level increment and direct LaunchBottle. |
| Bypass initial-hit warmup using unclamped authoritative damage hook | **Partial declaration only:** no PM invocation or COLA damage implementation. |

## 7. Documentation contradictions and stale claims

1. **Architecture completion language:** ISM Part 1 and Prop Manager “today/current” descriptions predate partial local edits. BP_Cola's executive summary reads as implemented, while its dependencies and companion documents describe Phase B proposals. Revision tables report documentation work, not proven code delivery.
2. **Two incompatible pricing formulas:** Mechanics/Technical Plan use `Nactual × Pportion × Mtemp + Cbottle × fillRatio²` (full rebate when full). BP_Cola uses `BasePrice × (1 − underfillDifference²)`. COLA implements the latter helper; scanner nodes encode the former. They cannot be treated as the same commercial rule.
3. **Underfill does not always cause a loss:** Mechanics §4.1 says an underfilled bottle guarantees net loss, but its own table gives **+$5.25 at 3/4** and **+$1 at half**. Profit depends on supply costs and temperature, not merely whether fill is below full.
4. **Temperature multipliers conflict:** Mechanics §3.3 says warm/hot **0.2×** and lukewarm **0.6×**; its pricing examples/Technical Plan use warm **0.5×**. Scanner has 1.2/1/0.5 nodes. Live cola assets use 5°C, outside the documented ≤4°C frosty band.
5. **Capacity/pressure models conflict:** Early workstation docs detonate above 1.25 fill; newer architecture allows 1.7 with separately gated overfill/pressure failure; native prototype clamps at 2 and uses StabilityTolerance. Decide the intended source of truth before tuning.
6. **Agitation values conflict:** GDD specifies running +5/sec and throwing/catching +35; Items says running +4/sec, throwing +30, catching +15. Native prototype uses jerk/impulse and 0–10 foam, not either table.
7. **Hazard quantities/durations conflict:** Items specifies 12–20 spiders and 4-sec cryo encasement; Cola Types specifies 15–30 spiders and 3-sec cryo encasement. These are design discrepancies, not implementation bugs.
8. **Variant identities/tuning conflict:** Live data assets/defaults differ from BP_Cola's CID and balance tables (finding D). Blueprint labels and legacy local variables do not prove the native settings are overridden.
9. **Memory/bandwidth claims need qualification:** 48 bytes is a proposed struct payload, not total per-prop memory: maps, IDs, transforms, packed mirrors, allocator slack, actor caches and network framing add overhead. Exact values require ABI checks/profiling. “Zero CPU cost” for resting props also excludes the intended background sweeper.
10. **Open decisions are partly fields, not delivered policies:** Uncapped demotion, thermal promotion, filled PID mapping, fragment allocation timing and persistent hulls remain described as awaiting sign-off. Configurable fields exist for some choices, but corresponding execution remains incomplete. This audit does not infer permission to implement them.
11. **Links and external dependencies:** Many Docs links use absolute file:/// paths and old line numbers. Prefer repository-relative links/symbol references. Bottle-containment text referenced under an external .gemini upload path is not in Docs; this audit covers its requirements reproduced in these documents, not unseen external text.

## 8. Coverage of every original document

| Document | Sections reviewed and cross-reference | Overall conclusion |
|---|---|---|
| [README](README.md) | Full index and design philosophy; checked referenced files and claims against all matrices | Index exists; it does not indicate implementation completion. Add a link to this audit when maintaining the index. |
| [GDD Master](GDD_Master.md) | §§1–7: C09–C17, D01–D22, E01–E21 | Physics foundations partial; complete service, logistics, fantasy patrons and progression missing. |
| [Mechanics and Tools](Mechanics_and_Tools.md) | §§1–6 including numeric tables/formulas: C06–C18, D01–D21 | Cable and early scanner/native helpers exist; production stations/vehicle fleet not implemented as specified. |
| [Items Catalog and Hazards](Items_Catalog_and_Hazards.md) | §§1–6: D12–D16, E01–E21 | Schema/prototype bottle variants exist; catalog behaviors and hazards mostly missing. |
| [Technical Implementation Plan](Technical_Implementation_Plan.md) | §§1–5, code sketches, supersession notice: A01–A20, C15–C19, D01–D22 | Legacy framework largely real; newer architecture incomplete; station/vehicle/HUD code sketches are not delivered classes. Superseded packing is historical, not a requirement to restore. |
| [Cola Types and Patrons](Cola_Types_and_Patrons.md) | §§1–6, all cola/patron profiles and addition templates: C10–C14, E03–E22 | Generic authoring/NPC foundations exist; supernatural disasters, protective tools, fantasy patrons and wrath events missing. |
| [ISM Data Architecture](ISM_Data_Architecture.md) | Part 1 and all Part 2 §§2.1–2.8, contracts, decisions, revision history: A01–A21, B05–B17 | Types/fields/ISM writer partly migrated; central ownership, clock, save and transport not integrated. |
| [Active Prop ASM Architecture](ActivePropActor_ASM_Architecture.md) | §§1–8, lifecycle contracts, all nine bug-fix rows: A09–A12, B02–B11, C01–C20, checklist above | Header refactor exists; implementation lags; the documented native cola containment upgrade is largely absent. |
| [Prop Manager Architecture](PropManager_ISM_ASM_Architecture.md) | §§1–10, channels, conversions, clock, decisions and contracts: A01–A21, B01–B16 | Legacy manager remains; new component consumers cannot integrate against it. |
| [BP_Cola Technical Documentation](BP_Cola_Technical_Documentation.md) | §§1–7, all component/feature/replication/variant tables: C01–C20, findings B–D | Morph/foam/rocket/basic economy helpers implemented; most newer containment/state/thermal/cap claims are not. |
| [Bottle Damage and Fragments](Bottle_Damage_Transformation_and_Fragments.md) | §§1–10, all ten fracture steps, both allocation options and transfer matrix: B11–B17, C11–C14, C20 | Schema and generic pool foundations only; fracture orchestrator, independent shard spawning, failure transformations and persistent damage visuals not established. |

## 9. Recommended implementation order and acceptance checks

1. **Restore a coherent source tree.** Finish or revert the partial header/component migration consistently; resolve missing PM APIs, BASE definitions, deleted-field accesses and COLA failure calls. Acceptance: a fresh Editor target build succeeds without relying on an already loaded binary.
2. **Finish authoritative persistent prop state.** Central registries + sparse unique storage, monotonic clock, initialization/write-back, reset modes, save schema migration, transport and stale-update rejection. Acceptance: fractional/overfilled content, cap state, strength and creation time survive repeated promotion/demotion and save/load; client/server values agree.
3. **Unify pricing and wire checkout.** Reconnect the overlap chain, read price before map removal, reconcile 4 vs 135 portions, select one formula, and put final credits/debits behind server authority. Acceptance: full/partial/empty and cold/warm bottles produce expected bills, removal reverses the preview correctly, and one sale credits once in two-client play.
4. **Complete bottle containment.** Content identity, dt-based accepted fill, cap stages, pressure split/dedup, structural damage, dual thermal solver, demotion gates and timer-safe pool reset. Acceptance: the explicit Phase B checklist passes, including immediate reuse within the cap's 5-second debris lifetime.
5. **Implement one fracture and one hazard path.** Retire source once, create independent shard IDs, inherit age/temp, enforce fresh fragment strength, handle full pool and cleanup. Acceptance: saving mid-flight and late network updates cannot resurrect the original or duplicate shards.
6. **Build a playable shop slice.** One functioning dispenser, capper, cooler, scanner, customer delivery/return and wash station, then quota/rent and a second player. Add specialized fleet, fantasy patrons and supernatural content after the base loop is reliable.

Deferred validation: fresh native build; Blueprint compile validation; two-client/late-join/disconnect tests; save/load migration fixtures; frame-rate comparison for filling; pressure/thermal boundary tests; repeated pool reuse; mass fracture at pool saturation; material visual review during transitions; packaged performance and platform/session checks. None are claimed as completed by this audit.
