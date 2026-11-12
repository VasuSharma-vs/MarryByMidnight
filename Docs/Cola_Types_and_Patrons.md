# Cola Types, Supernatural Hazards & Customer Compendium

**Project:** Midnight Fizz: Giant Thirst (*MarryByMidnight*)  
**Document Purpose:** Dedicated specification for standard colas, supernatural/hazardous beverages, failure mechanics, sanitation tools, and fantasy customer archetypes.

**Content authority — 11 November 2026:** Use [the current 21-type Bottle Contents Catalog](Bottle_Contents_Catalog.md). The demo contains only the five Normal Colas, with `Lemonade < Mint < Sweet < Spicy = Zero Calories`. This document's older formula names, numeric prices and type-specific effect descriptions are historical proposals, not approvals or additional demo requirements. Generic mechanics with separate approval remain governed by their approved specifications.

**Scale/mass authority — 11 November 2026:** Preserve the current NPC, player and Cola setup in `Lvl_FirstPerson`. The player keeps the existing default size for mechanic compatibility and feels small because the world is giant-scale. NPCs and movable actors remain lighter than the player. [GDD Master Section 3](GDD_Master.md#3-the-scale-twist-tiny-people-giant-world) supersedes the previous 25 cm player and fixed archetype/player multipliers; future patron variants require separate art authoring, not automatic character rescaling.

---

## 1. Architectural Rules for Cola Volatility

Every cola formula in the game inherits from `UInteractablePropDataAsset` and tracks dynamic physics and chemical state via `AActivePropActor`.

```
                        [ COLA BOTTLE DISASTER TRIGGERS ]
                                        |
       +--------------------+-----------+-----------+--------------------+
       |                    |                       |                    |
       v                    v                       v                    v
  [ TIMER EXPIRED ]    [ BAD TORQUE ]          [ HARD IMPACT ]     [ HIGH AGITATION ]
   Uncapped > 5.0s     Under / Over Torqued    Dropped / Crashed   Shaken (A >= 90)
```

### 1.1 The Four Failure Triggers
Any cola bottle triggers its catastrophic chemical reaction if:
1. **Uncapped Delay Expiration ($T_{\text{react}}$):** Left uncapped after dispensing past its reaction limit ($2.5\text{s} - 6.0\text{s}$).
2. **Improper Capping:** Released in the **Under-Torqued (Loose)** blue zone (blowout) or **Over-Torqued (Tight)** red zone (neck fracture).
3. **Severe Physical Impact:** Dropped from countertops, thrown against solid objects, or crashed by a shop vehicle.
4. **Agitation Saturation ($A \ge 90.0$):** Carried while sprinting, shaken violently by players, or transported across bumpy terrain without magnetic clamps.

---

## 2. Standard & Flavored Colas (Commercial Lineup)

These represent the everyday beverages ordered by common folk, travelers, and lower-tier monsters.

```
 [ Classic Caramel ]     [ Sugar Crush ]     [ Dragon Pepper Fire ]     [ Frostbite Mint ]
```

### 2.1 Historical Formula Proposals — Not Approved Content Balance or Effects
| Cola Name | Asset ID | Base Price | Agitation Multiplier | Ideal Temp | Failure Reaction & Shop Consequence |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Classic Midnight Fizz** | `DA_BasicCola` | $\$2.50$ | $1.0\times$ | $4.0^\circ\text{C}$ | **High-Velocity Rocket:** Cap shoots off; bottle launches into unguided ballistic flight, ricocheting off walls and knocking down nearby bottles. |
| **Sugar Crush Cherry** | `DA_SweetCola` | $\$3.25$ | $1.8\times$ | $3.0^\circ\text{C}$ | **Sticky Foam Explosion:** Explodes into a syrupy geyser that coats the floor in pink syrup. Player walking speed is halved ($50\%$ slowdown) until mopped. |
| **Dragon Pepper Fire Cola** | `DA_SpicyCola` / `BP_FireCola` | $\$4.50$ | $2.2\times$ | $8.0^\circ\text{C}$ | **Flaming Jet Detonation:** Launches with fiery exhaust trails, leaving scorched fire patches on counters that burn wooden crates. |
| **Frostbite Mint / Cryo** | `DA_CryoCola` | $\$4.75$ | $1.3\times$ | $-2.0^\circ\text{C}$ | **Flash Freeze Shockwave:** Shatters in a sub-zero blast; instantly turns nearby puddles into slippery ice and temporarily encases players in ice blocks for 3 seconds. |
| **Royal Golden Sparkler** | `DA_LuxuryCola`| $\$7.50$ | $0.8\times$ | $2.0^\circ\text{C}$ | **Blinding Flash:** Emits a glittering gold flashbang that whitewashes player screens for 4 seconds. Very expensive; loss hurts team quota! |

---

## 3. Hazardous & Supernatural Colas

These volatile mixtures cater to dark creatures, undead, and magical entities. Each demands strict protective protocols and specialized workshop clean-up tools.

```
 [ Poison Swamp ]       [ Spider Nest ]       [ Radioactive Neon ]       [ Phantom Ecto ]
  Emerald Poison Gas     Skittering Swarm       Ionizing Contagion        Sensory Hallucination
```

---

### 3.1 Poison Swamp Cola (`DA_ToxicCola`)
*A bubbling, virulent emerald soda brewed from stagnant marsh waters and viper venom.*

```
 [ Bottle Ruptures ] ===> [ Thick Green Smog Cloud (4m Radius) ]
                                |
                 +--------------+--------------+
                 v                             v
       [ Damage & Coughing ]         [ Tools Dropped to Floor ]
```

* **Target Clientele:** Bog Witches, Swamp Trolls, Necromancers.
* **Failure Disaster (Poison Smog Cloud):**
  - Upon detonation, releases an expanding dense green gas cloud ($4\text{m radius}$) that lingers for **20 seconds**.
  - Any miniature player inside the cloud suffers continuous HP damage, aggressive coughing fits, and is forced to drop whatever item or tool they are carrying.
* **Player Countermeasure & Clean-Up:**
  1. **Gas Masks:** Players must run to the Safety Rack and equip Gas Masks (`E` to equip) to become immune to the fumes.
  2. **Air-Scrubbing Fans:** Players must activate the wall-mounted exhaust blowers or carry portable ventilation fans to disperse the poison smog.

---

### 3.2 Spider Nest Cola (`DA_SpiderCola`)
*Infused with dormant arachnid eggs steeped in dark carbonated syrup. Highly unstable under pressure.*

```
 [ Bottle Bursts ] ===> [ 15 - 30 Skittering Spiders Spawn ]
                                |
                 +--------------+--------------+
                 v                             v
      [ Steal Loose Caps ]           [ Web Up Vehicle Wheels ]
```

* **Target Clientele:** Arachne Queens, Goblin Swarms, Cave Dwellers.
* **Failure Disaster (Arachnid Swarm):**
  - The bottle bursts open, scattering **15 to 30 tiny spiders** that scurry rapidly across the countertops and floors.
  - **Spider Mischief:**
    - Spiders pick up loose bottle caps from benches and run away with them.
    - Spiders spin sticky webs across traffic lanes, tangling micro-vehicle tires and cutting vehicle acceleration by $70\%$.
    - Spiders bite player heels, causing miniature players to trip and ragdoll.
* **Player Countermeasure & Clean-Up:**
  1. **Bug Swatters & Brooms:** Players equip swatters to squash spiders with primary click (`LMB`).
  2. **Bug Repellent Sprayer:** Spraying workstations clears webs and kills groups of spiders in an area of effect.

---

### 3.3 Radioactive Neon-Glow Cola (`DA_RadioactiveCola`)
*An intensely glowing, isotopic uranium soda that glows bright fluorescent lime in the dark.*

```
 [ Bottle Leaks / Detonates ] ===> [ Ionizing Radiation Aura (3m) ]
                                            |
                         +------------------+------------------+
                         v                                     v
           [ Mutates & Contaminates Stock ]          [ Sickness & Visual Distortion ]
             Neighboring Colas Turn Toxic!             Geiger Clicks, Screen Static
```

* **Target Clientele:** Mutants, Golems with radioactive power-cores, Alchemists.
* **Failure Disaster (Ionizing Contagion):**
  - Spills radioactive fluid and creates a permanent **Ionizing Radiation Field** until physically scrubbed.
  - **The Contagion Ripple:** Any standard cola bottles, ingredients, or crates situated within the radiation aura **become radioactive themselves**! Contaminated bottles are rejected by normal customers.
  - **Player Sickness:** Players in the zone suffer radiation sickness: the Geiger counter clicks furiously, screens develop static/chromatic aberration, and sprint is disabled.
* **Player Countermeasure & Clean-Up:**
  1. **Hazmat Lead Tongs:** Players cannot touch irradiated bottles with bare hands; they must use long Lead Tongs.
  2. **Lead Disposal Barrels:** Contaminated colas must be hauled and dumped into the sealed **Lead Biohazard Chute** to prevent further spread.
  3. **Decontamination Hose:** Players must wash the counter with chemical chelating foam to cleanse the spill site.

---

### 3.4 Ghost / Phantom Ecto-Cola (`DA_GhostCola`)
*Distilled from translucent ectoplasm and wailing spirits. The bottle itself hovers slightly off the ground.*

```
 [ Bottle Pops / Drops ] ===> [ Eerie Spectral Shockwave ]
                                        |
                 +----------------------+----------------------+
                 v                                             v
     [ Severe Hallucinations ]                    [ Poltergeist Anti-Gravity ]
      Fake Orders, Ghost Voices                    Props & Bottles Float in Air
```

* **Target Clientele:** Phantoms, Banshees, Wraiths, Shadow Sorcerers.
* **Failure Disaster (Sensory Hallucination & Poltergeist Surge):**
  - Releases a ghostly violet mist that affects the minds of all players in the tavern:
    - **Phantom Orders:** The Giant Customer ticket board shows 2–3 duplicate "fake" orders that vanish when approached.
    - **Audio Hallucinations:** Creepy laughter, reverse footsteps, and false customer timer chimes play through player headsets.
    - **Inverted Mind:** Player movement controls flip (Left is Right, Forward is Backward) for **10 seconds**.
    - **Poltergeist Surge:** Nearby bottles and empty crates lose gravity, floating into the air and bobbing unpredictably!
* **Player Countermeasure & Clean-Up:**
  1. **Holy Salt Shakers:** Throwing salt circles onto the floor grounds floating bottles and breaks the anti-gravity field.
  2. **The Tavern Exorcism Bell:** Ringing the giant brass bell at the counter sends a sonic pulse that banishes the ghost mist immediately.

---

### 3.5 Cursed Blood Cola (`DA_BloodCola`)
*A heavy, metallic crimson nectar fermented for the undead aristocracy.*

* **Target Clientele:** Vampire Lords, Blood Knights, Werewolves.
* **Failure Disaster (Vampiric Frenzy & Bat Swarm):**
  - Spills slick crimson fluid. Summons a swarm of blood-thirsty bats that flutter around lights, plunging the immediate station into darkness.
  - If a Werewolf customer smells the spilled blood, their **Patience Timer is instantly cut in half**, triggering immediate table-slamming wrath!
* **Player Countermeasure & Clean-Up:**
  - UV Torch / Garlic Fumigator to disperse bats; fast mop cleanup before Werewolf patrons smell the blood.

---

## 4. Supernatural Customer Roster (Patrons & Scaling)

These colossal patrons visit the tavern bar counter. Each archetype possesses specific beverage tastes, tolerances, physical presence, and devastating **Wrath Tantrums**.

```
    +---------------------------------------------------------------------------------+
    |                           PATRON PROFILE COMPARISON                             |
    +------------------+------------------+-------------------+-----------------------+
    | Mountain Giant   | Stone / Iron Golem| Lunar Werewolf   | Bog Witch             |
    | Authored giant   | Authored giant   | Authored patron   | Authored patron       |
    | High-Volume Metal| Cryo-Cold Metal  | Blood / Meat Cola | Poison & Spider Colas |
    | Counter Slam     | EMP Disruption   | Table Slash / Jump| Hexes & Curses        |
    +------------------+------------------+-------------------+-----------------------+
```

---

### 4.1 The Stone & Iron Golem
* **Physical Scale:** Authored giant proportions using `Lvl_FirstPerson` as the reference. Existing NPC size is preserved; no fixed player-height multiplier applies.
* **Personality:** Slow, mechanical, emotionless speech. Requires heavy mineral beverages.
* **Demands:** Heavy Metal Canisters filled with **Cryo-Frost Soda** or **Radioactive Neon Cola**. Cannot tolerate warm temperatures ($> 5^\circ\text{C}$).
* **Wrath Event (Electromagnetic Shockwave):**
  - If served warm soda or forced to wait, the Golem stomps its iron foot onto the floor.
  - Sends an **EMP Shockwave** across the counter:
    - Disables electric vehicle batteries for 15 seconds.
    - Jams the electronic checkout scanner.
    - Causes all loose un-clamped bottles to vibrate and gain $+25$ Agitation!

---

### 4.2 The Lunar Werewolf
* **Physical Scale:** Authored patron variant using the approved world proportions; no fixed player-height multiplier applies.
* **Personality:** Feral, twitchy, snarls continuously. Extremely aggressive and impatient.
* **Demands:** **Cursed Blood Cola** or **Dragon Pepper Fire Cola**. Demands drinks served quickly in heavy glass.
* **Patience Meter:** The shortest in the game (drains $1.5\times$ faster than other customers).
* **Wrath Event (Feral Counter Rampage):**
  - Leaps directly with both paws onto the bar counter!
  - Slashes at crates of bottles with sharp claws, shattering glass and sending shrapnel flying.
  - Howls with deafening force, knocking miniature players flat on their backs.

---

### 4.3 The Bog Witch
* **Physical Scale:** Authored patron variant using the approved world proportions; no fixed player-height multiplier applies.
* **Personality:** Hunched, sneering, speaks in rhyming riddles. Accompanied by a glowing familiar.
* **Demands:** **Poison Swamp Cola** and **Spider Nest Cola**. Refuses clean pristine bottles—actually prefers dirty, slobber-stained bottles!
* **Wrath Event (Witch's Hex):**
  - Cackles wildly and points her crooked wand at the player workstations:
    - **Reversed Conveyors:** Conveyor belts run in reverse for 20 seconds.
    - **Vehicle Transmutation:** Micro-vehicles turn into sluggish moving pumpkins for 15 seconds!
    - **Ice-to-Fire:** Chilling ice troughs temporarily heat up, boiling stored drinks.

---

### 4.4 The Vampire Aristocrat
* **Physical Scale:** Authored patron variant using the approved world proportions; no fixed player-height multiplier applies.
* **Personality:** Refined, arrogant, obsessed with cleanliness and etiquette.
* **Demands:** **Cursed Blood Cola** or **Royal Golden Sparkler** served in **Pristine, Spotless Glass**.
* **Special Condition:** If the bottle has even a speck of dirt or unwashed syrup (scanner cleanliness $< 100\%$), the Vampire refuses it with disdain.
* **Wrath Event (Shadow Veil):**
  - Summons dark vampire fog that extinguishes counter lanterns, plunging the tavern into pitch darkness. Players must navigate with flashlights until lanterns are relit.

---

### 4.5 The Mountain Giant
* **Physical Scale:** Authored giant proportions using `Lvl_FirstPerson` as the reference; preserve existing NPC size.
* **Personality:** Boisterous, thunderous laughter, massive appetite.
* **Demands:** Multi-bottle orders (3–4 large Metal Kegs of Classic or Fire Cola at once).
* **Wrath Event (Colossal Table Slam):**
  - Slams both massive fists onto the bar counter.
  - The impact acts as a localized earthquake:
    - Launches players, micro-vehicles, crates, and loose bottles high into the air.
    - Any bottle in mid-air that lands on a hard surface gains $+40$ Agitation or shatters!

---

### 4.6 The Phantom / Banshee
* **Physical Scale:** Authored floating patron variant; counter clearance and dimensions follow the approved world reference rather than the obsolete miniature-player measurements.
* **Personality:** Wailing, translucent, fades in and out of reality.
* **Demands:** **Ghost / Phantom Ecto-Cola**.
* **Wrath Event (Banshee Wail):**
  - Unleashes a high-frequency scream that instantly breaks the seals on all **Under-Torqued (Loose)** bottles in the workshop, causing multiple simultaneous rocket launches!

---

## 5. Technical Data Template for Adding New Colas

To add a new custom cola to the Unreal Engine project, follow this standardized template:

```cpp
// 1. Create a new UInteractablePropDataAsset derivative or instance (e.g. DA_LightningCola)
ItemDisplayName = FText::FromString("Tesla Lightning Cola");
ConsumableType = EConsumablePropType::Drink;
Price = 5.0f; // Retail per portion
NumOfPortions = 4;
ConsumableTemperature = 0.0f; // Preferred chilled temp
ThermalExchangeRate = 0.02f;

// 2. Configure Volatility & Failure Parameters
float ReactionTimeLimit = 3.0f; // Seconds before eruption
float AgitationMultiplier = 1.4f; // Sensitivity to shaking
TSubclassOf<AActor> FailureHazardActor = ABP_Hazard_ElectricField::StaticClass();
TSubclassOf<UNiagaraSystem> RuptureVFX = NS_LightningBurst;
USoundBase* RuptureSFX = SFX_ElectricDischarge;

// 3. Countermeasure Equipment Required
EPlayerToolType RequiredProtection = EPlayerToolType::RubberBoots;
EPlayerToolType CleanUpTool = EPlayerToolType::GroundingRod;
```

---

## 6. Technical Data Template for Adding New Patrons

To add a new customer archetype to the AI and shop system:

```cpp
// Customer AI Configuration
FText PatronName = FText::FromString("Sand Golem");
float PatronScaleMultiplier = 9.5f; // Relative to player
float PatienceDurationSeconds = 60.0f; // Base patience
TArray<UInteractablePropDataAsset*> FavoriteColas = { DA_SpicyCola, DA_DustCola };
bool bAcceptsPlastic = false; // Only drinks from stone/metal

// Wrath Event Delegate Binding
void TriggerWrathEvent()
{
    // E.g. Spawns sandstorm visual post-process, jams conveyor gears
    SpawnSandstormHazard();
    PlayWrathAudio();
}
```
