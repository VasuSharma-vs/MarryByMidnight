# Fragment actor integration proposal

Status: awaiting approval of one architecture extension. The gameplay lifecycle itself is approved in Implementation_Approvals_2026-11-10.md. Fragment recipes remain configurable; the user will supply the glass/concrete meshes.

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

No implementation of this representation extension should proceed before approval because it changes the existing two-representation runtime contract.
