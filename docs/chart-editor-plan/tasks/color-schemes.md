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

Today, `assets/themes/compose-field.json` has only viewport, rail, grid, label and overview roles, lane backgrounds by lane kind, and eight note accents. It has no per-key colors, selection or hover colors, or measure versus sub-grid lines.

## T-428 Make the Compose field color scheme selectable and complete

- **Status:** ready
- **Outcome:** Options → Appearance has a "Field colors" picker that lists every `assets/themes/field/*.json` scheme, switches live without a restart, and persists. The schema covers:
  - per-position key lanes: lane background (normal and alternate), note, long note and label text;
  - BGM lanes and timing lanes;
  - measure line, beat line and sub-grid line;
  - the selection box, selected note, hovered note, and erase hover.

  The current colors become the default scheme "Wormedit".
- **Read first:** `src/ui/nuklear/theme.h`, `src/ui/nuklear/theme.cpp`, `assets/themes/compose-field.json`, `scripts/preview-theme.py`, `src/ui/nuklear/options_window.cpp`.
- **Files owned:** `src/ui/nuklear/theme.h`, `src/ui/nuklear/theme.cpp`, `assets/themes/compose-field.json` (moved to `assets/themes/field/wormedit.json`), `src/ui/nuklear/compose_canvas.cpp` (read the new roles), `src/ui/nuklear/options_window.cpp`, `src/sys/preferences_runtime.cpp`, `src/sys/preferences_runtime.h`, `scripts/preview-theme.py` (validate the new schema), `CMakeLists.txt` (asset install), theme tests under `tests/ui/`, this card.
- **Steps:**
  1. Define schema version 2. Key colors are indexed by lane display position within the active layout (scratch separate), and timing lanes are keyed by kind. Missing roles fall back to the default scheme.
  2. Load every scheme in the directory, and reject invalid files with a diagnostic naming the file.
  3. Replace the hard-coded field colors in `compose_canvas.cpp` with role lookups. This includes the lane-number color from T-387 and the conflict color from T-420.
  4. Add the picker and the preference, and repaint on change.
  5. Make `scripts/preview-theme.py` render a swatch sheet for every scheme and report contrast below 3:1 between labels and their backgrounds.
- **Invariants:** the default scheme renders pixel-identically to today, checked by comparing smoke captures. UI chrome colors (`ui-dark.json`) are unaffected.
- **Reject/failure behavior:** a missing or broken scheme falls back to "Wormedit" with one warning.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'theme'`; `python3 scripts/preview-theme.py`; `scripts/compare-ui-captures.py` against a capture taken before the change.
- **Out of scope:** UI chrome themes and fonts.

## T-429 Import the iBMSC color schemes

- **Status:** blocked-on T-428
- **Outcome:** a checked-in converter, `scripts/import-ibmsc-theme.py`, turns the eight iBMSC theme files at the pinned commit into `assets/themes/field/ibmsc-*.json`, and all eight appear in the picker. Each scheme states its source file and the pinned commit.
- **Read first:** this file's introduction, [pinned reference](../../references/pbmsc-input-handling.md), `assets/themes/field/wormedit.json` (after T-428).
- **Files owned:** new `scripts/import-ibmsc-theme.py`, new `assets/themes/field/ibmsc-*.json`, new `assets/themes/field/IBMSC-PROVENANCE.md`, this card.
- **Steps:**
  1. Reuse is approved (user decision 2026-10-05). The iBMSC, uBMSC and pBMSC repositories carry no LICENSE file, but the user reports that the original iBMSC was released as-is under a de facto do-whatever understanding, and that nobody has objected through years of forks. The user maintains the uBMSC fork, and backported the BMSE-inspired colors to iBMSC themselves. Write this provenance into `IBMSC-PROVENANCE.md`.
  2. Write the converter. It reads UTF-16 XML, converts signed ARGB to `#RRGGBBAA`, and maps columns by title (`A1`–`A7` to key positions 1–7, `A8` to scratch, `D*` to 2P, `BGM`/`B*` to BGM lanes, and `BPM`/`STOP`/`SCROLL` to timing lanes). It also maps `VisualSettings` to grid, selection and hover roles, and premultiplies `kOpacity` into the note fills where the schema has no opacity.
  3. Generate the eight schemes, and write the provenance file with the repository URL, commit and file names.
  4. Run `scripts/preview-theme.py` and record the contrast warnings on this card. Do not alter the imported colors, except where a role is missing (filled from the default, and noted).
  5. Take Xvfb smoke captures of a 7K chart in IIDX and O2Mania.
- **Invariants:** the converter is deterministic, so re-running it on the pinned files reproduces the JSON byte for byte.
- **Reject/failure behavior:** a theme with an unknown column title is still imported, with that column left at the default and listed in the provenance file.
- **Verify:** `python3 scripts/import-ibmsc-theme.py --check` (regenerates and diffs); `python3 scripts/preview-theme.py`; smoke captures.
- **Out of scope:** iBMSC fonts and column widths. A Bemuse scheme is not part of this card. dtinth's Bemuse colors were never released as an iBMSC theme, and Bemuse itself is AGPL, so nothing is copied from its source. The default "Wormedit" scheme already gives a similar lane look (red scratch, alternating light and dark keys), as the user showed on 2026-10-05.
