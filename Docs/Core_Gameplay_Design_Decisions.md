# Cola Shop Game — Core Gameplay & Bottle System Design Decisions

This document defines the seven core design decisions for the first playable version of the game. These rules should be treated as the primary design requirements when implementing the bottle system, cola pricing, shop economy, NPC interactions, multiplayer architecture, and save system.

The game uses a multiplayer-ready ISM ↔ ASM architecture for physical products, including bottles and caps.

---

# 1. First Playable Scope

The first playable should demonstrate the complete cola shop business loop, from starting the business to preparing products, serving NPC customers, collecting payments, and reinvesting earnings.

## 1.1 Starting the Business

The player begins with a limited amount of money and must establish a cola shop.

The player can:

1. Take a business loan.
2. Pay advance rent for the shop.
3. Purchase the required equipment.
4. Purchase empty bottles.
5. Purchase compatible bottle caps.
6. Purchase large cola containers containing bulk cola.
7. Arrange equipment and shelves inside the shop.
8. Begin producing and selling bottled cola.

Loans, rent, equipment, bottles, caps, and bulk cola have separate purchase costs.

The player must manage business expenses and earnings to keep the shop operating.

## 1.2 Bottle Preparation

Players use purchased equipment to prepare cola bottles.

The preparation process:

1. Select an empty bottle.
2. Select a compatible cap.
3. Place the bottle under filling equipment.
4. Fill the bottle with cola from a large cola container.
5. Monitor Content Level, temperature, and pressure.
6. Stop filling at the desired Content Level.
7. Close or install the cap using the bottle's supported sealing mechanism.
8. Optionally chill the cola to improve its selling price.
9. Place the prepared bottle on a shop shelf.

Bottle filling must support fractional Content Level values.

The bottle's contents, temperature, pressure, age, expiry status, Strength, and cap state must be preserved during gameplay.

## 1.3 NPC Shopping

NPC customers enter the shop with shopping requirements.

NPC behavior:

1. Enter the shop.
2. Generate or retrieve a shopping checklist.
3. Search shelves for required cola products.
4. Pick up matching bottles.
5. Place selected bottles inside a shopping bucket.
6. Continue shopping until the checklist is complete or the NPC decides to stop.
7. Move to the checkout counter.
8. Wait for the player to complete checkout.
9. Pay for the purchased products.
10. Receive change when required.
11. Leave the shop.

NPCs may purchase multiple bottles during one visit.

The shopping bucket must support multiple physical products and retain their individual identities and product data.

## 1.4 Checkout System

The player operates the checkout counter.

Two checkout methods are available.

### Method A — Manual Calculator

The player manually enters the selling amount using the checkout calculator.

The player is responsible for entering the correct amount.

Incorrect entries may result in overcharging or undercharging customers.

### Method B — Product Scanner

The player scans each bottle.

The scanner retrieves the product's current information and calculates its selling price automatically.

The scanner must consider:

- Product Definition
- Cola type
- Price per portion
- Current Content Level
- Bottle portion capacity
- Temperature
- Optimum temperature
- Expiry status
- Applicable discounts or penalties

The scanner adds each product's calculated price to the checkout total.

### Cash Payment

After calculating the total:

1. NPC provides payment.
2. Player receives the money.
3. Checkout system determines the required change.
4. Player returns the correct change.
5. Transaction completes.
6. Purchased products are assigned to the customer.

The game should support cash-handling mistakes.

## 1.5 Reuse and Recycling

Players may recover eligible bottles for reuse or recycling.

Recycling returns a percentage of the bottle's original purchase price.

Caps cannot be recycled.

A reusable bottle must satisfy its material-specific requirements before being refilled.

## 1.6 First Playable Implementation Priority

The full gameplay loop is the target, but development should begin with:

- One plastic bottle.
- One normal plastic twist cap.
- One cola product.
- One bulk cola container.
- One filling machine.
- One shelf.
- One NPC customer.
- One shopping bucket.
- One checkout counter.
- Manual calculator checkout.
- Scanner checkout.
- Cash payment and change.
- Basic temperature and expiry handling.
- Basic ISM ↔ ASM conversion.
- Four-player multiplayer support.

