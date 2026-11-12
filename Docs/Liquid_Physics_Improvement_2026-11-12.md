# M_BaseLiquid motion and ground-impact correction

## Appearance follow-up — 12 November 2026

The user supplied a screenshot showing a tan bottle silhouette with only a bright liquid ring, then explicitly chose **Keep the amber tint**. The M_BaseGlass asset is byte-for-byte unchanged by this follow-up (including its original tint, 0.3 opacity and 0.2 roughness).

Inspection of the actual graph found that both final opacity outputs were driven by a meniscus-derived power chain instead of the full liquid mask. The final masked visibility now uses `MaterialExpressionComponentMask_1`, the R channel of the existing liquid simulation. Foam/color shading is separate from physical clipping. Two color/bubble lerps also used raw TwoSidedSign (-1/+1) as their alpha; a shared saturate face selector now maps it to 0/1, avoiding negative-weight extrapolation.

The planar UV helper used abs(NdotV), accepting rays facing away from the liquid surface. It now rejects those intersections and emits a surface-only foam mask. That previously unused mask gates the foam blend, preventing foam from covering the underwater walls. Its float3 UV/mask contract and existing inputs remain compatible. `Shaders/ColaLiquidSurfaceUV.hlsl` is the tracked source, and `Scripts/UpgradeColaLiquidMaterial.py` reproduces all graph repairs without duplicate helper nodes.

The material was saved and recompiled successfully through UE MCP, then inspected on the user's upright, tilted, sideways and inverted static previews from above and below. Liquid volume is now visible below the fill line; foam is absent from walls viewed from below and remains on the visible surface from above. Comparison image: `Saved/LiquidAppearance_20261112/After_Above.png`. Recovery copies are in `Saved/LiquidAppearance_20261112/Before`. The provisional multiply-only clipping gate was removed; final visibility directly uses the physical mask.

No C++ or authoritative gameplay state changed in this follow-up. The previous 51/51 native test result remains the 11198 baseline; the suite was not rerun for these material-only changes. Current shader compilation, visual orientation checks, Python syntax compilation and whitespace checks pass. A complete moving-bottle/remote-player appearance acceptance and large-population performance profile remain pending. The packaged 11193 demo is unchanged.

Unreal remains on `/Engine/Maps/Entry`, with PIE stopped. **The user has added preview bottle placements to this unsaved scene. Preserve them; do not blindly discard the whole Entry scene following older fixture-cleanup instructions below.** This follow-up did not save or change the user's level placements.

Validation in progress, 12 November 2026. This request follows the completed bottle resistance correction (Editor 11196, 48 native tests passing). Do not revert that correction or unrelated existing changes.

## Findings from the actual material and native code

Both M_BaseLiquid paths resolve to `/Game/Assest/Materials/ColaMaterials/Liquied/M_BaseLiquid`. The pasted velocity shader is the material's `MaterialExpressionCustom_3`. Its angular input receives degrees/second from C++, but its unbounded angular vector and squared spin were used directly alongside a unit gravity vector. Vertical velocity was also added to gravity. Large landing/tumbling inputs could cancel or invert the resulting normal. The shader has no history and generates continuous time-based motion from speed rather than acceleration. Its vortex sign raises the center. Its fill correction is not volume conservation.

The active shader's fill input is wired to a constant instead of the native material fill parameter. Existing surface shading uses a fixed normal, and its planar UV helper independently estimates the fill plane. The source material also contains older unused custom-node experiments, which are preserved.

## Implementation underway

- `FColaLiquidMotion` keeps bounded acceleration-driven spring motion and damped fluid spin only on active bottle actors. World-space surface normals always remain in the upper gravity hemisphere, including abrupt vertical landing impacts. Quaternion sampling avoids Euler wrap spikes. Teleports reset derivative history; visual dynamics remain separate from authoritative pressure, damage and content.
- Active bottles supply new optional material parameters while preserving existing velocity/level parameters and the main float2 mask/gradient output. Existing shading and planar UV nodes will use the same surface information.
- A closed-form cylindrical volume CDF and small CPU bisection determine the clipping offset. The cylinder is an approximation of the shaped/morphed bottle interior; it is not exact mesh-volume integration. Active results are cached until tilt/fill changes.
- ISM custom slots 0–3 keep their meanings. Slots 4–5 append a derived clipping offset and validity marker (8 bytes/instance), refreshed during existing data/transform updates. No new saved/network gameplay fields or per-frame idle motion updates are introduced.
- Cosmetic motion delays demotion briefly until it settles; an active resting bottle retries demotion afterwards. Fully resting ISMs use the same clipping solution without a MID or CPU oscillator.
- The tracked HLSL removes unbounded angular gravity, fixes vortex direction and radians, uses three movement-gated waves with per-bottle phases, and derives consistent surface shading. Resting bottles skip animated waves/vortex work. The graph remains in place.

