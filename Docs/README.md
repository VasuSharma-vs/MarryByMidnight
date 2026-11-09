# Midnight Fizz: Giant Thirst — Game Design Documentation (GDD)

Welcome to the central design repository for **Midnight Fizz: Giant Thirst** (Working Title / Project `MarryByMidnight`).

This documentation suite serves as the authoritative blueprint for all gameplay systems, artistic specifications, mechanics design, item databases, and technical implementation tasks in Unreal Engine 5.

## Primary design requirements and precedence

[Core Design Decisions — Update of 10 November 2026](Core_Design_Decisions_Update_2026-11-10.md) contains the latest decisions on the shared wallet, fixed pricing, refill expiry, reuse, autosaving and pool expansion. It takes precedence over conflicting earlier documents.

[Core Gameplay & Bottle System Design Decisions](Core_Gameplay_Design_Decisions.md) contains the underlying primary requirements supplied on 9 November 2026. Follow it where the latest update does not replace a rule. Configurable values and example balance figures are not final hardcoded constants.

[Implementation alignment notes](Core_Design_Implementation_Alignment.md) record the superseded requirements, structural consequences, and remaining design details. The earlier [implementation audit](Documentation_Implementation_Audit.md) remains a historical assessment against the earlier specifications; its counts are not a completion assessment against the new requirements.

---

## 📚 Document Index

