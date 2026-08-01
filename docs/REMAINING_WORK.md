# Remaining work (post code-review)

This document tracks everything from the code review that was **not** landed in
the refactoring pass, with enough detail to action each item later. The pass
itself fixed the correctness and hygiene items; what remains is one large
architectural change, a set of elective/quality refactors, optional
documentation, and a few items that were deliberately decided against.

Status legend:

- **Deferred** — valid and intended, but not landed yet (reason given).
- **Elective** — valid but a design/quality preference; needs a go-ahead before
  churning the codebase.
- **Decided** — intentionally not doing it; recorded so it is not re-raised.

---

## What was already done (for context)

Each item below was implemented, independently reviewed by a different model,
built (GUI on and off), passed the full `ctest` suite, and committed on its own
branch. Branches are stacked; the tip (`long-token-wrap`) contains all six
briefutil commits. mark2haru carries one commit on `expose-layout-headers`.

| Area | What | Where |
| --- | --- | --- |
| mark2haru unblock | Promote `markdown.h` / `table_layout.h` to public headers | `mark2haru/include/mark2haru/` |
| Data integrity | Reject impossible dates; validate profile image paths; strict colour parsing | `core/src/brief_service.cpp`, `core/src/sender_profile.cpp` |
| GUI hardening | No-persist-on-init-fail; atomic image import; rename-cleanup warning | `app/proxy.cpp` |
| Layout safety | Table-row overflow guard; closing-block fit guard | `core/src/rich_text_layout.cpp`, `core/src/letter_builder.cpp` |
| Hygiene | Launcher buffer hardening; `validate_directory` rename; doc/README/guard fixes | `app/portable_launcher_win.c`, `app/proxy.*`, `README.md`, embedded header |
| Inline text | Whitespace-faithful inline layout (`foo**bar**` -> `foobar`) | `core/src/rich_text_layout.cpp` |
| Inline text | Wrap over-long tokens instead of overrunning the margin | `core/src/rich_text_layout.cpp` |

**Runtime-verification caveat:** the GUI-side changes (proxy hardening,
`validate_directory`) are compile-verified only. There is no automated GUI test
harness, so the desktop app was not exercised. Running the app to confirm PDF
generation, template-directory selection, and image import still behave
correctly is an outstanding follow-up.

---

## Deferred

### 1. Generate PDFs in-process; stop driving the CLI from the GUI

This is the largest remaining item and the review's headline. It is deferred
because it cannot be gated by the headless test suite — the GUI generation path
is only verifiable by running the desktop app — and because it introduces
threading and removes currently-tested code. Do it in an attended session where
the app can be launched and observed.

**Current state.** `Proxy::make_pdf` (`app/proxy.cpp`) generates a PDF by:

1. resolving the path to the companion `briefutil_cli` executable,
2. writing the recipient and body to temporary files,
3. launching `briefutil_cli` through `QProcess` with a 300 s timeout,
4. scraping the produced PDF path out of stdout via
   `briefutil_pdf_path_from_cli_stdout` (`app/cli_output.cpp`),
5. opening the file.

The build also deploys a copy of the CLI next to the GUI through the
`briefutil_cli_companion` target (`app/CMakeLists.txt`).

The core already exposes the in-process API the GUI needs:

```cpp
// core/include/briefutil/brief_service.h
briefutil::Generation_result
briefutil::generate_brief_pdf(const briefutil::Generation_request& request);
```

**Target.** The GUI builds a `Generation_request` from its own state and calls
`generate_brief_pdf` directly, off the UI thread.

**Work.**

- Build a `Generation_request` from `Proxy` state: profile snapshot, recipient,
  subject, body, `output_dir`/`output_path`, `overwrite_output`, theme +
  typography, layout, and date. (The GUI currently passes no date, so the
  service defaults to today; preserve that unless date input is added.)
- Run `generate_brief_pdf` on a worker (a `QThread` worker object, or
  `QtConcurrent::run` + `QFutureWatcher`). Marshal the result back to the UI
  thread and emit `pdf_generated(bool, QString)`, mapping
  `Generation_result_code` to a clear user-facing message. The structured result
  removes the need to parse human-readable text.
- Open the produced PDF with the existing `QDesktopServices` path.
- **Delete** the now-dead glue in the same change (governance: dead-code sweep):
  - `app/cli_output.h`, `app/cli_output.cpp`
  - the `briefutil_cli_companion` target and its `add_dependencies` in
    `app/CMakeLists.txt`
  - the temp-file plumbing (`write_utf8_temp_file` and friends) and the
    `QProcess` timeout in `app/proxy.cpp`
  - `tests/test_cli_output.cpp` plus its `add_test`, `target_sources`,
    `target_include_directories`, and `check`-target entries in
    `tests/CMakeLists.txt`
- Keep `cli/main.cpp` as a thin frontend over `generate_brief_pdf` (unchanged).
  After this change both frontends share one generator.

**Risks.**

- Threading correctness: Qt object thread affinity, cross-thread signal/slot
  connections, worker lifecycle. These compile cleanly but fail at runtime, so
  the app must be run to verify.
