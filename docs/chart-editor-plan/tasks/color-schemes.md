# Field color schemes, including ones imported from iBMSC (2026-10-05 feedback)

[Back to the task card index](../tasks.md).

The user would like some iBMSC color schemes as choices. iBMSC ships eight themes:

| Theme | File |
| --- | --- |
| BMSE | `BMSE.Theme.xml` |
| BMSE mdk | `BMSEmdk.Theme.xml` |
| BMSE mdkp | `BMSEmdkp.Theme.xml` |
| Forme | `Forme.Theme.xml` |
| IIDX | `IIDX.Theme.xml` |
| MyO2 4K | `MyO2_4K.Theme.xml` |
| O2Mania | `O2Mania.Theme.xml` |
| Pomu | `Pomu.Theme.xml` |

They live under `misc/Data/` at [pBMSC `06aaf74`](https://github.com/Shimi9999/pBMSC/tree/06aaf7438dbca3c8a0611bd13d134f6d1ce04a28/misc/Data); see the [pinned reference](../../references/pbmsc-input-handling.md).

**File format:** UTF-16 XML. `<Column Index Width Title NoteColor TextColor LongNoteColor LongTextColor BG>` gives each column (signed 32-bit ARGB). `<VisualSettings>` holds `Bg`, `Grid`, `Sub`, `VLine`, `MLine`, `BGMWav`, `SelBox`, `kMouseOver`, `kMouseOverE`, `kSelected`, `kOpacity`, `ColumnTitle` and the timing-selection colors. Column titles identify roles: `A1`–`A8` are 1P keys and scratch, `D1`–`D8` are 2P, `BGM`/`B` are BGM lanes, and `BPM`, `STOP` and `SCROLL` are timing lanes.

Before this batch, `assets/themes/compose-field.json` had only viewport, rail, grid, label and overview roles, lane backgrounds by lane kind, and eight note accents. It had no per-key colors, selection or hover colors, or measure versus sub-grid lines.

## T-428 Make the Compose field color scheme selectable and complete

- **Status:** done (2026-10-11; native IIDX/O2Mania verification completes the remaining acceptance check)
- **Outcome:** Options → Appearance has a "Field colors" picker that lists every `assets/themes/field/*.json` scheme, switches live without a restart, and persists. The schema covers:
  - semantic key classes (white/blue/green/yellow/red): lane background (normal and alternate), note, long note and label text;
  - BGM lanes and timing lanes;
  - measure line, beat line and sub-grid line;
  - the selection box, selected note, hovered note, and erase hover.

  The current colors become the default scheme "Wormedit".
- **Read first:** `src/ui/nuklear/theme.h`, `src/ui/nuklear/theme.cpp`, `assets/themes/field/wormedit.json`, `scripts/preview-theme.py`, `src/ui/nuklear/options_window.cpp`.
- **Files owned:** `src/ui/nuklear/theme.h`, `src/ui/nuklear/theme.cpp`, `assets/themes/compose-field.json` (moved to `assets/themes/field/wormedit.json`), `src/ui/nuklear/compose_canvas.cpp` (read the new roles), `src/ui/nuklear/options_window.cpp`, `src/sys/preferences_runtime.cpp`, `src/sys/preferences_runtime.h`, `scripts/preview-theme.py` (validate the new schema), `CMakeLists.txt` (asset install), theme tests under `tests/ui/`, this card.
- **Scope extension (coordinator):** `include/wormedit/core/preferences.h`, `src/core/preferences.cpp`, `src/sys/options_runtime.cpp`, `src/sys/main.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_state.cpp`, `tests/CMakeLists.txt`, `assets/themes/README.md`, `memory/chart-editor-status/resume.md`, `docs/chart-editor-plan/tasks/compose-ux.md` and `editing-commands.md` (references to the relocated palette), and the batch report under `memory/coordination/` are necessary for additive preference persistence, typed Options dispatch, startup/live selection, per-position note colors, test registration, and documentation.
- **Steps:**
  1. Define schema version 2. T-533 supersedes the original positional mapping with semantic key classes; scratch and timing roles remain explicit. Missing roles fall back to the default scheme.
  2. Load every scheme in the directory, and reject invalid files with a diagnostic naming the file.
  3. Replace the hard-coded field colors in `compose_canvas.cpp` with role lookups. This includes the lane-number color from T-387 and the conflict color from T-420.
  4. Add the picker and the preference, and repaint on change.
  5. Make `scripts/preview-theme.py` render a swatch sheet for every scheme and report contrast below 3:1 between labels and their backgrounds.
- **Invariants:** the default scheme renders pixel-identically to today, checked by comparing smoke captures. UI chrome colors (`ui-dark.json`) are unaffected.
- **Reject/failure behavior:** a missing or broken scheme falls back to "Wormedit" with one warning.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'theme'`; `python3 scripts/preview-theme.py`; `scripts/compare-ui-captures.py` against a capture taken before the change.
- **Done so far:** schema 2, all valid directory schemes, typed Appearance picker and additive `display.field_scheme` persistence are implemented. Apply and startup select the scheme through the public frontend API; palette generations invalidate retained field tiles. Missing roles inherit Wormedit, invalid files are rejected with filename diagnostics, unknown IDs warn once, and built-in Wormedit survives a missing default asset. Key positions exclude scratch and separate stages; snap/semantic-layer views retain their meanings. Default software capture is pixel-identical to the HEAD baseline (zero differing pixels); warm retained IIDX switching matches a cold-selected, warmed IIDX render (zero differing pixels).
- **Verification results (2026-10-09):** normal build passes; full CTest 2955/2955; explicit theme filter 6/6; preview validates nine schemes; default and warmed live/cold capture comparisons both report zero different pixels. No new/increased clang-tidy cognitive-complexity warnings.
- **Remaining:** none.
- **Native completion (2026-10-11):** authenticated loopback TCP Xvfb avoids the shared Unix-socket limitation. Real SDL/Nuklear 7K IIDX and O2Mania captures show all seven keys plus scratch, normal bright notes and grid/lane roles at 1600×900. Inspected `/tmp/next-batch-iidx.png` and `/tmp/next-batch-o2mania.png`; commands succeeded (`/tmp/next-batch-{iidx,o2mania}.log`). Fresh theme-filter CTest passes 6/6 (`/tmp/next-batch-theme.log`); the pinned importer reproduces all eight schemes (`/tmp/next-batch-importer.log`); preview validates ten palettes with 13 source-preserved contrast warnings (`/tmp/next-batch-preview.log`). T-533/T-534/T-535 and their verified native captures supersede the old positional mapping/picker behavior.
- **Out of scope:** UI chrome themes and fonts.

## T-429 Import the iBMSC color schemes

- **Status:** done (2026-10-11; native IIDX/O2Mania verification completes the remaining acceptance check)
- **Outcome:** a checked-in converter, `scripts/import-ibmsc-theme.py`, turns the eight iBMSC theme files at the pinned commit into `assets/themes/field/ibmsc-*.json`, and all eight appear in the picker. Each scheme states its source file and the pinned commit.
- **Read first:** this file's introduction, [pinned reference](../../references/pbmsc-input-handling.md), `assets/themes/field/wormedit.json` (after T-428).
- **Files owned:** new `scripts/import-ibmsc-theme.py`, new `scripts/fixtures/ibmsc/*.Theme.xml` (pinned XML color data, normalized to UTF-16 for offline reproducibility), new `assets/themes/field/ibmsc-*.json`, new `assets/themes/field/IBMSC-PROVENANCE.md`, this card.
- **Steps:**
  1. Reuse is approved (user decision 2026-10-05). The iBMSC, uBMSC and pBMSC repositories carry no LICENSE file, but the user reports that the original iBMSC was released as-is under a de facto do-whatever understanding, and that nobody has objected through years of forks. The user maintains the uBMSC fork, and backported the BMSE-inspired colors to iBMSC themselves. Write this provenance into `IBMSC-PROVENANCE.md`.
  2. Write the converter. It reads UTF-16 XML, converts signed ARGB to `#RRGGBBAA`, and maps columns by title (`A1`–`A7` to key positions 1–7, `A8` to scratch, `D*` to 2P, `BGM`/`B*` to BGM lanes, and `BPM`/`STOP`/`SCROLL` to timing lanes). It also maps `VisualSettings` to grid, selection and hover roles, and preserves ordinary note/hold ARGB alpha. T-533 supersedes the original global `kOpacity` rule: it applies only to hidden-note rendering, never ordinary fills.
  3. Generate the eight schemes, and write the provenance file with the repository URL, commit and file names.
  4. Run `scripts/preview-theme.py` and record the contrast warnings on this card. Do not alter the imported colors, except where a role is missing (filled from the default, and noted).
  5. Take Xvfb smoke captures of a 7K chart in IIDX and O2Mania.
- **Invariants:** the converter is deterministic, so re-running it on the pinned files reproduces the JSON byte for byte.
- **Reject/failure behavior:** a theme with an unknown column title is still imported, with that column left at the default and listed in the provenance file.
- **Verify:** `python3 scripts/import-ibmsc-theme.py --check` (regenerates and diffs); `python3 scripts/preview-theme.py`; smoke captures.
- **Done so far:** all eight schemes are imported from the pinned XML color data and listed in the picker. UTF-16 fixtures make the converter/check deterministic and offline. Provenance records user reuse approval, exact files/revision, unknown columns, normalization, and missing-role fallback. Source colors and ordinary note/hold alpha are preserved (T-533); hidden opacity is handled by the renderer. No contrast correction is applied. Preview validates nine total schemes. Label contrast below 3:1: BMSE 0, BMSE mdk 0, BMSE mdkp 0, Forme 3, IIDX 18, MyO2 4K 0, O2Mania 0, Pomu 14, Wormedit 0 (35 total). Exact warning pairs are recorded in the [batch report](../../../memory/coordination/oct9-field-colors-batch.md). Main-object 7K IIDX/O2Mania software captures were produced.
- **Verification results (2026-10-09):** importer `--check` verifies all eight byte-for-byte; preview validates all nine schemes and reports the 35 warnings above; full suite passes 2955/2955. See the batch report for command tails and captures.
- **Remaining:** none.
- **Native completion (2026-10-11):** fresh 7K IIDX/O2Mania real-window captures pass using authenticated loopback TCP Xvfb. Both inspected images show the full key/scratch layout and normal notes: `/tmp/next-batch-iidx.png`, `/tmp/next-batch-o2mania.png`. Importer reproduces all eight schemes; preview validates ten palettes with 13 source-preserved contrast warnings. See the T-428 evidence above. T-531/T-533 supersede the original importer column mapping and opacity rule.
- **Out of scope:** iBMSC fonts and column widths. A Bemuse scheme is not part of this card. dtinth's Bemuse colors were never released as an iBMSC theme, and Bemuse itself is AGPL, so nothing is copied from its source. The default "Wormedit" scheme already gives a similar lane look (red scratch, alternating light and dark keys), as the user showed on 2026-10-05.
