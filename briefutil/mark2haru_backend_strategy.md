# mark2haru Backend Strategy

## Purpose

This note describes the shortest technically correct path to using `mark2haru`
as the PDF backend for `briefutil`.

The core idea remains correct:

- do not route `briefutil` back through Markdown
- do not replace `briefutil`'s letter semantics, parser, or layout rules
- do reuse `mark2haru`'s PDF writing and TrueType text machinery

But the integration is **not** a simple render-time backend swap.
`briefutil` currently depends on libHaru during layout and pagination, not only
when writing the final PDF.

## What Is Already True

`briefutil` already has a useful drawing model:

- `Document`
- `Page`
- `Page_element`

That is the right target for a second backend.

`mark2haru` already has most of the low-level drawing primitives needed for
that target:

- positioned text drawing
- top-origin coordinates
- line drawing
- filled rectangles
- embedded TrueType Unicode output

So the high-level reuse direction is still:

- `briefutil` owns semantics and layout
- `mark2haru` provides PDF/font/drawing machinery

## What Is Not True Yet

The current seam is only a **drawing seam**, not a full backend seam.

Today `briefutil` performs layout with Haru-owned measurement functions:

- `measure_text()`
- `wrap_text()`
- `measure_png()`

Those functions now live in the shared measurement layer
(`core/include/briefutil/pdf_measurement.h`, `core/src/pdf_measurement.cpp`,
and Haru-specific helpers in `core/src/pdf_haru_support.h/.cpp`), and they are
called from:

- `rich_text_layout.cpp`
- `letter_builder.cpp`

That means line breaks, table widths, footer placement, closing fit, and page
breaks are decided before rendering, using Haru metrics.

If `briefutil` keeps measuring with Haru and only swaps the final renderer to
`mark2haru`, the output may look plausible but will be subtly wrong:

- line breaks can drift
- table row heights can drift
- page counts can drift
- closing/signature fit can drift
- footer and page-number placement can drift

So the real prerequisite is to decouple **measurement + rendering**, not only
rendering.

## Current Primitive Inventory

From `briefutil`'s `Page_element` model:

| `briefutil` element | `mark2haru` primitive | Status |
| --- | --- | --- |
| `Text_block` | `draw_text` plus backend-side wrapping | possible after measurement refactor |
| `Text_span` | `draw_text` | already maps cleanly |
| `line_segment_t` | `stroke_line` | already maps cleanly |
| `filled_rect_t` | `fill_rect` | already maps cleanly |
| `Image_block` | no current primitive | missing |

Color, line width, and font-style slot mapping are also straightforward:

- `SANS` <-> `Regular`
- `SANS_BOLD` <-> `Bold`
- `SANS_ITALIC` <-> `Italic`
- `SANS_BOLD_ITALIC` <-> `BoldItalic`
- `MONO` <-> `Mono`

`mark2haru`'s top-origin `y_top_pt` drawing API is also compatible with
`briefutil`'s top-left page coordinates after the usual mm->pt conversion.

## Hard Preconditions

These are not optional risks. They are concrete pieces of missing work.

### 1. Backend-Agnostic Text Measurement

`briefutil` needs a measurement API that is not owned by `pdf_renderer_haru`.

At minimum, the backend-selected measurement layer must provide:

- text width
- wrapped height
- line count
- wrapping behavior compatible with the backend renderer

On the `mark2haru` side, that measurement facility must not be tied to a
`PdfWriter` instance, to a specific page size, or to per-document writer
lifetime.
Font loading and font metric access need their own reusable context.

This must be wired into:

- `rich_text_layout`
- `letter_builder`
- page-number measurement
- return-line underline measurement

Until this exists, a second PDF backend is incomplete.

### 2. PNG Image Support in `mark2haru`

Current `briefutil` output depends on images for:

- signature image
- optional logo image
- body images

Current `mark2haru` has no image embedding or image drawing support.

So parity requires real PNG support inside `mark2haru`, including:

