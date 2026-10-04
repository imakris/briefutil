# Invoice generation

`briefutil_cli` renders an invoice PDF and a JSON receipt from a sales-order
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
| `unit_amount_minor` | Net price of the one licence, in integer minor units |
| `tax_amount_minor` | Explicit tax amount in integer minor units |
| `total_amount_minor` | Net price plus tax |
| `currency` | `EUR` |
| `tax_note` | Operator-confirmed explanation of the tax treatment |
| `payment_due_date` | `YYYY-MM-DD`, on or after the invoice date |
| `update_term_months` | Agreed update entitlement for a permanent-use licence |
| `desktop_slot_grant` | Agreed installation allowance |

The parser accepts other order-service metadata, including `sku`, without
interpreting it. The entire original input, including such metadata, is bound
by the receipt's SHA-256. Monetary inputs must be nonnegative exact JSON
integers no larger than `9007199254740991`; the net price must be positive and
the supplied total must equal net plus tax. The renderer does not infer tax
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
layout uses an A4 page, a logo banner, seller and buyer blocks, invoice
references, licence description, amounts and payment details. Long text flows
onto additional pages. Neither a signer's personal name nor a letter closing
is added.

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
