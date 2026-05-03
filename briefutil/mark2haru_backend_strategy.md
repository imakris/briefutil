# mark2haru Migration Plan

## Goal

`briefutil` should use `mark2haru` as its only PDF/font engine.

`briefutil` remains responsible for the letter product:

- sender profiles
- DIN 5008 and US Letter layout specs
- recipient, subject, date, footer, fold-mark, logo, and signature placement
- body Markdown parsing and letter-specific rich text layout
- CLI, GUI, output-path handling, and user-facing messages

`mark2haru` becomes responsible for the lower-level PDF engine:

- TrueType font loading and text measurement
- Unicode PDF text output
- PDF object writing
- positioned text drawing
- line, rectangle, and image drawing
- PNG decoding and embedding

The implementation should not route `briefutil` letters through
`mark2haru::render_markdown_to_pdf()`. That renderer is useful for
`mark2haru`'s standalone Markdown tool, but it does not model letter semantics.

## Current State

The local `mark2haru` checkout already provides most of what `briefutil` needs:

- a linkable `mark2haru::mark2haru` target
- `mark2haru::Measurement_context`
- `measure_text_width()`
- `mark2haru::Pdf_writer`
- `draw_text()`
- `draw_png()`
- `fill_rect()`
- `stroke_rect()`
- `std::filesystem::path` based file IO
- bundled DejaVu font slots

The current `briefutil` integration does not build when enabled with:

```powershell
-DBRIEFUTIL_MARK2HARU_DIR=C:/plms/bsd_licensed/mark2haru
```

The adapter in `briefutil/core/src/pdf_renderer_mark2haru.cpp` expects writer
methods that current `mark2haru::Pdf_writer` does not expose:

- `set_fill_color()`
- `set_stroke_color()`
- `set_line_width()`
- `stroke_line()`

This is adapter drift, not a fundamental blocker. Current `mark2haru` accepts
color and line width directly in `fill_rect()` and `stroke_rect()`, and
`draw_text()` can be extended to accept color.

There is also a packaging gap: `mark2haru` copies bundled DejaVu fonts only for
its own CLI target. When consumed as a library, those fonts are not installed or
copied into `briefutil`'s runtime tree. The migration must make bundled fonts a
library/runtime asset, not a side effect of building the `mark2haru` CLI.

## Target Architecture

The target has one PDF engine and no backend switch.

`briefutil_core` links to `mark2haru::mark2haru` directly. The Haru renderer,
Haru measurement cache, Haru support helpers, and backend enum are removed.

The retained flow is:

```text
sender profile + letter input
    -> briefutil letter_builder
    -> briefutil document_t / page_element_t
    -> mark2haru-backed measurement and rendering
    -> PDF file
```

The document/page-element model stays in `briefutil` because it is the bridge
between letter layout and the PDF writer.

## Required mark2haru Changes

### 1. Add Colored Text Drawing

Add a text drawing overload or color parameter to `mark2haru::Pdf_writer`.

The needed shape is:

```cpp
void draw_text(
    double x_pt,
    double y_top_pt,
    double size_pt,
    Pdf_font font,
    const std::string& text,
    const color_t& color);
```

The existing `draw_text()` can delegate to the colored form with black.

`briefutil` needs colored text for:

- return-address text
- commercial footer text
- missing-image placeholders
- any future document element that carries `color_t`

### 2. Add Line Drawing or Use Thin Rectangles

Prefer adding a direct `stroke_line()` primitive to `mark2haru::Pdf_writer`:

```cpp
void stroke_line(
    double x1_pt,
    double y1_top_pt,
    double x2_pt,
    double y2_top_pt,
    const color_t& color,
    double line_width_pt);
```

This maps directly to `briefutil::line_segment_t`, including fold marks and
table borders. Thin rectangles are possible, but they are a weaker model for
general line segments.

### 3. Keep Measurement Context Independent

`mark2haru::Measurement_context` already has the right ownership shape.
`briefutil` should use it for all text measurement and wrapping decisions.

No `briefutil` layout code should use PDF-writer state for metrics.

### 4. Package Bundled Fonts as Runtime Assets

Move the bundled DejaVu fonts from a CLI-only post-build copy into a reusable
dependency asset path.

The required outcome is:

- `mark2haru` installs or exports its bundled font directory as package data
- `briefutil` copies those fonts into the normal app, CLI, install, and portable
  runtime layouts
- `briefutil` passes the runtime font directory to
  `mark2haru::Measurement_context`
- PDF generation works on a clean machine without relying on system fonts or a
  build-tree source path

The portable layout should have a stable font directory under
`briefutil_runtime`, for example:

```text
briefutil_runtime/
  fonts/
    DejaVuSans.ttf
    DejaVuSans-Bold.ttf
    DejaVuSans-Oblique.ttf
    DejaVuSans-BoldOblique.ttf
    DejaVuSansMono.ttf
```

The installed layout should use the same runtime lookup policy rather than a
compile-time absolute source path.

## Required briefutil Changes

### 1. Make mark2haru Mandatory in CMake

Replace the optional `BRIEFUTIL_MARK2HARU_DIR` path with a mandatory dependency.

Recommended local-development path:

- use `FetchContent_Declare(mark2haru SOURCE_DIR ...)` when a local checkout is
  configured
- otherwise use the canonical public repository URL

Then link:

```cmake
target_link_libraries(briefutil_core
    PUBLIC
        Qt6::Core
    PRIVATE
        mark2haru::mark2haru
)
```

Remove the libHaru, zlib, and libpng `FetchContent` blocks from `briefutil`.
`mark2haru` owns its own compression and PNG internals.

### 2. Remove Backend Selection

Remove these public concepts from `briefutil`:

