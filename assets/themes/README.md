# Theme files

`ui-dark.json` defines application chrome. The field picker never changes it.

`field/wormedit.json` is the default Compose field scheme. All `field/*.json`
files appear in Options → Appearance → Field colors. Apply switches the field
immediately, invalidates retained tiles, and saves the scheme ID in program
preferences. Revert and Restore defaults follow the standard Options draft flow.
An unknown or malformed scheme falls back to Wormedit with one diagnostic.

Schema version 2 requires `version: 2` and a nonempty `name`. Colors accept
`#RRGGBB`, `#RRGGBBAA`, or `$scale.shade` references into the file’s `scales`.
Missing color roles inherit Wormedit. `roles` covers canvas, grid, selection,
hover, erase and conflict colors. `key_colors` supplies layout-key-kind fallbacks.
`keys` supplies per-position overrides, for example
`"1p": {"1": {"note": "#abcdef"}, "scratch": {"lane": "#450a0a"}}`.
Each player/stage counts keys in display order excluding scratch. `2p` is the
second stage. A layout with more positions uses the key-kind fallback.

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