Additional bottle materials, cap types, NPC types, and advanced containment events can be introduced after the core loop works.

---

# 2. Pricing System

The game uses a linear, portion-based pricing system.

The native quadratic payout formula is not the authoritative pricing model.

The selling price is determined by the quantity of cola inside the bottle and adjusted according to temperature and expiry status.

Bottle purchase cost, recycling refunds, and cola selling revenue are separate economic values.

## 2.1 Portion Definition

One portion equals exactly 90 ml of cola.

```text
1 Portion = 90 ml
```

Each bottle has a configurable NumberOfPortions representing its nominal capacity at Content Level 1.0.

For example:

```text
NumberOfPortions = 25
VolumePerPortion = 90 ml

Nominal Bottle Capacity = 2250 ml
```

The number of portions is not globally fixed at 4, 25, 135, or 200.

Different bottles may have different nominal capacities.

## 2.2 Content Level

Content Level is a continuous floating-point value.

```text
0.0 = Empty
0.5 = Half of nominal capacity
1.0 = Full nominal capacity
1.7 = Maximum configured overflow level
```

The standard maximum overflow Content Level is 1.7, subject to bottle condition and containment rules.

Content Level must preserve fractional quantities.

Do not repeatedly convert Content Level into integer portions during filling, drinking, leakage, or other interactions.

## 2.3 Portion Calculation

```text
CurrentPortions = ContentLevel × NumberOfPortions
```

Alternatively:

```text
ContentLevelPerPortion = 1 / NumberOfPortions

CurrentPortions =
    ContentLevel / ContentLevelPerPortion
```

Both calculations produce the same result.

Example:

```text
NumberOfPortions = 200
ContentLevelPerPortion = 0.005

ContentLevel = 0.7

CurrentPortions = 0.7 / 0.005
CurrentPortions = 140
```

## 2.4 Base Cola Price

```text
BaseColaPrice =
    CurrentPortions × PricePerPortion
```

Example:

```text
NumberOfPortions = 25
PricePerPortion = $4

ContentLevel = 1.0

CurrentPortions = 25
BaseColaPrice = $100
```

### Example Payouts

| Content Level | Portions | Base Selling Price |
|---|---:|---:|
| 0.0 | 0 | $0 |
| 0.25 | 6.25 | $25 |
| 0.5 | 12.5 | $50 |
| 0.7 | 17.5 | $70 |
| 1.0 | 25 | $100 |
| 1.3 | 32.5 | $130 |
| 1.7 | 42.5 | $170 |

These examples use a 25-portion bottle and $4 per portion.

The prices above are before temperature penalties.

The bottle's purchase cost and recycling refund are not automatically added to the cola selling price.

## 2.5 Temperature-Based Price Adjustment

Each cola Product Definition contains an OptimumTemperature.

A product receives its full base selling price when its temperature matches the optimum temperature.

As the temperature moves away from the optimum, its selling price decreases.

The maximum temperature-related discount is 20%.

Required variables:

```text
OptimumTemperature
CurrentTemperature
TemperaturePenaltyRange
MaximumTemperatureDiscount = 0.20
```

Calculate normalized temperature deviation:

```text
TemperatureDeviation =
    Abs(CurrentTemperature - OptimumTemperature)

NormalizedDeviation =
    Clamp(
        TemperatureDeviation / TemperaturePenaltyRange,
        0.0,
        1.0
    )
```

Calculate the discount:

```text
TemperatureDiscount =
    NormalizedDeviation × 0.20
```

Calculate final price:

```text
TemperatureAdjustedPrice =
    BaseColaPrice × (1.0 - TemperatureDiscount)
```

### Example

A bottle contains 25 portions at $4 per portion.

```text
BaseColaPrice = $100
```

| Temperature Deviation | Discount | Final Price |
|---|---:|---:|
| 0% | 0% | $100 |
| 10% | 2% | $98 |
| 25% | 5% | $95 |
| 50% | 10% | $90 |
| 75% | 15% | $85 |
| 100% | 20% | $80 |

TemperaturePenaltyRange is configurable per cola product.

Temperature deviation should be calculated using actual temperature values rather than interpreting Celsius values directly as percentages.

## 2.6 Expired Cola Penalty

Selling expired cola triggers a customer compensation penalty.

