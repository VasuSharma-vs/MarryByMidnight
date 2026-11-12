# Colossal Cola Co. — Equipment Design Specification

**Status:** Game Design Specification

**Version:** November 11, 2026

**Scope:** Demo and Full Game

**Art Direction:** Victorian Steampunk, Dark Fantasy, Industrial Alchemy

**Local GDD integration — 11 November 2026:** The designer supplied this specification for inclusion in the local GDD. The complete source specification is retained below. [Equipment integration and conflicts](Equipment_Design_Integration_2026-11-11.md) records its relationship to existing rules and the outstanding design details. Only the **Manual Wheel Rotator** from this five-type catalog is in the demo; the Normal Bulk Cola Container, hose/dispenser and essential filling infrastructure remain in scope. Plastic bottles, five Normal Cola flavors and Normal manual twist caps remain the prototype's material/content/cap boundary. Preserve current player/NPC/bottle sizes and approved gameplay-mass policy. This is design documentation, not evidence that any machine has been implemented or tested.

---

# 1. Equipment System Overview

Colossal Cola Co. features various machines and tools that players use to fill, seal, regulate, and clean cola bottles.

Equipment differs in price, operating speed, power requirements, operating pressure, portability, player requirements, and potential failure risks.

The equipment system is designed around several gameplay principles:

- Manual equipment is generally cheaper but requires greater player effort and cooperation.
- Powered equipment improves efficiency but introduces electricity requirements and additional operating costs.
- More advanced equipment can process bottles faster or handle multiple bottles simultaneously.
- Incorrect equipment operation can cause pressure-related incidents, damaged bottle caps, cola spills, and other failure events.
- Equipment mobility varies from small handheld tools to large industrial machines requiring carrier vehicles.
- Equipment variants offer different capabilities, capacities, costs, and operating requirements.

The full game will introduce more equipment types and variants as development progresses.

---

# 2. Common Equipment Properties

Every powered machine must expose the following three properties.

### 2.1 RequiredVoltage

`RequiredVoltage`

Defines the minimum electrical voltage required for the machine to operate correctly.

- Machines require an adequate voltage supply.
- Different machines may have different voltage requirements.
- Advanced equipment may require higher voltage.
- Low-voltage events can affect equipment operation.
- Exact behavior when voltage falls below the required threshold will be determined during electrical-system development.

### 2.2 PowerConsumption

`PowerConsumption`

Defines how much electrical power the machine consumes during operation.

- More powerful equipment can have higher power consumption.
- Equipment with additional working components or nozzles may consume more power.
- Power requirements contribute to shop electricity management.
- Future electrical systems can use this property to calculate total electrical demand.

### 2.3 OperatingPressureRange

`OperatingPressureRange`

Defines the supported operating pressure range of the equipment.

Contains:

- MinimumOperatingPressure
- MaximumOperatingPressure

Pressure-sensitive equipment must operate within its supported limits.

Operating outside those limits may affect equipment performance or create failure risks, depending on the machine's mechanics.

For equipment that does not use liquid or pneumatic pressure, this property can be marked as not applicable.

### 2.4 Additional Common Equipment Attributes

The equipment system should also support:

- EquipmentType
- EquipmentVariant
- PurchasePrice
- Portability
- EquipmentSize
- RequiredPlayers
- EnergySource
- FuelConsumption, where applicable
- ProcessingCapacity, where applicable
- OperatingSpeed, where applicable

These additional attributes describe equipment characteristics and can be expanded as new equipment types are introduced.

---

# 3. Equipment Type 1 — Cap Gun

**Development Name:** Cap Gun

**Suggested In-Game Name:** Pitlock Cap Gun

**Availability:** Full Game Only

## 3.1 Description

The Cap Gun is inspired by the pneumatic impact wheel guns used by Formula 1 pit crews to remove and install the wheel nuts on racing cars.

It is a powerful, highly portable cap-closing tool designed to secure cola bottle caps rapidly.

Despite its compact size, the Cap Gun requires a high-voltage electrical supply.

## 3.2 Gameplay Mechanics

1. The player obtains the Cap Gun.
2. The player positions the tool on the bottle cap.
3. The player activates the tool to rotate and tighten the cap.
4. The tool applies tightening torque.
5. The player must avoid excessive tightening.
6. Once the cap is properly secured, the bottle is sealed.

## 3.3 Cap Failure

Excessive tightening torque can damage or break the bottle cap.

If the cap is tightened beyond its tolerance:

