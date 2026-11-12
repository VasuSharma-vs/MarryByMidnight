# Mechanics, Tools & Workstations Specification

This document provides technical design specifications for the interactive stations, player-operated tools, mathematical formulas, and physical interaction mechanics in **Midnight Fizz: Giant Thirst**.

**Latest equipment authority — 11 November 2026:** The designer's complete [Equipment Design Specification](Equipment_Design_Specification.md) defines five categories and the shared electrical/pressure/variant properties. **Only the Manual Wheel Rotator is in the demo from that catalog:** two players, G-press downward holding force with declining effectiveness, progressive rotation by the second player, no electricity, carrier-required relocation. It works with plastic/Normal/manual twist caps and existing pressure/leakage/rocket services. Bulk container, hose/dispenser and essential filling infrastructure remain in scope. The other four catalog types are full-game-only; old station/gun/freezer/vehicle proposals below do not expand scope. See [equipment integration and conflicts](Equipment_Design_Integration_2026-11-11.md) for G input contention, carrier/mass questions, unspecified control/tuning and required multiplayer acceptance.

**Latest prototype restriction — 11 November 2026:** [Plastic Bottle Pressure System](Prototype_Plastic_Bottle_Pressure_2026-11-11.md) limits the prototype to plastic bottles, the five Normal Colas and Normal manual twist caps. Hits/shaking/drops/impacts increase internal pressure; exceeding bottle tolerance causes pressure release and orientation/force-dependent physics rocket propulsion similar to `BP_Cola`. Other bottle materials, cap variants and fragmentation are future scope. Preserve current player/NPC/bottle sizes. Exact gains, tolerance, release/thrust tuning and special cap/held/empty cases remain open; this is not a claim of implementation or passing tests.

**Current chat-derived requirements — 11 November 2026:** Follow [the decisions and conflict register](Chat_Design_Decisions_2026-11-11.md). Demo liquids are the five Normal Colas only. The source is the Normal Bulk Cola Container: hot, high-pressure discharge, no breakage from its own pressure, no electricity or progressive source heating in the demo. Keep all existing reference sizes. Numeric workstation examples below are historical tuning proposals unless separately approved; they do not establish implemented or tested behavior.

