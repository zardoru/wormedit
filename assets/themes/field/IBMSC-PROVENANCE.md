# iBMSC field scheme provenance

Source: https://github.com/Shimi9999/pBMSC at `06aaf7438dbca3c8a0611bd13d134f6d1ce04a28`, files under `misc/Data/`.

Reuse was approved by the project owner on 2026-10-05. The iBMSC, uBMSC and pBMSC repositories have no LICENSE file. The owner reports that original iBMSC was released as-is under a de facto do-whatever understanding, with years of forks without objections. The owner maintains uBMSC and backported the BMSE-inspired colors into iBMSC. This records the owner’s decision and provenance; it does not assert a license grant absent from upstream.

The eight XML color-data files are vendored in `scripts/fixtures/ibmsc/`. GitHub returned decoded XML text; these fixtures normalize it to UTF-16 with a BOM and LF line endings, preserving XML values. `python3 scripts/import-ibmsc-theme.py --check` verifies the generated JSON offline. No source code, fonts, or widths are copied.

## Mapping

Signed ARGB becomes #RRGGBBAA. `kOpacity` multiplies the alpha channel of note and long-note fills. Per the task-card contract, A1–A7 map to 1P key positions, A8 to scratch; D1–D7 and D8 map to 2P. Positions exclude scratch and follow display order within each stage, so changing scratch placement does not shift key colors. B/BGM columns map to BGM and BPM/STOP/SCROLL map by timing kind.

`TextColor`/`LongTextColor` supply note labels; `ColumnTitle` supplies lane labels. Grid/Sub/MLine/VLine, SelBox, kSelected, kMouseOver/kMouseOverE, TSMouseOver/TSSel supply their corresponding grid and interaction roles. iBMSC has one selection-box color, reused for add/toggle modes. Unsupported Measure, spacers, BGA/LAYER/POOR columns are intentionally ignored. Other unknown columns remain default and are listed below.

## Fallback roles by source file

Fields without an upstream counterpart inherit Wormedit; imported colors are never contrast-corrected. This includes conflict indicators, preview fills, note borders, additional timing kinds, and positions beyond the source’s eight-column sides. The default uses key-kind colors for unmapped positions.

- `misc/Data/BMSE.Theme.xml` → `ibmsc-bmse.json`: unknown columns none; missing global roles: `bgm_border`, `bgm_label`, `bgm_selected`, `conflict`, `erase_fill`, `hover_fill`, `label`, `note_border`, `overview`, `rail`, `tail`, `timing_border`, `timing_note`, `timing_title_bg`.
- `misc/Data/BMSEmdk.Theme.xml` → `ibmsc-bmsemdk.json`: unknown columns none; missing global roles: `bgm_border`, `bgm_label`, `bgm_selected`, `conflict`, `erase_fill`, `hover_fill`, `label`, `note_border`, `overview`, `rail`, `tail`, `timing_border`, `timing_note`, `timing_title_bg`.
- `misc/Data/BMSEmdkp.Theme.xml` → `ibmsc-bmsemdkp.json`: unknown columns ['LB', 'LG', 'LW', 'LY', 'RB', 'RED', 'RG', 'RW', 'RY']; missing global roles: `bgm_border`, `bgm_label`, `bgm_selected`, `conflict`, `erase_fill`, `hover_fill`, `label`, `note_border`, `overview`, `rail`, `tail`, `timing_border`, `timing_note`, `timing_title_bg`.
- `misc/Data/Forme.Theme.xml` → `ibmsc-forme.json`: unknown columns ['Mes']; missing global roles: `bgm_border`, `bgm_label`, `bgm_selected`, `conflict`, `erase_fill`, `hover_fill`, `label`, `note_border`, `overview`, `rail`, `tail`, `timing_border`, `timing_note`, `timing_title_bg`.
- `misc/Data/IIDX.Theme.xml` → `ibmsc-iidx.json`: unknown columns none; missing global roles: `bgm_border`, `bgm_label`, `bgm_selected`, `conflict`, `erase_fill`, `hover_fill`, `label`, `note_border`, `overview`, `rail`, `tail`, `timing_border`, `timing_note`, `timing_title_bg`.
- `misc/Data/MyO2_4K.Theme.xml` → `ibmsc-myo2-4k.json`: unknown columns none; missing global roles: `bgm_border`, `bgm_label`, `bgm_selected`, `conflict`, `erase_fill`, `hover_fill`, `label`, `note_border`, `overview`, `rail`, `tail`, `timing_border`, `timing_note`, `timing_title_bg`.
- `misc/Data/O2Mania.Theme.xml` → `ibmsc-o2mania.json`: unknown columns none; missing global roles: `bgm_border`, `bgm_label`, `bgm_selected`, `conflict`, `erase_fill`, `hover_fill`, `label`, `note_border`, `overview`, `rail`, `tail`, `timing_border`, `timing_note`, `timing_title_bg`.
- `misc/Data/Pomu.Theme.xml` → `ibmsc-pomu.json`: unknown columns ['LB', 'LG', 'LW', 'LY', 'RB', 'RED', 'RG', 'RW', 'RY']; missing global roles: `bgm_border`, `bgm_label`, `bgm_selected`, `conflict`, `erase_fill`, `hover_fill`, `label`, `note_border`, `overview`, `rail`, `tail`, `timing_border`, `timing_note`, `timing_title_bg`.
