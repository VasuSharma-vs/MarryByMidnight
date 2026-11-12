# Equipment Design — GDD Integration and Conflicts

**Updated:** 11 November 2026. The designer requested inclusion of the supplied [Equipment Design Specification](Equipment_Design_Specification.md) in the local GDD. Its thirteen sections are preserved in that document; this integration record distinguishes requirements, deferred features and unresolved implementation details.

## 1. Scope and Priority

The equipment catalog contains five planned categories. **Only the Manual Wheel Rotator is demo equipment from this catalog.** Cap Gun, Automatic Fill and Cap Machine, Pressure Regulator and Automatic Bottle Cleaner are full-game-only; do not build their power, fuel or automation systems as demo prerequisites.

This restriction does not remove the already planned Normal Bulk Cola Container, hoses/dispenser, essential filling infrastructure, cooling, shelves, scanner/checkout or cash/ATM loop. It selects the cooperative manual-cap station for the existing production loop. The next equipment priority is the rotator's two-player closure workflow, integrated with the existing plastic-bottle pressure/rocket system.

Prototype bottle/content/cap scope remains **plastic bottles, Lemonade/Mint/Sweet/Spicy/Zero Calories Normal Cola, and Normal Caps (Manual Twist)**. A machine rotating a manual twist cap does not create an auto-lock cap variant. Preserve the current player, NPC and bottle setup in `/Game/FirstPerson/Lvl_FirstPerson`, the giant-world proportions and player-accessible tool/control interfaces. Existing full-game materials and assets remain preserved but outside prototype acceptance.

## 2. Shared Equipment Properties

Use common, configurable equipment data and shared behavior where appropriate; do not duplicate every variant's logic or add a competing bottle-state system. This is a design requirement, not approval for an unreviewed class/replication/save schema change.

| Property | Requirement |
| --- | --- |
| RequiredVoltage | Minimum adequate electrical voltage for electrically powered machines. Exact ratings and low-voltage outcomes remain open. Manual equipment does not require a connection. |
| PowerConsumption | Operating electrical demand, configurable by machine/variant. Zero for the Manual Wheel Rotator. |
| OperatingPressureRange | MinimumOperatingPressure and MaximumOperatingPressure. Configurable for pressure-sensitive equipment; explicitly not applicable when the equipment does not use liquid/pneumatic pressure. Manual rotator range remains undefined. |
| EquipmentType / EquipmentVariant | Common classification and variant configuration. Suggested display names are not final identifiers. |
| PurchasePrice | Configurable cost; relative descriptions are specified, final numeric prices are not. |
| Portability / EquipmentSize / RequiredPlayers | Handling, footprint, access and operator requirements. Large equipment can require transport/placement restrictions without changing character sizes. |
| EnergySource / FuelConsumption | Manual, electrical or fuel as applicable. Fuel variants need consumption data; no fuel system is implied for the demo rotator. |
| ProcessingCapacity / OperatingSpeed | Configurable where applicable; final counts/speeds remain open unless separately specified. |

The source requires electrically powered machines to expose the three standardized electrical/pressure properties; it does not require manual or fuel-only machines to consume electricity. Keep equipment supply pressure, bottle internal pressure, mechanical cap-holding force and torque conceptually distinct. Reuse normalized bottle-pressure units; the specification does not approve a new PSI scale, voltage rating, force conversion or thermal formula.

## 3. Manual Wheel Rotator — Demo Requirement

Development name: **Manual Wheel Rotator**. Suggested name: **Twin-Hand Cap Crank**, not final. It is the cheapest cap-closing equipment, a large mechanical station with extremely low portability. It requires **two players**, no electricity and a carrier vehicle to relocate it. Exact dimensions/weight are not finalized.

### Operator 1 — Hold the cap down

- Repeatedly press **G**; each press contributes downward holding force on the cap at the bottle opening.
- Continued pressing is required to maintain the temporary holding effect.
- Holding effectiveness gradually decreases with elapsed time **even if pressing continues rapidly**. No fixed press cadence, decrement, force value or universal countdown has been specified.
- If effective holding force becomes insufficient before closure, pressurized cola begins escaping through the existing leakage/pressure mechanics.