- `Pdf_backend`
- `pdf_backend_available()`
- CLI `--backend`
- backend-specific defaults in public APIs
- conditional `BRIEFUTIL_HAS_MARK2HARU` paths

All generation should use mark2haru.

The removal must include every caller, not only CLI/core code:

- `briefutil/app/proxy.cpp`
- `briefutil/app/proxy.h`
- QML-facing validation or settings text that mentions backend behavior
- CLI help and parser
- tests that pass or assert backend-specific options

### 3. Replace Measurement Implementation

Keep the high-level `briefutil` measurement functions if they help isolate
layout code:

- `pdf_measurement_ready()`
- `measure_text()`
- `wrap_text()`

But implement them only with `mark2haru::Measurement_context`.

This keeps `letter_builder.cpp` and `rich_text_layout.cpp` stable while
removing the duplicate Haru path.

### 4. Retarget Font Configuration

The font model must match mark2haru's actual capabilities.

The new `briefutil` policy is:

- bundled DejaVu fonts are the default
- explicit configured fonts must resolve to TrueType `.ttf` files
- PDF Base-14 names such as `Helvetica` and `Courier` are not renderer inputs
- CFF-backed `.otf` files are not accepted unless mark2haru grows real CFF
  parsing and `/FontFile3` embedding first

This requires updates to:

- `default_font_family()`
- `looks_like_font_file()`
- `is_valid_font_config()`
- CLI `--font-*` help and validation
- GUI font validation and help text
- saved settings handling

Existing saved Base-14 values should be mapped to the bundled default font slots
or cleared so the bundled defaults apply. No compatibility layer is needed
beyond making the current configuration valid under the new model.

### 5. Replace Rendering Implementation

Replace `render_pdf()` with a mark2haru-only renderer over `document_t`.

Each `page_element_t` maps as follows:

| `briefutil` element | mark2haru call |
| --- | --- |
| `text_block_t` | wrap if needed, then `draw_text()` |
| `text_span_t` | `draw_text()` |
| `line_segment_t` | `stroke_line()` |
| `filled_rect_t` | `fill_rect()` |
| `image_block_t` | `draw_png()` |

Keep missing-image placeholder behavior in `briefutil` so localization stays
owned by the application.

### 6. Remove Haru Files

Delete these files after the mark2haru renderer is passing tests:

- `briefutil/core/src/pdf_haru_support.h`
- `briefutil/core/src/pdf_haru_support.cpp`
- `briefutil/core/src/pdf_renderer_haru.cpp`

Then simplify:

- `briefutil/core/src/pdf_renderer.cpp`
- `briefutil/core/src/pdf_measurement.cpp`
- `briefutil/core/include/briefutil/pdf_renderer.h`
- `briefutil/core/include/briefutil/pdf_measurement.h`
- `briefutil/core/include/briefutil/letter_builder.h`
- `briefutil/core/include/briefutil/brief_service.h`
- CLI help and parsing
- README backend/font documentation

## Implementation Phases

### Phase 1: Update mark2haru Primitives

Make the small writer API additions:

1. colored text drawing
2. direct line drawing
3. bundled-font install/export support
4. tests that prove colored text and line output affect the emitted PDF

The new primitive tests should inspect generated PDF content after stream
inflation or use another deterministic check. Header-only smoke tests are not
enough. The tests must fail if:

- colored text is emitted as black
- a line draw call emits no stroke operator
- line coordinates or width are ignored

Run mark2haru tests.

### Phase 2: Repair briefutil's mark2haru Adapter

Configure `briefutil` with the local mark2haru checkout and make the current
mark2haru adapter build.

At this point, do not remove Haru yet. The purpose of this phase is to establish
a passing mark2haru path before deleting the duplicate engine.

Run:

```powershell
cmake --build <mark2haru-enabled-briefutil-build> --target check
ctest --test-dir <mark2haru-enabled-briefutil-build> --output-on-failure
```

### Phase 3: Switch briefutil to One Engine

Remove the backend enum and Haru files, make mark2haru mandatory, and update all
callers to the single-engine API.

This phase should delete more code than it adds.

Run the full briefutil test suite, including GUI-proxy and CLI tests that prove
no removed backend option is still passed internally.

### Phase 4: Clean Documentation and Packaging

Update:

- README build requirements
- CLI help
- GUI settings/help text
- portable build script
- CMake packaging
- any strategy or architecture notes that still describe multiple PDF engines

Run the portable build path after the normal test suite passes. Verify the
portable output contains the bundled fonts and can generate a PDF with no
system-font dependency.

## Verification Checklist

The final implementation must verify:

- full `briefutil` `check` target builds
- full `briefutil` CTest suite passes
- full `mark2haru` CTest suite passes
- CLI PDF generation works
- GUI PDF generation works
- generated PDFs open
- Unicode output path test passes
- signature image rendering works
- commercial logo rendering works
- body image rendering works
- multi-page letters preserve page numbers and footer placement
- tables preserve row height and page-break behavior
- bundled default fonts work from normal, installed, and portable runtime
  layouts
- configurable font slots work with explicit `.ttf` paths
- `.otf`, PDF Base-14 names, and unresolved installed font names are rejected or
  migrated to the bundled defaults according to the new font policy

PDF byte-for-byte comparisons are not useful here. Use behavioral assertions,
render success, page-count checks, and targeted document-element geometry checks.

## Review Notes

This plan intentionally removes the duplicate PDF engine instead of preserving a
runtime backend choice. `briefutil` and `mark2haru` are both owned projects, and
the target design should reflect the desired architecture directly.

Reviewer findings should focus on correctness, build feasibility, missing
required work, and test gaps. Subjective preferences about keeping multiple PDF
engines, adding optional dependency modes, or preserving removed switches are out
of scope for this plan.
