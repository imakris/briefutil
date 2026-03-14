# Markdown Support Refactor Plan

## Goal

Add body-content markdown support to `briefutil` without replacing the native
libHaru PDF pipeline.

Target feature set:

- inline formatting: bold, italic, bold+italic
- headings
- bullet and numbered lists
- images
- tables

Out of scope for the initial implementation:

- raw HTML
- syntax-highlighted code blocks
- nested tables
- colspan / rowspan
- remote image URLs
- arbitrary full-Unicode rendering beyond the current font/encoding limits


## Current State

The current pipeline is:

`QML input -> Proxy::make_pdf() -> build_letter() -> Document -> render_pdf()`

The important limitation is that the body is still treated as plain text:

- [`briefutil/letter_builder.cpp`](briefutil/letter_builder.cpp) wraps a single
  `input.body` string and emits one-font `Text_block`s.
- [`briefutil/document_model.h`](briefutil/document_model.h) only models
  positioned `Text_block`, `Line_segment`, and `Image_block`.
- [`briefutil/pdf_renderer_haru.cpp`](briefutil/pdf_renderer_haru.cpp) renders
  uniform text blocks, not mixed inline styles.

That is enough for plain letters, but not for markdown with inline formatting or
tables.

Plain text must continue to work after this change. Plain text is valid markdown,
so a body with no markdown syntax should render identically to the current
plain-text path.


## Recommended Architecture

Keep libHaru as the rendering backend.

Do not make the PDF renderer parse markdown.

Instead, introduce a new middle layer:

`markdown text -> parsed body blocks -> layout engine -> positioned Document -> PDF`

This keeps responsibilities separated:

- parser: markdown semantics
- layout engine: wrapping, spacing, pagination
- renderer: draw already-positioned primitives


## Parser Choice

Recommended initial approach: a hand-written parser for a constrained markdown
subset.

Reasons:

- the supported feature set is intentionally small
- zero third-party dependency and zero build integration overhead
- easier to debug in a business-letter app
- simpler to constrain than a full CommonMark/GFM engine

Supported subset for the first implementation:

- paragraphs
- `*italic*`
- `**bold**`
- `***bold italic***`
- ATX headings (`#`, `##`, `###`)
- bullet lists
- ordered lists
- markdown images
- pipe tables in a constrained GFM-like form

Fallback option: `md4c`

- acceptable if the hand-written parser becomes messy once tables are added
- much lighter than `cmark-gfm`

Recommendation: do not start with `cmark-gfm`. It is more capability and build
surface than this app needs.


## Refactor Overview

### 1. Add a body content model

Create a compact semantic model for the letter body before final PDF
positioning. This should be a flat list of body blocks, not a second full
document hierarchy.

Suggested new file:

- `briefutil/body_content_model.h`

Suggested types:

- `Inline_style`
- `Text_run`
- `Paragraph_block`
- `Heading_block`
- `List_block`
- `Image_content_block`
- `Table_block`
- `Table_row`
- `Table_cell`
- `Body_block`

Example shape:

```cpp
enum class Inline_style
{
    normal,
    bold,
    italic,
    bold_italic,
};

struct Text_run
{
    std::string text;
    Inline_style style = Inline_style::normal;
};

struct Paragraph_block
{
    std::vector<Text_run> runs;
};

struct Heading_block
{
    int level = 1;
    std::vector<Text_run> runs;
};
```

This model should represent body semantics, not page coordinates.


### 2. Add a markdown parser adapter

Create a parser module that converts markdown text into the body content model.

Suggested new files:

- `briefutil/markdown_parser.h`
- `briefutil/markdown_parser.cpp`

Responsibilities:

- parse the input markdown
- map markdown syntax to `Body_block` / `Text_run`
- reject or ignore unsupported constructs in a predictable way

Initial supported constructs:

- paragraphs
- emphasis / strong emphasis
- headings
- bullet lists
- ordered lists
- images
- GFM tables

Initial unsupported constructs should degrade clearly:

- raw HTML: ignore or treat as plain text
- unsupported table complexity: reject with a clear error or flatten to plain text


### 3. Add a rich layout engine

This is the main new subsystem.

Suggested new files:

- `briefutil/rich_text_layout.h`
- `briefutil/rich_text_layout.cpp`

Responsibilities:

- measure mixed-style text runs
- wrap lines across runs
- compute paragraph/list/table heights
- place blocks into the DIN body area
- paginate across pages
- emit final positioned PDF primitives

This module should consume:

