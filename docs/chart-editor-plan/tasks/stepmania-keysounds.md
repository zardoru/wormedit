# StepMania keysounds: import, export and conversion (2026-10-08)

[Back to the task card index](../tasks.md).

User direction (2026-10-08): expand the plan for importing StepMania keysounds,
following the [StepMania keysound reference](../../references/stepmania-keysounds.md)
written for [T-468](bmson-conversion.md#t-468-pin-stepmanias-keysound-model-in-a-reference).

The model is small. `.sm` and `.ssc` carry one song-level `#KEYSOUNDS:` list of file
names. Any note may be followed by `[n]`, a zero-based index into that list. The `K`
symbol is an *autokeysound*: a note that is not judged, not drawn in gameplay, and
plays its keysound when it crosses the receptors. StepMania puts no limit on the
count or the index. An out-of-range index plays nothing.

What exists today:

- `src/formats/stepmania.cpp` does not read `#KEYSOUNDS`. Worse, `normalize_note_row`
  rejects every keysounded chart: a `[n]` suffix makes the row longer than the column
  count, and `K` is not an accepted symbol. Such files fail to import at all.
- The writer reports note sounds as unsupported (`stepmania.cpp`, "StepMania note sound
  bindings are not emitted") and BGM events as a warning-level loss.
- osu!mania import already turns per-object hit samples into sample resources
  (`resolve_hit_samples` in `src/formats/osu_mania.cpp`), which is the pattern to follow.
- BMSON and BMS conversion slice stems through slice spans (T-465 to T-467).

Mapping decision (coordinator proposal, 2026-10-08; the user may override): a keysounded
tap, hold or lift keeps its sound as `note_t::sound`. A `K` autokeysound becomes a BGM
event, because it is an unjudged sound at a row, which is exactly what a BGM event is.
Its source column is kept in provenance so export can write it back on the same column.

Order: T-478, then T-479, then T-480. T-481 waits on T-480.

## T-478 Import `#KEYSOUNDS` and per-note `[n]` indices from SM and SSC

- **Status:** done
- **Result (2026-10-08):** `.sm` and `.ssc` import `#KEYSOUNDS` (SSC `\#` escape, placeholders warn) and per-cell `[n]` through a cell tokenizer; referenced files register as sample resources in list order, notes and hold heads carry `sound`, `[n]` stays in provenance spelling; out-of-range index warns with measure and row, malformed suffix is `invalid_notes`, `K` still rejected. Fixtures `keysounds.sm`/`.ssc`; 5 new tests, all 35 stepmania/format/documentation tests and the full suite (2814) pass.
- **Outcome:** an `.sm` or `.ssc` file with `#KEYSOUNDS` and `[n]` suffixes imports instead of being rejected. Each listed file becomes a sample resource, and each note with a valid index carries that resource as its sound. An out-of-range or malformed index keeps the note without a sound and adds one warning that names the measure and row.
- **Read first:** [the keysound reference](../../references/stepmania-keysounds.md), `src/formats/stepmania.cpp` (`normalize_note_row`, `parse_measure_rows`, `parse_one_chart`, `make_stepmania_song`), `src/formats/osu_mania.cpp` (`resolve_hit_samples`, the resource pattern), `include/wormedit/formats/stepmania.h`.
- **Files owned:** `src/formats/stepmania.cpp`, `include/wormedit/formats/stepmania.h`, `tests/formats/stepmania_tests.cpp`, new fixtures `tests/fixtures/stepmania/keysounds.sm` and `tests/fixtures/stepmania/keysounds.ssc`, this card.
- **Steps:**
  1. Parse the song-level `#KEYSOUNDS` field: split on `,` with no trimming, as StepMania does. For `.ssc`, strip one leading `\` before a `#`. Resolve each name with `stepmania_relative_path`. An empty list entry is kept as a placeholder so the indices stay aligned, and it gets one warning.
  2. Replace the per-row character check with a tokenizer. A row is a sequence of cells, where each cell is one symbol optionally followed by `[digits]`. The column count is the cell count, not the byte length. Unterminated `[` or non-digit content is the existing `invalid_notes` error.
  3. Register one sample resource per listed file, in list order, only when at least one note references it. Set `note.sound` for each note with a valid index. Hold heads carry the sound, and tails do not.
  4. Keep the original `[n]` spelling in note provenance so an unchanged chart round-trips byte-for-byte once T-480 lands.
  5. Tests: `.sm` and `.ssc` fixtures with taps, a hold, a lift and a mine with indices; an out-of-range index (note kept, warning); a malformed `[x]` (error); the `.ssc` `\#` escape; a file without `#KEYSOUNDS` that imports exactly as before.
- **Invariants:** charts without keysounds import byte-identically to today (same IDs, timing and diagnostics). Event-limit and size caps still apply, and `[n]` does not count as an event.
- **Reject/failure behavior:** a malformed suffix rejects the chart with the existing diagnostic code and no partial document. A missing keysound file is a warning, never an error, because SM ignores missing files at play time.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'stepmania|^formats\.'`.
- **Out of scope:** `K` autokeysounds (T-479) and export (T-480).

## T-479 Import `K` autokeysounds as BGM events

- **Status:** done
- **Result (2026-10-08):** `K` cells import as BGM events on `BGM n` source lanes (one per concurrent `K` in a row, left to right, reused across rows); provenance spelling is `K[n] column c`; no gameplay note is created; an index-less or out-of-range `K` imports as nothing with one warning.
- **Outcome:** each `K` cell imports as a BGM event at its row that plays the indexed keysound. Its StepMania column is kept in provenance. Gameplay lanes stay empty at that cell, so judged note counts match StepMania's.
- **Read first:** the reference's "Autokeysound" section, `src/formats/stepmania.cpp` after T-478, `include/wormedit/core/document.h` (`bgm_event_t`, `bgm_source_lane_t`), the BMS importer's BGM lane creation in `src/formats/bms.cpp`.
- **Files owned:** `src/formats/stepmania.cpp`, `include/wormedit/formats/stepmania.h`, `tests/formats/stepmania_tests.cpp`, the T-478 fixtures, this card.
- **Steps:**
  1. Accept `K` in the cell tokenizer.
  2. Create BGM source lanes on demand. Use one lane per concurrent autokeysound at a row, placed left to right and reused across rows, the way the BMS importer fills `#xx01` columns.
  3. Record the source column and spelling in the event's provenance.
  4. A `K` without a valid index imports as nothing and adds one warning, because it would play nothing in StepMania.
  5. Tests: autokeysounds on two columns at one row; one on a column that also holds a hold body; an index-less `K`; judged note counts unchanged.
- **Invariants:** no gameplay note is created for a `K` cell. Imports without `K` are unchanged.
- **Reject/failure behavior:** as T-478.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'stepmania|^formats\.'`.
- **Out of scope:** export (T-480).

## T-480 Write `#KEYSOUNDS`, `[n]` and `K` on SM and SSC export

- **Status:** done
- **Result (2026-10-08):** SM and SSC export writes one `#KEYSOUNDS` list (original list kept and extended, first-use order otherwise, SSC `\#` escape), `[n]` on sounded heads (original spelling kept when the index is unchanged, unresolvable original indices kept), and BGM events as `K` on their provenance column or the first free column; a full row or an unwritable sound path is a warning-level loss. The two "not emitted" diagnostics are gone. Both T-478 fixtures (now in writer-canonical form) and an inline `K` source round-trip byte-for-byte; new tests in `stepmania_tests.cpp` and `conversion_loss_matrix_tests.cpp`.
- **Outcome:** exporting a chart with note sounds and BGM events to SM or SSC writes one `#KEYSOUNDS` list, a `[n]` suffix on each sounded note, and BGM events as `K` cells. An imported keysound chart that was not edited round-trips byte-for-byte. Anything that cannot be written is reported as a warning-level loss, not an error.
- **Read first:** the reference's grammar and writer sections, `src/formats/stepmania.cpp` (`note_map_text`, the note-sound and BGM loss reports), `include/wormedit/formats/conversion.h` (loss reporting).
- **Files owned:** `src/formats/stepmania.cpp`, `include/wormedit/formats/stepmania.h`, `tests/formats/stepmania_tests.cpp`, `tests/formats/conversion_loss_matrix_tests.cpp`, this card.
- **Steps:**
  1. Build the keysound list from the referenced audio resources in first-use order, or in the original list order when every entry has StepMania provenance. Write the `.ssc` `\#` escape for a first name that starts with `#`.
  2. Emit `[n]` after each sounded note head.
  3. Place each BGM event as a `K` on its provenance column when that cell is free. Otherwise use the first free column at that row, the same search StepMania's `PlaceAutoKeysound` uses. If no column is free, report the event as lost.
  4. Remove the two "not emitted by this codec slice" diagnostics, and add loss rows for BGM events dropped by step 3 and for sounds whose files cannot be expressed as a relative path.
  5. Tests: a round trip of both T-478 fixtures (byte-identical); a BMS-sourced chart with sounded notes and BGM; a full row that forces a reported loss.
- **Invariants:** charts without sounds or BGM export byte-identically to today.
- **Reject/failure behavior:** a writer failure publishes nothing (existing atomic publish).
- **Verify:** `ctest --test-dir build --output-on-failure -R 'stepmania|conversion|^formats\.'`.
- **Out of scope:** slicing BMSON stems (T-481).

## T-481 Convert BMSON stems to StepMania keysounds through slice spans

- **Status:** done
- **Result (2026-10-08):** `conversion.cpp` now runs the T-466 slice stage for the StepMania target (`target_slices_stems`, same scratch-to-playable layout step as osu!mania, no capacity check), so each distinct span is one rendered WAV listed once in `#KEYSOUNDS`; the existing CLI/publication paths already handle the target. New test `BMSON slice conversion writes StepMania keysounds with one file per distinct span` (4 spans each listed once, note/BGM sound counts equal the osu!mania conversion's, source BMSON unchanged). Actual Kick via `wormedit-convert` to SSC writes 514 WAVs and 514 distinct `#KEYSOUNDS` entries, but StepMania has no free column at 4 rows, so 4 BGM events are reported as a warning-level loss (T-480 `K` placement), not retained (accepted as an inherent SM limit: `K` needs a free column).
- **Approved supporting scope (coordinator, 2026-10-08):** `tests/formats/conversion_loss_matrix_tests.cpp`, to give the first T-480 StepMania test slice sources (it now checks `#KEYSOUNDS:` and `.wav` instead of the unsliced `tone.wav`) and to source the unwritable-path test from a BMS file with a comma in its WAV name, since sliced BMSON gets generated safe names.
- **Outcome:** a BMSON chart with stems exports to SM or SSC by slicing stems into spans, exactly as the osu!mania path does (T-466). Each distinct span becomes one rendered file in `#KEYSOUNDS`. There is no capacity refusal, because StepMania has no keysound limit.
- **Read first:** [T-466](bmson-conversion.md) and its result, `include/wormedit/formats/slice_spans.h`, `src/formats/conversion.cpp`, `src/formats/conversion_publication.cpp`.
- **Files owned:** `src/formats/conversion.cpp`, `include/wormedit/formats/conversion.h`, `src/formats/conversion_publication.cpp`, `tests/formats/conversion_tests.cpp`, `tests/formats/conversion_publication_tests.cpp`, this card.
- **Steps:**
  1. Reuse T-466's span planning and publication for the StepMania target. Packed columns map to BGM, and BGM becomes `K` through T-480.
  2. Tests: the actual Kick BMSON fixture exports with every voice/onset tuple retained, like the T-466 evidence; a repeated span reuses one file.
- **Invariants:** the original BMSON is never modified, and generated WAVs publish atomically with the chart.
- **Reject/failure behavior:** a render failure publishes nothing.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'conversion|slice_span'`.
- **Out of scope:** StepMania-specific mixing or volume.
