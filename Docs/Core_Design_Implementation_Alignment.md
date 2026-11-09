# Core Design: Implementation Alignment

Received: 9 November 2026. Primary requirements: [Core Gameplay & Bottle System Design Decisions](Core_Gameplay_Design_Decisions.md), preserved verbatim from the user's attachment.

Updated: 10 November 2026, following [the user's six additional decisions](Core_Design_Decisions_Update_2026-11-10.md). Precedence: latest decision update, then the original primary requirements, then older specifications.

This note records implementation consequences; it does not add design requirements or claim gameplay implementation. The previous implementation audit and evidence remain historical snapshots.

## Resolved decisions and superseded requirements

| Area | Primary requirement | Consequence for older documentation and implementation |
|---|---|---|
| First playable | Complete business loop, developed first with one plastic bottle, normal twist cap, cola, bulk container, filler, shelf, NPC, bucket and counter | Include loans/rent/purchases, stocking, NPC shopping, manual/scanner checkout, cash/change, temperature and expiry. Fantasy patrons, specialized vehicles and advanced disasters are not prerequisites for the initial content set. |
| Pricing | Fixed product-defined prices; players cannot set or modify them; checkout retrieves prices and computes totals | Supersedes player-controlled/manual selling-price overrides. The prior portion-based quantity and temperature/expiry adjustments remain applicable unless explicitly replaced; a fixed configured rate is not permission to silently replace those formulas with one flat price for all fill levels. Bottle cost/recycling remain separate. |
| Wallet | One server-validated Shared Team Wallet for the session | All customer revenue and business expenses use the shared balance; clients cannot commit local wallet changes. |
| Temperature pricing | Symmetric deviation from product optimum; configurable range; discount capped at 20% | Supersedes older cold bonuses and 0.2/0.5/0.6 warm multipliers. Require a positive penalty range and currency rounding at transaction boundaries. |
| Expired sale | No valid cola revenue; void/refund collected payment, plus one replacement or compensation obligation | Penalty quantity is Max(1, ContentLevel) × nominal portions × price per portion, before temperature discount. Refund and compensation are separate operations; retries must not repeat either. |
| Units | Normalized pressure; Strength 0–1; Celsius; game timestamps | Supersedes authoritative PSI and Strength 0–100. Relative resistance percentages are comparison scores, not actual pressure thresholds. |
| Materials | Plastic, Glass, Metal Can, Diamond, Concrete | Supersedes treating the older four-material catalog as complete. Diamond/Concrete need distinct policies; do not silently reinterpret persisted enum values. Legacy saves need not be migrated. |
| Capacity and damage | Plastic/metal maximum content level and pressure tolerance scale by remaining Strength | At Strength zero, deform rather than automatically shatter. Resolve excess contents visibly/accountably. Glass/concrete fracture at zero; diamond resists ordinary structural damage. |
| Caps | Separate purchased physical products with independent stable IDs and ISM/ASM lifecycle | A bottle's CapType plus CapSealed fields alone are insufficient. Persist bottle↔cap association, seal state and relevant cap damage/valve state; prevent attachment to multiple bottles. |
| Cap mechanisms | Manual twist, auto-close, discharge twist; hammer stopper, welded lid, diamond twist, poured/curing plug | Old Twist/Cork/BoltLock/RuneSeal names and five removal modes do not define the complete new hardware/behavior model. Shared meshes may still have different gameplay definitions. |
| Open filled bottles | Always remain ASM; spill according to orientation/opening; no mandatory eruption timer | Supersedes optional uncapped-filled ISM demotion and automatic 5/3/2.5-second eruption rules. Empty uncapped bottles may demote when otherwise eligible. |
| Pressure/launch | Configured atmosphere exchange, limited discharge and force-based recoil/launch | Do not zero every pressure release or launch every vent. Filling, relief and eruption must use consistent pressure semantics. |
| Multiplayer | Four-player listen server; server-authoritative state and economy | Supersedes the suggested two-player initial acceptance test. Initial multiplayer validation must use four connected players, including interaction conflicts and duplicate-transaction prevention. |
| Saves | New versioned schema, periodic autosaves, no old-save migration, incomplete checkouts canceled rather than restored | Persist completed checkout effects only. Do not resume partial payment or ownership transfer after load. Shop expenses and other committed economic operations still persist. Never persist pooled actor pointers as identities. |
| Refill expiry | Prop Manager initializes newly added contents and their Creation Timestamp when an empty bottle is refilled | Preserve bottle identity and physical history. Content age must restart without resetting bottle Strength/damage or using a full physical reset. |
| Reuse/recycling | Reuse requires Strength >0.70, clean, and completely empty; recycling refunds eligible bottle purchase cost; caps never recycled | All reuse conditions must hold. Exactly 0.70 Strength fails. Material-specific restrictions remain applicable; expired contents alone do not permanently ruin a bottle. |
| ASM pool exhaustion | Prevent saturation in normal play; expand through Prop Manager in small configurable batches when full | Supersedes a rigid 500-unit runtime ceiling. Pool expansion must preserve identity and server/client mappings, and returned units must be reset/reused. Mandatory open-filled ASMs cannot be demoted to hide saturation. |

## Structural work required by these decisions

1. **Finish source/header integration first.** Resolve the provider/consumer mismatches identified by the audit before extending the runtime APIs.
2. **Separate container, liquid and cap definitions.** Container capacity/material, liquid per-portion price/optimum/expiry, and closure mechanics need unambiguous ownership. Resolve a composed product view for scanner/NPC queries rather than maintaining conflicting copies.
3. **Keep authoritative fractional liquid state.** Derive portions and millilitres from ContentLevel and nominal capacity. Effective damaged capacity limits containment; it must not redefine the nominal portion-count denominator or erase liquid.
4. **Persist cap relationships explicitly.** Keep cap identity distinct from cap hardware type and bottle identity. Attach/detach, pool transitions and save reconstruction must preserve the relationship atomically.
5. **Represent material-specific failure as policy.** A universal Strength≤0→shatter branch contradicts plastic/metal/diamond rules. Damage, effective capacity, pressure relief, leakage, fracture and visual damage must agree on normalized Strength.
6. **Make transfers and economic operations atomic on the server.** Validate bottle/cap/supply ownership and available liquid before accepting filling. Purchases, sales, refunds, compensation and recycling must execute once even with simultaneous players or retries.
7. **Model checkout as a transaction lifecycle.** Preserve individual item identities, server-calculated prices, received tender, returned change, ownership transfer and compensation status. Players cannot override prices. If a calculator UI remains, it cannot provide an authoritative editable selling-price path. Commit completed sale effects once; incomplete tenders remain outside the committed wallet.
8. **Extend persistence beyond props.** Store economy obligations, equipment placement, shelf/bucket ownership and cap associations. Reconstruct ISM/ASM from logical state, including open filled bottles that must load as ASM. Periodic saves must contain a consistent committed snapshot, with incomplete checkout reservations/tenders canceled or excluded and no lost or duplicated stock.
9. **Budget for mandatory active bottles and grow the pool when necessary.** Treat the normal pool target as expandable, not as a reason to demote open filled bottles. Configure a small batch size; prevent concurrent requests from creating duplicate units or conflicting ASM IDs. Preserve the existing pooled-actor/component architecture rather than interpreting component creation as a new representation model.
10. **Separate physical-container history from content age.** A refill must create/reset the content lifecycle through Prop Manager while retaining the bottle's stable identity, Strength, damage, temperature history and other persistent physical properties. Do not stamp the contents' Creation Timestamp on every filling frame. Finalize and apply the reuse predicate before admitting a refill; internal floating-point empty-state handling must not allow materially nonempty bottles to bypass it.

## Remaining details to settle during implementation

These do not invalidate the supplied decisions. Most numeric tuning can remain editable and use clearly labeled provisional defaults. The following behavioral choices need an explicit rule before implementing their affected path:

| Detail | Why it matters |
|---|---|
| Loan/purchase permissions | The shared balance is settled; define whether any player may take loans or purchase equipment and whether any transaction needs host-only permission. |
| Incorrect cash/change handling | Manual price-setting is superseded. Cash-handling mistakes remain possible under the original requirements; specify NPC reaction and the completion condition for incorrect tender/change. |
| Compensation settlement | Define replacement equivalence, overfilled replacement quantities, who selects credit versus replacement, and treatment when funds/stock are insufficient. |
| Contents during an in-progress fill and bulk supply expiry | Empty-bottle refill age now starts when the new contents are added. Define supply-expiry transfer and how additional liquid within that same filling operation is handled without repeatedly refreshing age; nonempty bottles cannot begin a new reuse/refill cycle. |
| Cleaning, material reuse limits and damaged-capacity handling | Reuse eligibility is settled (>0.70 Strength, clean, empty). Define the cleaning action, extra material-specific limits and where excess liquid goes when a sealed bottle loses capacity. Never silently delete it. |
| Caps during installation, consumption and removal | Define auto-close installation prerequisites, reusable versus consumed closures, welding/curing progress, and whether damaged closures can be recovered. |
| Pressure during ISM storage and save/load | Older schema omits pressure because it is transient/derived. Preserve any residual sealed pressure and valve state that cannot safely be reconstructed; define the reconstruction rule before demotion. |
| Autosave timing and canceled-checkout stock restoration | Configure save interval; define where canceled checkout goods/buckets return and how NPC shopping resumes. Unfinished checkout transactions themselves must not resume, and their uncommitted cash must not enter the saved wallet. |
| Disconnection and exceptional allocation failure | Pool growth is settled. Define release/reassignment of held objects after disconnect and a graceful response if additional allocation fails; preserve existing objects and contents. |
| Fragment policies for later materials | Immediate versus settled shard IDs, optional broken hull and full-pool fallback remain open for glass/concrete work. They need not block the plastic-first loop. |

## First acceptance examples

- A 25-portion bottle at $4/portion sells for $70 at ContentLevel 0.7 and optimum temperature, excluding container cost.
- The same bottle sells for $56 at maximum temperature deviation (20% discount).
- An expired bottle at ContentLevel 0.7 yields zero valid cola revenue and a $100 compensation obligation, plus refund/void of any collected payment. At 1.3, the obligation is $130. Apply the selected compensation once per product.
- Plastic with original maximum level 1.7 and Strength 0.5 has effective maximum level 0.85 and half its original pressure tolerance. Existing liquid above 0.85 must be resolved, not deleted.
- Open filled bottles remain ASM even when motionless; empty uncapped bottles can demote after settling.
- Bottle/cap IDs and their association survive repeated representation changes and save/load without persisting actor pointers.
- Four connected players cannot duplicate a purchase, sale, attachment, fill transfer or recycling reward through simultaneous requests.
- Reuse succeeds only when Strength >0.70, the bottle is clean and ContentLevel is empty; test exactly 0.70, dirty bottles and residual contents as failures.
- Refilling an eligible empty bottle starts the new content's expiry timestamp once, while preserving bottle identity, damage and Strength.
- Saving during checkout and reloading does not resume that checkout, persist its uncommitted payment or duplicate/lose its goods. Completed sales still affect the shared balance exactly once.
- Filling the current pool triggers one configurable batch expansion; newly available units have unique mappings, and returned units are reset and reusable.

These are specification examples, not executed tests. Numeric production tuning remains configurable as directed by the primary document.