- page frame constraints
- typography rules
- `std::vector<Body_block>`

And produce:

- positioned `Page_element`s for insertion into `Document`


### 4. Extend the final PDF render model

The current final render model is too coarse for mixed inline formatting.

Recommended change in [`briefutil/document_model.h`](briefutil/document_model.h):

- keep `Line_segment`
- keep `Image_block`
- replace or supplement `Text_block` for rich body layout with positioned spans

Suggested additions:

```cpp
struct Text_span
{
    float       x_mm;
    float       y_mm;
    std::string text;
    Font_id     font;
    float       size_pt;
    Color       color;
};
```

Then:

- paragraphs become many `Text_span`s
- tables become `Text_span`s plus `Line_segment`s
- images continue to use `Image_block`

Keep existing `Text_block` if it is still useful for simple one-style regions
such as sender block, address block, footer, and subject.


### 5. Extend font support in the renderer

The renderer currently only distinguishes:

- `sans`
- `sans_bold`

To support markdown formatting properly, extend [`briefutil/document_model.h`](briefutil/document_model.h)
and [`briefutil/pdf_renderer_haru.cpp`](briefutil/pdf_renderer_haru.cpp) to support:

- `sans`
- `sans_bold`
- `sans_italic`
- `sans_bold_italic`

Map those to libHaru base fonts:

- Helvetica
- Helvetica-Bold
- Helvetica-Oblique
- Helvetica-BoldOblique

This is a small renderer change and should happen before inline markdown layout.


### 6. Move body generation out of the plain-text path

Keep the surrounding letter builder logic in place:

- sender block
- return-address line
- recipient block
- date
- subject
- closing
- footer

Only replace the current body generation section in
[`briefutil/letter_builder.cpp`](briefutil/letter_builder.cpp):

- today: `wrap_text(input.body, ...)`
- target: `parse markdown -> layout rich content -> append positioned elements`

That keeps the DIN letter structure stable while upgrading only the body system.


## Implementation Phases

### Phase 1: Fonts, body model, and parser

Implementation steps:

1. Add italic and bold-italic values to `Font_id` and load the matching
   libHaru fonts in `pdf_renderer_haru.cpp`.
2. Update measurement helpers to accept and measure the new font ids.
3. Add `briefutil/body_content_model.h` with the body block and inline type
   definitions.
4. Add `briefutil/markdown_parser.h` and `briefutil/markdown_parser.cpp`.
5. Parse plain text into a single paragraph block and parse the supported
   markdown subset into body blocks and runs.
6. Add `briefutil/test_markdown_parser.cpp` and verify that plain text still
   parses as a single paragraph.

Outcome:

- markdown can be parsed into a constrained internal representation
- plain text still parses cleanly as a single paragraph

Risk:

- low to medium


### Phase 2: Layout, inline formatting, lists, images, and UX

Implementation steps:

1. Add `briefutil/rich_text_layout.h` and `briefutil/rich_text_layout.cpp`.
2. Implement paragraph layout with mixed-style runs and line-based pagination.
3. Add heading layout with level-specific font size and spacing rules.
4. Add list layout for bullet and ordered lists with indentation.
5. Add block-image layout with fit-to-width behavior and no scale-up.
6. Wire the markdown path into `build_letter()` and replace the current
   plain-text body generation path.
7. Add `briefutil/test_rich_text_layout.cpp`.
8. Update the `Body` label or placeholder text in `main.qml` so markdown
   support is visible in the UI.
9. Extend `test_letter_builder.cpp` with markdown body tests.

Outcome:

- `**bold**`, `*italic*`, headings, lists, and images work
- users can type markdown directly into the existing body `TextArea`
- plain text remains valid and behaves as before

Risk:

- medium

This is the first milestone where markdown is genuinely useful.


### Phase 3: Tables

Implementation steps:

1. Extend the parser for constrained pipe-table syntax.
2. Implement column width computation using minimum width, preferred width, and
   proportional shrinking.
3. Implement cell text wrapping and row height computation.
4. Implement border rendering via `Line_segment`.
5. Implement page splitting at row boundaries only.
6. Add table cases to the parser, layout, and end-to-end tests.

Outcome:

- simple business-letter tables become usable

Risk:

- high relative to the other phases

Constraints for first table version:

- no colspan
- no rowspan
- no nested blocks inside cells beyond paragraphs and inline formatting


## Detailed Design Notes

### Inline layout

Do not let the renderer perform line breaking.

The layout engine should:

