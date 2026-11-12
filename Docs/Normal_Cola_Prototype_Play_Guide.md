# Normal Cola prototype play guide

**Checkout E repair, Editor 11205:** enter Play, climb the front player access
deck, look at the counter near its front controls and press E. The old 150 cm
prop trace missed this giant desk; a station-specific C++ query now uses the
existing 350 cm control reach. Actual E opens the menu in standalone and on the
listen-server host. Remote testing currently hits a Prop Manager initialization
disconnect (`ReliableBufferOverflow`); see [the repair record](Checkout_Interaction_Fix_2026-11-12.md).
This repair is not in the older packaged 11193 candidate described below.

**Current acceptance boundary, 12 November:** the two-peer instrumented service/RPC loop seals a genuinely full bottle and passes sales, ATM banking, supplies and supported restocking. Staged-impact pressure flight and desktop role/G/R routing pass. Clean-start giant navigation passes three early starts, including the playable map without a test driver. The cooked standalone now starts after a repaired bottle-component template, but desktop inspection timed out; packaged multiplayer and performance remain pending. A complete player-driven manufacturing/carry session, shake/drop, late join/adverse networking, and the specified hot bulk supply/hose/cooling workflow still need acceptance. Current scope is plastic bottles, five Normal Colas and Normal manual twist caps. Preserve the reference character and bottle sizes.

## Open the playable world

The latest packaged candidate is `Saved/PrototypeResume_20261112/Packaged_11193/PackageArchive/Windows/MarryByMidnight.exe`. It opens the player-facing prototype map. The native Game runtime is 11193; the newer 11194 Editor build adds editor-only five-flavor filling coverage. This candidate passes instrumented packaged multiplayer and settled late join with both no emulation and verified 75 ms outgoing lag / 2% packet loss per peer. Human input and the hot hose/cooling workflow remain pending.

From the project directory, start two local game windows with:

```powershell
& ./Scripts/StartNormalColaPrototype.ps1 -ArchiveRoot 'Saved/PrototypeResume_20261112/Packaged_11193/PackageArchive' -Mode LocalPair
```

For two computers, use `-Mode Host` on the host, and `-Mode Client -ServerAddress '<host address>'` on the other computer. These modes target the same player-facing map and require the matching package on both machines. LocalPair is a convenient two-window check; cap sealing still requires two distinct player characters. No automated validation driver runs in this map. Normal play progress is isolated in `Saved/PrototypePlay/Host` and `Client`, separate from the original world saves and disposable test saves. Use `-WhatIf` to preview a launch without starting either game. The launcher is parse/preview-checked; its full player-driven session remains unaccepted.

Open `/Game/NormalColaPrototype/Lvl_NormalColaPrototype` and press Play. For multiplayer, use a listen-server PIE session with two players. The separate `_Validation` world contains an opt-in test driver; it is not the player-facing demo.

The bank and register start empty. The 356 approved starter bottles provide the first sales, so serve customers and deposit their coins before purchasing supplies. The prototype has its own `NormalColaPrototypeAutosave` slot and a 60-second autosave interval. Existing `ShopAutosave` progress belongs to the original world.

## Controls and first sale

- Move with WASD and look with the mouse.
- Hold G to pick up or place a prop at the aimed location. Double-tap G to drop it.
- Look at the workbench, checkout or ATM and use E to interact.
- Use the terminal buttons for purchases, production and billing. Escape closes a terminal.
- In the manual-capping terminal with one eligible bottle, the first G claims the holding role and contributes force in the same request. Keep pressing G; a different player presses R to claim rotation and advance it. Use the explicit role buttons to select between multiple bottles. Start holding before rotation to avoid unnecessary leakage. R is provisional prototype tuning; Escape leaves your role. These contextual inputs do not change ordinary G grab controls outside the terminal. The atomic-claim service/RPC repair passes on both peers with a full bottle; complete human manufacture remains pending.

