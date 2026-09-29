# Theme files

`ui-dark.json` contains editable 50–950 color scales and the UI roles that use
them. `neutral.950` (`#12131a`) and `blue.600` (`#145ce3`) are fixed anchors.
Coral and lime are the two accents on either side of blue's complement. Use
lighter shades for text on dark surfaces; `neutral.500` is for decoration only.

`compose-field.json` holds the Compose canvas base, lane and note colors. Its
values match the existing field palette so changing UI chrome does not alter
chart readability. Other specialized Compose marks still have local colors in
the renderer and can migrate here as the design evolves.

Build again after editing either file to copy it into `build/assets/themes`.
Alternatively set `WORMEDIT_THEME_DIR` to a directory containing both JSON files
before starting the app. The app reads the files at startup; invalid or missing
entries use built-in defaults.

Run `python3 scripts/preview-theme.py --output /tmp/wormedit-theme-preview.html`
to validate the scales and produce an HTML sheet with swatches, gradients and
contrast ratios.
