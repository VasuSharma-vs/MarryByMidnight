# Prototype Scope — Plastic Bottle Pressure System

**Temporary flight-mode override — 12 November 2026:** The designer explicitly
approved the original **BP_Cola Rocket Projectile Component flight** for now.
Tipping, dragging and erratic component flight are the current prototype path;
the fully physics-driven powered-flight wording below remains a later option.
Pressure thresholds, managed cap identity, liquid/pressure accounting, server
authority and stable ISIDs still apply. Read [the current implementation record](Original_Bottle_Rocket_2026-11-12.md)
for actual validation; historical Chaos-flight results do not accept this mode.

**Approved by the designer:** 11 November 2026. This decision follows the browser-chat GDD reconciliation and takes precedence over its broader prototype material/cap options.

**Subsequent equipment decision:** [Equipment Design Specification](Equipment_Design_Specification.md) selects the **Manual Wheel Rotator** for demo cap closure. Two players cooperate: repeated G presses supply downward holding force whose effectiveness declines over time even during pressing, while the second operator progressively rotates the Normal twist cap closed. Insufficient hold before sealing allows leakage; releasing hold after sealing does not reopen the cap. No electricity is required. This equipment-level hold decay does not reinstate an automatic bottle-eruption countdown. Other four catalog machines are full-game-only; existing bulk supply/hose/filling remains in scope. See [equipment dependencies and test requirements](Equipment_Design_Integration_2026-11-11.md).

## 1. Included and Excluded Content

The first prototype includes **Plastic Bottles, Normal Cola and Normal Caps (Manual Twist) only**.

- Normal Cola retains the five approved flavors: Lemonade, Mint, Sweet, Spicy and Zero Calories. This material/cap restriction does not reduce the approved Normal flavor list.
- Glass, metal, diamond, concrete and all other bottle materials are outside the prototype.
- Auto-lock caps, pressure-release cap variants, wooden stoppers, welded lids and all other cap types are outside the prototype. A pressure-induced release reaction is not a new pressure-release cap type.
- Toxic, Black Magic, Radioactive and Explosive Cola categories remain excluded. This shared pressure/rocket behavior applies to prototype Normal Cola; it does not approve flavor-specific fire, poison or other hazards.
- Glass/concrete fracture and fragment systems remain preserved for later use, outside prototype acceptance. Do not delete existing full-game systems or assets because of this scope restriction.

The complete buy → fill → manually seal → stock → NPC shop/queue → checkout/pay/change → ATM deposit → reinvest loop remains the wider vertical-slice objective. The immediate subsystem goal is to implement and test the plastic bottle pressure/rocket cycle. Existing premixed Normal Bulk Cola Container, cooling, pricing, finite cash, bank and persistence decisions remain in force.

## 2. Pressure Buildup and Threshold

Hits, shaking, drops and physical impacts increase the bottle's internal pressure. Reuse the existing distinction between base pressure and interaction-generated pressure; impact/motion contributions belong in the existing shared pressure model rather than a second independent accumulator.

Each plastic bottle has a configurable **maximum pressure tolerance**. When its authoritative internal pressure **exceeds** that tolerance (`InternalPressure > MaximumPressureTolerance`), trigger the pressure-release reaction and rocket launch. Equality alone is not the specified exceedance condition. No fixed numeric pressure tolerance or pressure gain has been supplied by this update.

This is a pressure-threshold reaction, not a mandatory capping countdown or an explosion caused solely by reaching a fixed fill percentage. Plastic must not be converted into glass fragments as its pressure response. Preserve the existing distinction between bottle Strength/damage and pressure.

## 3. Pressure Release and Rocket Propulsion

On overpressure, escaping pressurized cola propels the bottle into rocket-like motion, similar to the existing `BP_Cola` rocket projectile behavior. The bottle's current orientation and applied forces affect its movement and rotation.

Reuse the existing bottle/rocket implementation where compatible. The intended result is a **physics-driven bottle**, with thrust/reaction forces, gravity and collisions, rather than a fixed upward teleport or an orientation-independent scripted flight path. The request to launch “into the air” does not replace orientation-dependent physics.

Normal Caps remain manual twist closures with no intentional automatic locking or pressure-relief valve. How overpressure opens an escape path through a sealed bottle—forced cap detachment, seal failure or another physical release mechanism—has not yet been specified. Do not silently choose a different cap type to implement the reaction.

## 4. Multiplayer and Persistence Requirements

