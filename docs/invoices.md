# Invoice generation

`briefutil_cli` renders an invoice or credit-note PDF and a JSON receipt from a sales-order
export and a local template. It runs without the Qt Quick desktop app or a
display server, using the same native PDF renderer and bundled fonts as letters.

## Workflow

1. Record the business customer, named licence recipient, edition, agreed
   tax amount and tax explanation in the order service.
2. Download the order export. Save the exact UTF-8 `content` bytes supplied by
   the service, without parsing, reformatting or appending a newline.
3. Generate the PDF with your local template and an explicitly chosen invoice
   number and date:

   ```powershell
   briefutil_cli --invoice-json order.json --invoice-template invoice-template.json `
     --invoice-number "EXAMPLE-2026-001" --invoice-date "2026-10-04" `
     --output invoice.pdf --receipt invoice.receipt.json
   ```

4. Review the document, then select the PDF and receipt in the order service.
   That service verifies their relationship to the unchanged order before
   recording issuance. Rendering alone does not issue an invoice in its ledger.
5. Send the reviewed document to the customer using the order workflow. Confirm
   payment only after it has cleared; the account service owns licence delivery.

The two output paths must be distinct and unused. Invoice mode deliberately
does not accept the letter CLI's `--force` option. Both files are staged before
publication. A filesystem failure or interruption between the two final renames
can leave a complete PDF without its receipt; keep that PDF, regenerate to new
unused paths and import the matching new pair. A failed run never records an
issued order or a payment.

## Order input

The JSON object's `version` is `1`. This is a separate invoice capability
contract; sender-profile JSON and the letter editor are unaffected.

| Field | Meaning |
| --- | --- |
| `order_id` | Order identifier, retained in PDF and receipt |
| `product_name` | Product description for the invoice's one licence line |
| `recipient_email` | Named licence recipient |
| `company_name`, `billing_address`, `country_code`, `vat_number` | Buyer details; address may contain newlines |
| `unit_amount_minor` | Net price after discount, in integer minor units |
| `list_amount_minor`, `discount_amount_minor` | Optional pair: original price and discount; their difference must equal `unit_amount_minor` |
| `tax_amount_minor` | Explicit tax amount in integer minor units |
| `total_amount_minor` | Net price plus tax |
| `currency` | `EUR` |
| `tax_note` | Operator-confirmed explanation of the tax treatment |
| `payment_status` | Optional `unpaid` (default) or `no_payment_due` for a zero-total invoice |
| `payment_due_date` | `YYYY-MM-DD`, on or after the invoice date when unpaid; ignored when no payment is due |
| `update_term_months` | Agreed update entitlement for a permanent-use licence |
| `desktop_slot_grant` | Agreed installation allowance |

The parser accepts other order-service metadata, including `sku`, without
interpreting it. The entire original input, including such metadata, is bound
by the receipt's SHA-256. Monetary inputs must be nonnegative exact JSON
integers no larger than `9007199254740991`; the original price must be positive and
the supplied total must equal net plus tax. A zero-total invoice requires the explicit
`no_payment_due` status and zero net, tax and total. It prints **NO PAYMENT DUE**,
without payment instructions or a payment deadline. It never asserts a payment
was received. Discounts reduce the supplied net price; the order service owns
the tax treatment and amount. The renderer does not infer tax
treatment, allocate invoice numbers or perform VAT-number verification.

## Local template

The template is a JSON object with `version: 1` and a `seller` object containing:

- `company_name`
- `registered_address` (newlines permitted)
- `vat_number`
- `company_number`
- `payment_instructions` (newlines permitted)

Each seller field is required. Keep actual company banking instructions in
your local template, outside source control. Optional `logo_image` names a
PNG asset relative to the template directory; absolute paths and `..` are not
accepted. Optional `banner_color` and `rule_color` use `#RRGGBB` notation. The
layout uses an A4 page and a logo banner. Native mark2haru tables position the
seller, buyer and invoice references, licence description and amount, and
payment details beside the totals. These tables have no gridlines; a subtle
rule separates the item from the payment and totals area. Amount columns align
at the right edge. Rows move to additional pages when necessary; a row taller
than a full page is rejected rather than clipped. Neither a signer's personal
name nor a letter closing is added.

The template file and order input are limited to 1 MiB each. Both are read as
JSON; buyer and order text is rendered as text rather than Markdown, so a
customer name cannot introduce images or formatting instructions.

## Receipt

The generated receipt contains `version: 1`, `order_id`, `invoice_number`,
`invoice_date`, `input_sha256`, `pdf_sha256`, `template_sha256` and the complete
`seller` snapshot. Digests are lowercase SHA-256 hex. `template_sha256` binds
the JSON file; `pdf_sha256` binds the rendered output, including the logo.

