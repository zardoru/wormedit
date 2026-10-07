# BMSON conversion through slice spans: osu!mania, BMS, and StepMania keysounds (2026-10-07)

[Back to the task card index](../tasks.md).

User direction (2026-10-07):

- **BMSON → osu!mania** works in every case: slice the stems, and reuse one rendered file for each repeated slice span.
- **BMSON → BMS** works if and only if the keysound count, slices included, fits the BMS ID capacity.
- **StepMania** has a keysound model (`#KEYSOUNDS`), but its limits are unknown.

What exists:

- `src/audio/slice_render.cpp` renders slice ranges to WAV for the Sounds tab (T-334).
- `derive_sound_channel_slices` gives exact slice spans (T-105 audits its rules).
- `bms_identifier_capacity` reports the free ID slots.
- osu!mania import already reads per-object hit samples and storyboard `Sample` events.

No conversion path slices stems yet. `stepmania.cpp` does not read `#KEYSOUNDS` at all.

Continuation stems need slicing for BMS as well, because BMS cannot express continue points. Each slice becomes its own `#WAV`, and slices count against the limit.

## T-465 Build a shared slice-span stage that reuses identical spans

- **Status:** done (2026-10-07; stale search found no shared deduplicated conversion span stage; implementation and isolated gates complete)
- **Outcome:** a pure function maps a BMSON chart to a list of distinct source spans, each identified by (asset, begin frame, end frame). Every audible event maps to one span index, so identical spans share one entry; continuations at/past EOF and soundless gameplay events keep an explicit silent mapping. A render step writes one WAV per span through `render_audio_slices`. A test checks that a stem with repeated identical slices yields one file.
- **Read first:** `include/wormedit/core/sound_slice.h`, `include/wormedit/audio/slice_render.h`, `src/formats/conversion.cpp`, `src/core/sound_slice.cpp`.
- **Files owned:** a new `include/wormedit/formats/slice_spans.h` and `src/formats/slice_spans.cpp`, `src/formats/conversion.cpp` (stage hook only), `CMakeLists.txt`, a new `tests/formats/slice_spans_tests.cpp`, `tests/CMakeLists.txt` (root-owned manifests), `src/formats/slice_span_render.cpp`, `tests/formats/slice_span_render_tests.cpp` (root-approved pure/core and decoder/render dependency split), this card.
- **Ownership:** Sol owns the public contract, plan/render implementation and render tests; Luna implemented `tests/formats/slice_spans_tests.cpp` under an exclusive written brief and released it; Sol reviewed/corrected the actual file and executed checks. Root patches manifests. Other hybrid source/model paths stay read-only.
- **Frame contract:** derive native source boundaries then normalize with nearest-ties-even to explicit 44.1 kHz decoder-output frames. Deduplicate normalized (asset, begin, end). Only legitimate final-length resampler rounding is clamped. Continuations at/past EOF keep explicit silent mappings without creating empty spans/WAVs. Explicit stem links are authoritative even with restarts only; retained c=true channels also derive stem slices for compatibility. Unlinked all-restart packed markers map the complete native source clip. Linked semantic objects retain their channel-event mapping; unlinked audible notes/BGM use the complete source asset; soundless notes/BGM retain nullopt mappings without requiring decoded geometry.
- **Steps:**
  1. Derive the slices for every channel, and map each restart or continue event to the span it starts.
  2. Remove duplicate spans by exact (asset, begin, end). A packed one-shot is a span from 0 to its end.
  3. Name the files deterministically (`<stem>_<index>.wav`), with no collision against existing files.
  4. Test: repeated spans, a one-shot, a continuation chain, and identical output for identical input.
- **Invariants:** the input document is not modified. Rendered PCM equals the source frames bit-exactly before encoding.
- **Reject/failure behavior:** an undecodable asset rejects the stage and names the asset. No files are written.
- **Verify:** `ctest --test-dir build --output-on-failure -R slice_spans`.
- **Evidence (2026-10-07):** fresh isolated normal dependency build passed; final model rebuild passed 267/267 and final stage rebuild 12/12. Focused stage/render/documentation tests passed 18/18 (`tests-final.log`); decoder-OFF normal core build and pure/documentation tests passed 11/11 (`build-noaudio-final.log`, `tests-noaudio-final.log`). Configured cognitive-complexity tidy passed all five implementation/test files with no new findings (`tidy-final.log`). Logs are in `/home/zardoru/devel/wormedit-scratch-builds/hybrid-oct7/spans`. Real decoded-frame WAV byte comparisons cover deduplication and overlapping spans; rejection, cancellation, existing-file preservation, silent EOF/soundless mappings, explicit restart-only stems, packed full clips, rounding, immutable input and deterministic names are covered. Renderer preflights all assets with bounded one-decoder ownership, renders each distinct span independently, then stages and publishes exclusively with rollback. Read-only production Kick planner probe passed with 514 spans/1030 mappings and unchanged serialized input before T-457 model integration (`kick-spans-eof.log`); final actual lane-classification probe belongs to root after T-458 integration.
- **Out of scope:** format writers (T-466, T-467).