- Confirm `generate_brief_pdf` and the mark2haru render path are safe to call
  off the main thread and have no shared mutable state. Publishing the output
  file is already serialized per output target by an interprocess lock, and
  each run stages into a uniquely named file.

**Acceptance.**

- GUI-on build clean; full `ctest` green (minus the deleted `test_cli_output`).
- Launch the app: generate a PDF, confirm it opens and matches CLI output;
  confirm error cases (output exists without `--force`, invalid font config)
  surface in the UI; confirm no companion CLI binary is required at runtime.

**Subsumes** review items #10 (fragile stdout parsing — disappears with
`cli_output` removal) and #21 (per-generation process startup cost — eliminated).

---

## Elective (need a go-ahead before churning)

These are valid observations but design/quality preferences. They are larger
churn for stylistic or architectural benefit; confirm which are wanted.

### #24 Unify the public namespace

Move the core model types into `namespace briefutil` (it is currently mixed:
`brief_service.h` is namespaced, the rest are global). Affected headers:
`document_model.h`, `letter_builder.h`, `letter_layout_spec.h`, `localization.h`,
`pdf_measurement.h`, `pdf_renderer.h`, `sender_profile.h`,
`sender_profile_schema.h`, `typography_config.h`. Update all consumers (app, cli,
tests). No compatibility aliases (house rule: one canonical way). Mechanical but
sweeping; land as one isolated commit; build-gateable.

### #25 Table-driven CLI parser

`cli/main.cpp` is a ~180-line manual `if/else` chain. Convert to a table of
`{name, takes_value, apply}` entries or `QCommandLineParser`, centralising
help/validation. Gateable via `test_cli`. (Note: the CLI has no `--date` option
today; if date support is wanted, add it here.)

### #20 Document-level text-measurement cache

`layout_runs` (`core/src/rich_text_layout.cpp`) caches word widths per paragraph
only. Hoist it into a per-generation measurement context keyed by
(font, size, style, text) so repeated words and table-cell text are not
re-measured across the document. Performance; needs care; gateable. Pairs well
with a shared per-generation measurement/typography/layout context threaded
through `build_letter` and the renderer.

### #34a Hide seed data behind a function

`core/include/briefutil/default_profiles.h` exposes default-profile JSON as
public inline `const char*` constants. Move it behind a function (e.g.
`std::span<const Default_profile> default_profiles()`) so seed data is not part
of the public surface.

### QML consolidation

Extract a theme singleton and shared components (button, form row, path picker,
section header, etc.) across `app/qml/main.qml`, `SettingsWindow.qml`, and
`SenderProfileWindow.qml` to remove duplicated styling. Optionally migrate the
many `Q_INVOKABLE` getters/setters on `Proxy` to `Q_PROPERTY` (+ `NOTIFY`) or a
`SettingsModel`, and reduce autosave churn in `SenderProfileWindow.qml`
(longer debounce / suspend the watcher during self-initiated writes).

### Profile editing as a C++ model

Move profile form state, autosave, validation, and id/rename management out of
`SenderProfileWindow.qml` into C++ (`ProfileRepository` + `ProfileEditorModel`),
leaving the QML declarative. Larger refactor.

### Schema-driven profiles

Extend `sender_profile_schema.h` into the single source of truth that drives
JSON load/save, the QML form, CLI validation, docs, and tests, so adding a
profile field is a one-place change.

### Larger layout abstraction (optional rewrite)

The review suggested a uniform `Layout_box` protocol
(Paragraph/Heading/List/Table/Image/Signature/Footer/Rule) and reusable
primitives (LineBreaker, BlockLayouter, PageCursor, TableLayouter,
FooterLayouter, ClosingLayouter) to reduce special-casing in
`rich_text_layout.cpp`. Worthwhile only if more Markdown/layout features are
planned.

### Documentation set

The review proposed several docs. None are written yet:
`ARCHITECTURE.md`, `DOCUMENT_MODEL.md`, `PROFILE_SCHEMA.md`,
`LAYOUT_AND_MARKDOWN.md`, `FONT_AND_TEXT.md`, `BUILD_AND_PACKAGING.md`,
`TESTING.md`, plus header-level contract comments on `Generation_request`,
`Generation_result`, `Sender_profile`, `letter_layout_spec_t`, `Layout_result`,
and `render_pdf`.

---

## Decided (intentionally not doing)

- **#17 Pin the mark2haru dependency — rejected by design.** mark2haru exists to
  serve briefutil (its primary consumer) and the two are co-developed.
  briefutil's `FetchContent_Declare(mark2haru ... GIT_TAG master)` is
  intentional and must keep tracking `master`; it is never pinned. Locally,
  builds use the sibling checkout via `BRIEFUTIL_MARK2HARU_SOURCE_DIR`.
- **#9 Reject blank recipients — keep current behaviour.** Empty explicit
  recipients are accepted on purpose (there is a test asserting it). Not changed.
- **#5c Validate the profile `language` field — not pursued.** Cosmetic;
  `normalize_language` already maps unknown values to `en` at generation time.
- **#32 Local files committed — not a real issue.** `.claude/settings.local.json`
  and `.qtcreator/` are gitignored and untracked.
- **#33 README images missing — not a real issue.** `sample_screenshot.png` and
  `example_output.png` are present at the repo root.
