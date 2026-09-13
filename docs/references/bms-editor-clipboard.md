# BMSE and iBMSC clipboard compatibility notes

Research recorded for planning on 2026-09-12:

- [BMSE editing help](https://hitkey.nekokan.dyndns.info/bmse_help_full/edit.html)
- [BMSE source repository](https://github.com/Nekokan/BMSE)
- [BMSE releases](https://github.com/Nekokan/BMSE/releases)
- [iBMSC README in the pBMSC repository](https://github.com/Shimi9999/pBMSC/blob/master/README.md.iBMSC)

The iBMSC documentation explicitly describes BMSE clipboard-data compatibility, but public descriptions and release history do not establish one complete, version-independent grammar for every object type wormedit needs. The implementation must therefore be based on captured payloads and golden fixtures from named BMSE and iBMSC versions rather than an inferred format.

## Source audit: formats are already versioned and asymmetric

A 2026-09-13 source audit pinned [BMSE 3.0.0 at `26826aff6d51`](https://github.com/Nekokan/BMSE/blob/26826aff6d51f34e0aa24284a68d9e713db6a007/frmMain.vb) and [iBMSC 3.5.5.16 at `06aaf7438dbc`](https://github.com/Shimi9999/pBMSC/blob/06aaf7438dbca3c8a0611bd13d134f6d1ce04a28/iBMSC/MainWindow.vb). These sources establish useful bounds for capture work, but source-derived strings are not substitutes for clipboard captures from running releases.

- BMSE 3.0 writes `BMSE3.0 ClipBoard Object Data Format`, followed by CRLF-separated records. Each record concatenates a three-character channel, one attribute digit, a 16-character uppercase hexadecimal encoding of the relative-position `Double` bits, and a culture-formatted value. Its reader separately accepts the old `BMSE ClipBoard Object Data Format` seven-decimal-position form and both iBMSC headers.
- iBMSC writes `iBMSC Clipboard Data` or `iBMSC Clipboard Data xNT`. Each following CRLF-separated record has six space-separated fields: column index, relative vertical position, internal value, long-note flag or xNT length, hidden flag, and mine flag. Its reader recognizes the old BMSE header, but the audited revision does not recognize BMSE 3.0's new header.
- Compatibility is therefore directional for the audited revisions: BMSE 3.0 reads iBMSC records, while iBMSC 3.5.5.16 cannot directly read BMSE 3.0 records. Locale-sensitive decimal formatting is also part of the legacy surface and must be tested explicitly.

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

The exact grammar and version scope remain open until these samples are checked into the test fixture set.
