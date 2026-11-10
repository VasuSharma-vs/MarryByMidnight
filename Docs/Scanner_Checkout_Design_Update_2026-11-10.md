# Scanner, checkout and wallet — latest approved rules

The user's scanner specification and follow-up answers supersede older pricing/expiry rules for this implementation. The eleven excluded PSO assets remain outside scope.

- Automatic live scanning adds bottles inside the scanner volume and removes unsaved bottles when they leave. Save retains the individual ISIDs outside the volume. Entries can be removed before billing; one ISID cannot appear twice.
- Group display rows by product/content identity, retaining individual prices and expiry data.
- Normal base price is nominal portions multiplied by the configured portion rate. Charge for Clamp(ContentLevel, 0, 1); overfill adds no selling value or expiry penalty. Apply the temperature discount afterwards, symmetrically and capped at 20%.
- Keep detailed monetary values in cents. Round only the aggregate amount payable to the nearest whole dollar. Half-dollar ties round up for nonnegative bills. Record detailed total and rounding adjustment separately.
- Expired products are visible and removable before billing, but are not automatically prohibited from sale. Lock the original invoice before accepting final payment. Successful payment commits the immutable original sale.
- Each expired bottle sold creates one mandatory normal-base-price deduction, unaffected by either discount. Then the player chooses a fresh same-cola replacement for zero additional payment or another normal-base-price deduction. Original invoice items/amount remain unchanged; adjustments are recorded separately. Multiple expired bottles each create their own once-only case.
- Preserve unpaid obligations when the team wallet lacks money. They remain collectible from later funds. Pending, incomplete checkout cash does not enter persistent shared money.
- Any teammate may continue the shared transaction. The server validates actor access, cash identities, prices, stock, replacements, adjustments and retry IDs.
- Physical currency denominations are $1, $2, $5, $10, $20, $50 and $100. Coin appearance must stay configurable; the user will supply meshes. Temporary substitute coin meshes are not approved.

Additional approved answers:

- The register starts empty. Players withdraw coins from an ATM, deducting that amount from the shared shop bank account; depositing coins at the ATM adds their value to that account. The drawer therefore contains finite physical currency, not unlimited denomination dispensing.
- A compensation replacement must be full and fresh, with the same cola and nominal portion capacity.

The user approved ATM_Cash_Accounting_Proposal.md: physical sale coins remain in the register; only ATM deposits credit the shared bank for those coins. Expiry penalties and monetary compensation debit the bank or remain owed. Do not also credit bank funds at cash-sale completion.

Native scanner/cart, checkout and wallet services already exist. Extend those services and reuse Prop Manager identities/pooling; do not retain the production Blueprint's independent wallet credit path or implement a competing transaction system.