The player must compensate the customer through one of two methods:

1. Provide a free, equivalent, non-expired cola.
2. Provide a discount or credit equal to the expired cola penalty amount.

The penalty is calculated using a minimum Content Level of 1.0.

If the expired bottle contains less than 1.0 Content Level, calculate the penalty as though it contains 1.0.

If the expired bottle contains more than 1.0 Content Level, use its actual Content Level.

```text
PenaltyContentLevel =
    Max(1.0, ExpiredBottleContentLevel)
```

```text
ExpiredColaPenalty =
    PenaltyContentLevel
    × NumberOfPortions
    × PricePerPortion
```

The penalty is calculated before temperature discounts.

### Example

```text
NumberOfPortions = 25
PricePerPortion = $4
```

| Expired Content Level | Penalty Calculation Level | Penalty |
|---|---:|---:|
| 0.2 | 1.0 | $100 |
| 0.5 | 1.0 | $100 |
| 0.7 | 1.0 | $100 |
| 1.0 | 1.0 | $100 |
| 1.3 | 1.3 | $130 |
| 1.7 | 1.7 | $170 |

An expired cola sale generates no valid cola revenue.

Any payment collected for the expired product must be refunded or voided.

The replacement or discount penalty is additional compensation and must be applied once per affected product.

The exact cash-flow implementation must prevent accidentally charging the same penalty multiple times.

## 2.7 Expiry Detection Before Sale

If the player discovers that cola has expired before selling it, the player can remove it from inventory.

The player may:

- Discard the expired contents.
- Recycle the bottle if its material supports recycling.
- Recover the eligible recycling value.
- Clean and reuse the bottle if its material and condition permit reuse.

Expired contents and bottle condition must be tracked separately.

Expired cola does not automatically make the physical bottle unusable.

---

# 3. Units and Authoritative Tuning

## 3.1 Authoritative Units

| Property | Unit |
|---|---|
| Content Level | Normalized float |
| Portion volume | 90 ml |
| Nominal bottle capacity | Configurable portion count |
| Cola price | Currency per portion |
| Temperature | Celsius |
| Pressure | Normalized gameplay pressure |
| Strength | Normalized 0–1 |
| Radiation absorption | Percentage or normalized 0–1 |
| Recycling refund | Percentage of original bottle purchase price |
| Expiry | Game timestamp |
| Pressure tolerance | Normalized gameplay pressure |
| Temperature discount | Maximum 20% |

Normalized pressure is the authoritative pressure representation.

PSI is not used as the authoritative simulation unit.

Pressure values may exceed 1.0 if required by the simulation.

Each bottle material defines its own pressure tolerance.

## 3.2 Initial Bottle Material Definitions

The game contains five bottle materials:

1. Plastic
2. Glass
3. Metal Can
4. Diamond
5. Concrete

### Plastic Bottle

- Cheapest bottle material.
- Low pressure containment.
- Low melting point.
- Zero radiation absorption.
- Does not shatter.
- Bends and deforms when damaged.
- Reduced Strength lowers effective content capacity.
- Reduced Strength lowers pressure tolerance.
- Recycling returns up to 40% of purchase cost.
- Uses rotating twist caps.

Compatible caps:

- Normal Cap
- Auto Close Cap
- Pressure Discharge Cap

### Glass Bottle

- More expensive than plastic.
- Good pressure containment.
- Medium melting point.
- Zero radiation absorption.
- Can crack or shatter.
- Broken glass creates physical fragments.
- Low recycling refund.
- Uses a wooden stopper.
- Player seals the bottle by hammering the stopper into the opening.

### Metal Can

- More expensive than glass.
- Good pressure containment.
- High melting point.
- Low radiation absorption.
- Highly resistant to bending.
- Does not normally shatter.
- Can permanently deform under sufficient damage.
- Reduced Strength lowers content capacity and pressure tolerance.
- Recycling returns 20% of purchase cost.
- Uses a welded metal lid.

### Diamond Bottle

- Most expensive bottle material.
- Extremely high pressure containment.
- Does not break.
- Does not bend.
- Does not melt under gameplay conditions.
- Medium radiation absorption.
- Cannot be recycled.
- Uses a rotating twist cap.

