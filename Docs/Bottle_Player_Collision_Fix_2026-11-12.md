# Bottle resistance to player contact

## Request and preserved design

The designer requested heavy giant bottles which resist walking contact while continuing to react to strong impacts. Player, NPC and bottle geometry remain unchanged. This is a focused physics correction; the wider first-playable backlog is unchanged.

## Findings

- All six prototype empty/Normal-filled bottle definitions had a 5 kg gameplay mass.
- The player CharacterMovement used initial push 500, sustained push 750,000, mass-scaled touch impulses and repulsion 2.5.
- ISM wake-up applied a clamped impulse with `bVelChange=true`, assigning the same speed regardless of mass. The client/server existing-ASM path also bypassed mass.
- Physical-object ISM hits estimated impact from velocity alone, discarding incoming mass.

## Changes in progress

- Six prototype product assets now specify 80 kg. The authoring script retains that tuning on future upgrades. Older bottle definitions without a positive mass receive an editable 80 kg native fallback; explicit positive product masses retain priority. Pool mass restoration continues through the existing shared implementation.
- Ordinary bottle wake-up uses momentum (`AddImpulse` with `bVelChange=false`) on authority, client prediction and existing-ASM reconciliation. Other prop classes retain their legacy bounded velocity kick. Explicit launch/throw and pressure propulsion keep their existing semantics.
- Physics-body ISM contact preserves incoming momentum; passive character contact remains a small wake-up kick. Both peers use the same bottle calculation.
- The existing player input component applies modest CharacterMovement pushes on all peers: initial 100, sustained 2,500, unscaled touch factor 0.5 (maximum 50), repulsion 0.5. This also reduces passive pushes on other simulated props operated by this player. NPC movement and player mass remain unchanged.
- Bottles continue to block pawns when released and use the established held/carry collision exclusions. Collision is not disabled to hide the problem.
- Contact-based agitation is normalized to the original 5 kg tuning reference so extra mass does not amplify pressure/damage at the same velocity change. Explicit gameplay `ApplyBottleImpact` inputs retain their existing calibration.

## Validation and recovery

Before edits, Source/Scripts/Docs and prototype assets were copied to `Saved/BottlePawnPhysics_20261112/Before`. All dirty editor assets were saved successfully through UE MCP, PIE was confirmed stopped, and the editor closed normally. Native modules 11195 and 11196 compiled successfully. The first regression fixture (11195) failed because it manually simulated an inactive pooled actor and advanced multiple physics ticks within one engine frame; it was replaced with a real Prop Manager promotion and a latent test across separate frames.

The corrected targeted regression **passes on 11196**. Maximum passive character-contact speed was 15.572 cm/s in a gravity-free test; a 400 kg cm/s kick produced 4.983 cm/s at 80 kg and 79.733 cm/s at 5 kg. A 40,000 kg cm/s strong hit produced 498.333 cm/s. Client-side existing-ASM handling matched the authoritative gentle response at 4.983 cm/s. It checks blocking collision, unchanged geometry and mass-normalized agitation. Two fixture promotion collision-order warnings were recorded; no assertions failed.

**Full native regression: 48/48 passed (44 clean, 4 warning-bearing), zero failures or unrun tests.** This includes pressure rockets, cap identities, grabbing/placement/drop, mass restoration, scanner/physical cash/checkout, and Normal Cola production/reinvestment. Reports: `Saved/Automation/BottlePawnPhysicsOnly_11196/index.json` and `Saved/Automation/BottlePawnPhysicsFull_11196/index.json`; build log: `Saved/Logs/BottlePawnPhysicsBuild_11196.log`. The 11195 failed fixture report is retained for traceability. This is engine-simulated contact coverage, not a human hardware-input or real remote-network acceptance test.

No Git commit or push has been performed for this request. The pre-existing worktree is extensive; do not discard unrelated edits. Packaged prototype 11193 predates this correction and requires a new runtime build/cook to include it.

Final integration check: the editor reopened `/Game/NormalColaPrototype/Lvl_NormalColaPrototype` and its log confirms module `UnrealEditor-MarryByMidnight-11196.dll` loaded. UE MCP readback confirms all six saved product masses are 80 kg. The user's three non-automation save files were restored/hash-verified before reopening; only NPC cultural knowledge needed restoration after tests. Its hash changed again during normal editor startup, while the shop and prototype progress files still matched. Do not overwrite subsequent user gameplay saves with the test backup. Checkpoint source, scripts, docs, six product assets and module metadata are in `Saved/BottlePawnPhysics_20261112/Checkpoint_11196` with hashes beside that directory. Final human feel and real remote-player contact remain to be tested.