- The cap can break.
- A cap failure event is triggered.
- The bottle may no longer remain properly sealed.
- Pressurized cola may escape according to the bottle's pressure and failure mechanics.

## 3.4 Equipment Properties

- Price: Expensive
- Portability: Very high
- Size: Handheld
- Required Players: 1
- Energy Source: Electricity
- RequiredVoltage: High
- PowerConsumption: To be determined
- OperatingPressureRange: Applicable if required by the final tool design
- Main Risk: Excessive tightening torque

## 3.5 Advantages

- Fast cap installation.
- Highly portable.
- Suitable for operating in different shop locations.
- Does not require a second player for cap tightening.

## 3.6 Disadvantages

- Expensive.
- Requires high-voltage electrical power.
- Improper operation can damage caps.

---

# 4. Equipment Type 2 — Manual Wheel Rotator

**Development Name:** Manual Wheel Rotator

**Suggested In-Game Name:** Twin-Hand Cap Crank

**Final Name:** Not Yet Decided

**Availability:** Demo and Full Game

## 4.1 Description

The Manual Wheel Rotator is a large, fully mechanical bottle-cap installation station.

It requires two players to work together to safely close a cola bottle containing pressurized liquid.

One player applies downward pressure to prevent cola from escaping, while the second player rotates the cap to secure it.

This equipment does not require electricity.

It is the cheapest cap-closing equipment but is extremely heavy, occupies considerable space, and is difficult to transport.

## 4.2 Player 1 — Downward Cap Pressure

Player 1 is responsible for pressing the bottle cap against the bottle opening.

The player must rapidly press the `G` key.

Each press contributes to the downward pressure applied to the cap.

This downward force helps prevent pressurized cola from escaping while the cap is still being installed.

### Pressure Holding Mechanics

- Rapidly pressing `G` applies downward force to the cap.
- This temporarily prevents pressurized cola from escaping.
- The player must continue pressing to maintain the holding effect.
- As time passes, the effectiveness of pressing gradually decreases.
- The cap becomes increasingly difficult to hold against the cola's internal pressure.
- Once the holding force becomes insufficient, cola begins escaping.

The decreasing effectiveness creates time pressure for both players.

Even if Player 1 continues pressing rapidly, the holding effectiveness will gradually deteriorate.

## 4.3 Player 2 — Cap Rotation

Player 2 is responsible for mechanically rotating the bottle cap.

While Player 1 holds the cap down:

1. Player 2 operates the rotational mechanism.
2. The mechanism rotates the bottle cap.
3. The cap progressively moves toward its fully closed position.
4. Player 2 must complete the rotation before Player 1 loses effective pressure control.
5. Once the cap is fully secured, the bottle becomes sealed.

## 4.4 Cooperative Gameplay

Both players must coordinate their actions.

**Player 1:** Maintain downward pressure by repeatedly pressing `G`.

**Player 2:** Rotate and close the cap as quickly as possible.

If Player 2 takes too long, Player 1's pressure-holding effectiveness will decline.

When the applied downward force becomes insufficient, cola starts escaping.

If the cap has already been secured, releasing downward pressure will not cause the cola to escape through the cap opening.

## 4.5 Failure Conditions

Possible failure conditions include:

- Player 1 stops pressing before the cap is secured.
- Player 1 cannot maintain sufficient downward pressure.
- Player 2 takes too long to complete the rotation.
- Cola begins escaping before the cap is fully sealed.

The resulting cola release should follow the existing bottle pressure and leakage mechanics.

## 4.6 Equipment Properties

- Price: Cheapest cap-closing equipment
- Portability: Extremely low
- Size: Large station
- Required Players: 2
- Energy Source: Manual mechanical operation
- RequiredVoltage: Not applicable
- PowerConsumption: 0
- OperatingPressureRange: Not yet defined
- Transportation: Requires a carrier vehicle
- Main Challenge: Player coordination and declining pressure-holding effectiveness

## 4.7 Advantages

- Cheapest cap-closing option.
- No electricity required.
- Supports cooperative gameplay.
- Can operate during electrical outages.
- Encourages communication and teamwork.

## 4.8 Disadvantages

- Requires two players.
- Large and heavy.
- Very difficult to relocate.
- Requires a carrier vehicle for transportation.
- Operation is time-sensitive.
- Poor coordination can cause cola spills.

---

# 5. Equipment Type 3 — Automatic Fill and Cap Machine

**Development Name:** Automatic Fill and Cap Machine

**Suggested In-Game Name:** HydraSeal Filling Rig

**Final Name:** Not Yet Decided

**Availability:** Full Game Only

## 5.1 Description

