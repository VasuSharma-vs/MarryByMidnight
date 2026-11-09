# ISM Data Architecture & Zero-Copy Shared / Unique Split

**Project:** Midnight Fizz: Giant Thirst (*MarryByMidnight*)  
**Primary C++ Sources (Read-Only Reference — C++ Edits Gated Pending Doc Approval):**  
- [`InteractablePropDatabase.h`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Props/InteractablePropDatabase.h)
- [`InteractableInstancedStaticMeshComponent.h`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Components/InteractableInstancedStaticMeshComponent.h)
- [`InteractablePropDataAsset.h`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Props/InteractablePropDataAsset.h)
- [`PropBatchUpdateComponent.h`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Components/PropBatchUpdateComponent.h)
- [`InteractablePropManager.h`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Props/InteractablePropManager.h)

This document is organized into two clearly separated parts:
- **Part 1: Current ISM Data (As Implemented Today)** — Baseline audit of all 17 members of `FPropProductDefinition` / `IISMDB`, legacy per-slot duplication (`PropDatabase` + `IISMDB`), 64-bit `FPropPackedRuntimeData`, replication/save structs, and 3-float GPU Custom Data.
- **Part 2: Target Architecture (Zero-Copy Shared Registries & Split Unique Data)** — Eliminates shared-data duplication across ISM/ASM instances, splits per-instance unique data into **Continuous Update Data** vs **Event Update Data**, integrates the Bottle Containment & Failure System, expands GPU Custom Data to 4 floats, and defines SaveGame `SchemaVersion = 2`.

**Related Architecture Docs:**
- [`ActivePropActor_ASM_Architecture.md`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Docs/ActivePropActor_ASM_Architecture.md) — `AActivePropActor` & `AColaPropActor` runtime mirror, transient ASM physics, and Containment Failure Manager.
- [`PropManager_ISM_ASM_Architecture.md`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Docs/PropManager_ISM_ASM_Architecture.md) — `APropManager`, `UASMComponent`, `UInteractableISMComponent`, and `UPropBatchUpdateComponent` orchestration.
- [`Bottle_Damage_Transformation_and_Fragments.md`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Docs/Bottle_Damage_Transformation_and_Fragments.md) — GPU-driven damage (`Glass` Crack `Lerp` vs `Plastic` WPO), `Broken Glass -> Glass Fragment 1..N` 1-to-Many fracture pipeline, and `Melted`/`Burst` transformations.

---

# Part 1: Current ISM Data (As Implemented Today)

In the current codebase, ISM data lives across four layers, three of which currently duplicate shared configuration data:

1. **Shared Product Definition (`FPropProductDefinition` / `IISMDB`)** — The 17-member struct stored in [`APropManager::ProductDefinitions`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Props/InteractablePropManager.h#L87) and redundantly mirrored into [`UInteractableISMComponent::IISMDB`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Components/InteractableInstancedStaticMeshComponent.h#L52).
2. **Batch Component State (`UInteractableISMComponent`)** — Bidirectional `ISIDToISMI` / `ISMIToISID` maps, `PackedInstanceData`, and legacy replicated `PropDatabase` (`TArray<FInteractablePropData>`).
3. **Authoritative Per-Instance Record (`FISIDRuntimeIndex` / `PIIDB`)** — Master record keyed by permanent `ISID`, plus `FPropInstanceDelta`, `FPropRuntimeFieldDelta`, and `FPropSerializedInstance`.
4. **GPU Per-Instance Custom Data (`PerInstanceSMCustomData`)** — 3 float channels (`NumCustomDataFloats = 3`) pushed to ISM shaders.

---

## 1.1 `FPropProductDefinition` (`IISMDB`) — All 17 Members from Editor Panel

Defined in [`InteractablePropDatabase.h`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Props/InteractablePropDatabase.h#L753-L830). Every entry currently contains **17 members**:

| # | Editor Display Name | C++ Field Name | C++ Type | Default Value | Current Role |
|---:|---|---|---|---|---|
| 1 | `Product ID` | `ProductID` | `int32` | `0` | Unique Product Definition Identifier (`PID`) |
| 2 | `Static Mesh ID` | `StaticMeshID` | `int32` | `0` | Static Mesh Identifier (`SMID`) |
| 3 | `Display Name` | `DisplayName` | `FText` | `"Interactable Prop"` | Human-readable product name |
| 4 | `Static Mesh` | `StaticMesh` | `TObjectPtr<UStaticMesh>` | `nullptr` | Resting ISM static mesh (and ASM fallback) |
| 5 | `Material Overrides` | `MaterialOverrides` | `TArray<TObjectPtr<UMaterialInterface>>` | Empty (`0`) | Per-slot material overrides |
| 6 | `Source Data Asset` | `SourceDataAsset` | `TObjectPtr<UInteractablePropDataAsset>` | `nullptr` | Optional `UInteractablePropDataAsset` source |
| 7 | `Custom Active Prop Actor Class (BP)` | `CustomActiveActorClass` | `TSubclassOf<AActivePropActor>` | `nullptr` | Actor class spawned on ISM to ASM promotion |
| 8 | `Skeletal Mesh` | `SkeletalMesh` | `TObjectPtr<USkeletalMesh>` | `nullptr` | Optional skeletal mesh used while in ASM |
| 9 | `Interaction Profile` | `InteractionProfile` | `FPropInteractionProfile` | *(Struct)* | Grab/use/consume flags, range, cooldown, permission tag |
| 10 | `Physics Profile` | `PhysicsProfile` | `FPropPhysicsProfile` | *(Struct)* | Velocity tolerances, damping, gravity, collision |
| 11 | `Consumable Profile` | `ConsumableProfile` | `FPropConsumableProfile` | *(Struct)* | Drink/food type, portion count, price, empty behavior |
| 12 | `Temperature Profile` | `TemperatureProfile` | `FPropTemperatureProfile` | *(Struct)* | Thermal update toggle, serving temp, exchange rate |
| 13 | `Stimulus Profile` | `StimulusProfile` | `TArray<FNPCSimulationStatModifier>` | Empty (`0`) | NPC stat changes applied per consumed portion |
| 14 | `Taste Profile` | `TasteProfile` | `FNPCTasteVector` | *(Struct)* | 9-axis taste weights when fresh |
| 15 | `Expired Taste Profile` | `ExpiredTasteProfile` | `FNPCTasteVector` | *(Struct)* | 9-axis taste weights when expired |
| 16 | `Expiry Profile` | `ExpiryProfile` | `FPropExpiryProfile` | *(Struct)* | Expiry lifetime, sickness, penalty, expired stimuli |
| 17 | `Runtime Defaults` | `RuntimeDefaults` | `FPropPackedRuntimeData` | *(Struct)* | Initial packed runtime values for new instances |

### Nested Fields Inside Members 9–17

- **9. `Interaction Profile` (`FPropInteractionProfile`):** `bCanGrab` (`true`), `bCanUse` (`true`), `bCanConsume` (`true`), `bCanOpen` (`false`), `bCanClose` (`false`), `bCanToggle` (`false`), `bCanActivate` (`false`), `bCanBreak` (`false`), `bCanRepair` (`false`), `bCanDamage` (`false`), `bCanInspect` (`true`), `bCanCombine` (`false`), `bCanSplit` (`false`), `bCanThrow` (`true`), `bCanPickUp` (`true`), `bCanDrop` (`true`), `bCanMove` (`true`), `bCanRotate` (`true`), `MaxInteractionDistance` (`350.0f`), `InteractionCooldown` (`0.1f`), `RequiredPermissionTag` (`FGameplayTag`).
- **10. `Physics Profile` (`FPropPhysicsProfile`):** `LinearVelocityTolerance` (`10.0f`), `AngularVelocityTolerance` (`15.0f`), `UnsettledDelay` (`3.0f`), `LinearDamping` (`0.05f`), `AngularDamping` (`0.05f`), `bEnableGravity` (`true`), `DefaultCollisionProfile` (`"BlockAllDynamic"`).
- **11. `Consumable Profile` (`FPropConsumableProfile`):** `ConsumableType` (`EConsumablePropType::Drink`), `NumOfPortions` (`4`), `Price` (`2.0f`), `bDestroyWhenEmpty` (`true`).
- **12. `Temperature Profile` (`FPropTemperatureProfile`):** `bEnableTemperatureUpdate` (`false`), `ConsumableTemperature` (`4.0f` °C), `TemperatureDopamineBonus` (`0.5f`), `ThermalExchangeRate` (`0.015f`).
- **13. `Stimulus Profile` (`TArray<FNPCSimulationStatModifier>`):** Pairs of `Stat` (`ENPCSimulationStat`) and `ChangeRate` (`float`, default `0.2f`).
- **14 & 15. `Taste Profile` / `Expired Taste Profile` (`FNPCTasteVector`):** 9 floats (`0.5f` default): `Spicy`, `Sweet`, `Salty`, `Sour`, `Bitter`, `Meat`, `Vegetarian`, `Healthy`, `Luxury`.
- **16. `Expiry Profile` (`FPropExpiryProfile`):** `bHasExpiry` (`true`), `ExpiryLifetimeMinutes` (`60.0f`), `SicknessLevel` (`35.0f`), `ExpiredDopaminePenalty` (`25.0f`), `ExpiryStimulusProfile` (`TArray<FNPCSimulationStatModifier>`).
- **17. `Runtime Defaults` (`FPropPackedRuntimeData`):** `CreationMinute` (`0`), `LastUpdatedMinute` (`0`), `RemainingPortions` (`4`), `Temperature` (`21.0f`), `bIsGrabbable` (`true`), `SchemaFlags` (`0`).

---

## 1.2 Current Per-ISM Component, Network, Save & GPU Audit (Known Issues)

* **`UInteractableISMComponent` Redundancy:**
  * Replicates `IISMDB` ([`InteractableInstancedStaticMeshComponent.h#L51-L52`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Components/InteractableInstancedStaticMeshComponent.h#L51-L52)), copying the entire shared product struct onto every ISM component.
  * Replicates `PropDatabase` (`TArray<FInteractablePropData>`, [`L101-L102`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Components/InteractableInstancedStaticMeshComponent.h#L101-L102)), duplicating `ItemDisplayName`, `StaticMesh`, `DataAsset`, `Price`, `NumOfPortions`, `ExpiryStimulusProfile`, `StimuliPerPortion`, `TasteProfile`, etc. on every single ISM slot ([`AddInstanceWithISID`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Private/Components/InteractableInstancedStaticMeshComponent.cpp#L296-L314), [`ReuseOrAddInstanceWithISID`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Private/Components/InteractableInstancedStaticMeshComponent.cpp#L387-L405), [`SyncDatabaseWithInstanceCount`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Private/Components/InteractableInstancedStaticMeshComponent.cpp#L1456-L1490)).
* **Replication Waste:**
  * [`FPropBulkUpdateBatch`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Props/InteractablePropDatabase.h#L1020) embeds `TArray<FPropProductDefinition> ProductDefinitions` into every delta batch ([`BuildBulkUpdateBatch`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Private/Props/InteractablePropManager.cpp#L7083), [`SendInstanceDelta`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Private/Props/InteractablePropManager.cpp#L7120)) instead of sending product/content definitions once per join or registration.
* **64-Bit `FPropPackedRuntimeData` Bottleneck:**
  * [`InteractablePropDatabase.h#L421-L465`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Props/InteractablePropDatabase.h#L421-L465) packs only `CreationMinute` (`uint16`), `LastUpdatedMinute` (`uint16`), `RemainingPortions` (`uint8`), quantized `Temperature`, `bIsGrabbable`, and `SchemaFlags`, leaving only 3 unused bits (`53..55`). It cannot carry fractional `ContentLevel`, `ContentID`, float `CapSealed`, `Strength`, or double-precision timestamps required by the thermal solver.
  * [`GetCurrentWorldMinutes()`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Private/Props/InteractablePropManager.cpp#L326-L333) truncates world seconds to `uint16` minutes and resets to `0` on level load.
* **Current 3-Float GPU Custom Data (`NumCustomDataFloats = 3`):**
  * [`UpdateInstanceCustomData`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Private/Components/InteractableInstancedStaticMeshComponent.cpp#L807-L865) sets Slot `0 = PortionRatio`, Slot `1 = FrostFactor`, Slot `2 = ExpiryAge`. Because temperature is pre-converted into clamped `FrostFactor` (`0..1`), shaders cannot react to overheating/hot drinks, `Strength` (cracks/dents) is invisible, and `ContentLevel` is quantized or derived from `RemainingPortions`.

---
---

# Part 2: Target Architecture — Zero-Copy Shared Registries & Categorized Unique Data

To make the ISM layer lightweight, network-efficient, and natively compatible with the **Bottle Containment & Failure System** and **ISM <-> ASM** pooling, all data is strictly partitioned into:

1. **Category 1 — Shared Data (Dual Central Registries on `APropManager`):**
   - **Product Registry (`TMap<int32, FPropProductDefinition>`):** Container shell specs, physics/interaction rules, capacity/overfill limits, thermal thresholds, cap hardware rules, product conversion IDs, and **two feature enable booleans**. Keyed by `ProductID` (`PID`).
   - **Content Registry (`TMap<int32, FPropContentDefinition>`):** Liquid/content flavor specs (`LiquidMaterial`, `PressureStrengthMultiplier`, base portion price, ideal serving temp, stimuli/taste/expiry profiles). Keyed by `ContentID` (`CID`, where `0 = None / Empty`).
   - **Zero-Copy Rule:** Neither `UInteractableISMComponent` slots nor `AActivePropActor` instances duplicate shared structs/arrays. Instances carry **only** `PID`, `CID`, and per-instance unique data.
2. **Category 2 — Per-Instance Unique Data (`FPropInstanceUniqueData`):**
   Allocated, tracked, synced, and saved per `ISID` **only** when `bEnableInstanceUniqueData == true`:
   - **2.1 Continuous Update Data (`FPropContinuousUpdateData`):** Ticked in background batches over time without player interaction (ONLY `Temperature`, thermal `Strength` drain when exceeding temperature thresholds, and `LastUpdatedTime`, plus derived `ExpiryAge` pushed to GPU).
   - **2.2 Event Update Data (`FPropEventUpdateData`):** Mutated only at spawn or discrete gameplay events (`ContentLevel`, `ContentID`, `CapSealed`, `CreationTime`, `bIsGrabbable`, discrete `Strength` impact/repair, `EventSequence`, `ProductID` conversions).

```text
APropManager (Single Source of Truth for Shared & Unique Instance State)
├── ProductDefinitions     : TMap<ProductID, FPropProductDefinition>   [Category 1A: Container / Prop Specs]
│   ├── bEnableInstanceUniqueData      (Allocates/syncs Category 2 per ISID)
│   └── bEnableContinuousUpdateData    (Enables PropBatchUpdateComponent continuous ticks + GPU Slots 1, 2, 3)
├── ContentDefinitions     : TMap<ContentID, FPropContentDefinition>   [Category 1B: Liquid / Fill Specs]
├── ISIDData               : TMap<ISID, FISIDRuntimeIndex>             [Structural/Transform/Representation Identity]
└── UniqueDataByISID       : TMap<ISID, FPropInstanceUniqueData>       [Category 2: Allocated ONLY if bEnableInstanceUniqueData == true]
    ├── 2.1 Continuous (FPropContinuousUpdateData, 16 B) -> Temperature, LastUpdatedTime (+ thermal Strength drain)
    └── 2.2 Event (FPropEventUpdateData, 32 B)           -> ContentLevel, ContentID, CapType, CapSealed,
                                                            CreationTime, bIsGrabbable, Strength, EventSequence
```

---

## 2.1 Feature Enable Flags (`FPropProductDefinition` & `UInteractablePropDataAsset`)

| Flag Name | C++ Type | Default | What It Controls |
|---|---|---|---|
| `bEnableInstanceUniqueData` | `bool` | `false` | Enables allocating, replicating, and saving `FPropInstanceUniqueData` (`Continuous` + `Event`) per `ISID`. Static world props with `false` pay zero unique-state storage, delta replication, or save-file overhead. |
| `bEnableContinuousUpdateData` | `bool` | `false` | *(EditCondition / Requires `bEnableInstanceUniqueData == true`)* Registers the prop's `ISID` into `UPropBatchUpdateComponent` background time-sliced evaluation to step `Temperature`, evaluate thermal `Strength` drain/failure, recompute `ExpiryAge`, and refresh GPU Custom Data. *(Subsumes legacy `FPropTemperatureProfile::bEnableTemperatureUpdate`.)* |

**Validation Invariant:** If `bEnableContinuousUpdateData == true` while `bEnableInstanceUniqueData == false`, `RegisterProductDefinition` automatically promotes or warns to force `bEnableInstanceUniqueData = true` because continuous updates mutate per-instance `Temperature`, `LastUpdatedTime`, and thermal `Strength`.

---

## 2.2 Category 1 — Shared Data (Dual Central Registries, Zero Per-Instance Copy)

> **Refactor Rule (`[Needs Approval]`):** Remove [`UInteractableISMComponent::PropDatabase`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Components/InteractableInstancedStaticMeshComponent.h#L102) (`TArray<FInteractablePropData>`), remove [`UInteractableISMComponent::IISMDB`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Components/InteractableInstancedStaticMeshComponent.h#L52) replication, and remove `TArray<FPropProductDefinition> ProductDefinitions` from recurring [`FPropBulkUpdateBatch`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Props/InteractablePropDatabase.h#L1020) packets. Legacy query helpers such as [`APropManager::CanInteractWithMesh`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Private/Props/InteractablePropManager.cpp#L8102-L8150) and [`UInteractableISMComponent::GetPropDataForInstance`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Private/Components/InteractableInstancedStaticMeshComponent.cpp#L1191-L1199) assemble an ephemeral `FInteractablePropData` on stack memory from `FindProductDefinition(PID)` + `FindContentDefinition(CID)` + `FindUniqueData(ISID)` without storing arrays per ISM slot or ASM actor.

### 2.2.1 Shared Product Registry (`FPropProductDefinition` Keyed by `ProductID`)

Retains core visual/physics/interaction profiles and absorbs all bottle container & failure rules:

| Group | Fields | Purpose |
|---|---|---|
| **Feature Toggles** | `bEnableInstanceUniqueData`, `bEnableContinuousUpdateData` | Gate per-instance state and continuous batch ticking |
| **Core Identity & Meshes** | `ProductID`, `StaticMeshID`, `DisplayName`, `StaticMesh`, `MaterialOverrides`, `SourceDataAsset`, `CustomActiveActorClass`, `SkeletalMesh` | Shared asset references and ASM spawn class |
| **Behavior Profiles** | `InteractionProfile`, `PhysicsProfile`, `ConsumableProfile` (`NumOfPortions` = MaxPortions, `Price` = fallback container value, `bDestroyWhenEmpty`), `TemperatureProfile` (`ConsumableTemperature`, `TemperatureDopamineBonus`) | Core prop capabilities and physics settling tolerances (`LinearVelocityTolerance`, `AngularVelocityTolerance`, `UnsettledDelay`) |
| **Capacity & Overfill** | `NormalCapacity = 1.0f`, `OverfillWarningThreshold`, `MaxOverfillLevel` (default `1.7f`), `FillSlowdownCurve` (`TObjectPtr<UCurveFloat>`), `bEnableOverfillFailure` (`bool`) | Controls filling slowdown, warning cues, and optional hard overfill limit independent of pressure burst |
| **Container Shell & Strength** | `MaxStrength = 100.0f`, `BreakageFailureThreshold = 0.0f`, `BottleMaterialType` (`EBottleMaterialType`: `Glass`, `Plastic`, `HeavyMetal`, `EnchantedStone`), `PressureResistanceMultiplier` (`> 0`), `PressureDamageThreshold`, `PressureFailureThreshold`, `StrengthDamageRate`, deformation capability flags (`bCanInflate`, `bCanBend`, `bCanDent`, `bCanCrack`, `bCanRecover`, `bSupportsRepair`) | Defines shell resistance, monotonic `Strength` wear rate, GPU `Slot 3` interpretation (`Glass` crack `Lerp` vs `Plastic` WPO Vertex Texture), and breakage/burst thresholds |
| **Thermal Thresholds & Physics** | `ThermalMass`, `ThermalConductivity`, `HeatingRateMultiplier`, `CoolingRateMultiplier`, `MinTemperatureThreshold`, `MaxTemperatureThreshold`, `LowTempDamageThreshold`, `HighTempDamageThreshold`, `ThermalDamageRate` | Shared physical constants for exponential solver $K = \frac{U \cdot A}{m \cdot C_p}$ and extreme-temperature `Strength` drain / `ThermalFailure` |
| **Cap & Seal Hardware Rules** | `SupportedCapMask` (`uint8` bitmask of `EBottleCapType`), `DefaultCapType`, `DefaultCapMesh`, `CapReleasePressure`, `SafeOpenPressureThreshold`, `TorqueSweetSpotMin`, `TorqueSweetSpotMax` | Cap hardware compatibility (`Twist`, `Pop`, `Break`, `Pull`, `Cut`), safety vent threshold, and F1 capping sweet spot |
| **Compatible Liquids** | `SupportedContentIDs` (`TArray<int32>`, or empty = accepts all registered liquid `ContentID`s) | Used by `SetBottleContentType` before filling an empty bottle |
| **Product Conversion Map** | `EmptyBottleProductID`, `FilledProductIDByContentID` (`TMap<int32, int32>`), `BrokenProductID`, `MeltedProductID`, `BurstBrokenProductID`, `bPersistBrokenHullAfterFracture`, `FractureFragments` (`TArray<FPropFractureFragmentEntry>`), `FailurePriorityOverrides` | Drives 1-to-1 (`ChangePropProductWithMode`) and 1-to-Many fracture (`Broken Glass -> Glass Fragment 1..N`) transformations without storing `FailureType` or `bFailureTriggered` on ISM |
| **Unique Runtime Defaults** | `DefaultUniqueData` (`FPropInstanceUniqueData`) | Initial `Temperature`, `Strength` (`100.0f`), `ContentLevel`, `ContentID`, `CapType`, `CapSealed`, `bIsGrabbable` for newly spawned instances |

### 2.2.2 Shared Content Registry (`FPropContentDefinition` Keyed by `ContentID`)

Because a reusable `EmptyBottle` product can be filled with different drinks (`BasicCola`, `SpicyCola`, `SugarCrush`, `ToxicSwamp`, etc.) *before* or *without* needing separate per-instance structs, all liquid metadata lives once per `ContentID` (`0 = None`):

| Field Name | Type | Description |
|---|---|---|
| `ContentID` | `int32` | Unique liquid identifier (`0 = None`, `1 = BasicCola`, `2 = SpicyCola`, etc.) |
| `DisplayName` | `FText` | Liquid display name (`"Spicy Cola"`) |
| `LiquidMaterial` | `TObjectPtr<UMaterialInterface>` | Liquid shader material applied to `SkeletalMeshComponent` Slot `1` in ASM |
| `LiquidColor` | `FLinearColor` | Tint for fluid/foam/splash Niagara systems |
| `PressureStrengthMultiplier` | `float` | Carbonation agitation reactivity ($\times \text{Jerk/Impulse}$ inside pressure solver) |
| `BasePortionPrice` | `float` | Base commercial sale value for checkout scanner calculations |
| `IdealServingTemperature` | `float` (`°C`) | Ideal drink temperature for dopamine bonus |
| `SpecificHeatCapacity` & `Viscosity` | `float` | Content thermal mass scaling and flow/leakage speed |
| `HazardType` | `EColaHazardType` | Post-burst / leak hazard (`FoamRocket`, `FirePatch`, `ToxicCloud`, `SpiderSwarm`, `StickySlow`, `Radiation`) |
| `StimulusProfile` | `TArray<FNPCSimulationStatModifier>` | NPC stat modifiers per portion consumed when fresh |
| `TasteProfile` | `FNPCTasteVector` | 9-axis flavor profile when fresh |
| `ExpiredTasteProfile` | `FNPCTasteVector` | 9-axis flavor profile when expired |
| `ExpiryProfile` | `FPropExpiryProfile` | Liquid spoilage lifetime (`ExpiryLifetimeMinutes`), `SicknessLevel`, `ExpiredDopaminePenalty`, `ExpiryStimulusProfile` |

**Definition Precedence Rule:** When computing consumables/expiry/stimuli on an instance with `ContentID > 0`, shared queries prefer `ContentDefinitions.Find(ContentID)` and fall back to `ProductDefinitions.Find(ProductID)` when `ContentID == 0` or when a product encodes a self-contained non-liquid prop (e.g., food/snack).

---

## 2.3 Category 2 — Per-Instance Unique Data (`FPropInstanceUniqueData`)

Allocated per `ISID` **only** when `bEnableInstanceUniqueData == true`. Strictly divided into **Continuous Update Data** and **Event Update Data** so background ticks never overwrite discrete event fields (`CreationTime`, `ContentLevel`, `ContentID`, `CapSealed`) and never transmit static or event fields during ambient cooling/warming ticks.

### 2.3.1 Continuous Update Data (`FPropContinuousUpdateData`)

Updated periodically in background round-robin batches via `UPropBatchUpdateComponent::ProcessBatchUpdate` (when `bEnableContinuousUpdateData == true`):

| Field | C++ Type | Default | Purpose & Strict Mutation Rules |
|---|---|---|---|
| `Temperature` | `float` | `21.0f` | Authoritative resting shell temperature (`°C`). Advances toward local ambient/zone target using clamped exponential response: $T_{\text{new}} = T_{\text{src}} + (T_{\text{prev}} - T_{\text{src}}) \cdot e^{-K \Delta t}$. Drives GPU **`Slot 2`**. |
| `LastUpdatedTime` | `double` | `0.0` | Double-precision world elapsed seconds (`APropManager::WorldElapsedSeconds`) at the last thermal evaluation. Guarantees accurate $\Delta t$ across inactive intervals and upon ISM to ASM promotion. *(Replaces `uint16 LastUpdatedMinute`.)* |

**Continuous Thermal Strength Drain (Boundary Cross-Write):**  
If `Temperature > HighTempDamageThreshold` or `Temperature < LowTempDamageThreshold`, the continuous batch computes monotonic thermal damage and reduces `EventData.Strength` (syncing **GPU `Slot 3`**). If `Temperature` breaches `[MinTemperatureThreshold, MaxTemperatureThreshold]` or `Strength <= 0`, `UPropBatchUpdateComponent` requests `ThermalFailure` / `ChangePropProduct(BurstBrokenProductID)`.

**Dynamic GPU Age Refresh (Read-Only From `CreationTime`):**  
During continuous batch updates, `ExpiryAge = Clamp((CurrentWorldSeconds - CreationTime) / (ExpiryLifetimeMinutes * 60.0), 0.0, 1.0)` is recomputed in memory and pushed to **GPU `Slot 1`** *even if `Temperature` has already stabilized at ambient*. **`CreationTime` is NEVER modified by continuous updates.**

### 2.3.2 Event Update Data (`FPropEventUpdateData`)

Mutated **only** at spawn or on discrete gameplay events (Fill, Empty/Pour, Consume, Add/Seal/Remove Cap, Impact Damage, Repair, PID Conversion). Never touched by continuous background ticks unless thermal damage drains `Strength`:

| Field | C++ Type | Default | Purpose & Replacement Mapping |
|---|---|---|---|
| `ContentLevel` | `float` | `0.0f` | Continuous normalized fill volume (`0.0` empty, `1.0` normal full, `> 1.0` up to `MaxOverfillLevel` e.g. `1.7`). **Replaces `RemainingPortions`** (portions = `ContentLevel * NumOfPortions`, integer portions derived only at inventory boundaries). Also equals resting Base Pressure ($P_{\text{base}} = \text{ContentLevel}$). Drives GPU **`Slot 0`**. |
| `ContentID` | `int32` | `0` | Liquid content identifier (`0 = None`). Keys `ContentDefinitions` on `APropManager` to resolve material, pressure multiplier, price, stimuli, taste, and expiry without duplicating those fields per instance. |
| `Strength` | `float` | `100.0f` | Unified structural integrity (`0.0 .. MaxStrength`). Monotonically decreases from excess pressure, extreme temperature, or impacts; restored only by `RepairBottle` or `FullReset`. **`<= BreakageFailureThreshold` triggers `BottleBreakage` product transformation (`BrokenGlass -> Glass Fragment 1..N`).** **Replaces `PermanentDeformation`, `CrackProgression`, and `ThermalDamageLevel`.** Drives GPU **`Slot 3`**. |
| `CapSealed` | `float` | `0.0f` | Normalized cap seal state (`0.0` = no cap / removed / broken; `0.01 .. 0.99` = loosely attached / compromised seal integrity; `1.0` = hermetically sealed). **Replaces both `bIsCapSealed` (`bool`) and `SealIntegrity` (`float`).** |
| `CreationTime` | `double` | `0.0` | Immutable creation timestamp in `WorldElapsedSeconds`. **Invariant:** Never overwritten by continuous ticks, runtime state transitions (`ContentReset`, `PreserveUniqueState`), or ISM <-> ASM promotions/demotions; modified *only* on initial spawn or explicit `FullReset`. |
| `EventSequence` | `uint32` | `1` | Monotonically incremented per authoritative event mutation so late out-of-order network packets never revert newer discrete states. |
| `CapType` | `uint8` | `0` | Attached `EBottleCapType` identifier (`0=None`, `Twist`, `Cork`, `BoltLock`, `RuneSeal`) packed into existing bool alignment padding (`0` extra bytes — struct stays **`32 B`**). |
| `bIsGrabbable` | `bool` | `true` | Discrete interaction lock flag controlling player/NPC pickup eligibility. |

### 2.3.3 Deliberately Omitted from ISM Storage (Why Each Field Is Excluded)

| Omitted Field | Replacement / Why Excluded from ISM |
|---|---|
| `FrostFactor` | Material shader & UI compute frost/condensation directly from **GPU `Slot 2 (Temperature)`**. |
| `RemainingPortions` | Derived on demand via fractional formula $\text{RemainingPortions} = \text{ContentLevel} \times \text{MaxPortions}$. |
| `ContentTemperature` | Simulated only inside active `AActivePropActor`; `CanDemoteToISM()` blocks ASM to ISM demotion until `Abs(ContentTemperature - Temperature) <= Epsilon` (thermal equalization reached). |
| `BasePressure` & `AccumulatedInteractionPressure` | Resting $P_{\text{base}} = \text{ContentLevel}$; transient $P_{\text{interaction}}$ lives only on ASM and `CanDemoteToISM()` blocks demotion until interaction pressure decays back to zero (or vents). |
| `PermanentDeformation`, `CrackProgression`, `ThermalDamageLevel` | Consolidated into single scalar **`Strength`** (`0..MaxStrength`) fed to **GPU `Slot 3`**. Glass materials drive Crack Progression via **`Lerp(1 - Slot3)`**; Plastic materials drive Permanent Deformation via **WPO Vertex Texture (`1 - Slot3`)**. |
| `bIsCapSealed` & `SealIntegrity` | Consolidated into **`CapSealed`** (`float 0.0 .. 1.0`) paired with zero-padding `uint8 CapType`. |
| `FailureType` & `bFailureTriggered` | Represented via **Product Transformation** ([`Bottle_Damage_Transformation_and_Fragments.md`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Docs/Bottle_Damage_Transformation_and_Fragments.md)): 1-to-1 `ChangePropProductWithMode` (`BurstBrokenProductID`, `MeltedProductID`) or 1-to-Many `RequestProductTransformation` (`BrokenProductID -> Glass Fragment 1..N`). |

---

## 2.4 GPU Per-Instance Custom Data (`PerInstanceSMCustomData` — 4 Floats)

Expanded from `3` floats (`InteractableInstancedStaticMeshComponent.cpp#L23`) to **`NumCustomDataFloats = 4`** in both `UInteractableISMComponent::UpdateInstanceCustomData` and `AActivePropActor::UpdateMaterialCustomPrimitiveData`:

| Slot | Channel Name | Update Owner | Value Range / Formula | Shader & Material Interpretation |
|---:|---|---|---|---|
| **`0`** | `ContentLevel` | **Event Update** | Stored `EventData.ContentLevel` (`0.0 .. MaxOverfillLevel`) | Liquid column height inside translucent/cutout ISM & overfill bulge shader offset |
| **`1`** | `ExpiryAge` | **Continuous Update** | `Clamp((WorldElapsedSeconds - EventData.CreationTime) / ExpiryLifetimeSec, 0.0f, 1.0f)` | Cloudiness, sediment, color dulling when approaching `1.0` |
| **`2`** | `Temperature` | **Continuous Update** | Stored `ContinuousData.Temperature` (`°C`, raw Celsius float) | Material computes frost (`Temperature < 15°C`) and heat shimmer/scorch (`Temperature > 45°C`) directly in shader |
| **`3`** | `Strength` | **Event Update** *(+ Continuous Thermal Drain)* | `Clamp(EventData.Strength / MaxStrength, 0.0f, 1.0f)` (`1.0` = pristine, `0.0` = failed) | Shader reads `1.0 - Slot3` as permanent damage mask: vertex dent/warp on plastic/metal, crack mask on glass |

*(Zero-Copy Mesh Switching Note: Because major structural transformations — intact vs burst/broken shard vs empty vs distinct cola flavor bottle — switch `ProductID` via `ChangePropProduct`, each `ProductID` owns its exact resting `StaticMesh` and `MaterialOverrides` inside `UInteractableISMComponent` without extra GPU floats.)*

---

## 2.5 SaveGame Schema (`SchemaVersion = 2`) & Network Footprint

1. **`FPropWorldSaveData` (`SchemaVersion = 2`):**
   - Stores `double WorldElapsedSeconds` so `CreationTime` and `LastUpdatedTime` remain continuous across save/load sessions.
   - [`FPropSerializedInstance`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Props/InteractablePropDatabase.h#L1175-L1226) conditionally serializes `FPropInstanceUniqueData` only if `ProductDef.bEnableInstanceUniqueData == true`.
   - **Backward Migration (`SchemaVersion == 1 -> 2`):** Legacy saves containing `FPropPackedRuntimeData` (`CreationMinute`, `LastUpdatedMinute`, `RemainingPortions`, `Temperature`, `bIsGrabbable`) convert deterministically on load *(if legacy save compatibility is retained; see Section 2.7)*:
     - `ContentLevel = RemainingPortions / Max(1, NumOfPortions)`
     - `CreationTime = CreationMinute * 60.0`, `LastUpdatedTime = LastUpdatedMinute * 60.0`
     - `Strength = MaxStrength`, `CapSealed = (ContentLevel > 0) ? 1.0f : 0.0f`.
2. **RAM & Bandwidth Comparison (1,000 World Props):**
   - **Legacy:** `UInteractableISMComponent::PropDatabase` (~300+ bytes/slot including heap `TArray`/`FText`/`UMaterialInterface` copies across all slots) + `IISMDB` copies per component + `FPropProductDefinition` resent on every delta batch (~1,500+ bytes per product inside `FPropBulkUpdateBatch`).
   - **Target (Zero Heap Allocations per Instance):** Shared registries replicate **once** on client join/registration; `UniqueDataByISID` allocates **0 bytes** for static props (`bEnableInstanceUniqueData == false`) and exactly **48 bytes contiguous** per dynamic prop (`FPropContinuousUpdateData` `16 B` + `FPropEventUpdateData` `32 B`); hot continuous network sync packs `24 B` in memory / **`12–16 bytes` on wire** (`ISID + Temperature + relative LastUpdatedTime offset` vs batch `double BaseWorldSeconds`, plus optional `ThermalStrength` only when thermally damaged), and discrete events send only dirty fields via `NetSerialize`.

---

## 2.6 Proposed C++ Header Contracts (`[Needs Approval]`)

Targeting [`InteractablePropDatabase.h`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Props/InteractablePropDatabase.h) and [`InteractablePropDataAsset.h`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Props/InteractablePropDataAsset.h) — **held pending user approval**:

```cpp
// Category 2.1: Continuous Update Data (16 bytes, no CreationTime / Event fields)
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropContinuousUpdateData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Continuous")
    double LastUpdatedTime = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Continuous", meta = (Units = "Celsius"))
    float Temperature = 21.0f;
};

// Category 2.2: Event Update Data (32 bytes, mutated only on spawn or discrete events)
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropEventUpdateData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event")
    double CreationTime = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event", meta = (ClampMin = "0.0"))
    float ContentLevel = 0.0f; // BasePressure = ContentLevel; RemainingPortions = ContentLevel * MaxPortions

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event")
    int32 ContentID = 0; // 0 = None / Empty, >0 keys APropManager::ContentDefinitions

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event", meta = (ClampMin = "0.0"))
    float Strength = 100.0f; // Monotonic wear; replaces PermanentDeformation, CrackProgression, ThermalDamageLevel

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CapSealed = 0.0f; // 0 = Uncapped/Broken, (0..1) = Loose/Compromised, 1 = Hermetically Sealed

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event")
    uint32 EventSequence = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event")
    uint8 CapType = 0; // Attached EBottleCapType (0 = None); shares bool trailing padding (stays 32 B)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data|Event")
    bool bIsGrabbable = true;
};

USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropInstanceUniqueData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data")
    FPropContinuousUpdateData ContinuousData;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Unique Data")
    FPropEventUpdateData EventData;
};

// Category 1B: Shared Liquid / Content Registry (Keyed by ContentID on APropManager; 0 = None)
USTRUCT(BlueprintType)
struct MARRYBYMIDNIGHT_API FPropContentDefinition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
    int32 ContentID = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
    TObjectPtr<UMaterialInterface> LiquidMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
    FLinearColor LiquidColor = FLinearColor(0.25f, 0.08f, 0.02f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition", meta = (ClampMin = "0.01"))
    float PressureStrengthMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition", meta = (ClampMin = "0.0"))
    float BasePortionPrice = 2.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition", meta = (Units = "Celsius"))
    float IdealServingTemperature = 4.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition", meta = (ClampMin = "0.01"))
    float SpecificHeatCapacity = 4.18f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
    uint8 HazardType = 0; // Maps to EColaHazardType

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
    TArray<FNPCSimulationStatModifier> StimulusProfile;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
    FNPCTasteVector TasteProfile;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
    FNPCTasteVector ExpiredTasteProfile;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Definition")
    FPropExpiryProfile ExpiryProfile;
};
```

Additional Category 1 additions onto existing [`FPropProductDefinition`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Props/InteractablePropDatabase.h#L753-L830) and [`UInteractablePropDataAsset`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Props/InteractablePropDataAsset.h#L53-L199):
- `bool bEnableInstanceUniqueData = false;`
- `bool bEnableContinuousUpdateData = false;` *(EditCondition = `bEnableInstanceUniqueData`)*
- Container capacity/overfill: `float NormalCapacity = 1.0f;`, `float OverfillWarningThreshold = 1.15f;`, `float MaxOverfillLevel = 1.7f;`, `TObjectPtr<UCurveFloat> FillSlowdownCurve;`, `bool bEnableOverfillFailure = false;`
- Shell strength & thermal physics: `float MaxStrength = 100.0f;`, `float BreakageFailureThreshold = 0.0f;`, `float PressureResistanceMultiplier = 1.0f;`, `float PressureDamageThreshold = 1.4f;`, `float PressureFailureThreshold = 2.0f;`, `float StrengthDamageRate = 12.0f;`, `float ThermalMass = 1.0f;`, `float ThermalConductivity = 0.05f;`, `float HeatingRateMultiplier = 1.0f;`, `float CoolingRateMultiplier = 1.0f;`, `float LowTempDamageThreshold = -5.0f;`, `float HighTempDamageThreshold = 55.0f;`, `float MinTemperatureThreshold = -20.0f;`, `float MaxTemperatureThreshold = 85.0f;`, `float ThermalDamageRate = 8.0f;`, `float ThermalEquilibriumEpsilon = 0.25f;`
- Cap & transformation rules: `uint8 SupportedCapMask = 0xFF;`, `float SafeOpenPressureThreshold = 1.25f;`, `float TorqueSweetSpotMin = 0.41f;`, `float TorqueSweetSpotMax = 0.75f;`, `int32 EmptyBottleProductID = 0;`, `int32 BrokenProductID = 0;`, `int32 MeltedProductID = 0;`, `int32 BurstBrokenProductID = 0;`, `bool bPersistBrokenHullAfterFracture = false;`, `TArray<FPropFractureFragmentEntry> FractureFragments;`, `TMap<int32, int32> FilledProductIDByContentID;`, `TArray<int32> SupportedContentIDs;`
- `FPropInstanceUniqueData DefaultUniqueData;` *(initial values for newly spawned instances)*
- **Fracture & Transformation Contracts:** `EPropTransformationType`, `FPropFractureFragmentEntry`, and `FPropTransformationContext` (also targeting [`InteractablePropDatabase.h`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Source/MarryByMidnight/Public/Props/InteractablePropDatabase.h)) are specified in full inside [`Bottle_Damage_Transformation_and_Fragments.md` §8.1](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Docs/Bottle_Damage_Transformation_and_Fragments.md#L221-L286).

---

## 2.7 Open Edge-Case Decisions for User Sign-Off Before Phase B

1. **Uncapped Partial Bottle Demotion (`CapSealed == 0.0 && ContentLevel > 0.0`):** Should `CanDemoteToISM()` permit uncapped partial bottles resting flat on counters to demote back to ISM (preserving exact `ContentLevel` and `CapSealed = 0.0`), OR keep them in ASM until capped/emptied?
2. **Resting ISM Liquid Flavor Visual (`FilledProductIDByContentID` vs Generic Fill):** Using `FilledProductIDByContentID` maps `(EmptyBottle + ContentID)` to flavor-specific resting ISM components (preserving distinct liquid colors/labels at rest without adding a 5th GPU custom float), OR an uncapped generic bottle can rely on `ContentID`-agnostic resting glass.
3. **Resting ISM Extreme-Temperature Failure (`UPropBatchUpdateComponent`):** When an ISM bottle in a freezer/heater exceeds `[MinTemperatureThreshold, MaxTemperatureThreshold]` or hits `Strength <= 0`, should `UPropBatchUpdateComponent` convert `ProductID -> BurstBrokenProductID` / `MeltedProductID` silently in ISM (zero actor overhead), OR briefly promote (`MakeISMToASM`) to play 3D glass-shatter VFX/SFX and spawn spills?
4. **SaveGame `SchemaVersion 1 -> 2` Backward Compatibility:** Confirm whether upgrading existing `SchemaVersion == 1` saves on load is required or whether a clean break to `SchemaVersion = 2` is acceptable.
5. **Fragment `ISID` Allocation Timing (`At Spawn` vs. `At Settle`)** *(detailed in [`Bottle_Damage_Transformation_and_Fragments.md` §9](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Docs/Bottle_Damage_Transformation_and_Fragments.md#L333-L338))*: Should `Broken Glass Bottle` allocate each fragment's permanent `ISID` immediately at spawn (allows grabbing/sweeping shards mid-slide & mid-flight saves) or only after all shards settle?
6. **Broken Bottle Orchestrator Lifecycle (`Pure Fragments` vs. `Broken Hull + Fragments`)**: Should `Broken Glass Bottle` despawn/pool completely once `Glass Fragment 1..5` spawn (`100%` floor shards) OR optionally persist a resting base/neck hull (`bPersistBrokenHullAfterFracture`) alongside detached cap debris?

---

## 2.8 Implementation Roadmap & Document Revision History

| Iteration | Focus Completed |
|---|---|
| **Iteration 1** | Separated baseline Part 1 audit (all 17 `FPropProductDefinition` members) from Part 2 target architecture: dual central shared registries (`ProductDefinitions` + `ContentDefinitions`), two enable toggles, `Continuous` vs `Event` split, omitted-fields rationale, and 4-float GPU pipeline. |
| **Iteration 2** | Audited sparse per-ISID `UniqueDataByISID` storage (`0 B` for static props, exact `16 B + 32 B = 48 B` for mutable props), corrected net-serialized packet sizes (`12–16 B`), unified `EPropProductResetMode` names across all docs, added on-demand stack synthesis rules (`CanInteractWithMesh` / `GetPropDataForInstance`), and surfaced 4 open edge-case decisions. |
| **Iteration 3** | Added full `[Needs Approval]` C++ `USTRUCT` contracts (`FPropContinuousUpdateData`, `FPropEventUpdateData`, `FPropInstanceUniqueData`, `FPropContentDefinition`, and `FPropProductDefinition` extensions) and verified all clickable source line links against `InteractablePropDatabase.h`, `InteractablePropDataAsset.h`, `InteractableInstancedStaticMeshComponent.h/.cpp`, and `InteractablePropManager.cpp`. |
| **Iteration 4** | Integrated [`Bottle_Damage_Transformation_and_Fragments.md`](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Docs/Bottle_Damage_Transformation_and_Fragments.md): added zero-byte `uint8 CapType` packing into `FPropEventUpdateData` (preserving exact `32 B` alignment), added `BreakageFailureThreshold`, `BrokenProductID`, `MeltedProductID`, `bPersistBrokenHullAfterFracture`, and `FractureFragments` onto `FPropProductDefinition`, documented Glass crack `Lerp` vs Plastic WPO Vertex Texture in `Slot 3`, and linked Decisions 5–6. |
