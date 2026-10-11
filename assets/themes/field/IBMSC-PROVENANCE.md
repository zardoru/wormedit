# iBMSC field scheme provenance

Source: https://github.com/Shimi9999/pBMSC at `06aaf7438dbca3c8a0611bd13d134f6d1ce04a28`, files under `misc/Data/`.

Reuse was approved by the project owner on 2026-10-05. The iBMSC, uBMSC and pBMSC repositories have no LICENSE file. The owner reports that original iBMSC was released as-is under a de facto do-whatever understanding, with years of forks without objections. The owner maintains uBMSC and backported the BMSE-inspired colors into iBMSC. This records the owner’s decision and provenance; it does not assert a license grant absent from upstream.

The eight XML color-data files are vendored in `scripts/fixtures/ibmsc/`. GitHub returned decoded XML text; these fixtures normalize it to UTF-16 with a BOM and LF line endings, preserving XML values. `python3 scripts/import-ibmsc-theme.py --check` verifies the generated JSON offline. No source code, fonts, or widths are copied.

## Mapping

Signed ARGB becomes #RRGGBBAA. `kOpacity` controls hidden-note opacity in upstream `PanelDraw.vb`; it must not multiply ordinary note or long-note fills. Normal fills preserve the source ARGB alpha. Wormedit applies invisible-note dimming separately. BMSE, BMSE mdk, Forme and IIDX store the 1P scratch in A1 and keys in A2–A8; other A/D skins store it in A8 and keys in A1–A7. All A/D skins store the 2P scratch in D8 and keys in D1–D7. Positions exclude scratch and follow display order within each stage, so changing scratch placement does not shift key colors. B/BGM columns map to BGM and BPM/STOP/SCROLL map by timing kind.

`TextColor`/`LongTextColor` supply note labels; `ColumnTitle` supplies lane labels. Grid/Sub/MLine/VLine, SelBox, kSelected, kMouseOver/kMouseOverE, TSMouseOver/TSSel supply their corresponding grid and interaction roles. iBMSC has one selection-box color, reused for add/toggle modes. Unsupported Measure, spacers, BGA/LAYER/POOR columns are intentionally ignored. Other unknown columns remain default and are listed below.

## Fallback roles by source file

Fields without an upstream counterpart inherit Wormedit; imported colors are never contrast-corrected. This includes conflict indicators, preview fills, note borders, additional timing kinds, and positions beyond the source’s eight-column sides. The default uses key-kind colors for unmapped positions.

- `misc/Data/BMSE.Theme.xml` → `ibmsc-bmse.json`: unknown columns none; missing global roles: `bgm_border`, `bgm_label`, `bgm_selected`, `conflict`, `erase_fill`, `hover_fill`, `label`, `note_border`, `overview`, `rail`, `tail`, `timing_border`, `timing_note`, `timing_title_bg`.
- `misc/Data/BMSEmdk.Theme.xml` → `ibmsc-bmsemdk.json`: unknown columns none; missing global roles: `bgm_border`, `bgm_label`, `bgm_selected`, `conflict`, `erase_fill`, `hover_fill`, `label`, `note_border`, `overview`, `rail`, `tail`, `timing_border`, `timing_note`, `timing_title_bg`.
- `misc/Data/BMSEmdkp.Theme.xml` → `ibmsc-bmsemdkp.json`: unknown columns none; missing global roles: `bgm_border`, `bgm_label`, `bgm_selected`, `conflict`, `erase_fill`, `hover_fill`, `label`, `note_border`, `overview`, `rail`, `tail`, `timing_border`, `timing_note`, `timing_title_bg`.
- `misc/Data/Forme.Theme.xml` → `ibmsc-forme.json`: unknown columns ['Mes']; missing global roles: `bgm_border`, `bgm_label`, `bgm_selected`, `conflict`, `erase_fill`, `hover_fill`, `label`, `note_border`, `overview`, `rail`, `tail`, `timing_border`, `timing_note`, `timing_title_bg`.
- `misc/Data/IIDX.Theme.xml` → `ibmsc-iidx.json`: unknown columns none; missing global roles: `bgm_border`, `bgm_label`, `bgm_selected`, `conflict`, `erase_fill`, `hover_fill`, `label`, `note_border`, `overview`, `rail`, `tail`, `timing_border`, `timing_note`, `timing_title_bg`.
- `misc/Data/MyO2_4K.Theme.xml` → `ibmsc-myo2-4k.json`: unknown columns none; missing global roles: `bgm_border`, `bgm_label`, `bgm_selected`, `conflict`, `erase_fill`, `hover_fill`, `label`, `note_border`, `overview`, `rail`, `tail`, `timing_border`, `timing_note`, `timing_title_bg`.
- `misc/Data/O2Mania.Theme.xml` → `ibmsc-o2mania.json`: unknown columns none; missing global roles: `bgm_border`, `bgm_label`, `bgm_selected`, `conflict`, `erase_fill`, `hover_fill`, `label`, `note_border`, `overview`, `rail`, `tail`, `timing_border`, `timing_note`, `timing_title_bg`.
- `misc/Data/Pomu.Theme.xml` → `ibmsc-pomu.json`: unknown columns none; missing global roles: `bgm_border`, `bgm_label`, `bgm_selected`, `conflict`, `erase_fill`, `hover_fill`, `label`, `note_border`, `overview`, `rail`, `tail`, `timing_border`, `timing_note`, `timing_title_bg`.

Wormedit adds a presentation-only `rotate_dp5` default to BMSE: both
player scratches appear on the left only in DP5, matching preview conventions requested
by the user. This metadata is not imported from the pinned XML and can be
overridden in Appearance. The importer reproduces this addition deterministically;
upstream colors remain unchanged.

BMSE mdkp and Pomu are pop’n palettes, with named LW/LY/LG/LB/RED/RB/RG/RY/RW columns rather than usable BMS A/D positions. The importer binds their white/yellow/green/blue/red colors to semantic `pms_*` roles; mirrored right colors are identical in the pinned sources. White, blue and red also supply BMS white, black and scratch key classes on either stage. Hidden A/D placeholders do not override playable lane colors. This preserves source colors while making these palettes usable on DP5 and DP7.

## Semantic palettes

Bundled schemes now bind semantic lane classes rather than numbered BMS columns. White and blue come from the first normalized white/blue source keys; red comes from the source scratch. Missing green/yellow classes inherit Wormedit. O2Mania's center color supplies green and yellow. The pop’n palettes supply all five named colors. Aliases share class entries on every layout and stage, independent of source family. Position-specific opacity anomalies no longer override class colors.

MyO2 4K retains its four numbered source colors only for a single white/blue/blue/white stage; the explicit descriptor checks stage count and every slot's semantic class. Other layouts use Wormedit class colors. BMSE’s `rotate_dp5` presentation suggestion applies only to DP5, not DP7 or SP7.