### Operator 2 — Rotate and close

- Operate the rotational mechanism while the first player holds the cap down.
- Rotation progressively closes the Normal manual twist cap; finish before the holding effect becomes insufficient.
- The second player's exact input is **not specified**. Do not invent a key, speed, duration, turn count or torque minigame.
- Once properly secured, releasing downward holding pressure does not reopen the cap or cause leakage through that cap opening. Other existing pressure/containment rules still apply.

Stopping presses, insufficient holding force or delayed rotation can cause cola escape before sealing. This is a force/pressure-dependent equipment failure, not the old mandatory bottle-eruption timer. Leakage does not automatically mean rocket launch: retain the separately approved bottle-pressure threshold/reaction behavior and shared liquid accounting. The station does not independently force a new bottle explosion or fragment event.

## 4. Full-Game Equipment Reference

| Equipment | Confirmed operation and constraints | Open details |
| --- | --- | --- |
| Cap Gun | Expensive, highly portable, handheld, one player; high-voltage electricity. F1 pneumatic-wheel-gun inspiration; rotates/tightens caps. Excessive torque can damage/break the cap and compromise sealing, releasing cola according to bottle state. Suggested name Pitlock Cap Gun. | Power consumption, final torque values, pressure applicability and final naming. F1 inspiration does not itself require an additional pneumatic supply. |
| Automatic Fill and Cap Machine | Most expensive current catalog type; very high voltage. Central machine with flexible hoses and grabbable nozzles, separate fill and cap-close controls. Nozzles provide both functions. Overfill can create overpressure; excessive tightening can damage the cap. Size, portability, energy, pressure and simultaneous capacity depend on variant. Suggested name HydraSeal Filling Rig. | Exact controls, ratings and variants. One/two/four/eight nozzle counts are provisional suggestions, not finalized requirements or an auto-close-on-full cap rule. |
| Pressure Regulator | Increases or decreases supplied liquid pressure within variant limits. Fuel-powered and electric categories, with consumption data appropriate to each. Wider pressure range costs more; capability can increase operating demand. Suggested name Pressure Governor. | Pressure ratings, adjustment/energy rates and final variants/names. Generic examples are not four mandated assets. |
| Automatic Bottle Cleaner | Dirty bottles enter a chamber, are processed over several seconds, then removed for reuse evaluation. Capacity varies; larger capacities cost more. Cleaning removes dirt, not lost Strength. Reuse still requires empty, clean and Strength > 70%. Suggested name Rinse-O-Matic. | Energy source, exact duration and capacities. One/two/four/eight bottles are provisional suggestions; pressure applicability depends on final mechanism. |

All equipment failures feed existing bottle/cap pressure, damage, leakage and containment services. Electrical shortages may affect powered machines, but exact low-voltage behavior is deferred. Neither insufficient-voltage shutdown nor reduced performance is finalized by an example.

## 5. Conflicts and Unresolved Dependencies

