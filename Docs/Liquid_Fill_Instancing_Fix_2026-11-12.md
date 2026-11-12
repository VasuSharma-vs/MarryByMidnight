# Liquid fill and instance-space checkpoint — 2026-11-12

## Completed and saved

Final build **11201** includes source-material comparison for compatible
cross-product ISM reuse beneath the runtime MID. Its continuity assertions and
full regression suite pass. The final saved material was verified without
rewriting it after the editor's close/save dialog.

The designer's 151-expression reorganized M_BaseLiquid is backed up under
`Saved/LiquidFillFix_20261112/UserReorganizedMaterial`. Meshes, player/customer
sizes, colors and appearance branches are preserved. Editor was saved and
closed normally before native compilation. Current module: 11201.

## Confirmed causes

- The active fill reroute was a constant 0.5, disconnected from the Liquid Level
  parameter written by the bottle actor. A separate instance-fill input also
  pointed at that constant.
- The clipping plane could still come from custom data slot 4 for the runtime
  instance's actual fill (often 1.0). A manual half fill and a cached full-bottle
  plane were mixed.
- Container height was 150 cm. The actual skeletal bottle body extends from
  approximately 0 to 186.58856 cm, with radius 37.65829 cm. This put the old
  half-fill plane at 75 cm instead of the body's center near 93.29428 cm.
- Generic pixel-stage World/Local transforms are not reliably individual-ISM
  transforms. UE 5.8 MaterialTemplate.ush has separate vertex/pixel paths and
  conditional instance transform availability. Instance space is explicitly
  supported by MaterialExpressionTransform.h; this is not a universal ban on
  Local-to-World conversion.

## Implementation

- `Shaders/ColaLiquidFill.hlsl` resolves the selected fill and matching plane
  together in the vertex stage. Manual previews use Liquid Level; managed
  runtime ISMs opt into custom data. Native ASM cached planes carry a fill stamp.
- `Scripts/FixColaLiquidFill.py` patches the existing graph, embeds tracked HLSL,
  corrects proxy dimensions, interpolates resolved fill/plane, and supplies
  explicit instance axes to the foam camera calculation.
- One shared dynamic liquid material per runtime ISM component selects custom
  data. Each active bottle selects its own dynamic material values. No per-frame
  CPU work or per-bottle material is added to resting instances.
- Slots 0–3 retain gameplay data; slots 4–5 retain derived plane and validity.
  Promotion/demotion preserve ISIDs and existing ISM slots.
- Existing spring inertia, waves, retained spin, pressure and physics are kept.

## Validation results

Builds 11199 and 11200 succeeded. The first full 11199 regression run passed
50/51 tests and caught material reconciliation discarding the shared liquid MID
on demotion. Both demotion and placed-actor conversion now restore the runtime
selector after authoritative material reconciliation, while product definitions
retain the original asset references. This is covered by the continuity test.
The 11200 GPU material commandlet and final read-only 11201 material verification
succeeded with **zero errors/warnings**. All three embedded HLSL sources and all
three custom-node input arrays still match the tested graph patch after the
editor's final save. The full 11201 native suite passes **51/51**: 48 clean,
3 warning-bearing, 0 failures,
0 not run. Existing warnings concern ABP_Unarmed's thread-unsafe punching query
and physics/collision setup in existing fixtures; the liquid continuity test is
clean. The float literal warning in that test was also corrected before 11201.
The earlier 11200 full run passed 47 clean and 4 warning-bearing tests.

The live unsaved SIE fixture rendered five real native ASMs and ten ISMs, all
ISM bottles sharing **one** UInteractableISMComponent. Half-filled bottles at
0/45/90/135/180 degrees have matching horizontal fill planes in both
representations. Empty/quarter/half/three-quarter/full fills display independently
in the same component. The camera faces +Y, so screen order is reversed: left
to right, the comparison rows are inverted/135/90/45/upright; the bottom row is
100/75/50/25/0 percent. Artist preview uses the parameter; runtime contents remain
authoritative. No gameplay contents were changed merely to make previews half full.

Evidence:

