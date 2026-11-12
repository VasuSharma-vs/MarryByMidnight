# Colossal Cola Co. — Browser Chat Decisions and Conflicts

**Recorded locally:** 11 November 2026 (Asia/Calcutta).

**Purpose:** Reconcile the nine available chats in the ChatGPT project **Colossal Cola Co** with the local GDD.

**Change boundary:** Documentation only. No code, Blueprint, asset, map, balance or character-size changes are established by this update.

**Subsequent designer decision — 11 November 2026:** [Plastic Bottle Pressure System](Prototype_Plastic_Bottle_Pressure_2026-11-11.md) now restricts the prototype to **plastic bottles, five Normal Colas and Normal manual twist caps**. It approves impact/shake/drop pressure buildup and an orientation/force-dependent, physics-driven cola rocket when internal pressure exceeds bottle tolerance. This supersedes broader material/cap options for the prototype and resolves its material/closure choice; the nine source chats remain the provenance for the earlier decisions below.

**Subsequent supplied equipment specification — 11 November 2026:** The [Equipment Design Specification](Equipment_Design_Specification.md) adds a two-player Manual Wheel Rotator to the demo, with G-press downward hold that becomes less effective over time, second-player cap rotation and shared pressure/leakage failure behavior. Only this type from its five-type catalog is in the demo; essential bulk-source/hose/filling infrastructure remains. Cap Gun, Auto Fill and Cap, Pressure Regulator and Automatic Bottle Cleaner are full-game-only. Common equipment properties, final-value gaps and new input/transport/operator-session dependencies are recorded in [equipment integration/conflicts](Equipment_Design_Integration_2026-11-11.md); this attachment is separate evidence from the nine browser chats.

## 1. Authority and Source Limits

The designer requested this local GDD update and explicitly retained the **Normal-only demo** and the **current player, NPC and bottle sizes**. Those current instructions override conflicting older documents or chat summaries. Existing separately approved scanner, physical-cash/ATM, save, fragment and pooling requirements remain in force.

Chat text was read through the connected conversation tools, including older turns until each chat reported no more pages. Uploaded files and generated images were not all independently reviewed. An assistant's statement that something was approved, implemented or saved to memory is not independent evidence of designer approval, runtime implementation or testing.

| Source chat title | Evidence used and local treatment |
| --- | --- |
| Normal Bulk Cola Container | Direct designer instructions define the new name, demo container behavior and four-stage filling flow. These are new GDD requirements. |
| Document Cola Types | Direct designer taxonomy and Spicy/Zero Calories price tie confirm the existing local contents catalog. |
| Bottle art direction | Direct designer instructions confirm the hammered wooden glass stopper and that the glass pressure-release cap has no full-bottle auto-lock. The designer approved the stopper concept. The assistant's proposed art-style name is a working descriptor, not separately approved branding. |
| Create Game Coin Image | Direct designer requests specify seven physical denominations and readable, practical coins with Victorian steampunk and dark fantasy/alchemy influence. Rendered concepts were not inspected in this update. |
| Plastic Cola Types Details | An assistant recap. Plastic cap/material behavior is retained where corroborated by local core decisions, rather than treated as fresh approval of every detail. |
| Glass Bottle Types Details | An assistant recap. Corroborated closure/material rules are retained; its old PSI figure and uncertain fragment-count statement do not override local specifications or supplied assets. |
| Can Metal Bottle Details | An assistant recap. The welded lid, deformation and 20% recycling rule already appear in local core decisions; welding/reopening specifics remain open. |
| Plan Food Migration | Historical assistant plan, superseded by the designer's explicit exclusion of PSO_FoodContainer and the other ten legacy PSO assets. No migration is reactivated. |
| Store Project Files | Historical uploaded-document summary. Its pause/build/test claims are not a current project audit and do not override subsequent work or current instructions. |

For existing requirements, also follow [core decisions](Core_Gameplay_Design_Decisions.md), the [scanner update](Scanner_Checkout_Design_Update_2026-11-10.md), [approved ATM accounting](ATM_Cash_Accounting_Proposal.md), [current development scope](Current_Development_Scope.md) and [approved scale/contents update](Scale_and_Contents_Update_2026-11-11.md).

## 2. Demo Boundary and Preserved Scale

