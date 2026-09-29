# Lucide toolbar icon subset

The SVGs here are from [Lucide](https://github.com/lucide-icons/lucide) at
commit `66d8f9fc394b8530377e5f6112f0b8908ba01280` and
are used for every icon-only application control. `audio-lines` and `layers`
replace the older Phosphor audio asset subset. The `LICENSE` file contains
Lucide's ISC notice and the MIT notice for Feather-derived icons.

`scripts/generate-lucide-icons.py` rasterizes these sources into the checked-in
alpha masks in `src/ui/nuklear/lucide_icon_pixels.h`. Regenerate after changing
the subset; the app does not need an SVG renderer at runtime. This script needs
`rsvg-convert` and Pillow on the development machine.
