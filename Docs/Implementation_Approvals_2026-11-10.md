# Implementation approvals — 10 November 2026

The user approved these policies during implementation. These supersede earlier open questions in the alignment notes.

- Any player may purchase equipment or take loans using the shared wallet.
- Incorrect cash/change prevents checkout completion.
- Canceled checkout goods return to shop inventory.
- Autosave every 60 seconds (configurable).
- Expired-product compensation remains owed when funds or stock are insufficient.
- Fixed prices retain quantity-based calculation and the symmetric temperature discount capped at 20%; player overrides are prohibited.
- Extend persistent instance state with sealed pressure, explicit bottle/cap relationships, and a separate liquid creation timestamp while retaining stable ISIDs.

## Fragment architecture

The user specified a pooled Fragment Destruction Actor containing the assembled original static-mesh pieces. Each piece has a Product Definition and its own permanent ISID before destruction. On destruction its original component simulates independently and reports settlement to Prop Manager. Its existing ISM slot moves from the reset location to the final gameplay transform; its original component disables collision/physics and returns to its reset position. Each piece transitions independently. The original destruction actor returns to its pool only after every original piece has completed this transition. Returning the actor must not alter the settled pieces.

Subsequent interactions promote a settled piece to a dedicated Fragment ASM with the same ISID and move the existing ISM slot to its reset position. That Fragment ASM later writes back and returns to its own pool. The original destruction actor is not reused for subsequent interactions with those pieces. Destruction, identities, state transitions and authoritative transforms belong to the server. Resting ISMs and idle pooled actors/components avoid unnecessary ticking, collision, physics and replication. An example glass bottle has eight separately authored pieces; shared fragment Product Definitions are allowed.

The user's final example was truncated after its “All Fragments Settled” heading. The lifecycle rules above were supplied completely. Fragment mesh assets and production tuning remain authored content, not generated substitutes.

The user subsequently approved keeping glass/concrete fragment recipes configurable until they supply the fragment mesh assets.