- Demo liquids: **Lemonade, Mint, Sweet, Spicy and Zero Calories Cola only**. Their base-price order is `Lemonade < Mint < Sweet < Spicy = Zero Calories`, compared at the same nominal capacity.
- The full-game [21-type catalog](Bottle_Contents_Catalog.md) remains a working reference. Toxic, Black Magic, Radioactive and Explosive mechanics are deferred. A flavor name does not approve a special effect, including fire effects for Spicy.
- The demo uses purchased, pre-mixed Normal Cola. Raw-ingredient processing belongs to the full game.
- Preserve the current player, NPC and bottle setup in `/Game/FirstPerson/Lvl_FirstPerson`, including component transforms. Do not shrink bottles or the player, or normalize giants to human size.
- The surrounding coins, scanner, checkout, racks, furniture and structures serve giants. Player vehicles, direct-use tools and filling/cleaning equipment fit the unchanged player while reaching the giant objects. Provide suitable access and clearance.
- Keep the existing approved gameplay-mass policy: NPCs and movable actors are lighter than the main player; visual size and thermal mass do not determine gameplay mass. Exact mass values remain tuning work.
- The designer subsequently selected plastic bottles and Normal manual twist caps exclusively for the prototype. Other material/cap descriptions below are full-game reference. Align the previously glass-labeled prototype products to the selected material without changing bottle size or stable identities; this documentation update does not modify assets.

## 3. Normal Bulk Cola Container — New Demo Requirements

**Normal Bulk Cola Container** is the official design name for the demo's one bulk liquid container type. It supports the five Normal flavors through the existing content/product identity system. This name does not require renaming existing C++ types, asset paths or saved identities containing Cylinder or Tank.

| Property | Demo requirement |
| --- | --- |
| Contents | Pre-mixed Normal Cola, with remaining supply tracked |
| Pressure | Very high internal pressure; opening the valve produces immediate, powerful, fire-hose-like discharge |
| Structural behavior | Its own internal pressure must not break the container; this does not grant immunity to every unrelated damage source |
| Output temperature | Cola comes out hot; filled bottles must be cooled for the intended production workflow |
| Electricity | Not required |
| Progressive container heating | Disabled in the demo; hot liquid output is distinct from the container heating over time |
| Capacity and tuning | Final capacity, pressure, hot-output temperature and flow/valve values are not specified |

The existing 40-portion source is a prototype configuration, not approved final capacity. Legacy ranges of 50–200 portions and fixed fill speeds are not final requirements. Use normalized gameplay pressure, not a newly invented PSI rating.

Cooling is part of preparing the hot product. This does **not** introduce a new scanner rejection or sale lock for warm bottles: the separately approved, capped temperature discount remains authoritative. The cooling method and acceptance temperature must be specified without inventing a new pricing model.

### Four-stage filling architecture

1. **Normal Bulk Cola Container:** stores the selected liquid and remaining quantity.
2. **Flexible hose and dispenser nozzle:** routes and controls the pressurized supply.
3. **Bottle filling equipment:** a separate, interchangeable stage controls entry into the bottle.
4. **Individual bottle(s):** receive accepted liquid and update Content Level and relevant runtime state.

The designer mentioned glass-specific attachments, attachments filling four bottles and pumps increasing/decreasing liquid pressure as examples for later design. Their specific mechanics, compatibility, number of supported bottles and numerical settings are **not yet finalized demo requirements**. Do not bypass the equipment stage in the design or treat these examples as already implemented.

Reuse Prop Manager identity, liquid transfer, bottle filling, cap and persistence services. The server validates connections and quantities; fractional liquid is preserved and source deductions equal accepted transfer. Bottle damage/Strength and container age survive refill; new liquid receives its own creation/expiry lifecycle. Cosmetic spray does not create or silently remove liquid. Uncaptured spray/spill handling needs a defined policy before implementation.

### Full-game extension, outside demo scope

The designer plans electricity-dependent container cooling, low-voltage consequences and a coal-fired steam generator. Insufficient electrical supply would reduce cooling and permit progressive container heating. Exact voltage, consumption, generation and thermal rules remain for later design. Additional bulk-container types for other liquid categories are also deferred. No such power dependency should be introduced into the demo.

## 4. Bottles, Closures and Art