| Topic | Disposition |
| --- | --- |
| Earlier direct/single-player cap-close acceptance versus required two-player station | **New demo requirement:** the Manual Wheel Rotator requires two cooperating players. Existing direct attach/close service tests and single-request UI actions do not establish playable station acceptance. Solo completion/accessibility and replacement of the current player-facing closure path are not defined; do not add a one-player bypass by assumption. |
| G press-to-hold role versus existing G / GG / hold-G grab controls | **Input conflict:** retain G as the specified cap-holding input, but define how station interaction selects its input context and prevents unintended grab/drop/placement. No runtime rebind or input-routing change was made by this update. |
| Carrier-required relocation versus vehicles deferred from the prototype | **Transport dependency:** keep the carrier requirement. Whether the demo starts with an installed station or provides a limited carrier/delivery method is open. This does not automatically add the full vehicle fleet or allow hand-carrying the machine. |
| “Extremely heavy” machine versus approved below-player gameplay mass | **Weight/handling ambiguity:** final physical weight is unspecified. Preserve the approved gameplay-mass policy pending explicit revision; low portability/carrier restrictions can be independent handling mechanics. Do not derive a greater-than-player rigid-body mass from the description or giant mesh volume. |
| Declining holding effectiveness versus withdrawn eruption countdown | **Compatible but distinct:** the new decay belongs to cap-holding effectiveness during this station's operation. Do not restore an inevitable bottle rocket after a fixed number of seconds. Exact decay/recovery/reset rules remain open. |
| Cap-holding force insufficient causes leakage versus pressure > tolerance causes rocket | **Both requirements remain:** use the existing bottle-pressure/leakage systems for escape, while the prototype's explicit tolerance exceedance triggers its rocket reaction. Force/pressure conversion, cap release during attachment and held/uncapped/empty edge cases still need specification. |
| Broader power/fuel equipment fields versus electricity-free demo | **Scope resolved:** powered data/behavior is a full-game requirement; manual rotator RequiredVoltage is not applicable and PowerConsumption is 0. Demo source electricity and progressive source heating stay disabled. |
| Only one catalog machine versus four-stage filling infrastructure | **Scope resolved by source:** essential container/hose/dispenser/filling infrastructure remains in the demo. Do not require the full-game Automatic Fill and Cap Machine or Pressure Regulator to make the demo fill bottles. |
| Large station versus player-scale filling/control exceptions | **Compatible with placement work:** size the machine/supports around unchanged giant bottles, keep grips/operator positions practical for the unchanged player, and preserve route/camera/collision access. Exact footprint and transport clearances remain open. |
| Suggested names, counts and balance versus final asset requirements | **Provisional:** final names, prices, voltages/power, operating ranges, dimensions/weights, speeds, nozzle counts, cleaner capacities/durations and voltage effects are not finalized. Do not generate a full asset/variant matrix from suggestions. |

## 6. Implementation and Acceptance Guidance

Prefer C++ shared systems, Blueprint only for necessary art/adapters, and existing Prop Manager/bottle/cap/liquid services. Before implementation, resolve required inputs and missing gameplay policies; present any major architecture change for approval. A reusable equipment component/data arrangement should extend established ownership rather than introduce duplicate bottle identity, pressure or wallet state.

For the two-player station, the server must validate operator roles/access, actual bottle/cap identities, input contributions, holding effectiveness, rotation progress, seal commit and leakage/failure transitions. Separate operators must share the same authoritative station state. Handle role contention, leaving, disconnect/reconnect, cancellation and completion without replayed presses, duplicated liquid loss or stale cap/ownership bindings. Exact reset/cancel rules and saved in-progress equipment-session behavior still need design; existing cancellation of unfinished checkout is not automatically an equipment-session policy.

Required tests, **not yet performed for this feature**:

1. Two real player roles cooperate to secure a Normal manual twist cap on a plastic bottle at the approved size. A single generic CloseCap request is insufficient evidence.
2. G presses increase holding contribution; stopping can lose effective hold, and holding effectiveness declines over time despite continued rapid pressing. Verify progressive rotation and the boundary between sufficient/insufficient holding force using approved tuning once defined.
3. Before sealing, pressure loss causes quantity-accounted leakage through shared services. After proper closure, releasing hold does not reopen the cap. Verify the existing pressure-threshold rocket response rather than adding a forced timeout launch.
4. Server and remote-client roles, role swapping/contention, input loss/latency, disconnect/rejoin and cancel/reset keep bottle/cap IDs and state consistent. Ensure station G presses do not invoke unintended grab/drop behavior.
5. No electricity is required. Essential Normal bulk supply/filling still works without any of the four full-game machines. Verify placement/access and the approved transport solution once selected, with unchanged character/bottle sizes.
6. Repeat the Normal production → shelf → NPC → scanner/payment/change → ATM/reinvestment loop and relevant pressure/cap/persistence regressions. Keep automated service evidence distinct from live multiplayer station playtesting.

## 7. Documentation Status

This update imports the equipment design and reconciles the local GDD. It does not implement machines, alter inputs/physics masses/assets, add electrical/vehicle systems, run a build or establish passing gameplay tests. Existing validation reports remain historical evidence with their stated scope. Prioritize the cooperative Manual Wheel Rotator and plastic pressure integration before advanced equipment.