### Concrete Bottle

- More expensive than metal cans.
- Cheaper than diamond.
- Extremely high radiation absorption.
- Specifically designed for radioactive contents.
- High fracture resistance.
- Breaks when Strength reaches zero.
- Does not melt under gameplay conditions.
- Cannot be recycled.
- Sealed by pouring concrete into the opening.

## 3.3 Initial Balance Table

The following values are provisional gameplay tuning values, not final production balance.

| Material | Example Cost | Relative Pressure Resistance | Radiation Absorption | Recycling |
|---|---:|---:|---:|---:|
| Plastic | $10 | 25% | 0% | 40% |
| Glass | $25 | 60% | 0% | 10% |
| Metal Can | $40 | 70% | 15% | 20% |
| Concrete | $65 | 85% | 95% | 0% |
| Diamond | $150 | 100% | 50% | 0% |

Relative pressure resistance is a material comparison score, not necessarily the actual normalized pressure tolerance.

Actual pressure thresholds must be configured in each bottle Product Definition.

Temperature tolerances, melting thresholds, and optimum cola temperatures also remain configurable.

## 3.4 Cap Types

### Normal Cap

- Plastic twist cap.
- Manually installed.
- No automatic closing.
- No pressure relief.

### Auto Close Cap

- Plastic twist cap.
- Automatically closes when Content Level reaches the configured threshold.
- Default threshold is Content Level 1.0.
- Does not automatically remove accumulated pressure.

### Pressure Discharge Cap

- Plastic twist cap.
- Supports automatic closing.
- Contains a pressure relief mechanism.
- Releases pressure when the configured threshold is reached.
- Has limited pressure discharge capacity.
- Cannot prevent bursting when pressure generation exceeds discharge capacity.

Other closures:

| Bottle | Closure | Mechanism |
|---|---|---|
| Glass | Wooden stopper | Hammer installation |
| Metal Can | Metal lid | Welding |
| Diamond | Diamond cap | Rotating twist |
| Concrete | Concrete plug | Pouring and curing |

Caps are purchased separately.

Caps cannot be recycled.

Each bottle must validate compatible cap types before installation.

Caps use the same ISM ↔ ASM architecture as bottles.

Plastic cap variants may share the same mesh while using different material textures or Per Instance Custom Data for their appearance.

Functional differences must remain in the cap's gameplay data, not solely in its material.

---

# 4. Open Bottle Behavior

An uncapped bottle containing cola must remain an active ASM.

It cannot return to resting ISM representation while it contains liquid and remains uncapped.

## 4.1 Upright Bottle

When an uncapped bottle is upright:

- Keep it as ASM.
- Preserve its Content Level.
- Preserve temperature and expiry timestamps.
- Allow pressure exchange with the surrounding atmosphere.
- Do not automatically spill liquid merely because the cap is missing.
- Continue monitoring gameplay interactions that may cause leakage or eruption.

## 4.2 Sideways or Tilted Bottle

When an uncapped bottle tilts sufficiently:

- Keep it as ASM.
- Determine whether the opening permits liquid to escape.
- Calculate liquid loss using the configured leakage or pouring behavior.
- Reduce Content Level according to the escaped quantity.
- Trigger appropriate Niagara liquid effects.
- Preserve the remaining fractional content.

Spilling depends on orientation, liquid level, opening geometry, and relevant gameplay parameters.

## 4.3 Held Bottle

When a player or NPC holds an uncapped bottle:

- Keep it as ASM.
- Preserve its identity and runtime data.
- Allow drinking, pouring, filling, and other supported interactions.
- Evaluate orientation-based liquid loss.
- Apply appropriate pressure and temperature behavior.

## 4.4 Empty Uncapped Bottle

An empty, stationary bottle may return to ISM when it satisfies the normal resting-state conditions.

An empty bottle does not require a cap solely to become ISM.

## 4.5 Eruption and Pressure Release

An uncapped bottle does not automatically require an eruption timer.

Eruption occurs only when the relevant liquid or pressure system determines that an eruption condition exists.

Removing a cap may release pressure.

Pressure release must follow the bottle's configured pressure-release behavior rather than always setting pressure to zero.

An uncapped bottle generally cannot maintain a sealed internal pressure differential.

