# Colossal Cola Co. — Bottle Contents Catalog (Working Reference)

**Recorded:** 2026-11-10  
**Updated:** 2026-11-11 — Zero Calories Cola price confirmed equal to Spicy Cola.  
**Status:** Working content taxonomy; names and final balance remain subject to approval.  
**Demo scope:** Only the five Normal Colas.  
**Authority:** Designer-provided type names and relative price constraints. No numeric content prices, physical properties or type-specific effects are approved by this catalog.

This is the GDD's current bottle-fill taxonomy. Older formula names, numeric balance tables and named hazard effects in the compendiums are historical proposals where they differ from this record. Existing generic bottle, scanner, temperature, expiry and cash rules remain governed by their separately approved specifications.

## 1. Normal Colas — Demo and Full Game

| Local price order | Cola type | Relationship |
| --- | --- | --- |
| 1 | Lemonade Cola | Cheapest Normal Cola |
| 2 | Mint Cola | More expensive than Lemonade |
| 3 | Sweet Cola | More expensive than Mint |
| 4, tied | Spicy Cola | Joint most expensive Normal Cola |
| 4, tied | Zero Calories Cola | Same price as Spicy; joint most expensive Normal Cola |

## 2. Toxic Colas — Future Scope

| Local price order | Cola type | Relationship |
| --- | --- | --- |
| 1 | Acidic Cola | Cheapest Toxic Cola; more expensive than Spicy |
| 2 | Flammable Cola | More expensive than Acidic |
| 3 | Biohazard Cola | More expensive than Flammable |
| 4 | Heavy Metal Cola | More expensive than Biohazard; most expensive Toxic Cola |

## 3. Black Magic Colas — Future Scope

| Local price order | Cola type | Relationship |
| --- | --- | --- |
| 1 | Eyeball Cola | Cheapest Black Magic Cola; more expensive than Heavy Metal |
| 2 | Spider Cola | More expensive than Eyeball |
| 3 | Blood Cola | More expensive than Spider |
| 4 | Souls Cola | More expensive than Blood; most expensive Black Magic Cola |

## 4. Radioactive Colas — Future Scope

| Local price order | Cola type | Relationship |
| --- | --- | --- |
| 1 | Microwave Cola | Cheapest Radioactive Cola; more expensive than Souls |
| 2 | Infrared Cola | More expensive than Microwave |
| 3 | X-Ray Cola | More expensive than Infrared |
| 4 | Metallic Gamma Cola | More expensive than X-Ray; most expensive Radioactive Cola |

## 5. Explosive Colas — Future Scope

| Local price order | Cola type | Relationship |
| --- | --- | --- |
| 1 | Gun Powder Cola | Cheapest Explosive Cola; more expensive than Heavy Metal and cheaper than Eyeball |
| 2 | LPG Cola | More expensive than Gun Powder and cheaper than Blood |
| 3 | Oxygen Cola | More expensive than Blood and cheaper than Souls |
| 4 | Nitrogen Cola | More expensive than Oxygen and cheaper than Souls; most expensive Explosive Cola |

## 6. Cross-Category Price Constraints

`<` means less expensive than; `=` means equal price under the same capacity and pricing conditions.

**Main ascending chain:**

`Lemonade < Mint < Sweet < Spicy = Zero Calories < Acidic < Flammable < Biohazard < Heavy Metal < Eyeball < Spider < Blood < Souls < Microwave < Infrared < X-Ray < Metallic Gamma`

**Explosive constraints:**

`Heavy Metal < Gun Powder < Eyeball`

`Gun Powder < LPG < Blood < Oxygen < Nitrogen < Souls`

Explosive Colas overlap the Black Magic price range. They do not form a separate tier entirely above or below Black Magic. LPG's position relative to Eyeball and Spider, and any other pair not connected by these constraints, remains undefined. Do not invent a total ordering or numeric price gaps for such pairs.

The Spicy/Zero Calories tie applies to their configured base prices at matching nominal capacity. Existing content/temperature discounts and whole-bill rounding still apply normally; differently filled or differently conditioned bottles can produce different final quotes.

## 7. Content Count and Prototype Boundary

| Category | Count | Demo enabled |
| --- | --- | --- |
| Normal | 5 | Yes |
| Toxic | 4 | No |
| Black Magic | 4 | No |
| Radioactive | 4 | No |
| Explosive | 4 | No |
| **Total** | **21** | **5 Normal types only** |

**Subsequent prototype restriction:** the [Plastic Bottle Pressure System](Prototype_Plastic_Bottle_Pressure_2026-11-11.md) uses plastic bottles and Normal manual twist caps exclusively, retaining all five Normal flavors. Other bottle materials and cap types are excluded. Hits/shaking/drops/impacts raise internal pressure; exceeding bottle tolerance causes orientation/force-dependent physics rocket propulsion from escaping cola. This shared bottle behavior does not approve advanced categories or separate flavor-specific hazards. Current player/NPC/bottle sizes remain unchanged.

The demo uses the **Normal Bulk Cola Container**, the one pre-mixed bulk source type for the five Normal flavors, and the existing buy/fill/cap/stock/shop/checkout/reinvest loop. Its valve discharges hot cola under very high pressure; filled bottles need cooling. It must not break from its own internal pressure and needs no electricity or progressive container heating in the demo. The path is container → hose/dispenser → filling equipment → bottle(s). Exact capacity, output temperature, flow, pressure and equipment details remain open; see [the chat decisions and conflict register](Chat_Design_Decisions_2026-11-11.md). This shared production requirement does not approve distinct flavor-specific hazards or effects.

Raw-ingredient mixing, additional bulk-container types and electrical/generator systems belong to later full-game design. Recording future types here does not authorize implementing their mechanics in the Normal-only demo. Existing Cylinder/Tank asset identifiers can remain for compatibility. Preserve current player, NPC and bottle sizes from `Lvl_FirstPerson`.

## 8. Properties and Effects — To Design Later

The following fields are future design work, not approved traits of any named type:

- Appearance: liquid color, opacity, particles, animation and bottle/label distinctions.
- Physical behavior: fizz, viscosity, leaks, spills and volatility.
- Handling/hazards: storage, transport, breakage and spill outcomes.
- Gameplay effects: effects on players, NPCs/enemies and the environment.
- Mechanics: duration, intensity, stacking, countermeasures and combinations.
- Economy: numeric purchase/sale prices, rarity and crafting/processing costs.

Existing numeric prototype settings are provisional configuration, not approved final content balance. Previously approved prototype supply costs are a separate decision; this catalog does not change them. A type's name alone does not approve fire, acid damage, radiation, explosions, summoned creatures or other effects.

## 9. Naming Notes and Existing Asset Compatibility

Editorial normalization from the designer's discussion:

- Eye Boll → Eyeball Cola.
- heavy Matel → Heavy Metal Cola.
- soles → Souls Cola, with the intended meaning to be confirmed when finalizing labels.
- Gun powder → Gun Powder Cola.

Use **Zero Calories Cola** as the working display name. Existing internal identifiers such as `ZeroCalorie` and `DA_NormalZeroCalorie` can remain for asset/save compatibility; they refer to the same Normal type. These notes do not approve final branding or art.