These keys are read from the actual `/Game/Input/IMC_Default` mappings. Saved Blueprint carry callbacks have automation coverage; a complete human hardware-input acceptance pass is still required.

1. A giant customer takes a bucket, collects Normal Cola from shelves and joins the checkout queue.
2. At the front, the customer puts the bucket at the checkout. Place each requested bottle in the SCAN area. The live list preserves each bottle identity and groups matching products.
3. Inspect content, temperature and expiry. Remove/replace unwanted products before locking the invoice. Save preserves selected bottles after they leave the scanner volume.
4. Use **Finalize and lock bill**. The customer provides physical coins in PAYMENT.
5. If change is due, move existing shop-owned coins into CHANGE. Use **Complete paid checkout** once tender and change match the bill. Customers pay exactly when the register cannot make change.
6. The customer collects purchased bottles, returns the bucket at the exit and leaves. The counter can serve the next customer.

Sale proceeds remain physical shop cash. They are not also credited to the bank. Move shop-owned coins into the ATM deposit tray to convert them into shared bank funds. The ATM terminal withdraws the selected denomination from that same bank into real coins.

## Reinvest and manufacture

1. At the workbench, buy an empty plastic bottle, a Normal manual twist cap and a pre-mixed Normal Bulk Cola Container. Prototype costs are configurable: $4, $0.50 and $20 respectively. Deliveries use the supported outboard platform; a full area rejects the order without charging the bank. Clear delivered goods to make room.
2. Place the items in the labeled Bottle, Cap and Cylinder input areas. Close the terminal while moving physical items, then interact again.
3. Select **Fill from** the desired Normal Cola cylinder. Filling stops at normal full capacity; a four-portion bottle uses 10% of a forty-portion cylinder.
4. Attach the compatible physical cap. Two distinct players then use the Manual Wheel Rotator: one maintains holding force with G presses, and the other advances rotation with R. Holding effectiveness declines over time; insufficient hold permits accounted liquid leakage before closure. A completed seal remains closed after releasing hold. An open bottle is not ready for NPC shelf selection.
5. Place the finished bottle within a shelf stock area. Shelf stock registers automatically; customers can select the new product.
6. Repeat sales, ATM deposits and purchases. The five demo contents are Lemonade, Mint, Sweet, Spicy and Zero-Calorie.

There is no raw-ingredient mixing in this demo; it remains a later full-game feature. Empty bottles must meet the approved strength, cleanliness and empty-content requirements for reuse.

The current fill control uses the existing direct source-to-filler service. The specified physical hose/dispenser and hot-source cooling workflow are still pending and must not be mistaken for completed equipment.

## Plastic pressure

Impacts and shaking increase persistent agitation pressure. Strictly exceeding
the plastic bottle's strength-adjusted tolerance ejects its physical cap. Per the
designer's latest choice, Editor **11207** defaults to the original **BP_Cola
Rocket Projectile Component** tipping/dragging/erratic flight. Actual liquid is
consumed and pressure vents; exhaustion ends powered movement with a physics
falling handoff. The editable **Cola > Pressure > Rocket > Use Original Rocket
Flight** option can select the retained Chaos-thrust mode instead. This option
selects flight behavior; it does not bypass the pressure/cap launch conditions.

The new original-flight native regression passes cleanly; physical-mode coverage
also passes. Full suite: 57/58, with the existing liquid-height assertion failing.
Visual handling, actual remote flight, late join and adverse networking require
acceptance. The older 11175 two-peer physical-thrust result is historical evidence
for the optional mode, not validation of this new default. The packaged 11193
candidate does not contain this change. Read [the current rocket record](Original_Bottle_Rocket_2026-11-12.md).

## Current validation boundary

See [the current checkpoint](Prototype_Progress_2026-11-12.md) for exact build numbers and evidence; the older validation record retains historical results. Automated native tests and the two-player driver establish only their exercised financial, manufacturing, shopping and persistence paths. They do not replace a full player-driven keyboard/controller, pressure-failure, packaged-build or performance pass.
