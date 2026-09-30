# BMSE and iBMSC clipboard compatibility notes

Research recorded for planning on 2026-09-12:

- [BMSE editing help](https://hitkey.nekokan.dyndns.info/bmse_help_full/edit.html)
- [BMSE source repository](https://github.com/Nekokan/BMSE)
- [BMSE releases](https://github.com/Nekokan/BMSE/releases)
- [iBMSC README in the pBMSC repository](https://github.com/Shimi9999/pBMSC/blob/master/README.md.iBMSC)

The iBMSC documentation explicitly describes BMSE clipboard-data compatibility, but public descriptions and release history do not establish one complete, version-independent grammar for every object type wormedit needs. The implementation must therefore be based on captured payloads and golden fixtures from named BMSE and iBMSC versions rather than an inferred format.

## Source audit: formats are already versioned and asymmetric

A 2026-09-13 source audit pinned [BMSE 3.0.0 at `26826aff6d51`](https://github.com/Nekokan/BMSE/blob/26826aff6d51f34e0aa24284a68d9e713db6a007/frmMain.vb) and [iBMSC 3.5.5.16 at `06aaf7438dbc`](https://github.com/Shimi9999/pBMSC/blob/06aaf7438dbca3c8a0611bd13d134f6d1ce04a28/iBMSC/MainWindow.vb). A second, field-level pass on 2026-09-30 read both encoders and decoders line by line and produced the source-derived fixtures in [`tests/fixtures/bms/clipboard/`](../../tests/fixtures/bms/clipboard/README.md). Captures from running releases remain welcome as confirmation, but the grammar below is complete enough to implement against.

Both editors share one position unit: **192 per 4/4 measure**, positions relative to the start of the measure containing the earliest selected object. Pasting adds that relative position to the start of a visible measure chosen by the target editor's scroll position.

### BMSE 3.0 (`BMSE3.0 ClipBoard Object Data Format`)

Encoder `CopyToClipboard` in `frmMain.vb`; helpers `double2bin` in `modLog.vb`, `strFromNumZZ` in `modInput.vb`.

- Payload: header, CRLF, records joined by CRLF, **trailing CRLF**.
- Record: `CCC` `A` `PPPPPPPPPPPPPPPP` `V…`, no separators.
  - `CCC`: BMSE's internal channel number in base 36, three uppercase characters, zero-padded. It equals the BMS channel spelled with a leading `0` for ordinary channels: `008` BPM (direct `#xxx03` and extended `#xxx08` are both normalized to 008 on load), `009` STOP, `004` BGA, `006` POOR, `007` LAYER, `0SC` SCROLL, `0SP` SPEED, `011`–`019` 1P, `021`–`029` 2P. Invisible, long-note, and mine channels are **not** used: BMSE subtracts the family offset on load, so `31`/`51`/`D1` become `011` with an attribute. BGM lanes are `101` + lane index (internal `36^2 + 1 + j`), up to 128 lanes.
  - `A`: attribute digit `0` normal, `1` invisible, `2` long-note endpoint, `3` mine (`OBJ_ATT` in `modMain.vb`). A long note is two records, head and tail, both with `2`.
  - `P…`: the relative position as the raw IEEE-754 bits of a `Double`, 16 uppercase hex digits (`BitConverter.ToInt64(...).ToString("X16")`). Free-grid positions are exact: 192/7 is `403B6DB6DB6DB6DB`.
  - `V…`: the object's `Single` value converted with VB's implicit `& .sngValue`, i.e. .NET Framework 4.8 `Single.ToString()` in the **current culture**. Keysounds, BGA images, and BGM carry the decoded `#WAV`/`#BMP` index (base 36, or base 62/16 when that option is active); BPM carries the BPM itself (for example `150`); STOP carries the `#STOPxx` value in 1/192-whole-note units; SCROLL/SPEED carry their defined values; mines carry the damage index, capped at 1295.
- Decoder: header must match exactly; requires at least three `CRLF`-split elements (`UBound < 2` exits) and skips the last element, so it relies on the trailing CRLF. Values are parsed with `Val`, which is culture-invariant and stops at the first non-numeric character.

### BMSE legacy (`BMSE ClipBoard Object Data Format`)

Documented in BMSE's `specification/clipboard.txt` and kept as a commented-out line in 3.0: `CCC` + `A` + position as a **seven-digit decimal integer** (`Format(…, "0000000")`) + value. Positions off the 192 grid are rounded. BMSE 3.0 still decodes it. The spec file also names an older `BMSC ClipBoard Object Data Format` (`AAABBBBBBCCC`, BGM as channels 022–049) that neither audited editor decodes.

### iBMSC 3.5.5.16 (`iBMSC Clipboard Data` and `… xNT`)

Encoder `CopyNotes` in `MainWindow.vb`; columns in `EditorColumns.vb`.

- Payload: header, then each record **prefixed** by CRLF; **no trailing CRLF**. The ` xNT` suffix is written when the editor is in NT (length-based long note) input mode.
- Record: six fields separated by one space: `column position value ln hidden mine`.
  - `column`: iBMSC column index. `1` SCROLL, `2` BPM, `3` STOP, `5` 1P scratch (`16`), `6`–`12` 1P keys 1–7 (`11`–`15`, `18`, `19`), `14`–`20` 2P keys 1–7, `21` 2P scratch (`26`), `23` BGA, `24` LAYER, `25` POOR, `27 + k` the k-th BGM column. Columns 0, 4, 13, 22, 26 are measure/spacer columns and never hold notes.
  - `position`: `Double.ToString()` in the current culture. On .NET Framework 4.8 this is **15 significant digits**, so free-grid positions are rounded: 192/7 is written `27.4285714285714`.
  - `value`: `Long` equal to the underlying value × 10000: `#WAV01` → `10000`, BPM 150 → `1500000`, STOP 48 → `480000`. Non-integer BPMs survive to four decimals.
  - `ln`: in normal mode, `CInt(Boolean)`, which in VB is **`-1` for true** and `0` for false; each long-note endpoint is its own record. In xNT mode this field is instead the long-note length in position units (`Double.ToString()`), `0` for ordinary notes, and there is no tail record.
  - `hidden`, `mine`: `-1` or `0`.
- Decoder: the header must match exactly; lines split on CRLF; blank lines skipped; a record is accepted only with exactly six fields and a position inside the chart. Fields are parsed with culture-sensitive `CDbl`/`CInt`/`CLng`/`CBool`. Pasting normal-mode data into xNT mode, or the reverse, runs the editor's LN conversion.
- It also decodes BMSE legacy records, mapping `CCC` through `BMSEChannelToColumnIndex` (`EditorColumns.vb`), value × 10000, attribute `2` → long note, `1` → hidden. Attribute `3` (mine) is dropped to a normal note. `CInt` on channels such as `0SC` or `0D1` throws.

### Cross-editor behavior at the pinned revisions

| Copied from → pasted into | Result |
| --- | --- |
| BMSE 3.0 → iBMSC 3.5.5.16 | Rejected: iBMSC does not recognize the `BMSE3.0` header. |
| BMSE legacy → iBMSC 3.5.5.16 | Accepted with losses: mines become normal notes; SCROLL/SPEED throw; positions are integers. |
| iBMSC 3.5.5.16 → BMSE 3.0 | Header accepted (`modIBMSC.vb`, added in BMSE commit `fbbe66a`, 2023-11-14), but BMSE's column table assumes a **different iBMSC layout** (26 lanes per side, BGM from column 63). With pBMSC's layout (BGM from column 27) most lanes land on the wrong key or are dropped, and BGM is dropped. BMSE reads `ln` > 0 as an xNT length and adds a tail; normal-mode `-1` is treated as a plain long-note endpoint. A payload with exactly one record has no trailing CRLF, so BMSE's `UBound < 2` guard ignores it. |

### Hazards to encode as negative fixtures

- Locale: both encoders format numbers with the current culture. On a comma-decimal locale iBMSC writes `27,4285714285714`; BMSE's `Val` then reads `27`.
- Precision: iBMSC positions and xNT lengths carry 15 significant digits. A decoder must recover exact rational rows by snapping to the nearest fraction with a bounded denominator (for example ≤ 192 × 64 per measure) and diagnose anything farther than 1e-9 measure.
- `Single` values in BMSE: BPMs are stored as `Single`, so values such as `123.456` are already rounded to 7 significant digits before copying.
- Record order is internal object order in both editors; decoders must not depend on it.

Wormedit should identify these grammars independently, preserve their exact header/newline spelling in fixtures, and never label one generic “BMSE-compatible” format. The legacy BMSE, BMSE 3.0, iBMSC endpoint-pair, and iBMSC xNT-length variants each need their own capability profile.

## Stage 0 capture matrix

For each editor version selected for support, capture both copy and paste behavior for:

- a single visible note and several notes at different subdivisions;
- playable lanes, scratch lanes, long-note heads/tails, and mode-specific lanes;
- repeated BGM lanes, including leading, intermediate, and trailing `00` positions;
- direct and extended BPM events, STOP events, and any other timing objects the editor permits on its clipboard;
- `#WAV`, BPM, and STOP identifiers with and without their referenced definitions;
- selections spanning measures of different lengths and objects on a free or high-resolution grid;
- an empty selection and a selection containing an object unsupported by the other editor.

Record the raw clipboard bytes/text, platform newline form, editor/version, locale, source chart encoding, selection bounds, and resulting paste position. Re-import every captured payload into its source editor and the other editor where practical.

## Compatibility contract

- The native wormedit clipboard payload is versioned and lossless for wormedit data.
- BMSE and iBMSC encoders emit only behavior demonstrated by the selected fixtures.
- Their decoders accept documented/captured variants within explicit size and record-count limits.
- Paste first builds and validates a proposed command batch. Invalid or unsupported input leaves the document and selection unchanged.
- Cut removes notes only after a clipboard write succeeds.
- Missing resources, lane mismatches, timing objects, quantization, and BGM-layer loss appear in the paste preview rather than being silently discarded.

## Required negative fixtures

Include empty, truncated, oversized, mixed-newline, unknown-record, duplicate-record, invalid-number, out-of-range-lane, impossible-long-note, undefined-resource, and unrepresentable-position payloads. Also mutate valid captures one field at a time. Assert bounded parsing, deterministic diagnostics, no crashes or hangs, and no partial document mutation.

The grammar is now source-derived for the pinned revisions and backed by the fixtures above. Runtime captures, especially from a comma-decimal locale and from the unidentified iBMSC variant that BMSE's column table targets, would confirm the hazards rather than define the format.
