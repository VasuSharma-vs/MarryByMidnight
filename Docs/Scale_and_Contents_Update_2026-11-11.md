# Approved scale, mass and contents update — 11 November 2026

Development remains active. These latest designer instructions supersede contradictory historical scale and content proposals.

## Binding decisions

1. Preserve the current NPC, player and Cola sizes in `/Game/FirstPerson/Lvl_FirstPerson`. The default player size is intentional for compatibility with existing mechanics. Do not shrink the player to the old 25 cm GDD target or resize existing characters/bottles to satisfy earlier ratios.
2. The surrounding world is built primarily for giants. Coins, scanners, checkout/counters, racks, furniture and structures must reflect that reference. Player vehicles, filling/cleaning equipment and direct-use tools are player-scale exceptions. Reach, collision, placement, ramps and visibility must support those proportions.
3. NPCs and movable actors are lighter than the main player. Gameplay mass is independent of visible size and thermal mass. Keep static structures fixed; per-type numeric mass tuning is not specified by the designer.
4. [Bottle Contents Catalog](Bottle_Contents_Catalog.md) records 21 types. The demo contains only Lemonade, Mint, Sweet, Spicy and Zero Calories. Relative base prices: `Lemonade < Mint < Sweet < Spicy = Zero Calories`. Numeric content prices and type-specific effects remain unapproved. Future categories are deferred.

## Measured reference and implementation findings

- Native `Cola1` bounds are approximately 190.04 cm tall and 75.32 cm wide. Original level bottle actor scale is 1. The prototype's hard-coded 0.15 scale reduced them to about 28.5 cm; that prototype reduction must be corrected.
- Original player capsule: radius approximately 34 cm / half-height 96 cm, component scale 1. Giant customer capsule: radius 204 cm / half-height 528 cm. The designer explicitly approved the current setup; these measurements are verification data, not resizing instructions.
- Original bucket actors use scale 6. The prototype reduced its bucket to 90 cm width; restore suitable reference proportions and corresponding pocket spacing.
- The reference checkout area uses large raised platforms and existing `SM_StoreRackRamp` meshes. Floor is around Z1755 cm and counter platform top around Z2217.5 cm, approximately 462.5 cm higher. Reuse these proportions and accessibility principles in the prototype.
- Currency recipes currently miniaturize raw coin meshes with a 0.04 multiplier. The $1 mesh is roughly 68 cm across at native scale. Giant currency requires corrected recipe scale, spacing and tray/query dimensions together.
- Prototype supply output is scaled to 0.15. It must not shrink delivered bottles/caps. Player-facing station roots are `DefaultSceneRoot`, so working-surface/root placement and mesh offsets can be adjusted without replacing the station architecture.
- The prototype Zero Calorie asset was configured below Spicy. Synchronize its provisional base price with Spicy and use the working display name Zero Calories; preserve internal asset IDs for compatibility.

Inspection evidence is in `Saved/NormalColaResume_20261111`: `GiantScaleReferenceBounds.json`, `CharacterComponentScaleReferences.json`, `OriginalGiantCheckoutDimensions.json`, `GiantReferenceBucketAndRamps.json`. The designer's `Lvl_FirstPerson` reference was inspected without saving changes to it.

## Validation boundary and next work

Build 11156 passes 39 native tests, including the expanded real disk round trip. Earlier remote loop/UI/reconnect/queue tests remain useful financial and lifecycle evidence, but they used the reduced prototype props and do not accept the corrected world scale. The 11156 live run was interrupted by the designer's scale correction; it does not yet establish elimination of the pooling warning.

Next: correct prototype geometry/delivery/currency scale using the reference while preserving character and source mesh sizes; implement below-player mass policy through shared product/actor setup; synchronize the Normal price tie; update integration fixtures to the real prop dimensions; rerun the complete loop and inspect practical player access. Do not generate Toxic, Black Magic, Radioactive or Explosive demo systems from the catalog.

Unreal is currently open on `Lvl_FirstPerson`, PIE stopped. The selected native module is 11156. Background CPU throttling is restored to true. Original one-client listen-server PIE settings, dimensions, position, port and mouse-control settings were restored and read back in `RestoredPlaySettingsAfterDesignUpdate.json`. The shop save hash still matches the pre-test baseline. The global NPC knowledge test save must be preserved/restored at the next safe editor shutdown; do not overwrite active editor memory. Existing user/Gemini changes remain uncommitted and must be preserved. No pause or stop instruction is active.

The documentation update is complete: the master GDD, core decisions, item/Cola compendiums, mechanics and technical plan now follow the approved size/mass policy and the 21-type catalog. Catalog rows were verified to contain exactly 21 entries; `git diff --check` passes. These documentation edits do not claim that runtime scale, mass or the prototype Zero Calories price have already been corrected.
