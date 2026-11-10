# ATM, finite drawer and sale accounting

The user specified an initially empty register, ATM coin withdrawals debited from the shared bank account, and ATM deposits credited to it. This conflicts with the earlier rule that a cash sale also immediately credits that same shared wallet if the accepted physical coins remain spendable in the register.

## Approved interpretation

The user explicitly approved keeping sale cash in the register until ATM deposit. This supersedes automatic shared-bank credit on physical cash sales. Bank-funded expiry deductions remain mandatory or owed as described below.

Treat the existing shared wallet as the team's **bank account**. Physical coins are a separate form of the same money, with server-owned identities/custody, never an additional bank credit.

Example: a $500 bank balance withdraws $100 in coins, becoming $400 bank + $100 physical cash. A $240 cash sale accepted into the register becomes $400 bank + $340 cash before any expiry penalties. Depositing $200 becomes $600 bank + $140 cash. No step duplicates money.

- Customer tender and change transfer actual coin identities. A denomination cannot be returned unless its coin exists in the drawer. Cash counted for an incomplete transaction is escrowed and returned on cancellation; it never becomes bank money by being scanned.
- The completed invoice records sale revenue separately from bank balance. ATM deposit, rather than sale completion, credits bank funds for those same coins.
- Mandatory expiry deductions and the optional monetary compensation debit the bank once, retaining debt when it lacks funds. Replacement transfers one full, fresh, same-cola/same-capacity shop bottle without extra charge.
- ATM withdrawals and coin generation commit together. Deposits retire or bank the accepted coin identities and credit exactly once. Pooling/ISM promotion never creates monetary value. Coin identity/custody and completed ATM receipts persist; incomplete checkouts are canceled on load.
- Existing native and Blueprint sale-credit paths must both be reconciled with the chosen policy, including tests. Keeping physical cash while crediting bank automatically is not acceptable double accounting.

An alternative is automatic banking at sale completion, which must remove accepted physical currency rather than leave it spendable. That changes the physical register/deposit loop and needs explicit selection.

The user supplied all seven denomination meshes in `/Game/Assest/meshs/Coins` and added collision on 2026-11-10. The native currency recipes now use `/Game/Props/Currency/DA_Coin_{1,2,5,10,20,50,100}` and the corresponding `SM_{denomination}DCoin` mesh. Mesh appearance and gameplay scale remain configurable. No substitute coin meshes are used.
