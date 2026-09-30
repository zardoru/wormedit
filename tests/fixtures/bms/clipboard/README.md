# BMSE and iBMSC clipboard payloads (source-derived)

These payloads were **derived from the encoder source code**, not captured from a
running editor. Each byte follows the copy routine at the pinned commit; the
derivation is documented in
[`docs/references/bms-editor-clipboard.md`](../../../../docs/references/bms-editor-clipboard.md).
Captures from real releases may later be added beside them as `*-capture-*.clip`.
If a capture ever disagrees with a file here, the capture wins.

`.clip` files are marked `-text` in `.gitattributes`: line endings are CRLF and must
not be normalized.

## Shared scenario

All four files encode the same selection. The earliest selected object is in
measure `#001`, so every position is relative to that measure's start. Positions
use the editors' shared unit: 192 per 4/4 measure.

| Object | Row (of 192) | BMS meaning |
| --- | --- | --- |
| BPM change | 0 | BPM 150 |
| 1P key 1, visible | 0 | `#WAV01` |
| 1P key 2, long note head | 0 | `#WAV04` |
| 1P key 3, invisible | 24 | `#WAV05` |
| 1P key 5, visible, free grid | 192/7 | `#WAV06` |
| 1P scratch, visible | 48 | `#WAV02` |
| 1P key 4, mine | 72 | damage `01` |
| 1P key 2, long note tail | 96 | `#WAV04` |
| first BGM lane | 96 | `#WAV03` |
| STOP | 144 | `#STOPxx 48` |

## Files

| File | Tool / version | Source commit | Header | Bytes | SHA-256 |
| --- | --- | --- | --- | --- | --- |
| `bmse-3.0-mixed.clip` | BMSE 3.0 | Nekokan/BMSE `26826aff6d51` `frmMain.vb` `CopyToClipboard` | `BMSE3.0 ClipBoard Object Data Format` | 271 | `4d90239cad31d36def237d363d0b948cf661d53fe1e15156c587d3b972bdc7b1` |
| `bmse-legacy-mixed.clip` | BMSE before 3.0 | same file, commented-out legacy line, plus `specification/clipboard.txt` | `BMSE ClipBoard Object Data Format` | 178 | `169183e26fcdf9bb60d702032a9847f0dab891fbaa10894a87650b6da25645e3` |
| `ibmsc-3.5.5.16-mixed.clip` | iBMSC 3.5.5.16 (pBMSC) | Shimi9999/pBMSC `06aaf7438dbc` `MainWindow.vb` `CopyNotes` | `iBMSC Clipboard Data` | 221 | `e0c2a7137c9e7b2f0b71af60f120764a8cddfde1ba9d7fa4998694cbb24fcb32` |
| `ibmsc-3.5.5.16-xnt-mixed.clip` | iBMSC 3.5.5.16, xNT input mode | same | `iBMSC Clipboard Data xNT` | 206 | `d7c1787ed44323022119851a8323024c4c6f13a61c950d4d8b77b8d5a4824d15` |

OS and locale: not applicable to source derivation. Every file assumes an
invariant/en-US culture (`.` as decimal separator); see the locale hazard in the
reference.

## Confidence

- `bmse-3.0-mixed.clip`, both iBMSC files: high. Every field follows a direct
  string concatenation in the pinned encoder.
- `bmse-legacy-mixed.clip`: medium. The legacy encoder survives only as a
  commented-out line in BMSE 3.0 that uses 3.0's channel numbering; the VB6-era
  releases that actually emitted it were not audited. The integer position
  rounds 192/7 to `0000027`.
- Record order is the editors' internal object order and carries no meaning.
  BMSE files list objects by channel; iBMSC files list them by position.