The Automatic Fill and Cap Machine is an advanced industrial bottling machine.

It combines two functions:

1. Filling a cola bottle.
2. Automatically closing its cap.

The equipment consists of a central machine connected to multiple handheld nozzles by flexible hoses.

Players can grab these nozzles and position them on bottles.

The equipment is relatively compact in its smaller variants, but larger variants require more space.

It is the most expensive equipment type in the current equipment catalog and requires very high electrical voltage.

## 5.2 Nozzle Mechanics

Each nozzle is physically connected to the main machine by a flexible hose.

The player can:

1. Grab a nozzle.
2. Move the nozzle toward a bottle.
3. Position it correctly on the bottle opening or cap interface.
4. Activate the filling function.
5. Activate the cap-closing function.
6. Remove the nozzle after the operation is complete.

The nozzle provides both liquid filling and cap-closing functionality.

## 5.3 Controls

The machine uses two separate control inputs.

**Control 1 — Fill Bottle**

Activates the cola-filling operation.

**Control 2 — Close Cap**

Activates the automated cap-closing mechanism.

This separation allows the player to control filling and sealing independently.

## 5.4 Overfilling Failure

If the player fills the bottle beyond its safe capacity, excessive internal pressure can develop.

Possible consequences:

- Bottle overpressure.
- Pressure-related damage.
- Existing bottle pressure failure events.
- Cola leakage or release, depending on bottle conditions.

The final result depends on the bottle type, bottle strength, pressure tolerance, and cap state.

## 5.5 Excessive Cap Tightening

If the player applies excessive cap-tightening force:

- The cap can become damaged.
- Cap failure events can occur.
- The bottle may lose its seal.
- Pressurized cola may escape.

## 5.6 Equipment Variants

The Automatic Fill and Cap Machine comes in multiple variants.

The primary distinction is the number of available nozzles.

Increasing nozzle count affects:

- Equipment size.
- Equipment weight.
- Portability.
- RequiredVoltage.
- PowerConsumption.
- Required operating pressure.
- Purchase price.
- Potential simultaneous operating capacity.

### Suggested Variant Structure

- Single-Nozzle Variant
- Dual-Nozzle Variant
- Four-Nozzle Variant
- Eight-Nozzle Industrial Variant

These nozzle counts are provisional suggestions, not finalized specifications.

## 5.7 Equipment Properties

- Price: Most expensive equipment type
- Portability: Depends on variant
- Size: Small to large
- Required Players: Depends on usage and nozzle count
- Energy Source: Electricity
- RequiredVoltage: Very high
- PowerConsumption: Depends on variant
- OperatingPressureRange: Depends on variant
- Main Risks: Overfilling and excessive cap tightening

## 5.8 Advantages

- Combines filling and cap installation.
- Reduces manual work.
- Multiple nozzles can improve bottling throughput.
- Advanced variants support greater production capacity.

## 5.9 Disadvantages

- Extremely expensive.
- Very high electrical requirements.
- Larger variants consume more space.
- Incorrect operation can cause pressure or cap failures.

---

# 6. Equipment Type 4 — Pressure Regulator

**Development Name:** Pressure Regulator

**Suggested In-Game Name:** Pressure Governor

**Availability:** Full Game Only

## 6.1 Description

The Pressure Regulator controls the pressure supplied to liquid-handling equipment.

It can both increase and decrease pressure.

Different equipment may require different operating pressure levels, making pressure regulation important for advanced bottling operations.

The regulator requires an energy source, either fuel or electricity, and operates as part of a liquid-handling system.

## 6.2 Pressure Increase

The regulator can increase output pressure within the limits of its selected variant.

This allows equipment requiring higher operating pressure to receive an appropriate supply.

The regulator cannot increase pressure beyond its supported maximum range.

## 6.3 Pressure Decrease

The regulator can reduce output pressure.

This can help bring an excessively pressurized liquid supply into the operating range required by other equipment.

Pressure reduction is particularly useful when working with high-pressure bulk cola containers.

## 6.4 Power Variants

The Pressure Regulator has two primary energy-source categories.

### Fuel-Powered Regulator

- Uses fuel during operation.
- Does not require electrical power for its primary operation.
- Has fuel consumption requirements.
- Available in multiple pressure-capability variants.

### Electric Regulator

- Uses electrical power.
- Has RequiredVoltage and PowerConsumption properties.
- Available in multiple pressure-capability variants.
- Can be affected by future shop voltage problems.

## 6.5 Pressure Range Variants

Each energy-source category can have multiple subvariants.