- The server owns impact/motion pressure accumulation, tolerance evaluation, the release event, propulsion and authoritative transforms. Clients must observe the same bottle identity and event/state changes without independently charging the same pressure event or launching another copy.
- Reuse Prop Manager ISIDs and active-actor/pool lifecycles. Active rocket physics uses the existing ASM path. Preserve damage, liquid quantity, temperature and pressure state through supported state transitions; no ID or hidden ISM slot is discarded by launching.
- Handle real external impacts while retaining existing guards against artificial spawn/pool contacts and duplicate callbacks. Such guards must not suppress the designer's intended shaking/drop/impact pressure behavior.
- Escaping cola and pressure changes must use shared content/pressure services. Preserve liquid accounting and damage history; exhaust effects must not create liquid or reset bottle state by themselves.
- Save/load, reconnect and late join must follow the existing approved persistent-state rules. How an in-flight rocket resumes must be defined/verified rather than assumed from a passing generic bottle save test.

## 5. Required Implementation and Test Evidence

These are **acceptance requirements, not completed tests**:

1. Verify genuine hits, shaking, drops and physical impacts raise pressure using reproducible inputs; stationary/no-event behavior must not manufacture impact pressure.
2. Exercise below-tolerance, exactly-at-tolerance and above-tolerance cases; require the release/launch on exceedance without duplicate launch initialization from repeated or replicated notifications.
3. Verify oriented propulsion and rotation using upright, tilted and sideways bottles, gravity, external impulses and collisions. Observe rendered behavior, not only a changed state flag.
4. Verify the release mechanism and escape path once defined, including the manual cap's identity/state and the bottle's remaining content/pressure. Define empty, partially filled, uncapped and held-overpressure behavior before accepting those cases.
5. Exercise server and remote-client hits/carry/drop interactions, late join/reconnect, repeated pooled bottle reuse and persistence. Confirm shared authoritative identity/state and no duplicate pressure or propulsion events.
6. Run appropriate regressions for filling, manual cap attachment/open/close, shelf/scanner identity, checkout/cash and the complete five-flavor Normal loop. No glass, fragment, automatic-cap or advanced-liquid path is needed for prototype acceptance.
7. Validate the Manual Wheel Rotator's distinct two-player roles, G input isolation, declining holding effectiveness, rotation and shared leakage/seal/pressure behavior. Existing single-request cap tests are not sufficient. Handle operator/session changes according to a defined policy; exact cancel/reset/save behavior is still open.

Keep separate evidence for code implementation, native automated checks and live multiplayer physics/playtesting. Earlier builds and reduced-scale tests do not establish this subsystem's acceptance.

## 6. Open Tuning and Release Details

- Pressure gains for each interaction, shaking/motion sampling, tolerance values and pressure relaxation/discharge rates.
- The overpressure escape mechanism with a sealed manual cap and how it affects that cap's identity/state.
- Behavior when the bottle is held, already uncapped, empty or nearly empty at the tolerance check, and re-arming after a release episode.
- Thrust/torque/exhaust direction, liquid consumption, propulsion termination and safe return to the existing active/resting lifecycle.
- Save/load treatment of an in-flight propulsion episode and any additional persistent event state it requires.
- Rotator force/decay/rotation tuning and controls, G-versus-grab context, operator/session reset/cancel policy and carrier/delivery arrangement. These are recorded in the [equipment integration conflicts](Equipment_Design_Integration_2026-11-11.md#5-conflicts-and-unresolved-dependencies).

These details remain open; this document does not invent tuning constants or approve an architecture change. Preserve the current player, NPC and bottle sizes from `/Game/FirstPerson/Lvl_FirstPerson`, the giant-world layout rules and the below-player gameplay-mass policy.

## 7. Conflict Resolution and Documentation Status

The earlier question about whether to use the prototype's glass bottle with a twist cap is **resolved in design**: the prototype must use plastic with the normal manual twist cap. Aligning product material/settings and labels is now implementation work, with the existing bottle size and stable identity mappings preserved. Glass stopper, glass pressure-release, metal welding and other closure discussions remain future/full-game reference.

This update records requirements only. No C++/Blueprint/asset changes, builds, gameplay tests or runtime acceptance are claimed. See the [master GDD](GDD_Master.md), [core decisions](Core_Gameplay_Design_Decisions.md) and [conflict register](Chat_Design_Decisions_2026-11-11.md).