- loading image bytes
- creating PDF image objects
- placing/scaling them on the page

This is a prerequisite for real letter output, not a later polish item.

### 3. Font Configuration Compatibility

`briefutil`'s font model is per-slot and already public:

- each slot may be a base-14 PDF font name
- or a concrete `.ttf` / `.otf` path

Current `mark2haru` does not expose that model.
It searches for a fixed set of font filenames under one root directory and
embeds TrueType fonts only.

To serve as a `briefutil` backend, `mark2haru` must accept explicit per-slot
font configuration for:

- regular
- bold
- italic
- bold italic
- mono

The remaining policy question must be made explicit:

- if `briefutil` passes a base-14 name such as `Helvetica`, either reject it,
  or map it deterministically to a configured TrueType replacement

This is not a hypothetical corner case.
`briefutil`'s current default font family already uses base-14 names.

So Phase 1 must choose and document one concrete policy up front.
The recommended policy is:

- keep explicit `.ttf` / `.otf` values as-is
- map the known `briefutil` defaults (`Helvetica`, `Helvetica-Bold`,
  `Helvetica-Oblique`, `Helvetica-BoldOblique`, `Courier`) deterministically
  to the `mark2haru` font slots

That keeps the default caller path usable while still letting explicit TrueType
paths pass through unchanged.

### 4. Unicode-Safe File Paths

`briefutil` already supports Unicode output paths on Windows.

`mark2haru` currently reads and writes files through narrow `std::string`
paths. That is not an acceptable drop in behavior for the `briefutil`
integration path.

The `mark2haru` side must preserve Unicode-safe handling for:

- output PDF paths
- input image paths
- configured font paths

### 5. Library Boundary

Current `mark2haru` is built as an executable.
`briefutil` needs a linkable library surface.

That means:

- split reusable code into a library target
- keep the CLI as a thin wrapper on top

The reusable part should be:

- `pdf_writer`
- `ttf_font`
- a measurement context separate from `PdfWriter`
- backend measurement helpers
- document renderer helpers

The Markdown CLI should remain a separate frontend layer on top of that.

Two concrete loose ends also need to be handled:

- the current executable-only post-build font copy step should stay with the
  CLI target, not with the reusable library target
- the integration mode must be chosen explicitly: `add_subdirectory`,
  `FetchContent`, or installed package consumption

## Recommended Architecture

Do not introduce a grand new document model.
`briefutil` already has the right one.

Do introduce a minimal backend-selected API for the parts currently tied to
Haru.

A realistic target looks more like this:

```cpp
enum class Pdf_backend
{
    Haru,
    Mark2haru,
};

struct text_metrics_t
{
    float width_pt;
    float height_pt;
    int   line_count;
};

text_metrics_t measure_text(
    const std::string& text,
    Font_id font,
    float size_pt,
    float leading_pt,
    float max_width_mm,
    bool wrap,
    const Font_family_config& fonts,
    Pdf_backend backend);

std::vector<std::string> wrap_text(
    const std::string& text,
    Font_id font,
    float size_pt,
    float max_width_mm,
    const Font_family_config& fonts,
    Pdf_backend backend);

Render_result render_document_to_pdf(
    const Document& doc,
    const std::string& output_path,
    const Font_family_config& fonts,
    const Localization& loc,
    Pdf_backend backend);
```

The exact dispatch mechanism can be a backend enum plus switch, or a small
backend object.
The important point is shared signatures and shared call sites, not a specific
function-pointer pattern.

And PNG probing should be moved out of the Haru backend header into a shared
utility, because PNG dimension reading is not inherently Haru-specific.

Measurement should remain locale-agnostic.
Localized strings are produced upstream in `briefutil`, then measured and
rendered as plain UTF-8 text.

This keeps the seam small:

- shared document model
- shared font config
- shared localization
- backend-selected text measurement
- shared PNG probing
- backend-selected PDF rendering

## What Should Stay Unchanged

