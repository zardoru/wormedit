# Theme files

`ui-dark.json` defines application chrome. The field picker never changes it.

`field/wormedit.json` is the default Compose field scheme. All `field/*.json`
files appear in Options → Appearance → Default field skin. The current layout
also has a skin picker; “Use default” inherits the global choice. Layout choices
follow the selected lane profile, even when it differs from the imported format.
Changes preview immediately and invalidate retained tiles; Apply saves them in
program preferences. Close, Revert and Restore defaults follow the standard
Options draft flow. Unknown or incompatible choices use the compatible default,
then Wormedit.

Schema version 2 requires `version: 2` and a nonempty `name`. Colors accept
`#RRGGBB`, `#RRGGBBAA`, or `$scale.shade` references into the file’s `scales`.
Missing color roles inherit Wormedit. `roles` covers canvas, grid, selection,
hover, erase and conflict colors. `lane_classes` supplies semantic `white`, `blue`,
`green`, `yellow`, and `red` entries. White also colors pms_white, blue colors
black/pms_blue, and red colors scratch/pms_red. Missing classes inherit Wormedit.
`key_colors` remains available for generic, foot_pedal and free_zone colors.
Colors follow each authored lane's class and ID on every layout, regardless of
source file family, scratch side, stage count or visual ordering.

A genuinely numbered palette must declare an exact compatibility guard:

```json
"numbered_layout": {
  "stages": [["white", "blue", "blue", "white"]],
  "colors": [[{"note":"#f274b3"}, {"note":"#7987ff"},
              {"note":"#7987ff"}, {"note":"#f274b3"}]]
}
```

This describes one stage with four playable lanes, in authored display order.
Each color slot has an expected semantic class; matching lane count alone is
insufficient. Auxiliary lanes do not consume slots. MyO2 4K uses this exception;
its numbered colors are unavailable for other layouts. Legacy ungated `keys`
and `key_count` files reject with a diagnostic instead of recoloring wrong lanes.
Compatible layout overrides win, then the compatible configured default, then
Wormedit. An unavailable override is retained in preferences for its layout.

Optional `scratch_placement` suggests explicit display-only sides (`1p`/`2p`,
`left`/`right`). `rotate_dp5: true` instead suggests both scratches on the left
only for two stages with five playable keys and one scratch each. BMSE uses this
DP5-only default; SP7 and DP7 preserve chart layout. Appearance overrides remain
independent for Single/P1 and P2, preserving authored channels and notes.

Lane entries accept `lane`, `lane_alternate`, `lane_label`, `note`, `note_border`,
`hold_body`, `long_note`, `label`, and `long_label`. Missing label colors preserve
the default automatic contrast behavior. `bgm` has one lane entry; `timing` has
entries keyed by `bpm`, `scroll`, `speed`, `stop`, `delay`, `warp`, `kiai`, `anchor`.
Semantic-layer and snap color views retain their established color meanings.

Build after editing files to copy them into `build/assets/themes`. Alternatively,
set `WORMEDIT_THEME_DIR` to a directory containing `ui-dark.json` and `field/`
before starting. Files are read at startup; picker changes use the loaded schemes.

`python3 scripts/preview-theme.py` validates chrome and all field schemes, writes
HTML swatch sheets under `/tmp/wormedit-theme-preview-fields`, and reports
composited label contrast below 3:1. Warnings do not modify colors.
See [iBMSC provenance](field/IBMSC-PROVENANCE.md) for the eight imported schemes
and `python3 scripts/import-ibmsc-theme.py --check` for deterministic verification.