Subvariants differ in:

- Minimum supported pressure.
- Maximum supported pressure.
- Total pressure adjustment range.
- Purchase price.
- Fuel consumption or electricity consumption.

A regulator with a wider adjustable pressure range is more expensive.

More capable variants may also consume more fuel or electricity.

## 6.6 Suggested Variant Examples

- Basic Fuel Pressure Governor
- Advanced Fuel Pressure Governor
- Basic Electric Pressure Governor
- Industrial Electric Pressure Governor

The exact pressure ratings and energy consumption values remain to be designed.

## 6.7 Equipment Properties

- Price: Depends on pressure range and energy source
- Portability: Depends on variant
- Energy Source: Fuel or electricity
- RequiredVoltage: Applicable to electric variants
- PowerConsumption: Applicable to electric variants
- FuelConsumption: Applicable to fuel-powered variants
- OperatingPressureRange: Variant-dependent
- Liquid Supply: Required for liquid-system operation
- Main Purpose: Increase or decrease supplied liquid pressure

---

# 7. Equipment Type 5 — Automatic Bottle Cleaner

**Development Name:** Automatic Bottle Cleaner

**Suggested In-Game Name:** Rinse-O-Matic

**Final Name:** Not Yet Decided

**Availability:** Full Game Only

## 7.1 Description

The Automatic Bottle Cleaner is a machine that automatically cleans used cola bottles.

The player places dirty bottles inside the cleaning chamber.

After several seconds, the machine finishes cleaning the bottles.

The equipment is designed to support bottle recycling and reuse.

## 7.2 Gameplay Flow

1. Player obtains a dirty bottle.
2. Player opens or accesses the cleaning machine.
3. Player places the bottle inside.
4. Player activates the cleaning process.
5. The machine processes the bottle for several seconds.
6. Cleaning completes.
7. Player removes the cleaned bottle.
8. The bottle can be evaluated for reuse.

## 7.3 Cleaning Capacity

The machine comes in different capacity variants.

Each variant determines how many bottles can be cleaned simultaneously.

Higher-capacity machines can clean more bottles per cleaning cycle.

### Suggested Capacity Variants

- Single-Bottle Cleaner
- Two-Bottle Cleaner
- Four-Bottle Cleaner
- Eight-Bottle Industrial Cleaner

These are provisional capacity suggestions.

## 7.4 Bottle Reuse Requirements

Cleaning a bottle does not automatically guarantee that it can be reused.

A bottle must satisfy the established bottle-reuse requirements:

- The bottle must be empty.
- The bottle must be clean.
- Bottle strength must be greater than 70%.

Cleaning removes the dirty condition but does not automatically restore lost structural strength.

## 7.5 Equipment Properties

- Price: Depends on variant
- Portability: Depends on variant
- ProcessingCapacity: Number of bottles cleaned simultaneously
- CleaningDuration: Several seconds; exact duration not yet defined
- Energy Source: To be finalized
- RequiredVoltage: Applicable if electrically powered
- PowerConsumption: Applicable if electrically powered
- OperatingPressureRange: Applicable only if the cleaning mechanism requires pressure

## 7.6 Advantages

- Automates bottle cleaning.
- Improves bottle reuse efficiency.
- Higher-capacity variants reduce repetitive player work.

## 7.7 Disadvantages

- Requires dedicated shop space.
- Higher-capacity variants cost more.
- Cleaning does not repair damaged bottles.

---

# 8. Equipment Comparison

| Equipment | Cost | Portability | Players | Energy |
|---|---|---|---|---|
| Cap Gun | Expensive | Very High | 1 | Electricity |
| Manual Wheel Rotator | Cheapest | Extremely Low | 2 | Manual |
| Auto Fill and Cap | Most Expensive | Variant-Based | Variable | Electricity |
| Pressure Regulator | Variant-Based | Variant-Based | To Be Defined | Fuel/Electricity |
| Automatic Bottle Cleaner | Variant-Based | Variant-Based | To Be Defined | To Be Defined |

Exact prices, operating speeds, power ratings, pressure ranges, and equipment dimensions will be defined during implementation and balancing.

---

# 9. Equipment Failure Integration

Equipment failures must integrate with the existing bottle and pressure systems rather than introducing independent or conflicting bottle physics.

## 9.1 Cap Failure

Excessive tightening can cause cap failure.

Applicable Equipment:

- Cap Gun
- Automatic Fill and Cap Machine

## 9.2 Pressure-Related Failure

Overfilling or inappropriate operating pressure may result in bottle pressure exceeding its tolerance.

