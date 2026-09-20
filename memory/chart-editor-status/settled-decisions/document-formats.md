# Settled decisions: document and formats

[Back to the settled-decision index](../settled-decisions.md).

- Support authoring and semantic round trips for BMS-family charts, `.sm`, `.ssc`, osu!mania `.osu`, and BMSON 1.0.
- Keep Quaver rhythm-game chart support as a late-stage read/write adapter; it does not displace the initial format milestones.
- Store several ordered difficulties in one native project. Bundle compatible SM/SSC difficulties in one output and emit deterministic transactional per-difficulty output sets for BMS-family, BMSON, and osu!mania targets.
- Provide a dedicated Metadata tab with shared song fields, per-difficulty fields, inheritance/overrides, and visibly grouped target-format fields. Preserve BMS `#SUBARTIST`/`#STAGEFILE` and osu!mania difficulty name, OD, HP, key count, retained AR, spelling, and provenance.
- Support osu!mania `CircleSize` values from 1 through 18 inclusive in both readers and writers using dynamic lane layouts; do not restrict support to common 4K/7K charts.
- Require an exhaustive metadata field inventory for both read and write bounds of every advertised format/profile. Each accepted field keeps scope, multiplicity, order/default/provenance and must round-trip, remain losslessly format-specific, or emit an explicit loss diagnostic; no adapter may silently omit fields outside the common normalized subset.
- Count a format/profile as supported only when both its reader and writer pass semantic import-edit-export-reimport tests; import-only adapters remain experimental.
- Store editable chart content in ordered measure chunks rather than one chart-wide event buffer.
- Store notes, holds, and autoplay/BGM lane objects in measure chunks. Keep BPM, stops, delays, warps, SV/scroll/speed, kiai, bookmarks, and other cross-measure information in separate chart-level tracks addressed by exact measure-local positions.
- Use one editable global division count per chart, expressed as divisions per standard 4/4 measure. New charts default to 960 (`192 * 5`); 192 remains supported.
- BMSON stores pulses per quarter note rather than the editor's divisions per standard measure. Map 960 editor divisions naturally to BMSON resolution 240 and 192 to 48; emit exact measure prefixes as BMSON bar lines and promote or reject rather than truncate pulses.
- Store variable measure lengths exactly. Keep the exact length ratio separate from its optional time-signature interpretation and let users resolve equivalent-meter ambiguity.
- Express measure length in BMS units (`1` = four quarter-note beats, `0.75` = three). Parse persisted decimal strings to exact reduced rationals; convert bare floating values only through a checked algorithm with fixed iteration/denominator limits and explicit approximation diagnostics.
- Show the exact BMS length and musical meter together. A user-entered meter such as `6/8` derives BMS length `0.75` but retains and displays `6/8`; length and meter remain separate fields and must agree exactly when both are supplied.
