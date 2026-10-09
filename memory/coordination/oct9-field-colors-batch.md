# Field color schemes batch — 2026-10-09

T-428/T-429 implementation after `2873dff`. Both cards remain partial solely for their native Xvfb smoke gate. Other worktrees are preserved.

## Behavior and structures

Options → Appearance → Field colors lists Wormedit and eight iBMSC schemes. Selection remains a draft until Apply; Apply repaints without restart and persists the additive `display.field_scheme` ID. Startup restores it through the public frontend API. Chrome is unchanged.

Schema 2 covers key-kind fallback plus stage/display-position overrides (scratch separate), BGM/timing lanes, lane and note labels, note/long-note fills, grid, selection, hover/erase and conflict roles. `field_palette_t` holds resolved role tables, per-kind keys, BGM/timing colors and position overrides. `field_lane_t` binds active layout display orders to stage/key positions once per frame with a cached binding; painting looks up the resolved display lane. `field_generation()` enters retained tile style keys, so a palette change never reuses old palette textures.

`load_field_schemes` discovers sorted JSON files, validates schema/colors and reports rejected filenames. Missing roles inherit Wormedit, and the built-in Wormedit fallback includes the complete key-kind table. `select_field_scheme` handles empty/unknown IDs and warning deduplication. Existing snap and semantic-layer colors retain their meaning.

`scripts/import-ibmsc-theme.py` converts pinned UTF-16 XML to deterministic JSON offline. Source data and provenance live in `scripts/fixtures/ibmsc/` and `assets/themes/field/IBMSC-PROVENANCE.md`. The GitHub connector returned decoded XML; fixtures normalize encoding/line endings to UTF-16 BOM/LF without changing values. Card-specified A1–A7/A8 and D1–D7/D8 map to keys/scratch. Unknown nonstandard column titles are listed and left at fallback. Fonts/widths are excluded.

## Verification and limitations

The first full run passed 2954/2955 and caught a direct toolkit include in main; wiring was moved behind `worm_ui_frontend_select_field_scheme`, preserving the frontend boundary. Focused picker/schema/persistence tests passed before that correction. The final application build passes and the full suite passes 2955/2955 (43.15 s), including 271 event-harness cases. The explicit `-R theme` run passes 6/6 (0.14 s). The architecture/documentation checks pass. Final default and warmed live/cold captures both have zero differing pixels.

Production main-object captures use an isolated SDL dummy/software renderer at 1800×1400 with a BMS 7K chart, holds and BGM. The previous batch binary and HEAD palettes provide the baseline. Default comparison has zero different pixels. The first live/cold check mixed cache warm-up phases; after equal cache warm-up, IIDX live-switch and cold-selected renders also have zero different pixels. This is a palette/cache verification, not a native cadence measurement.

Evidence directory: `/home/zardoru/devel/wormedit-scratch-builds/oct9-field-colors/captures/` (`before.png`, `after.png`, `iidx.png`, `o2mania.png`, `cold-iidx.png`, `live-iidx.png`, reproducible probe source/linker). Logs: `/tmp/wormedit-field-colors/`. Swatch sheets: `/tmp/wormedit-theme-preview-fields/`.

The required `scripts/smoke-compose-virtual-x.sh` was attempted. Xvfb failed to create listening sockets (`Cannot establish any listening sockets`), so the native Xvfb gate remains outstanding on both cards. No user-desktop window was opened.

## Contrast warnings

`python3 scripts/preview-theme.py` validates all nine schemes and reports 35 composited label/background pairs below 3:1. Colors are not modified. Counts: Forme 3, IIDX 18, Pomu 14, all other schemes including Wormedit 0.

