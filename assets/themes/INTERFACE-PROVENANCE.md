# Interface palette provenance

The Windows 98, Classic Mac, Modern Mac Light and Modern Mac Dark palettes are
original Wormedit adaptations of familiar desktop color families. They contain
no Microsoft or Apple artwork, fonts, widget code or third-party theme assets,
and are covered by Wormedit's Unlicense. They change colors only; the existing
Nuklear controls retain their geometry and behavior.

The osu! stable palette is an original adaptation of the familiar charcoal,
pink and orange interface shown in the [official interface guide](https://osu.ppy.sh/wiki/en/Client/Interface).
It is covered by Wormedit's Unlicense; no stable client code or skin assets are
imported. The osu!lazer palette adapts selected MIT-licensed color constants
listed below. Neither palette imports osu! logos, artwork, fonts or widget code.

The following upstream palettes are distributed under MIT terms. Their full
copyright and permission notices are retained in `licenses/` and installed under
`third-party/interface-themes/` together with this file. These notices apply to
the adapted palettes; the project's own code remains under the Unlicense.

| Bundled theme | Pinned palette source | Notice |
| --- | --- | --- |
| Nord | [nordtheme/nord, 1cef71605416a222e57225b544540ce0fcec18d4, src/nord.css](https://github.com/nordtheme/nord/blob/1cef71605416a222e57225b544540ce0fcec18d4/src/nord.css) | [Sven Greb, MIT](licenses/NORD-LICENSE.txt) |
| Catppuccin Mocha | [catppuccin/palette, 07d02aa110ef9eb7e7427afca5c73ba9cf7f8ebd, palette.json](https://github.com/catppuccin/palette/blob/07d02aa110ef9eb7e7427afca5c73ba9cf7f8ebd/palette.json) | [Catppuccin, MIT](licenses/CATPPUCCIN-LICENSE.txt) |
| Solarized Light / Dark | [altercation/solarized, 62f656a02f93c5190a8753159e34b385588d5ff3, README.md](https://github.com/altercation/solarized/blob/62f656a02f93c5190a8753159e34b385588d5ff3/README.md) | [Ethan Schoonover, MIT](licenses/SOLARIZED-LICENSE.txt) |
| osu!lazer | [ppy/osu, 3e7f0f4c619adfc5133dc443df14a1b7574223db, osu.Game/Graphics/OsuColour.cs](https://github.com/ppy/osu/blob/3e7f0f4c619adfc5133dc443df14a1b7574223db/osu.Game/Graphics/OsuColour.cs) | [ppy Pty Ltd, MIT](licenses/OSU-LICENSE.txt) |

Checked 2026-10-28. The imported palettes' JSON `source` fields point to these exact revisions.
Only palette values are adapted, with Wormedit-specific role assignments.
Nord uses Polar Night surfaces, Snow Storm text, Frost selection and Aurora
feedback. Mocha uses crust/base surfaces, blue selection with crust text,
green accents and red conflict feedback. Solarized uses its base tone pairs,
cyan selection with base03 text, and blue/green accents. Its light active surface
(`#ddd6c1`), light cyan accent (`#007e9e`) and dark active surface (`#0b4350`)
are local adaptations to keep text legible across the control states.
The lazer adaptation maps GreyVioletDarker/Dark to the background and surface,
GreyViolet to hover/separators, GreyVioletLight to borders, Pink3 to selection,
Pink0/1 to focus/accent, Purple1/Blue1 to waveform fills and Yellow to its playhead.
Its active surface, text, status and conflict tones are local adaptations.

No field skins, logos, fonts or code are imported from these projects.
