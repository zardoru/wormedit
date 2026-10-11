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

`chevrons-left`, `chevrons-right`, and `magnet` are used by Compose audio-offset
nudges and beat-fit BPM snapping. `metronome` is the transport metronome toggle.

`music-2` is the transport Keep pitch toggle.

Compose timing headers use `metronome`, `waves-horizontal`, `gauge`, `pause`, `hourglass`,
`fast-forward`, `flame` and `anchor` for BPM, SV, SPEEDS, Stop, Delay, Warp,
Kiai and Anchor. The added sources use the same pinned Lucide commit above.
