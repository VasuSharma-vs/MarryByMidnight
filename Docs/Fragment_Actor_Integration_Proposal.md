# Fragment actor integration proposal

Status: the user explicitly approved the temporary initial-fragment representation during the resumed implementation on 10 November 2026. The supplied glass assembly has eleven pieces in /Game/Assest/meshs/Bottle_Fragments_Mesh, referenced by BP_bottel_Fragments_Visioul_Socase. Concrete recipes remain configurable pending concrete meshes.

## Conflict with the existing runtime

Every active ISID currently points to exactly one pooled AActivePropActor and ASMID. The approved initial destruction actor owns multiple independently simulated static-mesh components. Assigning that one actor/ASMID to all fragment ISIDs would corrupt the existing one-to-one maps. Pretending the pieces are resting ISMs while their slots are parked would permit incorrect queries, promotion, saves and client rendering.

## Proposed extension

Append CompositeFragment to EPropRepresentation, preserving existing ISM and ASM values. Use it only for the original components during the first destruction sequence. A sparse manager binding maps each fragment ISID to its owning Fragment Destruction Actor and component index. Dedicated subsequent Fragment ASMs continue to use ordinary ASM representation and the existing class-specific pool allocator.

| Lifecycle stage | Canonical identity and rendering | Authority |
|---|---|---|
| Initial assembly | Allocate all fragment ISIDs and permanent ISM slots; park their slots; assembled components render the intact object | Server creates and replicates the recipe and ISIDs |
| Initial destruction | CompositeFragment binding owns each independently simulated original component | Server physics; bounded replicated pose batches |
| Individual settlement | Manager writes transform/runtime data to the same ISM slot, switches representation to ISM, then parks/disables that original component | Server commits each piece independently |
| Original actor pool return | Only after every original component has transitioned; remove sparse bindings and disable the original actor | Existing settled ISIDs are untouched |
| Subsequent interactions | Ordinary ASM record points to a dedicated AFragmentPropActor from its class-specific pool | Existing manager promotion/demotion protocol |

All existing representation-dependent query, transform, deletion, replication, batch and save paths must explicitly handle CompositeFragment. No fragment shares a logical ID or ASMID. Actor pool return never deletes the fragment ISM slot.

## Persistence

Save each component's current transform and unique runtime state by its fragment ISID. A save made during initial destruction restores those pieces as independent dedicated Fragment ASMs (or resting ISMs when already settled). It does not resurrect the original pooled actor or replay the destruction. Saving an intact assembled composite also needs its parent recipe/assembly state; the intact parent remains the bottle's canonical logical item until destruction commits. The parent is removed only after fragment allocation and registration have succeeded, so allocation failure preserves the bottle and contents.

## Acceptance checks

- Eight configured pieces receive eight unique ISIDs and permanent slots before separation.
- One piece can settle while seven remain in motion.
- Returning the original actor cannot alter settled pieces.
- Reactivating a settled piece uses a dedicated Fragment ASM with the same ISID and slot.
- Mid-destruction save/load preserves pieces without replaying impulses or duplicating them.
- Four-player tests cover late join, simultaneous interactions and independent settlement.

The approved extension is implemented by AFragmentDestructionActor, the manager's sparse original-component bindings, and AFragmentPropActor. Reliable manager deltas carry group IDs and component indices, including late-join snapshots. Bounded unreliable pose batches carry group ID, record version and sample time; clients reject stale or mismatched updates. The original carrier is a local pooled actor driven through the manager's channel, so it does not introduce a competing replicated actor channel.

The eight-piece counts above are illustrative. The supplied production assembly and native integration test use all eleven meshes. Mid-destruction saves normalize moving originals to dedicated ASM restoration, retaining transform and velocities without replaying scatter impulses. See Implementation_Continuation_2026-11-10.md for current validation and remaining work.