```text
/tmp/wormedit-theme-preview.html
CONTRAST ibmsc-forme.json timing/scroll/long_label: 2.14:1
CONTRAST ibmsc-forme.json timing/bpm/long_label: 2.14:1
CONTRAST ibmsc-forme.json timing/stop/long_label: 2.14:1
CONTRAST ibmsc-iidx.json 1p/1/label: 2.40:1
CONTRAST ibmsc-iidx.json 1p/1/long_label: 2.44:1
CONTRAST ibmsc-iidx.json 1p/3/label: 2.40:1
CONTRAST ibmsc-iidx.json 1p/3/long_label: 2.51:1
CONTRAST ibmsc-iidx.json 1p/5/label: 2.40:1
CONTRAST ibmsc-iidx.json 1p/5/long_label: 2.51:1
CONTRAST ibmsc-iidx.json 1p/7/label: 2.40:1
CONTRAST ibmsc-iidx.json 1p/7/long_label: 2.51:1
CONTRAST ibmsc-iidx.json 2p/2/label: 2.40:1
CONTRAST ibmsc-iidx.json 2p/2/long_label: 2.51:1
CONTRAST ibmsc-iidx.json 2p/4/label: 2.40:1
CONTRAST ibmsc-iidx.json 2p/4/long_label: 2.51:1
CONTRAST ibmsc-iidx.json 2p/6/label: 2.40:1
CONTRAST ibmsc-iidx.json 2p/6/long_label: 2.51:1
CONTRAST ibmsc-iidx.json 2p/scratch/label: 2.40:1
CONTRAST ibmsc-iidx.json 2p/scratch/long_label: 2.44:1
CONTRAST ibmsc-iidx.json BGM/label: 2.51:1
CONTRAST ibmsc-iidx.json BGM/long_label: 2.55:1
CONTRAST ibmsc-pomu.json 1p/1/label: 1.00:1
CONTRAST ibmsc-pomu.json 1p/1/long_label: 1.00:1
CONTRAST ibmsc-pomu.json 1p/7/label: 1.00:1
CONTRAST ibmsc-pomu.json 1p/7/long_label: 1.00:1
CONTRAST ibmsc-pomu.json 1p/scratch/label: 1.00:1
CONTRAST ibmsc-pomu.json 1p/scratch/long_label: 1.00:1
CONTRAST ibmsc-pomu.json 2p/1/label: 1.00:1
CONTRAST ibmsc-pomu.json 2p/1/long_label: 1.00:1
CONTRAST ibmsc-pomu.json 2p/6/label: 1.00:1
CONTRAST ibmsc-pomu.json 2p/6/long_label: 1.00:1
CONTRAST ibmsc-pomu.json 2p/7/label: 1.00:1
CONTRAST ibmsc-pomu.json 2p/7/long_label: 1.00:1
CONTRAST ibmsc-pomu.json 2p/scratch/label: 1.00:1
CONTRAST ibmsc-pomu.json 2p/scratch/long_label: 1.00:1
Validated 9 field schemes; 35 contrast warnings; swatch sheets: /tmp/wormedit-theme-preview-fields
```

## Final verification tails

```text
$ CCACHE_DIR=/tmp/wormedit-batch-ccache cmake --build build -j4
[100%] Built target wormedit_frontend_event_harness_tests

$ ctest --test-dir build --output-on-failure -j4
100% tests passed out of 2955
Total Test time (real) = 43.15 sec

$ ctest --test-dir build --output-on-failure -R theme
100% tests passed out of 6
Total Test time (real) = 0.14 sec

$ python3 scripts/import-ibmsc-theme.py --check
Verified 8 pinned iBMSC field schemes

$ python3 scripts/preview-theme.py
Validated 9 field schemes; 35 contrast warnings; swatch sheets: /tmp/wormedit-theme-preview-fields

$ python3 scripts/compare-ui-captures.py <before.png> <after.png> <cold-iidx.png> <live-iidx.png>
PASS before / after: different pixels=0, bbox=None
PASS cold IIDX / live IIDX: different pixels=0, bbox=None
Compared 2 capture pairs
```

Configured clang-tidy checks covered affected C++ sources/tests. New functions are below the 25 threshold. Main's existing 27-point functions were compared against HEAD using a temporary baseline compile database and did not increase; the existing frontend status-bar warning (28) is in an unchanged function. No suppressions were added. The initial test macro complexity warnings were resolved with the repository's assertion-helper pattern.

Work-card inventory: 300 done, 40 ready, 5 partial, 34 blocked-on, 1 blocked-on-user, 32 deferred, 2 merged (414 total). T-428/T-429 remain partial only for native smoke evidence.

```text
$ ctest --test-dir build --output-on-failure -L event-harness
100% tests passed out of 271
Total Test time (real) = 7.35 sec

$ ctest --test-dir build --output-on-failure -R documentation.chart_editor_state
100% tests passed out of 1
Total Test time (real) = 0.10 sec
```