---

# 5. Bottle Damage and Structural Strength

Bottle damage is controlled by the existing Strength variable.

Strength represents the bottle's remaining structural integrity.

```text
Strength = 1.0
Fully intact bottle

Strength = 0.5
50% remaining structural integrity

Strength <= 0.0
Material-specific structural failure
```

Strength is persistent.

Reducing pressure or temperature must not automatically restore lost Strength.

## 5.1 Glass and Concrete Failure

Glass and concrete bottles break when Strength reaches zero or below.

```text
If Strength <= 0:

    Trigger Bottle Break Event
```

For glass:

- Break the bottle.
- Spawn glass fragments.
- Trigger glass break effects.
- Handle remaining contents as spilled or released.
- Remove its ability to contain cola.

For concrete:

- Break the concrete container.
- Spawn appropriate fragments.
- Trigger containment failure effects.
- Release or spill remaining contents according to the contained product.

## 5.2 Plastic and Metal Can Failure

Plastic bottles and metal cans do not automatically shatter when Strength reaches zero.

Instead, they deform.

Strength directly controls remaining content capacity and pressure tolerance.

```text
NormalizedStrength =
    Clamp(Strength, 0.0, 1.0)
```

```text
EffectiveMaxContentLevel =
    OriginalMaxContentLevel
    × NormalizedStrength
```

```text
EffectiveMaxPressureTolerance =
    OriginalMaxPressureTolerance
    × NormalizedStrength
```

This includes overflow capacity.

### Capacity Example

Assume:

```text
OriginalMaxContentLevel = 1.7
```

| Strength | Effective Maximum Content Level |
|---|---:|
| 1.0 | 1.70 |
| 0.8 | 1.36 |
| 0.6 | 1.02 |
| 0.5 | 0.85 |
| 0.3 | 0.51 |
| 0.1 | 0.17 |
| 0.0 | 0.00 |

At Strength 0, the plastic bottle or metal can has no usable content capacity.

The same Strength multiplier applies to its pressure tolerance.

If current contents exceed the reduced capacity, the containment system must resolve the excess.

Excess contents must not disappear silently.

They may leak, spill, or trigger another appropriate containment failure.

## 5.3 Diamond Bottle

Diamond bottles are indestructible under normal gameplay rules.

- No breaking.
- No bending.
- No melting.
- No structural damage from ordinary gameplay interactions.

Their pressure containment and cap systems remain subject to their own configured rules.

## 5.4 Damage Sources

Bottle Strength may decrease because of:

- Physical impacts.
- Excessive pressure.
- Extreme temperatures.
- Material-specific containment stress.
- Improper sealing interactions.
- Other gameplay damage events.

Each material determines how it reacts to these sources.

## 5.5 Damage Examples

### Example A — Glass Bottle Falls

A glass bottle falls from a shelf.

The collision produces sufficient impact damage to reduce Strength to zero.

Expected result:

- Bottle shatters.
- Glass fragments appear.
- Contents spill.
- Bottle becomes unusable.

### Example B — Plastic Bottle Is Crushed

A plastic bottle loses Strength from 1.0 to 0.5.

Expected result:

- Bottle visibly bends.
- Maximum content capacity decreases to 50%.
- Pressure tolerance decreases to 50%.
- Existing contents are checked against the new capacity.
- Permanent deformation remains after the impact.

### Example C — Metal Can Is Damaged

A metal can loses Strength from 1.0 to 0.25.

Expected result:

- Can develops visible dents.
- Effective capacity becomes 25% of original.
- Effective pressure tolerance becomes 25% of original.
- Excess contents may leak or spill.
- Damage remains persistent.

### Example D — Concrete Bottle Breaks

A concrete bottle receives enough damage to reduce Strength to zero.

Expected result:

- Concrete fractures.
- Fragments appear.
- Containment is lost.
- Any radioactive contents are handled by the appropriate hazard system.

### Example E — Pressure Exceeds Tolerance

A sealed bottle's CurrentPressure exceeds its effective MaxPressureTolerance.

Expected result:

- Trigger pressure-related containment failure.
- Apply the configured damage or failure response.
- Reduce Strength where appropriate.
- Apply material-specific deformation, cracking, bursting, or leakage.