- measure each run
- build lines from runs
- emit positioned `Text_span`s

That keeps pagination deterministic and testable.


### Pagination

Pagination should remain in the builder/layout stage, not the renderer.

For markdown blocks:

- paragraphs may continue across pages only at already-laid-out line boundaries
- lists may split across pages
- images should move as whole blocks
- tables should split only between rows initially

In the first version, do not attempt arbitrary mid-line or mid-fragment page
breaks. The layout engine should fully shape paragraph lines first, then paginate
whole laid-out lines.


### Table layout strategy

Initial algorithm:

1. determine available table width
2. compute each column's minimum width from the longest unbreakable token in that
   column plus cell padding
3. compute each column's preferred width from unwrapped content width plus cell
   padding
4. if the sum of minimum widths exceeds the available body width, reject the
   table with a clear user-facing error in the first version
5. if the preferred widths fit, use them
6. otherwise shrink columns proportionally, but never below their minimum width
7. wrap cell contents to the resolved column widths
8. compute each row height from the tallest wrapped cell
9. draw borders and cell content

If a row does not fit on the current page:

- move the whole row to the next page

Do not attempt intra-row splitting in the first version.

This first table version is intentionally conservative. If a table cannot fit
within the letter body area under these rules, fail clearly instead of silently
degrading the layout.


### Image handling

Restrict initial support to local PNG files.

This matches the existing renderer path and avoids format expansion work.

If later needed, JPEG can be added separately.

Initial image rules:

- markdown images are treated as block elements, not inline elements
- resolve image paths relative to the profile directory first
- preserve natural size if the image already fits the body width
- never scale images up
- scale down only when the natural width exceeds the available body width
- keep alt text in the parsed model for diagnostics or future UI use, but do not
  render it into the PDF in the first version


### QML / UX

The first implementation should keep the existing body input field:

- users continue to type into the existing `Body` `TextArea`
- no preview pane in the first version
- no plain-text / markdown mode toggle in the first version

Because plain text is valid markdown, a mode switch is not needed for initial
adoption.

Minimal UI change for discoverability:

- update the `Body` label or placeholder text to mention markdown support
- optionally add a short hint such as `Supports Markdown: **bold**, *italic*,
  lists, images, tables`

Do not scope a live preview into the first implementation.


### Unicode limitation

Current text rendering is still tied to WinAnsi in
[`briefutil/pdf_renderer_haru.cpp`](briefutil/pdf_renderer_haru.cpp).

That is acceptable for German business letters, but it is a real ceiling for
general markdown content.

Recommendation:

- accept this limit for the first markdown implementation
- plan a later follow-up for embedded TTF fonts and broader Unicode support

Do not mix that font migration into the first markdown feature wave unless it
becomes necessary.


## Testing Plan

### Parser tests

Add dedicated parser tests for:

- bold / italic / bold-italic
- headings
- bullet and numbered lists
- images
- GFM tables
- unsupported constructs

Suggested new file:

- `briefutil/test_markdown_parser.cpp`


### Layout tests

Add tests for:

- mixed-style line wrapping
- paragraph spacing
- list indentation
- image fit and placement
- table width allocation
- table row pagination
- plain text parity with the existing body path

Suggested new file:

- `briefutil/test_rich_text_layout.cpp`


### End-to-end tests

Extend [`briefutil/test_letter_builder.cpp`](briefutil/test_letter_builder.cpp)
to cover:

- markdown body with bold/italic
- markdown body with image
- markdown body with table
- multi-page markdown body


## Concrete File-Level Change List

Likely new files:

- `briefutil/body_content_model.h`
- `briefutil/markdown_parser.h`
- `briefutil/markdown_parser.cpp`
- `briefutil/rich_text_layout.h`
- `briefutil/rich_text_layout.cpp`
- `briefutil/test_markdown_parser.cpp`
- `briefutil/test_rich_text_layout.cpp`

Likely modified files:

- `briefutil/document_model.h`
- `briefutil/pdf_renderer_haru.h`
- `briefutil/pdf_renderer_haru.cpp`
- `briefutil/letter_builder.cpp`
- `briefutil/main.qml`
- `briefutil/CMakeLists.txt`
- `briefutil/test_letter_builder.cpp`


## Recommendation

Proceed with an incremental refactor, not a rewrite.

The current codebase is a good base for this work because:

- page generation is already native
- the DIN letter chrome is already separate from body content
- the renderer is small and understandable

The main missing piece is a richer body-content and layout layer between markdown
input and the final PDF primitives.