Applicable Equipment:

- Automatic Fill and Cap Machine
- Pressure Regulator, through connected systems

## 9.3 Manual Pressure Loss

If the Manual Wheel Rotator cannot maintain sufficient downward pressure before cap closure, cola begins escaping.

Applicable Equipment:

- Manual Wheel Rotator

## 9.4 Electrical Problems

Future shop electrical systems may introduce voltage drops and insufficient power availability.

All electrically powered equipment must use the shared electrical properties so voltage-related behavior can be handled consistently.

The specific consequences of insufficient voltage, such as reduced performance or machine shutdown, will be defined later.

---

# 10. Demo Equipment Scope — Confirmed

**The demo will include only one equipment type from this equipment catalog: the Manual Wheel Rotator.**

## 10.1 Included in Demo

### Manual Wheel Rotator

Required functionality:

- Two-player operation.
- One player repeatedly presses `G` to apply downward force to the cap.
- The downward force temporarily prevents pressurized cola from escaping.
- Holding effectiveness gradually decreases as time passes.
- The second player rotates the cap.
- The cap must be secured before the holding pressure becomes insufficient.
- Insufficient pressure holding can cause cola to escape.
- The equipment operates without electricity.

The machine must support the existing two-player co-op gameplay design.

## 10.2 Excluded from Demo

The following equipment will not be included in the demo:

1. Cap Gun.
2. Automatic Fill and Cap Machine.
3. Pressure Regulator.
4. Automatic Bottle Cleaner.

These are reserved for the full game or later development.

## 10.3 Demo Bottle Compatibility

The current prototype focuses on:

- Plastic bottle.
- Normal Cola.
- Normal Cap (Manual Twist).
- Bottle filling.
- Manual cap closure.
- Pressure-related bottle behavior.

The Manual Wheel Rotator must work with the existing bottle pressure mechanics, cap state, and failure events.

**Important:** The restriction to one demo equipment type applies to this equipment catalog. It does not remove the Normal Bulk Cola Container, hoses, dispenser, or essential filling infrastructure already planned for the demo.

---

# 11. Full-Game Expansion

The full game can introduce the remaining equipment and their variants.

Future equipment systems may include:

- Advanced cap-closing tools.
- Multiple automated filling nozzles.
- Fuel-powered and electric pressure regulation.
- Higher-capacity cleaning systems.
- Voltage-dependent machine behavior.
- Additional equipment categories not yet designed.

No additional equipment types are finalized at this stage.

---

# 12. Implementation Guidelines

1. Use a shared equipment architecture for common machine properties.

2. Every electrically powered machine must expose:
   - `RequiredVoltage`
   - `PowerConsumption`
   - `OperatingPressureRange`

3. Equipment without electrical requirements should not require an electrical connection.

4. Fuel-powered equipment must support fuel-consumption properties.

5. Pressure-sensitive equipment must use configurable minimum and maximum operating pressures.

6. Equipment variants should use configurable data rather than requiring completely separate gameplay logic for each variant.

7. Bottle and cap failure events should use the existing bottle physics, pressure, and containment-failure systems.

8. The Manual Wheel Rotator must support cooperative interaction between two players.

9. Large equipment should have appropriate transport and placement restrictions.

10. The demo implementation must remain focused on the Manual Wheel Rotator. Full-game equipment should not be implemented as a prerequisite for completing the demo.

---

# 13. Final Design Decisions

**Confirmed:**
- Five equipment categories are currently planned.
- All powered machines expose the three standardized electrical/pressure properties.
- The Cap Gun is expensive, highly portable, electrically powered, and can break caps through excessive tightening.
- The Manual Wheel Rotator requires two-player cooperation and no electricity.
- The Automatic Fill and Cap Machine combines filling and cap installation through connected grabbable nozzles.
- The Pressure Regulator supports pressure increase and decrease, with fuel and electric variants.
- The Automatic Bottle Cleaner processes bottles automatically and has different capacity variants.
- The demo includes only the Manual Wheel Rotator from this equipment catalog.

**Not Yet Finalized:**
- Final in-game equipment names.
- Exact equipment purchase prices.
- Voltage ratings and electrical consumption values.
- Operating pressure ranges.
- Number of nozzles per automatic filling machine variant.
- Automatic cleaner capacities and cleaning durations.
- Exact equipment dimensions and weights.
- Specific machine reactions to low voltage.
- Additional full-game equipment categories.

**Development Priority:** Complete the Manual Wheel Rotator's cooperative cap-closing gameplay for the demo before implementing the advanced equipment systems.