The exact event depends on bottle material and containment configuration.

### Example F — Thermal Failure

A bottle exceeds its configured temperature tolerance.

Expected result:

- Trigger Thermal Failure evaluation.
- Apply material-specific effects.
- Plastic may soften or deform.
- Glass may fracture under appropriate thermal stress.
- Metal may deform or lose containment if its limits are exceeded.
- Concrete follows its configured thermal damage rules.
- Diamond remains resistant under normal gameplay rules.

Thermal failure does not automatically mean every bottle instantly breaks.

## 5.6 Temporary and Permanent Deformation

Temporary deformation includes:

- Pressure inflation.
- Temporary bulging.
- Temporary expansion.
- Reversible stress effects.

Permanent deformation includes:

- Dents.
- Bends.
- Cracks.
- Structural damage.
- Lost Strength.

Temporary effects may recover when pressure decreases.

Permanent damage must not automatically recover.

## 5.7 Pressure Impulse Protection

A single collision or sudden stop must not apply multiple pressure impulses because of repeated physics callbacks.

Use an event identifier, deduplication mechanism, or configurable cooldown to prevent duplicate damage events.

## 5.8 Launch Behavior

A bottle may launch or recoil when rapid pressure release produces sufficient force.

Launching should depend on:

- Pressure differential.
- Release rate.
- Bottle mass.
- Release direction.
- Cap or opening configuration.
- Relevant physical forces.

Not every pressure release should launch the bottle.

---

# 6. Multiplayer Target

The initial multiplayer target is a four-player listen server.

```text
Network Mode:
Listen Server

Maximum Players:
4

Authority:
Server Authoritative
```

The first playable must support four players interacting with the same shop.

## 6.1 Server Responsibilities

The server is authoritative for:

- Player money.
- Loans and rent.
- Equipment purchases.
- Bottle purchases.
- Cap purchases.
- Inventory.
- Product ownership.
- Product Definition references.
- Bottle and cap identities.
- Filling transactions.
- Content Level.
- Pressure.
- Temperature state.
- Strength.
- Expiry.
- Sealing state.
- Recycling payouts.
- NPC shopping decisions.
- Checkout totals.
- Customer payments.
- Change.
- Penalties.
- Persistent game state.

Clients may predict or display interactions locally, but final gameplay state must be validated by the server.

## 6.2 ISM ↔ ASM Architecture

Bottles and caps use the existing ISM ↔ ASM conversion architecture.

When an ISM becomes active:

```text
ISM at gameplay position
    ↓
Move original ISM instance to Reset Position
    ↓
Activate or create pooled ASM
    ↓
Copy required runtime information
    ↓
ASM handles active interaction
```

The original ISM instance is not removed.

When ASM returns to rest:

```text
ASM at gameplay position
    ↓
Save updated runtime information
    ↓
Update original ISM instance
    ↓
Move original ISM to gameplay position
    ↓
Return ASM to Reset Position
    ↓
Disable ASM collision, physics,
replication and unnecessary ticking
    ↓
Return ASM to pool
```

The same logical instance identity is preserved during representation conversion.

Bottle and cap identities remain separate.

A cap may be attached to only one bottle at a time.

## 6.3 Replication Strategy

Replicate gameplay-relevant state changes.

Avoid continuously replicating every visual material parameter.

Use Per Instance Custom Data and local material updates for visual effects such as:

- Content Level.
- Temperature appearance.
- Frost.
- Age.
- Crack visibility.
- Deformation appearance.
- Radiation effects.

The server remains authoritative for the underlying gameplay data.

Clients reconstruct visual effects from replicated state.

## 6.4 Initial Multiplayer Test

The first multiplayer validation must use four connected players.

Test scenarios include:

1. Multiple players filling bottles.
2. Multiple players interacting with the same bottle.
3. One player installing a cap while another interacts with the bottle.
4. Bottle ownership and interaction conflicts.
5. NPC shopping while players stock shelves.
6. Multiple checkout interactions.
7. Bottle damage replication.
8. Bottle ISM ↔ ASM conversion.
9. Cap ISM ↔ ASM conversion.
10. Correct server-authoritative money and inventory updates.