**Current scale and mass policy:** [GDD Master, Section 3](GDD_Master.md#3-the-scale-twist-tiny-people-giant-world) supersedes earlier miniature-player and overweight-cylinder assumptions. Keep the current default player, existing NPCs and Cola sizes from `Lvl_FirstPerson`. Tools, vehicles, filling equipment and cleaning controls fit that player; interfaces and supports accommodate giant bottles. NPCs and movable props use gameplay masses below the player baseline.

---

## 1. Station 1: Normal Bulk Cola Container and Filling Equipment

The **Normal Bulk Cola Container** is the demo's one bulk container type for pre-mixed Lemonade, Mint, Sweet, Spicy and Zero Calories. Existing Cylinder/Tank class and asset identifiers can remain for compatibility. Opening its valve produces immediate, powerful, fire-hose-like discharge of hot cola. Its own pressure does not break the source. Electricity and progressive container heating are disabled in the demo; liquid output is still hot.

```
 [ Normal Bulk Cola Container ]
           |
           v
 [ Flexible Hose + Dispenser Nozzle ]
           |
           v
 [ Compatible Bottle Filling Equipment ]
           |
           v
 [ Individual Bottle(s) ]
```

### 1.1 Physical Components & Setup
- **Container:** Tracks liquid identity and remaining fractional supply. The existing 40 portions is prototype configuration; final capacity is open. Gameplay mass remains below the unchanged player's reference mass.
- **Hose/dispenser:** Routes and controls supply before the separate filling-equipment stage. Specific hose implementation, reach, connection geometry and valve/control interaction still need definition; the earlier 350-unit example does not override giant-prop accessibility.
- **Filling equipment:** An explicit interchangeable stage between dispenser and bottle. Glass-specific attachments, four-bottle heads and pressure-control pumps are designer examples for later design. Their exact compatibility, pressure behavior and throughput are not finalized demo requirements.
- **Access:** Fit controls and tools to the current player, and supports/interfaces to the current giant bottles. Solve reach, collision, sight lines and placement without rescaling the player, NPCs or bottles.

### 1.2 Dispensing Flow & Fill Mechanics
- Use the existing server-authoritative Prop Manager liquid-transfer and bottle-filling services; do not duplicate quantity or identity ownership in a nozzle Blueprint.
- Preserve fractional content. Deduct the quantity actually accepted from the source, initialize the new liquid's expiry lifecycle and preserve the bottle's Strength, damage and other physical history.
- Hot-output temperature, source pressure, valve flow, fill rate and cooling acceptance values remain open. Earlier 25%-per-second and PSI tables are superseded proposals, not new balance approval.
- Standard full bottle Content Level is 1.0; configured overflow can extend to 1.7, subject to condition/containment rules. Extra fill has no extra selling value. Neither 100% nor 125% is an automatic detonation trigger; pressure/structural limits determine failure.
- Full uncapped bottles remain active ASMs according to the existing core lifecycle; no mandatory eruption timer is introduced.
- Define uncaptured spray/spill quantity handling before implementing free discharge. Cosmetic spray must not create or silently delete liquid.

### 1.3 Demo and Full-Game Boundary

Cool filled bottles toward the selected cola's configured optimum. Warm bottles retain the separately approved temperature discount; cooling is not a new scanner rejection rule. Cooling equipment and numerical targets are not finalized by the browser conversation. Full-game electricity-dependent source cooling, low-voltage behavior, progressive heating, coal-fired steam generation and additional bulk-container types are deferred.

---

## 2. Station 2: Manual Wheel Rotator — Cooperative Manual Cap Closure

The prototype uses **Normal manual twist caps on plastic bottles only**, closed through the two-player **Manual Wheel Rotator**. Development name is confirmed; Twin-Hand Cap Crank is a suggested display name, not final. This is the cheapest cap-closing equipment, a large manual station with extremely low portability and carrier-required relocation. It consumes no electricity. Exact footprint, weight and demo transport arrangement remain open.

**Player 1:** repeatedly press G to apply downward force holding the cap against the opening. Continued pressing maintains temporary hold, but its effectiveness gradually deteriorates even during rapid pressing. **Player 2:** operate the rotation mechanism and progressively close the cap before effective hold becomes insufficient. The rotation input, force/decay/turn values and reset/cancel behavior are not specified. Once sealed, releasing hold does not reopen the cap.

Insufficient hold, stopping presses or slow rotation can allow pressurized cola to escape before closure. Reuse the existing bottle's pressure, cap and leakage/failure systems, with server-owned shared operator state and liquid accounting. Leakage is not itself an automatic rocket timer; the approved pressure-threshold reaction remains authoritative. G conflicts with existing grab gestures and needs an agreed station-input context. No solo bypass, new force conversion or vehicle-fleet dependency is assumed. See [the equipment specification](Equipment_Design_Specification.md#4-equipment-type-2--manual-wheel-rotator) and [integration/conflicts](Equipment_Design_Integration_2026-11-11.md).

### Historical F1 Equipment Proposal — Full-Game Reference Only

The pneumatic-gun diagram, torque windows and mobile rig below are historical proposals. The new full-game Cap Gun specification supersedes conflicting gun details; it is electrically powered despite F1 pneumatic-tool inspiration. These are not the demo rotator's controls or tuning. Other plastic cap variants, glass stoppers/pressure caps and welded lids remain outside the prototype. The overpressure escape mechanism for a fully sealed manual cap is still open in the [pressure specification](Prototype_Plastic_Bottle_Pressure_2026-11-11.md).

```
                [ Pneumatic / Mechanical Torque Wrench ]
                                   |
                                   v  (Spinning Nut Gun)
             [ Cap ]  =======>  [ Bottle Neck ]
```

### 2.1 Capping Timing Authority

The former mandatory 2.5–5 second Cap Urgency Countdown is superseded. Uncapped orientation, leakage, pressure, sealing integrity and configured containment behavior determine what happens; nozzle removal alone does not start an inevitable rocket launch. Cap sealing integrity remains separate from bottle Strength.

### 2.2 The Torque Minigame
When the player mounts the capping gun onto the bottle neck and activates it:
1. A **Circular Torque Dial / Moving Bar** appears on screen or in-world (Diegetic HUD above the capping rig).
2. A torque needle begins accelerating rapidly through three sectors: **Loose (Blue) $\rightarrow$ Ideal Torque (Green) $\rightarrow$ Over-Torqued (Red)**.
3. The player must release the torque trigger within the **Green Sweet Spot**.

```
[  BLUE: Under-Torque (0-40%)  |  GREEN: Perfect Seal (41-75%)  |  RED: Over-Torque (76-100%)  ]
```

### 2.3 Capping Outcomes Matrix
| Torque Result | Needle Position | Cap Integrity | Physical Outcome |
| :--- | :--- | :--- | :--- |
| **Under-Torqued** (Too Loose) | Blue (0% – 40%) | Weak Seal ($< 15\text{ Nm}$) | Cap pops off after a short delay or upon the slightest shake. The bottle turns into an erratic rocket. |
| **Perfect Seal** (F1 Sweet Spot) | **Green (41% – 75%)** | **Hermetic Seal** | Perfect seal. Bottle can withstand standard handling, shaking thresholds, and temperature changes. |
| **Over-Torqued** (Too Tight) | Red (76% – 100%) | Structural Failure ($> 60\text{ Nm}$) | **Cap snaps or bottle neck fractures!** Glass bottles shatter instantly, while metal caps strip their threads and leak violently. |

### 2.4 Cola-Specific Failure Reactions
**Historical effect proposals only:** these named reactions and durations are not approved Normal-demo effects. A flavor name such as Spicy does not approve a fire hazard.

If capping fails (blown off by delay, popped off by under-torquing, or broken by over-torquing), the bottle triggers its unique chemical reaction:
- **Basic Cola:** High-speed foam rocket that bounces off walls.
- **Spicy / Fire Cola:** Fiery backblast that leaves scorching flames on the workstation.
- **Toxic Swamp Cola:** Releases a lingering green toxic gas cloud that blinds and slows players.
- **Spider Nest Cola:** Bursts open and unleashes crawling spiders that run across the counters and jam tools.
- **Hangover Cola:** Emits dizzy vapor causing player controls to invert for 8 seconds.

### 2.5 Alternative Tool: The Mobile Capper Buggy (Drive-by Capping)
In addition to the stationary capping workbench, the team can deploy the **Mobile Capper Buggy**:
- **Operation:** A player drives a motorized cart equipped with an overhead pneumatic press down a line of freshly filled bottles sitting on floor crates or conveyor belts.
- **Drive-By Torque Action:** The driver lines up the crosshair over the bottle neck and taps the torque pedal (`Spacebar` / Handbrake). 
- **Advantage:** Eliminates the need to carry heavy bottles one by one across the giant room to the stationary bench.
- **Risk:** Requires precise vehicle alignment; hitting a bottle with the cart bumper knocks it over, instantly launching it as an unsealed rocket!

---

## 3. Station 3: Chilling & Temperature Management

The new demo source dispenses hot cola, so cooling is part of production. Use each cola's configured optimum and the approved temperature discount, capped at 20%. A warm bottle is not automatically rejected. Final hot-output temperature and practical cooling workflow remain open. The numerical thermal/equipment examples in Sections 3.1–3.2 are tuning proposals, not newly approved values.

### 3.1 Temperature Dynamics
- **Ambient Shop Temperature:** $22.0^\circ\text{C}$.
- **Target Serving Temperature:** $\le 4.0^\circ\text{C}$ (Ideal: $2.0^\circ\text{C} - 4.0^\circ\text{C}$).
- **Thermal Decay (Newton's Law of Cooling):**
  $$\frac{dT}{dt} = -k \cdot (T_{bottle} - T_{ambient})$$
  Where $k$ is the thermal conductivity of the bottle material (e.g., Metal cools and warms quickly; Glass warms slowly).

### 3.2 Cooling Equipment
1. **The Crushed Ice Trough:**
   - Low-tech, passive cooler.
   - Players haul blocks of ice from the cellar into the trough.
   - Cools submerged bottles at **$-1.5^\circ\text{C}/\text{second}$**. Holds up to 6 bottles.
2. **The Blast Freezer / Cryo-Chamber:**
   - Advanced high-speed machine.
   - Cools bottles to $2.0^\circ\text{C}$ in **$3.0\text{ seconds}$**.
   - Risk: Leaving a bottle in the blast freezer for $> 10\text{ seconds}$ causes the liquid to freeze and expand, shattering the bottle!

### 3.3 Customer Temperature Multipliers
**Superseded pricing proposal:** the multipliers/rejection below must not be used for the current scanner. Use the configured optimum and at most 20% discount; no frosty payout multiplier is approved by this chat update.

- **Frosty ($\le 4.0^\circ\text{C}$):** $1.2\times$ Payout Multiplier + "Brain Freeze" Giant Tip.
- **Chilled ($4.1^\circ\text{C} - 10.0^\circ\text{C}$):** $1.0\times$ Standard Payout.
- **Lukewarm ($10.1^\circ\text{C} - 18.0^\circ\text{C}$):** $0.6\times$ Payout Penalty + Giant Grunts in Disgust.
- **Warm / Hot ($> 18.0^\circ\text{C}$):** $0.2\times$ Payout or outright customer rejection.

---

## 4. Station 4: Checkout Scanner & Pricing Engine

The Checkout Scanner is an automated laser scanning scale mounted at the serving counter. When a bottle is placed on the scanning platter, it evaluates liquid fill portion, temperature, and bottle integrity to calculate the customer bill.

```
       [ Optical & Portion Scanner ]
                    |
                    v
   [ Scanning Platter: Bottle Placed ]  ===> [ Digital Readout Screen ]
                                             "Portions: 3/4"
                                             "Temp: 3.5 C"
                                             "Total Bill: $3.25"
```

### 4.1 The Underfill Economic Rule

Use the latest [scanner/checkout rules](Scanner_Checkout_Design_Update_2026-11-10.md). Underfill reduces liquid revenue proportionally. Bottle purchase cost, supply cost and recycling refund are separate values, so net profit depends on configured costs; the scanner does not guarantee a loss for every underfilled bottle or add a bottle-cost rebate.

### 4.2 Scanner Pricing Formulas

```text
FullBottleBase = NominalPortions × FixedPricePerPortion
ContentAdjusted = FullBottleBase × Clamp(ContentLevel, 0, 1)
TemperatureDiscount = 0.20 × Clamp(Abs(CurrentTemperature - OptimumTemperature)
                                  / ConfiguredTemperaturePenaltyRange, 0, 1)
BottleQuote = ContentAdjusted × (1 - TemperatureDiscount)
```

Optimum temperature and the positive deviation range are configured per cola. Apply content and temperature discounts sequentially, with no extra value for overfill. Base prices follow `Lemonade < Mint < Sweet < Spicy = Zero Calories` at matching nominal capacity. Detailed prices remain in cents; round only the aggregate payable bill to the nearest whole dollar, with half-dollar ties rounded up for nonnegative totals. Players cannot override configured selling prices.

### 4.3 Checkout, Expiry and Banking

Live scans follow bottles entering/leaving the volume; saved entries retain unique ISIDs and can be cleared before billing. Lock the original invoice before final payment. Expired bottles are visible for player inspection but not automatically prohibited from sale. Each expired bottle sold triggers one full-base-price penalty plus a full fresh same-cola/same-capacity replacement, or another full-base-price deduction. Discounts/overfill do not alter that base penalty; preserve debt when funds are insufficient.

Payment and exact change transfer finite physical $1/$2/$5/$10/$20/$50/$100 coin identities. Any teammate may continue the transaction, subject to server validation. The register starts empty; the bank starts $0 with existing starter stock preserved. Sale cash stays in the register until ATM deposit. Do not also credit the bank at sale completion. See [ATM accounting](ATM_Cash_Accounting_Proposal.md).

---

## 5. Station 5: Bottle Cleaning & Decontamination Station

**Scope and reuse authority:** this is later station-design work; it does not reactivate the excluded PSO_Sink or PSO_FoodContainer assets. The approved prototype recycles NPCs and retires purchased goods after departure. Reuse requires Strength > 70%, clean and completely empty. The contamination discounts/effects and numerical brush timing below are historical proposals, not additional approved scanner penalties or Normal-demo hazards.

When giant customers finish their beverages, they slam the empties onto the return conveyor chute. Empty bottles can be recycled and reused, but they are returned dirty and unfit for immediate refilling.

```
 [ Giant Return Chute ] ===> [ Soaking Basin ] ===> [ Rotary Scrub Brush ] ===> [ Clean Rack ]
```

### 5.1 Bottle Cleanliness States
1. **Pristine / New:** Directly from an unboxed crate. Ready to fill.
2. **Dirty (Normal):** Covered in sticky dried syrup residues. If filled without washing, the scanner applies a $50\%$ "Contaminated" deduction.
3. **Hazardous / Infested:**
   - *Slime-Coated:* Returned by Witches or Swamp Golems. Slippery; slips out of player hands when grabbed.
   - *Spider Egg Infested:* Returned from Spider Nest Cola. If refilled without sterilizing, it spawns spiders upon filling.

### 5.2 Cleaning Workflow & Tools
- **The Scrub Sink:** Contains warm soapy water and a foot-pedal operated rotating bristle brush.
- **Workflow:**
  1. Player dumps the empty bottle into the sink.
  2. Player presses the foot pedal to spin the rotary brush for **$2.5\text{ seconds}$**.
  3. Clean bubbles pop, and the bottle changes material from `M_DirtyGlass` to `M_PristineGlass`.
  4. Place on the clean drying rack for the dispenser operator.

---

## 6. Station 6: Shop Vehicles & Material Handling Machines

The giant-scale workshop creates long transport routes for the unchanged default-sized player. The full game includes player-sized vehicles and handling machines to improve traversal and throughput. Their design does not require a 25 cm player or cargo heavier than the player.

```
       [ Micro-Forklift ]                   [ Fizz-Mule Flatbed ]
    Hoists Bulky Cylinders               Transports Crates & Bottles
    Docks into Manifolds                  2-Player Co-op Drivable
              \                                     /
               +-----------------------------------+
               |    [ Mobile Capper Buggy ]        |
               | Drives down bottle lines to torque|
               +-----------------------------------+
```

### 6.1 The Fizz-Mule (Flatbed Logistics Kart)
- **Role:** High-speed transport across the long tavern counter.
- **Capacity:** Flatbed holds up to 2 bottle crates (12 bottles total) or 1 bulk ice block or 1 empty cylinder.
- **Controls:** Standard driving (WASD / Left Stick for throttle & steering; Handbrake for drifting).
- **Passenger Seat:** Supports a second player standing on the bed to load/unload items on the move!
- **Cargo Clamping:** Features a magnetic lock switch (`F` key) that prevents crates from sliding off during turns. If the player forgets to engage the magnet, sharp turns fling cargo off the bed!

### 6.2 The Micro-Forklift (Heavy Cylinder Hoist)
- **Role:** Material handling for bulky Cola cylinders, with configured gameplay mass below the player baseline.
- **Hydraulic Lift:** Uses dedicated controls (`Q` to lower forks, `E` to raise forks).
- **Cylinder Manifold Docking:** 
  1. Driver approaches the freight delivery bay and slides forks beneath the cylinder pallet.
  2. Driver raises the forks and navigates through the workshop to the Dispenser Station.
  3. Driver aligns the cylinder over the manifold dock and lowers the forks.
  4. A loud pneumatic *clunk-hiss* locks the cylinder into the shop's liquid line.

### 6.3 The Mobile Capper Buggy ("The Pit-Stop Machine")
- **Role:** High-efficiency mechanized capping for mass-production lines.
- **Mechanism:** Fitted with a roof-mounted pneumatic capping piston and an automated cap feed chute.
- **Workflow:** 
  1. The team fills an entire row of 6 bottles on the counter.
  2. The Capper Buggy drives along the row.
  3. As the buggy passes over each bottle neck, the driver presses the torque stomp-pedal (`Spacebar`).
  4. The hydraulic arm slams down, spins the nut with F1 pneumatic torque, and retracts in $0.4\text{ seconds}$.
- **Skill Test:** Requires steady throttle control. Driving too fast misses the bottle neck; driving too close can sideswipe bottles and shatter them!

### 6.4 Vehicle Handling Dynamics & Transit Agitation
Driving vehicles introduces unique physical gameplay and risks:
- **Transit Agitation:** Driving smoothly on flat counter surfaces adds only $+1.0\,A/\text{sec}$ to carried bottles.
- **Drifting / Sharp Turns:** Adds $+12.0\,A$ to all bottles on the flatbed.
- **Bumping & Crashes:** Crashing into walls, barrels, or teammates causes a violent impact ($+35.0\,A$), risking immediate bottle detonation in the cargo bed!
- **Ramp Traversal:** Ramps connect the lower floor, barstools, and high serving counters. Vehicles with heavy loads must maintain momentum to make it up steep inclines.

