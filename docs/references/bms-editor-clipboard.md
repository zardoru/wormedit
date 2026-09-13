# BMSE and iBMSC clipboard compatibility notes

Research recorded for planning on 2026-09-12:

- [BMSE editing help](https://hitkey.nekokan.dyndns.info/bmse_help_full/edit.html)
- [BMSE source repository](https://github.com/Nekokan/BMSE)
- [BMSE releases](https://github.com/Nekokan/BMSE/releases)
- [iBMSC README in the pBMSC repository](https://github.com/Shimi9999/pBMSC/blob/master/README.md.iBMSC)

The iBMSC documentation explicitly describes BMSE clipboard-data compatibility, but public descriptions and release history do not establish one complete, version-independent grammar for every object type wormedit needs. The implementation must therefore be based on captured payloads and golden fixtures from named BMSE and iBMSC versions rather than an inferred format.

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
