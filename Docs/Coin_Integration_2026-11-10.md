# Physical currency integration — 2026-11-10

## Approved accounting

The shared wallet is the shop bank. Sale tender stays physical until deposited. A register begins with no coins. Only server-issued currency identities have monetary value; generating another instance of the same mesh does not create spendable money.

| Denomination | Mesh | Product asset |
| --- | --- | --- |
| $1 | `/Game/Assest/meshs/Coins/SM_1DCoin` | `/Game/Props/Currency/DA_Coin_1` |
| $2 | `/Game/Assest/meshs/Coins/SM_2DCoin` | `/Game/Props/Currency/DA_Coin_2` |
| $5 | `/Game/Assest/meshs/Coins/SM_5DCoin` | `/Game/Props/Currency/DA_Coin_5` |
| $10 | `/Game/Assest/meshs/Coins/SM_10DCoin` | `/Game/Props/Currency/DA_Coin_10` |
| $20 | `/Game/Assest/meshs/Coins/SM_20DCoin` | `/Game/Props/Currency/DA_Coin_20` |
| $50 | `/Game/Assest/meshs/Coins/SM_50DCoin` | `/Game/Props/Currency/DA_Coin_50` |
| $100 | `/Game/Assest/meshs/Coins/SM_100DCoin` | `/Game/Props/Currency/DA_Coin_100` |

Each definition disables consumption, expiry, temperature updates and shell damage. Source meshes are untouched. Recipes default to scale 0.04: imported diameters are approximately 68–148 cm, giving gameplay diameters about 2.7–5.9 cm. All seven user-added collision setups were inspected through UE MCP: two convex shapes per mesh, including a shape spanning the visible diameter, with the default collision trace mode.

## Native implementation

- `UShopCheckoutComponent` owns currency identities, custody, ordered immutable operations, finite payment allocations and the bounded replicated team cash total.
- ATM withdrawal preflights all recipes/collision and bank funds, creates real Prop Manager instances, then atomically commits the bank debit and currency registry. Failure releases generated props without a bank debit. Duplicate operation IDs cannot mint again.
- ATM deposit validates team ownership and absence of checkout escrow, retires the logical prop identities through the existing empty-slot architecture, and commits one bank credit. Deposited coins cannot be spent again. Existing expiry debt is collected from subsequent deposits.
- Checkout requires actual tender and change identities. It transfers customer tender to the team and team change to the customer; it does not credit the bank. Amount-only legacy payment entry points deliberately reject payment.
- `UShopCashRegisterComponent` queries physical tender/change boxes on the server. Coins removed from a tray stop counting. The scanner refreshes the trays immediately before completion.
- `UShopATMComponent` supports physical drop deposits and configured withdrawal output. `UShopPlayerInteractionComponent` validates player possession, world, distance and command order before ATM withdrawal.
- `UShopCustomerCashComponent` provides a server-owned visit identity and configurable finite payment budget. Its native services join a scanner and place physical payment once for a locked invoice.
- Autosave exports only committed ownership. Unfinished transactions/escrow are canceled on restore, customer cash is excluded from world snapshots, and team coins persist. Ordered cash operations validate denomination, origin, ownership transfers and retirement; world loading also cross-checks bank receipts and live coin props.

## Validation and integration status

Module 11116 passed all 23 native tests (22 clean, one with the existing `ABP_Unarmed` warning), including actual supplied-coin ATM conservation, withdrawal/deposit idempotency, a real world SaveGame/load, failed atomic rollback, missing recipe handling, owned-player ATM checks and updated checkout/expiry regressions.

A subsequent build added currency product-ID cross-checks, replicated physical cash totals and additional physical-volume/pooling and finite-drawer tests. Module 11118 passed 24 of 25 tests; the coin-volume test resolved all seven coins but failed demotion after an InMotion promotion. The final fixture correction now uses Drop/Unsettled before Settling. It compiled with the new native terminal UI/adapters in module 11119, but has not been rerun because the user explicitly paused development. See `PAUSE_CHECKPOINT_2026-11-10_SCANNER_CASH.md` for the exact tested/source distinction.

The new runtime components have not yet been wired into the production scanner/customer/player Blueprints. The existing scanner's float-price/sale-credit graph remains a prototype and must be replaced before calling the in-game checkout loop complete. A playable ATM placement, checkout UI, NPC checkout handoff and multiplayer end-to-end validation remain integration work. Native automation success does not certify these unfinished paths.
