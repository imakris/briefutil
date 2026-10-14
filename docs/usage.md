# Letter usage and configuration

For a first letter and a CLI example, see the [README](../README.md). Invoice
and credit-note generation has a separate [workflow and JSON contract](invoices.md).

## Desktop app

Select a sender profile, enter the recipient, subject, and body, and generate
the PDF. The settings window controls the template directory, page layout, and
PDF typography. The app remembers these settings and dark mode.

The sender profile selects English or German wording for closing lines, dates,
page-number footers, and generation errors. English is the default.

## Letter CLI options

Run `briefutil_cli --help` to see the available options. In a portable Windows
package, use `briefutil_cli.bat`.

| Option | Use |
| --- | --- |
| `--to TEXT` or `--to-file PATH` | Recipient block; the file is UTF-8 text. Without either option, the recipient is read from stdin. |
| `--subject TEXT` | Letter subject |
| `--body TEXT` or `--body-file PATH` | Letter body; the file is UTF-8 Markdown or plain text |
| `--profile ID` or `--profile-path PATH` | Select a profile by its `id` or load an exact JSON file; the default is the first discovered profile |
| `--template-dir PATH` | Sender-profile directory |
| `--output PATH` or `--output-dir PATH` | Exact PDF path or directory for a generated filename |
| `--layout NAME` | `din_5008_form_b` (default), `din_5008_form_a`, or `us_letter` |
| `--font-sans PATH` | Regular sans `.ttf` font |
| `--font-sans-bold PATH` | Bold sans `.ttf` font |
| `--font-sans-italic PATH` | Italic sans `.ttf` font |
| `--font-sans-bold-italic PATH` | Bold italic sans `.ttf` font |
| `--font-mono PATH` | Monospace `.ttf` font |
| `--body-size PT` | Body font size, `6..24` points |
| `--body-leading PT` | Body line leading, `6..36` points |
| `--header-scale PCT`, `--body-scale PCT`, `--footer-scale PCT` | Font scales, `50..200` percent |
| `--force` | Replace an existing explicit `--output` file |

Use `\n` for line breaks in `--to` and `--body` text arguments. Each pair of
alternative options is mutually exclusive. Letter options and `--force` do
not apply to [invoice mode](invoices.md).

## Data locations

On first use of an empty template directory, the app or CLI seeds default
sender profiles and a signature image. The default locations are:

```text
Windows: %APPDATA%/briefutil/templates/
Linux:   $XDG_DATA_HOME/briefutil/templates/ (or ~/.local/share/briefutil/templates/)
macOS:   ~/Library/Application Support/briefutil/templates/
```

The seeded files are:

- `Max Mustermann.json`
- `Max Mustermann, Mustermann AG.json`
- `mustermann_signature.png`

Generated PDFs default to `~/briefutil/output/`, which is
`%USERPROFILE%/briefutil/output/` on Windows.

Override the template or output location for one process with:

```powershell
$env:BRIEFUTIL_TEMPLATE_DIR = "C:/work/briefutil/templates"
$env:BRIEFUTIL_OUTPUT_DIR = "C:/work/briefutil/output"
```

`BRIEFUTIL_TEMPLATE_DIR` takes precedence over the desktop app's saved template
directory. CLI `--template-dir` and `--output-dir` take precedence over the
corresponding environment variables; `--output` chooses an exact PDF path.

An optional `output_dir.conf` file contains an output-directory path. The
app and CLI look beside the portable launcher first, then beside the actual
executable, then in the current working directory. The chosen directory must
already exist. `BRIEFUTIL_OUTPUT_DIR` takes precedence over this file.

For a [portable package](building.md#portable-windows-package), place
`output_dir.conf` next to the visible top-level `briefutil.exe`.

## Sender profiles

Sender profiles are JSON files in the active template directory. The main
fields are:

| Field | Value |
| --- | --- |
| `id` | Profile name used by `--profile` |
| `style` | `simple` or `commercial` |
| `sender_lines` | Array of sender address lines |
| `email` | Sender email address |
| `language` | `en` (default) or `de` for built-in wording and date format |
| `return_address_line` | Return address above the recipient block |
| `closing_phrase` | Custom closing line; empty uses the language default |
| `signer_name` | Name below the closing line |
| `signature_image` | Relative PNG asset name in the profile directory |

Commercial profiles can also define `top_rule_color` as an RGB array of three
integers in `0..255`, `footer_lines` as an array of strings, `signer_title`, and
`logo_image` as a relative PNG asset name. Image paths cannot be absolute or
contain `..`. An empty image name omits that image.

## Markdown support

The body field accepts plain text or Markdown. The implemented subset includes:

- paragraphs
- bold and italic, with asterisk and underscore variants
- headings
- bullet lists
- ordered lists
- horizontal rules (`---`, `***`, `___`)
- images
- tables

## Fonts

Leave font fields empty to use the bundled default TrueType fonts. TrueType
rendering preserves non-CP1252 scripts when the selected font contains the
needed glyphs.

The desktop settings window accepts explicit `.ttf` paths on all platforms
and installed TrueType font family names on Windows. The CLI accepts explicit
`.ttf` paths.

Both interfaces configure regular sans, bold sans, italic sans, bold italic
sans, and monospace fonts. Body size, line leading, and header, body, and footer
scales are also configurable; the CLI ranges are listed above.