Prevent duplicate purchases, duplicate checkout payments, and duplicate recycling rewards.

---

# 7. Save Compatibility

The game will use a new versioned save schema.

Existing save compatibility is not required.

No migration from older save structures is required.

## 7.1 Save Versioning

Every save must contain a schema version.

Example:

```text
SaveSchemaVersion = 1
```

Future save changes can introduce additional versions.

## 7.2 Save Data

The save system must preserve relevant persistent gameplay information.

### Shop Data

- Shop ownership and progression.
- Current money.
- Outstanding loan.
- Rent obligations.
- Purchased equipment.
- Equipment placement.
- Shop inventory.
- Shelf inventory.
- Business progression.

### Bottle Data

- Product Definition ID.
- Stable bottle instance identity.
- Transform.
- Content Level.
- Cola product identity.
- Current temperature or thermal state.
- Creation timestamp.
- Last updated timestamp.
- Expiry information.
- Strength.
- Permanent deformation state.
- Cap attachment information.
- Other persistent containment state.

### Cap Data

- Cap Definition ID.
- Stable cap instance identity.
- Cap type.
- Current attachment state.
- Associated bottle identity, if attached.
- Seal integrity.
- Relevant persistent damage or valve state.

### NPC and Checkout Data

Persist only the NPC and checkout information required by the game's chosen save and resume behavior.

Avoid saving temporary effects that can be reconstructed from authoritative state.

## 7.3 Runtime Reconstruction

When loading a save:

1. Load the save schema version.
2. Restore shop and economy data.
3. Restore Product Definition references.
4. Restore bottle and cap identities.
5. Restore inventory and world placement.
6. Restore persistent bottle and cap runtime data.
7. Reconstruct appropriate ISM or ASM representations.
8. Restore attachments and ownership relationships.
9. Resume gameplay systems using restored timestamps.

Do not serialize transient pooled ASM references as permanent object identities.

---

# 8. Final Design Decisions Summary

| Decision | Final Choice |
|---|---|
| First playable | Complete shop business loop |
| Starting economy | Loan, advance rent, equipment and inventory purchases |
| Bottle preparation | Purchase, fill, seal, chill, stock |
| NPC shopping | Shopping checklist, bucket, checkout |
| Checkout | Manual calculator or product scanner |
| Payment | Receive cash and return change |
| Pricing model | Linear portion-based pricing |
| Portion volume | 90 ml |
| Portion capacity | Configurable per bottle |
| Content Level | Continuous normalized float |
| Standard overflow limit | 1.7 before condition-based restrictions |
| Temperature pricing | Up to 20% discount |
| Expired cola | Free replacement or equivalent discount/credit |
| Expiry penalty quantity | Maximum of 1.0 and actual Content Level |
| Pressure | Normalized authoritative gameplay units |
| Strength | Normalized 0–1 |
| Plastic damage | Deformation and proportional capacity loss |
| Metal can damage | Deformation and proportional capacity loss |
| Glass damage | Breaks at Strength ≤ 0 |
| Concrete damage | Breaks at Strength ≤ 0 |
| Diamond damage | Indestructible under normal gameplay |
| Open filled bottle | Remains ASM until sealed or emptied |
| Bottle representation | ISM ↔ ASM |
| Cap representation | ISM ↔ ASM |
| Multiplayer | Four-player listen server |
| Authority | Server authoritative |
| Save system | New versioned schema |
| Old save migration | Not required |

# 9. Implementation Notes

These design decisions establish the intended gameplay behavior and architecture.

The following values remain configurable rather than hardcoded:

- Individual bottle pressure tolerances.
- Exact damage multipliers.
- Thermal failure thresholds.
- Optimum temperatures.
- Temperature penalty ranges.
- Cap discharge rates.
- Seal integrity thresholds.
- Eruption conditions.
- Launch force calculations.
- NPC purchase preferences.
- Loan terms and rent amounts.
- Equipment purchase prices.
- Exact recycling and reuse conditions.

The first playable should prioritize a working end-to-end business loop, correct bottle state preservation, accurate pricing, server-authoritative interactions, and reliable ISM ↔ ASM transitions.

Advanced effects and additional bottle materials should build upon this foundation without requiring the core architecture to be redesigned.