## T-466 Convert BMSON to osu!mania through slice spans

- **Status:** blocked-on T-465
- **Outcome:** converting `kick_your_ass2.bmson` to osu!mania writes the `.osu` and one WAV per distinct span. Playable notes carry their span as a hit sample, and BGM events become storyboard `Sample` events. Playing the converted chart through wormedit's own preview matches the BMSON preview PCM within the resampling tolerance.
- **Read first:** `src/formats/osu_mania.cpp` (writer, `hit_sample`), `src/formats/conversion.cpp`, `include/wormedit/formats/slice_spans.h`, [T-289](export.md) (osu! sample and volume handling).
- **Files owned:** `src/formats/osu_mania.cpp`, `src/formats/conversion.cpp`, `tests/formats/conversion_tests.cpp`, `tests/formats/conversion_loss_matrix_tests.cpp`, this card.
- **Steps:**
  1. Run the slice stage, then map playable events to object hit samples and BGM events to `Sample` events.
  2. Update the loss matrix: BMSON→osu! is now lossless for audio and lossy only where osu! has no equivalent.
  3. Test with a fixture that has one stem, three slices with a repeat, and two one-shots, plus a preview PCM comparison.
- **Invariants:** conversions from other formats to osu!mania are unchanged.
- **Reject/failure behavior:** a failure in the slice stage rejects the whole conversion.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'conversion|osu'`.
- **Out of scope:** osu! storyboard visuals.

## T-467 Convert BMSON to BMS when the sounds fit the ID capacity

- **Status:** blocked-on T-465
- **Outcome:** a BMSON chart converts to BMS when the distinct spans fit the free `#WAV` capacity. Each span gets one ID, packed columns map to BGM columns, and stem lanes map to BGM columns in lane order. Over capacity, the conversion is refused before anything is written, with the message "needs N sounds, BMS holds M".
- **Read first:** `src/formats/bms_writer.cpp`, `include/wormedit/core/resource_assignment.h` (`bms_identifier_capacity`), `include/wormedit/formats/slice_spans.h`, `src/formats/conversion.cpp`.
- **Files owned:** `src/formats/bms_writer.cpp`, `src/formats/conversion.cpp`, `tests/formats/conversion_tests.cpp`, `tests/formats/conversion_loss_matrix_tests.cpp`, this card.
- **Steps:**
  1. Count the spans and compare them with the capacity of the target profile: base-36 by default, and base-62 only if the beatoraja profile (T-282) allows it.
  2. Allocate IDs in natural span order, as T-351 does.
  3. Test: a chart that fits, a chart over capacity (refused atomically), and a stem with continuation points that becomes N sliced IDs.
- **Invariants:** BMS→BMS round trips are unchanged.
- **Reject/failure behavior:** a refusal writes nothing.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'conversion|bms_writer'`.
- **Out of scope:** BMS→BMSON.

## T-468 Pin StepMania's keysound model in a reference

- **Status:** ready
- **Outcome:** `docs/references/stepmania-keysounds.md` documents, from the StepMania 5 source code (pinned commit):
  - the `#KEYSOUNDS` tag grammar in `.sm` and `.ssc`;
  - the per-note `[n]` index syntax;
  - autoplay and BGM keysound behavior;
  - any limits on count and index.

  It also records whether SM/SSC conversion is worth a card.
- **Read first:** `src/formats/stepmania.cpp`, [Quaver reference](../../references/quaver-qua-format.md) (shape to follow).
- **Files owned:** `docs/references/stepmania-keysounds.md`, this card.
- **Steps:**
  1. Read the NoteData loader and writer in the StepMania 5 source at a pinned commit, and quote file:line for each rule.
  2. Record the limits, or that there are none, along with how notes without keysounds and BGM-only keysounds behave.
  3. Recommend a follow-up card or none.
- **Invariants:** no code changes.
- **Reject/failure behavior:** a rule that cannot be confirmed in the source is marked unconfirmed, not guessed.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.
