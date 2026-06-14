# Merge fix plan for `docs-remaining-work`

Date: 2026-06-14

## Decision

Initial review decision: do not merge `briefutil/docs-remaining-work` to
`master` until the blockers below are fixed.

The branch builds and the automated test suite passes, but review found a
save/load invariant regression in the changed profile-validation path. A simple
profile can persist a hidden invalid `logo_image`; the stricter loader then
rejects that profile during later discovery.

`mark2haru/expose-layout-headers` was reviewed separately and fast-forwarded to
`mark2haru/master` because its header move is mechanically sound and tested.

Implementation status: completed on this branch. The blockers below were fixed
and covered by regression tests before the final merge review.

## Branch contents

The `briefutil` branch contains:

- sender profile image/color validation and invalid date rejection
- GUI proxy hardening for template directories, image imports, and profile saves
- layout overflow guards for too-tall table rows and closing blocks
- inline whitespace preservation and over-long token wrapping
- Windows portable launcher bounds hardening
- regression tests for the core/layout changes
- `docs/REMAINING_WORK.md`

## Blocking fixes implemented

### 1. Align `logo_image` save/load invariants

Problem:

- `load_sender_profile` now rejects invalid `logo_image` for all profile styles.
- `Proxy::save_sender_profile` only validates `logoImage` for commercial
  profiles, but still writes the field for simple profiles.
- `SenderProfileWindow.qml` ignores `logoImageOk` while the profile is simple,
  and the logo field is hidden in that mode.

Repro:

1. Edit a profile as commercial.
2. Enter an invalid logo path.
3. Switch the profile back to simple.
4. Let autosave run.
5. Restart or rediscover profiles.

Expected: the profile remains loadable.

Recommended fix:

- Treat commercial-only GUI fields as non-persistent when saving a simple
  profile. In `Proxy::save_sender_profile`, clear `updated.logo_image` for
  simple profiles before saving.
- Keep validating `logo_image` for commercial profiles.
- Keep the loader strict so stored JSON cannot reference absolute paths,
  traversal paths, or non-PNG assets.
- Consider also clearing other commercial-only fields on simple save only if
  product behavior should discard them; the merge blocker is specifically the
  `logo_image` field because it is now load-validated.

Regression coverage:

- Added `test_proxy_sender_profile`, a GUI-enabled test target guarded by
  `BRIEFUTIL_BUILD_GUI`.
- The test saves a simple profile through `Proxy::save_sender_profile` with a
  stale invalid `logoImage`, verifies the saved JSON clears `logo_image`, and
  verifies the strict loader can reload it.
- The same test verifies commercial saves still reject an invalid logo.

### 2. Reject partial date tuples

Problem:

- `generate_brief_pdf` only validates when year, month, and day are all
  positive.
- `localized_date` treats any component `<= 0` as "use today".
- A partially specified date such as `year=2026, month=13, day=0` silently
  renders as today's date instead of failing.

Recommended fix:

- Define the date contract as either completely unset or completely valid.
- In `generate_brief_pdf`:
  - if all three components are `<= 0`, keep the current "today" fallback;
  - if exactly some components are set, return `INVALID_REQUEST`;
  - if all are set, require `QDate(year, month, day).isValid()`.

Regression coverage:

- Extended `test_brief_service` with partial-date cases:
  - year set only
  - year and month set, day unset
  - invalid month with day unset
  - all components unset still succeeds using the existing fallback contract

### 3. Normalize tab whitespace in inline layout

Problem:

- The new inline layout preserves source spacing correctly for ASCII spaces, but
  tabs are still treated as part of an unbreakable token.
- That makes text from files or CLI input with tabs wrap differently from normal
  whitespace.

Recommended fix:

- Replace direct `' '` checks in `layout_runs` with a small breakable-whitespace
  predicate, initially handling at least space and tab.
- Emit a single rendered space for any run of breakable whitespace, preserving
  the current "no synthesized spaces between adjacent styled runs" behavior.

Regression coverage:

- Extended `test_letter_builder` inline whitespace cases with tab-separated text
  and styled-run boundaries around tabs.
- Added a longer tab-separated sequence to prove tabs are wrap points and are
  not emitted into rendered spans.

## Non-blocking follow-up

For `mark2haru`, consider a later documentation/versioning pass:

- Document whether `<mark2haru/markdown.h>` and
  `<mark2haru/table_layout.h>` are supported low-level APIs.
- Add an installed downstream consumer smoke test that includes both headers.
- Document `Table_columns` invariants or enforce them defensively.
- Bump the project version before publishing a release that promises the new
  public headers.

These are not required for the local merge already completed into
`mark2haru/master`, but they should be handled before a packaged release if the
new headers are intended as stable consumer API.

## Verification before merging `briefutil`

Run from `C:\plms\bsd_licensed\briefutil`:

```powershell
cmake -S briefutil -B briefutil/build-review-mingw -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER=C:/Qt/Tools/mingw1310_64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/c++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/mingw_64 `
  -DBRIEFUTIL_BUILD_GUI=OFF
cmake --build briefutil/build-review-mingw --parallel
ctest --test-dir briefutil/build-review-mingw --output-on-failure
```

Also run a GUI-enabled build when Qt Quick is available, because the blocker is
in the GUI profile-save path. The automated GUI build should include
`test_proxy_sender_profile`.

Manual runtime smoke is still useful before a packaged release:

- create/edit a commercial profile with a logo
- switch it to simple and save
- restart or rediscover profiles and confirm the profile is still present
- generate a PDF from the edited profile

Merge only after the automated checks pass and `git diff --check master...HEAD`
is clean.