| Bottle | Current design rule | Remaining detail |
| --- | --- | --- |
| Plastic | Manual twist cap; auto-lock cap closes at full Content Level 1.0; pressure-release variant also supports auto-lock and limited relief. Plastic deforms rather than shattering. | Final cap prices, pressure thresholds, relief rates and durability tuning. These behaviors are corroborated by local core decisions. |
| Glass | Shared glass silhouette with a cylindrical wooden stopper installed by hammering, or a pressure-release cap. **The glass pressure-release cap does not auto-lock when full.** Glass cracks/shatters rather than denting. | Hammer force/quality and glass cap relief/sealing details. The existing fragment recipe has eleven supplied pieces; counts are recipe-specific. |
| Metal can | Fill, place a compatible lid, weld, then seal. Metal dents/deforms rather than shattering. Existing core design records 20% recycling recovery. | Welding equipment, duration, quality, reopening, cleaning and intact-can reuse are unresolved. Do not replace the current demo closure with welding. |

Cap sealing integrity and bottle Strength remain separate. Reuse requires **Strength > 70%, clean and completely empty**, plus any material-specific opening/closure constraints. Plastic is not categorically disposable; caps cannot be recycled. Preserve cap/bottle identity and persistent state through pooling, ASM/ISM transitions and save/load.

Art direction: semi-realistic stylized 3D with **Victorian steampunk, dark fantasy and alchemy** influences. Keep functioning machinery and coins readable and physically plausible; do not make everyday coins excessively decorative. Denominations remain **$1, $2, $5, $10, $20, $50 and $100**. Existing supplied meshes remain configurable assets. “Stylized Victorian Alchemy” is an assistant-suggested shorthand, not a new approved title. No new silhouette dimensions or character/bottle rescaling are authorized by artwork descriptions.

## 5. Conflicts and Disposition

**Superseded** means the newer decision resolves the conflict. **Open** means a missing requirement needs design work before its dependent implementation. **Implementation gap** means documentation is clear but runtime alignment is not established by this update.

| Conflict or gap | Disposition |
| --- | --- |
| Old 25 cm player, fixed patron/player ratios, miniaturized prototype bottles versus approved level sizes | **Superseded:** preserve `Lvl_FirstPerson`. Correct undersized equipment/layout, not the reference characters/bottles. Runtime prototype scale correction remains an **implementation gap**. |
| Heavy cylinder/prop assumptions versus below-player gameplay mass | **Superseded:** explicit gameplay mass below the unchanged player baseline. Tuning and pooling consistency remain implementation work. |
| “Cylinder/Tank” names and 40-portion prototype source versus new container name and undefined final capacity | **Resolved naming; open tuning:** use Normal Bulk Cola Container in design/UI; preserve internal IDs. No automatic capacity rebalance. |
| Fixed 25%/second dispensing, PSI fill bands and explosion above 125% versus configurable transfer/normalized pressure | **Superseded:** fractional transfer and configured pressure/structural limits. Overfill alone is not an automatic detonation trigger; final container flow/pressure are **open**. |
| Mandatory 2.5–5 second cap countdown and inevitable uncapped rocket launch | **Superseded:** no mandatory eruption timer. Leakage, pressure release and force determine behavior. A full uncapped bottle remains ASM under existing core rules. |
| New hot discharge/cooling workflow versus existing filling acceptance evidence | **Implementation gap:** prior liquid-transfer/loop tests do not verify hot pressurized physical discharge or practical cooling. Exact output temperature, cooling equipment/target and uncaptured-spray liquid accounting are **open**. |
| Older global <=4°C target, temperature payout bonuses/rejection and quadratic bottle rebate | **Superseded:** use each cola's configured optimum; sequential content discount then temperature discount capped at 20%. No automatic bottle-cost rebate or new warm-product rejection. |
| Older proportional overfill price/penalty, manual price overrides and old one-flavor/calculator-first demo | **Superseded:** five Normal flavors, fixed configured base prices, no overfill value and approved shared automatic scanner checkout. Existing historical examples do not reopen pricing. |
| Automatic bank credit on every sale versus retained physical sale coins | **Superseded:** sales retain cash; ATM deposit credits bank exactly once. Bank starts $0; register starts empty; existing starter stock is retained. |
| Old plastic single-use/no-recycling rule and indestructible metal keg with bolt-lock lid | **Superseded:** reuse gate and material-specific damage; plastic recycling up to 40%, metal can 20%, welded metal lid. Exact welding/reopening remains **open**. |
| Plastic auto-lock/relief behavior accidentally applied to glass | **Resolved:** glass pressure-release cap has **no** auto-lock on filling; plastic variant retains auto-lock. Other glass mechanism details remain **open**. |
| Current prototype glass labeling paired with a twist cap versus the glass closure design | **Resolved in design by the subsequent plastic-only restriction; implementation gap:** prototype bottles must be plastic with Normal manual twist caps, preserving bottle size and stable identity. `Scripts/ConfigureNormalColaPrototype.py` still labels the bottle “Empty glass bottle” and creates `DA_NormalTwistCap`; material/settings and labels need alignment. Glass stopper/pressure-cap and welding mechanics are outside this prototype. No runtime asset was changed by this documentation task. |
| Generic containment descriptions versus the new plastic pressure/rocket requirement | **Latest explicit rule:** hits/shaking/drops/impacts raise internal pressure; exceeding the plastic bottle's tolerance triggers cola-powered physics-driven rocket propulsion, with orientation and applied forces affecting motion/rotation. No new numeric constants, mandatory countdown, automatic-relief cap or glass fragmentation are implied. Sealed-cap release, held/empty/uncapped cases and flight termination remain **open** in the pressure specification. |
| Earlier direct cap closing versus the new cooperative station | **New requirement:** demo closure uses the two-player Manual Wheel Rotator. One operator presses G for deteriorating downward hold, the other rotates. Insufficient hold before sealing allows leakage; proper sealing persists after hold release. Generic CloseCap tests do not establish this workflow. Solo fallback and operator/session reset/save behavior are **open**. |
| Station G presses versus G/GG/hold-G grab gestures | **Open input conflict:** preserve specified G but define station input context to prevent unintended grab/drop actions. No automatic rebind is authorized by this documentation update. |
| Carrier-required machine transport and “extremely heavy” description versus deferred vehicles/below-player gameplay mass | **Open dependency/tuning:** keep the relocation restriction and existing scale/mass policy; choose a demo carrier/delivery or installed-station arrangement before implementation. Exact weight/dimensions remain undefined. Do not silently add the full vehicle fleet or greater-than-player rigid-body mass. |
| Eight-fragment examples or recap uncertainty versus supplied eleven-piece recipe | **Resolved for full-game reference:** each recipe owns its piece count. Preserve eleven configured bottle fragments, unique ISIDs, persistent ISM slots and approved initial-component representation; do not hardcode eight. Fracture/fragment acceptance is excluded from the plastic-only prototype. |
| Future hazard formulas/effects or proposed nozzle/pump types treated as demo features | **Superseded scope / open design:** Normal only; effects and specific filling attachments need their own design. Electricity, generator and other bulk-container types remain deferred. |
| Browser “Plan Food Migration” versus explicit local legacy exclusions | **Superseded:** all eleven PSOs stay excluded, including PSO_FoodContainer. Preserve assets; do not resume migration. |
| Prototype Zero Calories price is below Spicy | **Implementation gap:** `Scripts/ConfigureNormalColaPrototype.py` currently configures per-portion rates 2.25 and 4.5 respectively. They must match under the approved relative-price rule; no numeric balance is changed in this documentation task. |
| Old browser pause/test summaries versus current development state | **Historical only:** use the latest local validation record. A chat claim or this GDD update does not establish implementation, a new passing test or a fully playable build. |

