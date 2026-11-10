# Current development scope

Updated 10 November 2026 from the user's explicit instruction: the following assets are not currently part of the game and are to be ignored for now.

- PSO_BaseMesh
- PSO_FoodChopper
- PSO_FoodContainer
- PSO_FoodDrum
- PSO_GasCylender
- PSO_HeavyMetalDesk
- PSO_Patatos
- PSO_Plate
- PSO_Sink
- PSO_Stove
- PSO_StovePlate

These correspond to the eleven saved Blueprint packages under Content/Assest/Blueprints. The potato asset's saved name is PSO_Patatos.

Exclude their repair, migration, gameplay integration and dedicated validation from the active backlog and completion assessment. In particular, defer the previously approved PSO_FoodContainer migration; it is not a current development priority or gameplay blocker. The newer scope instruction takes precedence over older handoffs/proposals directing that migration next.

Preserve the assets and placed references. This is a scope exclusion, not an instruction to delete, migrate, disable, reparent or remove them from maps. Existing compile findings and raw audit evidence remain historical facts; excluded work must not be labeled completed. Shared native systems used by the active cola game remain in scope.

Applying this exclusion to the existing 108-Blueprint audit leaves 97 non-excluded Blueprint assets: 96 clean statuses, one animation warning, zero recorded compile errors. This is a filtered interpretation of the existing audit, not a new test run or proof of complete runtime functionality.

The next implementation priority is the active cola-shop path: authoritative scanner/checkout/wallet integration, material-correct bottle configuration, filling equipment, caps and the NPC shopping loop.