## Recovery and pending verification

Source, scripts, docs and liquid material assets were copied to `Saved/LiquidPhysics_20261112/Before`; the saved material folder was backed up again in `BeforeEditorRestart`. Existing custom-node code/input snapshots are in `Inspection`. User saves were backed up immediately before native validation. All editor dirty assets were saved through UE MCP and the editor closed normally. Editor builds 11197 and 11198 succeeded. The material was upgraded in place (191 to 200 expression nodes); original unused experiments remain. Conflicting duplicate dimension defaults were aligned with the active mask node's existing height 150, radius 35 and base pivot 1, without resizing meshes.

**All 51 native tests pass on 11198 (47 clean, 4 warning-bearing).** New tests cover opposing acceleration response, stop/oscillation/settlement, hard ground impact plus tumbling, spin-up/spin-down, empty/full limits, angular wrap, frame rates and independent cylinder volume quadrature across orientations/fill levels. Worst independent sampling error was 0.0125 (1.25 percentage points); the sampling grid itself has finite resolution. The real saved quarter-full bottle test confirms matching ISM/ASM clipping planes, existing slot/identity retention and clean pooling after cosmetic settlement. Existing pressure/grab/shop tests remain passing.

The first commandlet graph update ran without a rendering backend, so its zero shader statistics were not treated as GPU verification. A second pass explicitly enabled commandlet rendering and **compiled PCD3D_SM6 successfully**: 352 worst-case pixel instructions, 367 vertex instructions, 3 samplers, 2 pixel texture samples and 3 vertex texture samples. These are compiler statistics, not a frame-time benchmark or a measured before/after comparison. No texture assets were added. The graph upgrade script's API/array-editing errors were fixed before any successful save; logs and backups are retained.

Reports: `Saved/Automation/LiquidPhysicsFull_11198/index.json`, `Saved/LiquidPhysics_20261112/MaterialUpgrade.json`, and `Saved/Logs/LiquidMaterialGpuValidation_11198.log`. An isolated unsaved Entry-map SIE fixture is being prepared with low/quarter/half/high fills, upright/tilted/sideways/inverted bottles, a real drop/tumble and sustained spin followed by stopping. It disables shop saves. Visual verification and measured large-population performance remain pending. Do not describe the visuals as accepted until observed. The older packaged 11193 prototype does not include these changes.

## Checkpoint after physical Escape stopped screen control

The user stopped Computer Use with physical Escape. No further desktop input or development validation was attempted after that stop. **PIE was already stopped.** Unreal remains open on the unsaved `/Engine/Maps/Entry` test scene. Do not save this temporary scene over the Engine asset or treat its fixtures as production content. The native source, HLSL, graph changes and glass usage fix are saved; there was no Git commit/push or new packaged build.

The live SIE fixture successfully ran real falling/tumbling and spin/stop cycles. Recorded landing samples remained upright (minimum sampled normal Z about 0.962), impact wave energy reached about 0.671 and decayed below 0.004, while fluid spin built to about 4.964 rad/s and decayed below 0.003 after stopping. These are parameter/physics observations, not a visual-quality acceptance verdict. Runtime samples and actor identities are in `Saved/LiquidPhysics_20261112/Visual/Runtime_BeforeGlassFix.json` and `Runtime.json`.

Viewport captures exposed an existing checkerboard fallback on the bottle shell: `/Game/Assest/Materials/M_BaseGlass` had `bUsedWithSkeletalMesh=false`. That material was backed up to `Before/M_BaseGlass.uasset`; the skeletal usage flag was enabled, recompiled and saved through UE MCP. Its existing translucency, colors and other art settings were retained. **The corrected shell/liquid visuals were not rechecked before Escape.** Do not claim the liquid appearance is fully accepted. The initial fixture launcher auto-closed; the second `-ExecCmds=py ...` launch stayed open and produced the live samples. Fixture shutdown wrote `Visual/Error.txt` when it queried a destroyed manager after PIE ended; this is a test-harness cleanup issue, not a gameplay failure, and needs a small cleanup before reusing that harness.

Resume with a fresh isolated Entry scene (discard only these test fixtures), correct the fixture's shutdown handling, run the visual drop/rotation/fill checks with the saved skeletal glass flag, then return the editor to the user's prototype map. Preserve existing user edits and the save backups. Large-population GPU frame-time profiling, real remote-client visual comparison and a rebuilt packaged demo remain pending.
