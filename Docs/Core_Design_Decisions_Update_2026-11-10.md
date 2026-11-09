# Core Design Decisions — Update of 10 November 2026

The following six decisions were supplied by the user on 10 November 2026. They take precedence over conflicting earlier design documents. Implementation implications and remaining details are recorded separately in [Core Design Implementation Alignment](Core_Design_Implementation_Alignment.md).

## 1. Shared Team Wallet

The game will use a **Shared Team Wallet** system. All players in the multiplayer session will share the same balance. Any money earned from customer purchases will be added to the shared wallet, while expenses will be deducted from it.

The wallet balance will be managed and validated by the server to ensure consistent synchronization across all players.

## 2. Fixed Product Pricing

Players cannot manually set or modify product prices. Each product will have a **fixed selling price**, defined in its Product Definition.

The checkout system will automatically retrieve the product's configured price and calculate the total purchase amount.

## 3. Bottle Refilling and Product Expiry

When an empty bottle is refilled, the filling system will request the **Prop Manager** to add the appropriate product.

The Prop Manager will create or assign an **ASM (Active Static Mesh) or ISM (Instanced Static Mesh)** representation depending on the product's current interaction and physics requirements.

When the product is added, the Prop Manager will record its **Creation Timestamp** in the product's per-instance data. This timestamp determines when the newly filled product's expiry period begins.

The bottle's existing physical history, including its Strength, damage, and other persistent properties, must be preserved during refilling. Only the newly added product's content-related data and expiry lifecycle will be initialized.

## 4. Bottle Reusability Requirements

A bottle can be reused only when all the following conditions are satisfied:

- **Strength:** Greater than 70%.
- **Cleanliness:** The bottle must be clean.
- **Content Level:** The bottle must be completely empty.

If any condition is not satisfied, the bottle will not be eligible for reuse until the applicable requirement is met.

## 5. Auto-Save and Unfinished Checkouts

The game will include an **Automatic Save System** that periodically saves persistent gameplay progress.

However, unfinished customer checkout transactions will not be restored when a saved game is loaded.

When the game session closes, all incomplete checkouts will be considered canceled. Upon loading the saved game, these transactions will not resume.

Only successfully completed transactions will affect the persistent Shared Team Wallet balance.

## 6. ASM Pool Capacity Management

The game will be designed and optimized to prevent the **ASM (Active Static Mesh) Pool** from reaching its maximum capacity during normal gameplay.

The expected number of simultaneously active physics objects will be controlled through gameplay design, interaction limits, and efficient ASM-to-ISM conversion.

However, if the ASM Pool reaches full capacity, the **Prop Manager will dynamically expand the pool** by creating additional ASM components as needed.

New components will be added in small, configurable batches to minimize unnecessary memory allocation and performance overhead.

When active objects return to their inactive state, their ASM components will be reset and returned to the pool for reuse.