The receipt is an operator handoff, not a signature or proof of payment. The
order service must compare its order ID and exact exported-input digest and
apply its own issuance, authorization and uniqueness checks. Selecting the
local PDF together with the receipt lets the operator UI check the PDF digest
without storing or publishing the document in a cloud service.

Synthetic input and template examples are in `briefutil/examples/invoice/`.

## Reserved documents (version 2)

Version 2 renders the order service's frozen invoice or credit-note snapshot.
The service reserves its number and date; the renderer does not allocate a
series, issue an accounting document, calculate tax, decide refund rights or
confirm a payment. The version-1 manual workflow above remains supported.

```powershell
briefutil_cli --invoice-json document.json --invoice-template template.json `
  --output document.pdf --receipt document.receipt.json
```

Optional `--invoice-number` and `--invoice-date` values must match the reserved
values exactly. A conflicting override fails. The version-1 template remains
unchanged, and its complete `seller` object must equal the document's seller
snapshot. Changing a company detail requires a new service-approved snapshot;
changing the local template alone cannot silently alter an issued document.

The input has these fields:

| Field | Meaning |
| --- | --- |
| `version` | `2` |
| `document_id`, `order_id` | Frozen document and commercial order identifiers |
| `kind` | `invoice` or `credit_note` |
| `number`, `date`, `supply_date` | Reserved reference and ISO issue/supply dates |
| `currency`, `currency_minor_digits` | Currency code and decimal places, from 0 to 3; initial account orders use EUR and 2 |
| `seller` | The same five seller fields required by the local template |
| `buyer` | `name`, `billing_address`, `country_code`, `tax_id`; consumer `tax_id` may be empty |
| `lines` | Nonempty array of line objects described below |
| `totals` | `net_minor`, `tax_minor`, `gross_minor`, equal to the sum of their lines |
| `payment` | `status` (`unpaid`, `paid`, `no_payment_due`, or `credited` for a credit note), `due_date` (ISO date required when unpaid, otherwise date or null), `reference` (possibly empty) |
| `original_document` | Null for an invoice; credit notes require the original `document_id`, `number` and `date` |
| `correction_reason` | Empty for an invoice; required for a credit note |
| `tax_reporting` | Optional supplied VAT conversion: `currency`, `currency_minor_digits`, `tax_minor`, `fx_reference` |

Each line contains `line_id`, `original_line_id` (null for an invoice, required
for a credit), `sku`, `description`, positive integer `quantity`, and exact
nonnegative `unit_net_minor`, `discount_minor`, `net_minor`, `tax_minor` and
`gross_minor`. Monetary integers cannot exceed `9007199254740991`, preserving
exact values across JSON consumers. The renderer checks `quantity × unit_net_minor − discount_minor
= net_minor` and `net_minor + tax_minor = gross_minor`, including overflow.
Descriptions include the agreed licence or service terms supplied by the
order service; the renderer does not describe every SKU as a permanent licence.
An invoice with zero net, tax and total requires `no_payment_due` and prints
**NO PAYMENT DUE**. A positive-total document cannot use that status. Original
unit prices and discounts remain visible; a zero-total invoice does not become
a paid receipt. Credit notes retain positive reduction totals.

Each line also has a nonempty `taxes` array. A component has `tax_name`,
`jurisdiction`, `rate_ppm` (integer or null), `treatment`, `legal_basis`
(possibly empty), `taxable_base_minor` and `amount_minor`. A rate of 19% is
`190000` parts per million. Component amounts must sum to the line's tax.
The supplied taxable bases, rates, amounts and treatment are printed; the
renderer never recalculates tax or guesses that a zero amount is zero-rated.
Any FX amount is supplied with its conversion reference, not calculated from
today's rate or a bank settlement.

Credit notes use positive reduction amounts and print **TOTAL CREDIT**. They
identify the original invoice and reason. Their issue date cannot precede the
original invoice date. The account service must enforce remaining credit
balances, original line/tax/FX references and the original reporting period;
the local renderer cannot inspect that history. A credit note alone makes no
claim that a cash refund completed or a licence was cancelled.

The version-2 receipt contains exactly `version`, `document_id`, `order_id`,
`kind`, `number`, `date`, `input_sha256`, `pdf_sha256`, `template_sha256` and
`seller`. Existing digest, publication and authority boundaries are unchanged.
The account service must reject documents that do not match its reserved
snapshot and must not issue a duplicate direct customer invoice for a sale
whose seller is a merchant of record.

`document.json`, `credit-note.json` and `mixed-tax-document.json` are synthetic
format examples. Their illustrative taxes are not route-qualification advice
or authority to issue a real invoice.