## 6. Implementation and Acceptance Follow-up

1. Correct prototype prop/equipment/delivery scale, spacing, collision and accessibility against `Lvl_FirstPerson`, preserving player/NPC/bottle reference sizes. Validate server and remote-client carry, release, placement, shelf, bucket and scanner paths at those dimensions.
2. Align the configurable Normal price tie and gameplay masses through existing shared systems. Retain finite cash, ATM accounting, fixed prices and stable IDs.
3. Define hot-output temperature, container pressure/flow, valve interaction, practical cooling and uncaptured spray behavior. Finalize a minimum compatible filling-equipment path before implementing its missing interactions. Do not add advanced attachments or power simulation by assumption.
4. Integrate and test the container → hose/nozzle → filling equipment → bottle path using existing authoritative services. Verify accepted-volume conservation, no internal-pressure destruction of the demo source, no demo electrical dependency, new liquid expiry and preserved bottle history.
5. Repeat the complete plastic/Normal/manual-twist loop from purchase to physical payment/change, ATM deposit and reinvestment, including reconnect/save-load and repeated NPC visits. Validate the newly approved pressure/rocket subsystem under its acceptance matrix; test relevant thermal/manual-cap regressions and profile the corrected world. Glass/fragment and other closure systems are deferred from prototype acceptance.

The latest existing native baseline is build **11156, 39 passing tests**, with the known animation warning. Earlier remote-loop evidence used undersized prototype props; it does not accept the corrected scale or these new container mechanics. No build, PIE session, gameplay test or performance measurement was run for this documentation reconciliation.
