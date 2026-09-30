# BMS command memo notes

Reference fetched for planning on 2026-09-12:

- [BMS command memo, English mirror](https://saxxonpike.github.io/bms-command-memo/index.html)
- [BMS command memo, original English page](https://hitkey.nekokan.dyndns.info/cmds.htm)
- [Executable BMS behavior specifications](https://github.com/bemusic/bmspec)

The command memo is a compatibility survey rather than one authoritative specification. BMS behavior differs among players and editors, so wormedit should expose target profiles and back each declared behavior with fixtures.

## Requirements taken from the memo

- `#xxx01` is the BGM channel and may occur repeatedly for the same measure.
- Repeated `#xxx01` lines are separate layers; unlike ordinary repeated channel statements, they are not merged.
- Each two-character `00` is an empty slot. It contributes to the line's subdivision and positional layout even though it creates no sound event.
- `#xxx02` is the measure-length ratio, with `1` corresponding to 4/4.
- `#xxx03` represents direct BPM changes and `#xxx08` extended BPM definitions/events.
- `#xxx09` references `#STOPxx` definitions.
- One BMS STOP unit corresponds to 1/192 of a whole note under the documented DDR-style STOP behavior.
- STOP duration is independent of a measure's `#xxx02` length ratio.
- Player behavior diverges for malformed, missing, fractional, zero, and negative STOP definitions and for collisions among timing events.

## Playable channel maps

The memo's KEYMAP table is the basis for built-in BMS-family lane profiles. Channel identity, semantic role, and visual order are separate; a scratch may be displayed outside the keys even though its raw channel sits within the numeric range.

| Profile | Semantic channel map |
| --- | --- |
| BMS 5-key, player 1 | keys 1–5 = `11 12 13 14 15`; scratch = `16`; `17` may be free zone or foot pedal by implementation profile |
| BMS 5-key, player 2 | keys 1–5 = `21 22 23 24 25`; scratch = `26`; `27` may be free zone or foot pedal |
| BME 7-key, player 1 | scratch = `16`; keys 1–7 = `11 12 13 14 15 18 19`; optional foot pedal = `17` |
| BME 7-key, player 2 | keys 1–7 = `21 22 23 24 25 28 29`; scratch = `26`; optional foot pedal = `27` |
| PMS 9-button, standard | buttons 1–9 = `11 12 13 14 15 22 23 24 25` |
| PMS 9-button, BME-style | buttons 1–9 = `11 12 13 14 15 18 19 16 17` |
| PMS 5-button | buttons 3–7 of the 9-button layout = `13 14 15 22 23` |
| PMS 18-button | left = `11 12 13 14 15 18 19 16 17`; right = `21 22 23 24 25 28 29 26 27` |
| Double scratch/FPP | scratch, seven keys, scratch = `16 11 12 13 14 15 18 19 26` |
| Octave/foot-pedal | scratch, thirteen keys, scratch = `16 11 12 13 14 15 18 19 22 23 24 25 28 29 26`; foot pedal = `21` |

BMS 10-key combines both BMS 5-key sides, and BME 14-key combines both BME 7-key sides. The memo also records Project2DX's single-player mapping `16 11 12 13 14 15 21 22`, DDR-derived arrow subsets, pomu battle, and implementation-specific O2-style orders; these are named compatibility profiles rather than auto-normalized into BME.

Playable channel families extend beyond visible objects. The memo assigns visible objects to `11-1Z`/ `21-2Z`, invisible objects to `31-3Z`/`41-4Z`, and ordinary RDM long-note objects to `51-5Z`/`61-6Z`; the corresponding relative lane position must remain stable across those families. Mines use `D1-D9` and `E1-E9` where defined. Reserved channels must never be silently treated as playable.

Historical MGQ uses extended channel members and defines an obsolete `#LNTYPE 2` behavior. Wormedit preserves such input as raw/custom BMS data when encountered, but it does not present MGQ as a supported Keyboardmania layout and does not infer MGQ-LN from a high lane count. The editor's Keyboardmania-style 24K/48K layouts are BMSON-only profiles.

## Wormedit consequences

- Import must retain the ordered source identity of repeated BGM lines and their subdivisions. It must not flatten them into one time-keyed event map.
- The BGM editor presents BGM as **per-measure columns**, as iBMSC and BMSE do. Within each measure, the k-th `#mmm01` line (in active source order) belongs to column k; the next measure starts again at column 1. The chart shows as many columns as the busiest measure needs, not one column per source line. (iBMSC 3.5.5.16 `ChartIO.vb` keeps a per-measure counter `mColumn(xMeasure)`; BMSE 3.0 `modInput.vb` takes the first free `m_blnBGM(measure * 128 + j)` slot.)
- `00` is positional padding, not an object. Import creates no BGM event for it. An event's row comes from its slot index and the line's slot count; source provenance keeps the original line and slot for in-place byte-preserving edits.
- New BGM events use a stable lane allocator. A collision advances to the next BGM lane rather than overwriting or merging an existing event.
- Export writes, per measure, one `#mmm01` line for each non-empty column in column order, filling gaps with `00` at the line's original or explicitly normalized subdivision. Empty columns emit nothing. Unedited source lines keep their original bytes.
- STOP events need a format-specific value/unit representation in addition to normalized playback duration.
- Tests must be parameterized by BMS compatibility profile where surveyed applications disagree.
- Built-in lane profiles must retain the exact raw channel for every semantic lane and define a separate display order/stage/scratch/free-zone role.
- Import should select a built-in mapping only when extension, headers, used channels, and profile rules make it unambiguous. Otherwise it should preserve a custom map and ask the user rather than reinterpret notes.
- Invisible, LN, and mine channels follow the visible semantic mapping where the dialect defines a corresponding family; unsupported family/lane combinations remain raw and produce an export diagnostic.

Do not copy assumptions from the old `SupportLevel` flags into exporter guarantees. Verify import, editing, export, and reload against fixtures derived from the referenced behaviors.

Wormedit additionally reads and writes `#OFFSET <signed-seconds>` as an editor extension for beat-zero/audio alignment. It is not part of the surveyed baseline commands above, so target profiles report likely player support or ignore behavior while the source directive remains round-trippable.