1. [**Master Game Design Document (GDD_Master.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Docs/GDD_Master.md)
   - High Concept, Game Pillars & The Scale Twist (Tiny Bartenders vs. Giant Customers).
   - Core Game Loop (Buy $\rightarrow$ Fill $\rightarrow$ Cap $\rightarrow$ Chill $\rightarrow$ Scan $\rightarrow$ Clean).
   - Co-op Multiplayer Architecture (Shared Shop, Shared Wallet, Shared Inventory).
   - Day & Shift Cycles, Customer Satisfaction, and Shop Progression.

2. [**Mechanics, Tools & Stations (Mechanics_and_Tools.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Docs/Mechanics_and_Tools.md)
   - **Station 1: Cola Cylinder & Gas-Pump Dispenser Nozzle** (Heavy 100kg bulk cylinders, docking manifolds, pressure, sweet spots, fill thresholds, rocket trigger).
   - **Station 2: Capping Station & F1 Wheel-Nut Timing/Torque Mechanic** (Manual rotation, torque mini-game, blowouts & breaks).
   - **Station 3: Temperature & Chilling System** (Consumable temperature, thermal decay, ice baths).
   - **Station 4: Checkout Scanner & Pricing Engine** (Fill-portion scanner logic, underfilling loss penalty formula).
   - **Station 5: Bottle Cleaning & Decontamination Station** (Reusability conditions, brush scrubbers, hazard sanitation).
   - **Station 6: Shop Vehicles & Heavy Machinery** (Fizz-Mule flatbed, Micro-Forklift cylinder hoister, Mobile Capper Buggy for drive-by bottle capping).

3. [**Items, Colas & Hazards Catalog (Items_Catalog_and_Hazards.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Docs/Items_Catalog_and_Hazards.md)
   - **Bottle Types:** Glass, Plastic, Metal, Ceramic/Enchanted (physics, durability, reusability).
   - **Cap Types:** Plastic Twist, Crown Cork, Bolt-Lock, Rune Seal (strengths, features, torque windows).
   - **Cola Flavors & Types:** Basic, Sugar Crush, Spicy/Fire, Toxic Swamp, Spider Nest, Hangover Haze, Cryo-Frost.
   - **Shaking & Agitation Mishaps:** Rocket launches, Fragile shatter, Toxic gas clouds, Spider swarms, Dizziness.
   - **Customer Demographics:** Giants, Golems, Witches, Goblins (height differences, scale challenges, preferences).
   - **Vehicle Fleet & Logistics Catalog:** Fizz-Mule Flatbed, Micro-Forklift, Mobile Capper Buggy (specs, fuel/battery, and roll-over hazards).

4. [**Technical Implementation & Unreal Engine 5 Architecture (Technical_Implementation_Plan.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Docs/Technical_Implementation_Plan.md)
   - Integration map between workstation blueprints (`BP_ColaTank`, `BP_CappingStation`, `BP_Chiller`, `BP_CheckoutScanner`), `AShopVehiclePawn`, and UI/HUD specs *(legacy 64-bit packing details superseded by item 6 below)*.

5. [**Cola Types, Supernatural Hazards & Customer Compendium (Cola_Types_and_Patrons.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Docs/Cola_Types_and_Patrons.md)
   - Dedicated encyclopedia for standard & supernatural colas (Poison, Spider, Radioactive, Ghost, Blood).
   - Catastrophic disaster reactions (Poison clouds, spider swarms, ionizing radiation contagion, sensory hallucinations).
   - Player countermeasure tools & safety gear (Gas masks, air-scrubbing blowers, bug swatters, lead barrels, holy salt, exorcism bell).
   - Supernatural patron roster (Golems, Werewolves, Witches, Vampires, Mountain Giants, Banshees) and wrath tantrums.
   - Standardized developer data templates for easily authoring new colas and monster patrons.

6. [**ISM Data Architecture & Zero-Copy Shared / Unique Split (ISM_Data_Architecture.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Docs/ISM_Data_Architecture.md)
   - Part 1 baseline audit of all 17 `FPropProductDefinition` members, legacy `PropDatabase` / `IISMDB` duplication, 64-bit `FPropPackedRuntimeData`, and 3-float GPU data.
   - Part 2 zero-copy Category 1 dual shared registries (`ProductDefinitions` + `ContentDefinitions`), Category 2 split (`Continuous` vs `Event`), feature flags (`bEnableInstanceUniqueData`, `bEnableContinuousUpdateData`), 4-float GPU pipeline, `SchemaVersion = 2`, and proposed C++ header contracts (`[Needs Approval]`).

7. [**Active Prop Actor & Cola ASM Runtime Architecture (ActivePropActor_ASM_Architecture.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Docs/ActivePropActor_ASM_Architecture.md)
   - 3-tier ASM state separation (Persistent Event mirror, Persistent Continuous mirror, Transient ASM-only state) eliminating per-actor shared-array copies.
   - Complete mapping of the Bottle Containment & Failure System onto `AActivePropActor` and `AColaPropActor`, `CanDemoteToISM()` gates, `PoolGeneration` timer safety, verified Phase B bug fixes, and `[Pre-Authorized]` C++ contracts.

8. [**Prop Manager & ISM <-> ASM Subsystem Architecture (PropManager_ISM_ASM_Architecture.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Docs/PropManager_ISM_ASM_Architecture.md)
   - Orchestration across `APropManager`, `UASMComponent`, `UInteractableISMComponent`, and unified `UPropBatchUpdateComponent`.
   - Split replication channels (`Registry`, `Event` ordered by `EventSequence`, hot `Continuous`), monotonic `double WorldElapsedSeconds` clock, `EPropProductResetMode` conversions, safe `CustomActiveActorClass` swap, and `[Needs Approval]` C++ contracts.

9. [**BP_Cola & AColaPropActor Technical Documentation (BP_Cola_Technical_Documentation.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Docs/BP_Cola_Technical_Documentation.md)
   - Deep-dive implementation spec for `AColaPropActor` (`BP_Cola` and variants): `ContentID` / `ContentLevel`, morph targets, 4-float CPD & Slot 1 liquid shaders, separated `Base` vs `Interaction` pressure, `Strength` wear, dual thermal solver, 4-stage `AddCap` & 5-mode `RemoveCap`, `ContainmentFailureManager`, quadratic underfill economy, and pooling lifecycle.

10. [**Bottle Damage, Product Transformation & Fragment Lifecycle (Bottle_Damage_Transformation_and_Fragments.md)**](file:///c:/Users/PC/Documents/Unreal%20Projects/MarryByMidnight/Docs/Bottle_Damage_Transformation_and_Fragments.md)
    - GPU-driven damage visuals (Glass crack `Lerp` vs Plastic WPO Vertex Texture via `Slot 3`), ASM <-> ISM persistence (`CapType` + `CapSealed` + `Strength`), 1-to-Many `Broken Glass Bottle` -> `Glass Fragment 1..N` independent lifecycles, `Melted Glass` / `Burst Bottle` transformations, and property inheritance matrix.

---

## 🛠️ Design Philosophy
Every feature in this game is designed to enforce:
1. **Physical Comedy & High Chaos:** Bottling high-pressure soda should be as volatile as handling live munitions.
2. **Cooperative Interdependence:** You cannot do it alone. Players must synchronize filling, capping, chilling, and serving.
3. **Skill-Based Timing & Precision:** Mechanical depth inspired by F1 pit stops and physics simulations.
4. **David vs. Goliath Wonder:** Physical scale contrast makes everyday tasks feel heroic and perilous.
