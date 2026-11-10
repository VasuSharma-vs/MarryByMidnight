# Legacy food-container compatibility decision

Status: the user approved migration to the modern food-container behavior, preserving placements and quantity data. Work was subsequently paused at their request. Do not ask for this same architectural choice again. The original asset and twelve placed packages have not been changed; see PAUSE_CHECKPOINT_2026-11-10.md.

The broader saved-Blueprint check compiles 49 of 50 assets after restoring the historical NPCConsumableProp → ActivePropActor class redirect. Git commits b8d53c4 and de3bda7 establish that rename chain. The original Blueprint packages were not rewritten.

PSO_FoodContainer remains broken. Its asset registry has no ParentClass, NativeParentClass or GeneratedClass. Twelve external actors in Lvl_FirstPerson still reference it, so treating it as unused would be incorrect. BP_IPA_FoodContainer is a newer, compiling asset based on BP_BaseInteractableProp and the current native prop architecture.

An isolated in-memory copy was tested under PSO_BaseMesh. The original SHA-256 remained 7e3669ebae0b3ae457d9afb3d6388a716b042cbccc2bc565e725d9ed84605a1b. Reparenting alone is insufficient: the graph also requires missing Widget and Food Mesh properties and invalid event-delegate signatures. The copy did not pass compilation and was not saved as an accepted repair.

The required decision is whether to migrate these containers to the modern food-container interaction or reconstruct the legacy widget interaction. Migration must preserve the twelve placements/transforms and explicitly map the old Food amount state into the current quantity model. Reconstructing the legacy interaction needs its missing widget/component contract. Do not silently choose an inherited API or remove the placed actors.

The approved bottle fragment implementation does not depend on this legacy asset. Inspection and failed repair-copy artifacts are under Saved/ResumeBlueprintInspection; the production food-container package and placed actor packages remain untouched by the assistant.
