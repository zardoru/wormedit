# Byte-authored BMS encoding fixtures

These are raw BMS byte streams, not UTF-8 text files. `byte-source/*.hex` contains the authoritative lowercase hexadecimal bytes; recreate a `.bms` file with `xxd -r -p byte-source/<name>.hex <name>.bms`. The recipes use LF (`0A`) line endings, except the truncated CP932 case, which deliberately ends at EOF after its incomplete lead byte. The inventory test opens every sample in binary mode and checks its path without decoding it.

| Fixture | Expected decoded text / outcome | Byte and provenance expectation |
| --- | --- | --- |
| `utf8-bom.bms` | Strict UTF-8 after removing the signature: `#TITLE UTF-8 BOM / 雪`, `#BPM 120`, `#WAV01 fixture.ogg`, `#00011:01`, each followed by LF. | Begins `EF BB BF`; retain that a UTF-8 BOM was present and preserve original bytes for source round-trip. |
| `cp932-shift-jis.bms` | Strict CP932: `#TITLE 日本語譜面`, `#ARTIST 音楽家`, `#BPM 120`, `#WAV01 fixture.ogg`, `#00011:01`, each followed by LF. | Encoded with CP932 (Windows-31J, in the Shift-JIS family); retain the chosen source codec and original bytes. The Japanese text uses characters accepted by both common CP932 and Shift-JIS decoders. |
| `euc-kr.bms` | Strict EUC-KR: `#TITLE 한국어`, `#ARTIST 음악`, then the same three ASCII chart lines as above, each followed by LF. | Encoded with EUC-KR; retain codec provenance and original bytes. |
| `cp949-extension.bms` | Strict CP949/UHC: `#TITLE 뷁`, `#ARTIST 한국어`, then the same three ASCII chart lines as above, each followed by LF. | Encoded with CP949/UHC. The title's `뷁` is representable in CP949 but not EUC-KR, so this sample must not be silently relabeled EUC-KR. |
| `windows-1252.bms` | Strict Windows-1252: `#TITLE café déjà`, `#ARTIST Björk`, then the same three ASCII chart lines as above, each followed by LF. | The non-ASCII letters use bytes `E9`, `E9`, `F6`; retain Windows-1252 provenance and the original bytes. |
| `ascii-ambiguous.bms` | The ASCII text `#TITLE Plain ASCII`, then the same three ASCII chart lines as above, each followed by LF. | All bytes are in `00`–`7F`; the content alone cannot distinguish UTF-8, ASCII, CP932, EUC-KR, CP949, or Windows-1252. Do not infer source codec from this sample. |
| `invalid-utf8-after-bom.bms` | No strict UTF-8 decoded text: after the BOM, the title contains malformed sequence `E2 28 A1`; decoding must return an error while preserving the original bytes. | `EF BB BF` declares the UTF-8 path; `E2 28 A1` is not a valid UTF-8 sequence. Do not silently replace bytes or retry another codec. |
| `invalid-cp932-truncated-lead.bms` | No strict CP932 decoded text: the byte `82` at EOF is an incomplete lead byte; decoding must return an error while preserving the original bytes. | Expected codec is CP932; stream ends with the single byte `82`, without a trail byte or line terminator. |

These expectations describe decoding and provenance only. They do not claim that a reader/writer currently supports these byte streams or that a non-ASCII title changes chart semantics.