These pieces already express the application correctly and should remain in
place:

- `briefutil` markdown parser
- `briefutil` body content model
- `briefutil` rich text layout rules
- `briefutil` letter builder
- `briefutil` letter layout specs
- `briefutil` sender profile concepts

The point of the integration is to swap out the PDF/font backend, not to
rebuild application logic.

## `mark2haru` Refactor Scope

The useful internal split in `mark2haru` is smaller than a three-layer rewrite.
`PdfWriter` already is most of the drawing context.

The practical split is:

1. PDF/font core
   - `pdf_writer`
   - `ttf_font`
   - image support
   - measurement helpers
2. Frontends
   - current Markdown frontend and CLI
   - foreign `Document` renderer used by `briefutil`

That is enough.

## Revised Integration Plan

### Phase 0: Extract the Real Seam

Before adding a second renderer:

1. keep `text_metrics_t`, `measure_text()`, and `wrap_text()` in the shared
   `pdf_measurement` seam instead of regressing them back into Haru-only code
2. move `image_dimensions_t` and `measure_png()` into a shared utility
3. make `rich_text_layout` and `letter_builder` depend on backend-selected
   measurement functions instead of directly depending on Haru

This is the actual prerequisite.

### Phase 1: Make `mark2haru` Consumable

Refactor `mark2haru` so `briefutil` can link to it:

1. build a library target, not only an executable
2. expose explicit per-slot font configuration
3. preserve Unicode-safe path handling
4. add PNG image embedding/drawing
5. add a reusable measurement context separate from `PdfWriter`, with helpers
   that satisfy `briefutil`'s metric contract

Only after this phase does `mark2haru` have the minimum capability set needed
by `briefutil`.

### Phase 2: Add `pdf_renderer_mark2haru`

Now add the second backend on the `briefutil` side:

1. implement backend measurement using `mark2haru`
2. implement `render_document` over `Document` / `Page_element`
3. keep `pdf_renderer_haru` alongside it
4. make backend choice available in tests and dev tools first

At this point, `briefutil` can perform end-to-end layout and rendering against
either backend without mixing Haru measurement with `mark2haru` drawing.

### Phase 3: Verify Parity

Check parity on the actual things that matter:

- line breaks
- table row heights
- page count
- footer placement
- closing/signature fit
- logo/signature/body image placement
- font selection behavior
- Unicode output correctness
- Unicode output path handling

Avoid byte-exact PDF comparisons.
They are too brittle.

Instead, validate:

- test corpus output success
- cross-backend parity on the same corpus
- page counts
- selected geometric assertions
- presence of expected strings and images where practical

### Phase 4: Switch Default Only After Evidence

Only after Phase 3 is solid:

- switch default backend to `mark2haru`
- remove Haru later when it is genuinely unnecessary

## Testing Implications

Current smoke coverage is too weak for this migration.

The backend work should add or extend tests so both backends are exercised for:

- renderer smoke tests
- full letter-builder tests
- Markdown-to-letter PDF tests
- Unicode output path tests
- image rendering tests
- cross-backend element-stream parity checks on the same input corpus

The important comparison is not "does it emit a PDF header."
The important comparison is "does layout stay correct when measurement and
rendering use the same backend."

The cheapest direct drift detector is:

- build the same `Document` or letter corpus through both backends
- compare page counts
- compare element counts per page
- compare element positions and sizes within a small tolerance

## Bottom Line

The strategic direction is still:

- keep `briefutil`'s parser, layout, and letter semantics
- do not route through Markdown again
- reuse `mark2haru` as the lower-level PDF backend

But the truthful implementation plan is:

- first extract measurement out of the Haru backend
- then add missing `mark2haru` capabilities: PNG images, explicit font slots,
  Unicode-safe paths, and backend-compatible measurement helpers
- only then add `pdf_renderer_mark2haru`

That is the shortest path that is technically honest and unlikely to produce
subtle layout regressions.