- `Saved/Automation/LiquidFillFull_11201/index.json`
- `Saved/Logs/LiquidFillBuild_11201.log`
- `Saved/Logs/LiquidFillMaterial_11200.log`
- `Saved/Logs/LiquidFillFinalMaterial_11201.log`
- `Saved/LiquidFillFix_20261112/FinalSavedMaterialVerification.json`
- `Saved/LiquidFillFix_20261112/FinalConnectionComparison.json`
- `Saved/LiquidFillFix_20261112/MaterialFix.json`
- `Saved/LiquidFillFix_20261112/Visual/Runtime.json`
- `Saved/LiquidFillFix_20261112/Visual/ASM_ISM_HalfFill.png`

The initial visual harness used positional Python Rotator arguments (roll rather
than pitch), and old Entry previews overlapped its layout. The final fixture uses
named rotation arguments and a cleared unsaved test world. No Engine Entry asset
was saved: its pre-existing designer preview bottles remain on disk unchanged.
PIE is stopped, and Unreal is back on `/Game/FirstPerson/Lvl_FirstPerson`.
ShopAutosave, NormalColaPrototypeAutosave and NPC_CulturalKnowledge saves are
restored and hash-verified against the pre-test snapshot. No Git commit/push or
packaged build was performed.

## Instance limitations and operating rules

- Instance position and axes originate in the vertex stage, with explicit
  Instance & Particle Space transforms. Pixel foam projection uses dot products
  against these axes rather than a component World-to-Local transform. No
  pre-skinned-position dependency was introduced (that UE path is incompatible
  with GPU skin cache).
- Raw per-instance data is consumed in the vertex resolver and passed through a
  VertexInterpolator. Active material values never overwrite shared instance
  contents. Runtime ASM/ISM switching is automatic, including demotion and
  placed-actor conversion; no artist toggle is required during gameplay.
- Cross-product reuse compares the underlying material asset, not the transient
  component MID pointer. Compatible materials can still share reusable slots;
  distinct appearance assets remain distinct.
- A shared material cannot store independent motion history for thousands of
  instances. Resting bottles use cached per-instance planes with no CPU motion
  tick; active bottles retain the existing native spring/spin history. Visible
  unsettled liquid continues to prevent premature pooling.
- The analytic preview solver runs in the vertex stage. A branch uses existing
  CPU-derived planes for valid runtime data. SM6 compilation reports 391 pixel
  and 859 vertex instructions, 3 samplers, 2 pixel texture samples, 3 vertex
  texture samples, 2 UV scalars and 14 interpolator scalars. These are compiled
  statistics, not measured GPU frame time or proof of large-population speed.
- Volume conservation uses a cylinder calibrated to the actual bottle body,
  rather than the exact irregular interior. Positive uniform scale and the
  existing rigid bottle geometry are the validated path; arbitrary nonuniform
  or mirrored scale and strongly deformed interiors need additional calibration
  and normal/volume handling. Existing player/NPC/bottle transforms were preserved.
- This change was tested in local SIE and native regression tests. Real remote
  client visual comparison, hardware drop/move acceptance, large-population GPU
  profiling and a new packaged build remain pending. The existing motion tests
  still pass; the fill comparison itself uses stationary bottles.

## Changed files and next work

The existing `M_BaseLiquid.uasset` now has 157 expressions (151 designer nodes
preserved plus six fill-resolver nodes). Tracked changes are in
`Shaders/ColaLiquidFill.hlsl`, `ColaLiquidSurface.hlsl`, `ColaLiquidSurfaceUV.hlsl`,
`Scripts/FixColaLiquidFill.py`, `ValidateColaLiquidFill.py`, the guarded historical
`UpgradeColaLiquidMaterial.py`, `InteractableInstancedStaticMeshComponent.h/.cpp`,
`ColaPropActor.cpp`, `InteractablePropManager.cpp`, and
`ColaLiquidIntegrationTests.cpp`. Appearance branches, colors, wave tuning and
amber shell art are preserved.

For manual previews, adjust **Liquid Level** with **Use Instance Liquid Data = 0**
(the default). Managed gameplay selects its correct source automatically. To
reapply the saved graph patch, use FixColaLiquidFill.py with the editor closed;
the historical upgrade script is now a compatibility entry point to this same
scoped repair and cannot replay its obsolete appearance rewiring.

Next validation: compare moving bottles on host/client during pickup, ground
impact, spin/stop and repeated promotion/demotion; profile a representative shop
population, then rebuild the packaged candidate. Do not alter the preserved
world scale or replace the designer's reorganized graph.
