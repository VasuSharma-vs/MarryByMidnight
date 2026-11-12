# Placed bottle Content Level — current work

The designer reported that the existing bottles in Lvl_FirstPerson still appear
unchanged, and requested an ActivePropActor debug option to randomize Content
Level on ISM registration. This is a continuation of the liquid fix, not a new
test-map acceptance claim.

Read-only inspection of the actual running Lvl_FirstPerson session found a
visible active BP_Cola with ISID 10077 and canonical/ColaLevel 1.0. Its MID has
Liquid Level 1.0, Liquid Cached Fill 1.0, Native Liquid Motion 1 and Use Instance
Liquid Data 0. The Spicy/Good/Sweet liquid material instances all inherit the
patched M_BaseLiquid and have no conflicting fill/dimension scalar overrides.
The material preview default of 0.5 does not replace actual gameplay contents.

New native implementation: ActivePropActor has an opt-in
Randomize Content Level On ISM Registration checkbox, minimum and maximum
initial levels. The server samples a clamped 0–1 range before assigning the first
ISID; the normal canonical instance update then supplies ISM custom data and ASM
materials. Shipping ignores randomization. Existing registered identities return
their existing canonical contents, and promotion/pooling/save-load do not reroll.
The default is off. Both CDO and per-instance settings are available.

Current source: InteractableProp.h, ActivePropState.cpp,
InteractablePropManager.cpp, PropInitialContentDebugTests.cpp. Native coverage
checks default-off behavior, varied/reversed ranges, canonical/GPU data,
reproducible 0.5 via equal bounds, save/load and promotion/demotion continuity.

Next: finish native build and tests, then validate the actual placed bottles in
Lvl_FirstPerson using a fresh unsaved PIE session. Preserve original level
placements and saved progress. User assets were saved before restarting; map,
material and current gameplay saves are backed up under
Saved/PlacedBottleFill_20261112. Do not restore the previous task's older saves.

Build interruption: 11202's tool session ended before its log completed. An
incremental up-to-date result was insufficient: Windows rejected that DLL with
error 193, so no tests ran. A fresh 11203 link with build acceleration disabled
identified two corrupt generated object files (ShopManualCappingTests.cpp.obj
and ShopProcurementComponent.cpp.obj). They were moved to the checkpoint's
CorruptObjects directory and only those objects are being rebuilt. Source and
assets were not reverted. Do not accept a build solely from the module manifest
or process exit code; require DLL load and an actual automation report.
