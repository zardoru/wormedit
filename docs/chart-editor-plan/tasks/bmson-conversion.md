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

- **Status:** done (2026-10-08; integrated with editor warning exports; main build and 2,710/2,710 tests pass)
- **Outcome:** converting `kick_your_ass2.bmson` to osu!mania writes the `.osu` and one WAV per distinct span. Playable notes carry their span as a hit sample, and BGM events become storyboard `Sample` events. Playing the converted chart through wormedit's own preview matches the BMSON preview PCM within the resampling tolerance.
- **Read first:** `src/formats/osu_mania.cpp` (writer, `hit_sample`), `src/formats/conversion.cpp`, `include/wormedit/formats/slice_spans.h`, [T-289](export.md) (osu! sample and volume handling).
- **Files owned:** `src/formats/osu_mania.cpp`, `src/formats/conversion.cpp`, `tests/formats/conversion_tests.cpp`, `tests/formats/conversion_loss_matrix_tests.cpp`, `include/wormedit/formats/conversion.h` (explicit geometry/detached plans), `include/wormedit/formats/slice_spans.h`, `src/formats/slice_span_render.cpp` (decoder-only preparation/render helpers), `include/wormedit/formats/conversion_publication.h`, `src/formats/conversion_publication.cpp`, `src/formats/conversion_publication_storage.cpp` (atomic generated-WAV ownership and staging callback), `src/tools/wormedit_convert.cpp` (sliced-target CLI wiring), `tests/formats/conversion_publication_tests.cpp`, `tests/formats/slice_span_render_tests.cpp` (approved supporting transaction/PCM gates), `include/wormedit/audio/slice_render.h`, `src/audio/slice_render.cpp`, `tests/core/audio_slice_render_tests.cpp` (root-approved explicit float32 WAV option, preserving the existing PCM16 default), this card.
- **PCM audit:** real Kick has finite Ogg overshoots (kick_bass peak 1.11281), so per-span PCM16 clipping cannot meet preview PCM tolerance despite exact voice mappings. Conversion explicitly selects float32 WAV; the Sounds slicer default stays PCM16.
- **Ownership/contract:** Sol owns production and build writes. Core conversion stays detached with no filesystem/decoder dependency and accepts explicit geometry; decoder-only helpers prepare/render spans. Generated WAVs and chart bytes publish through one sibling-directory transaction with ownership/rollback. Root owns manifests and the narrow app caller patch in `src/sys/compose_actions.cpp` after Draw releases that shared path; this worker does not edit it. Luna may append pure tests in `tests/formats/conversion_tests.cpp` only after an exclusive root-approved handoff.
- **Steps:**
  1. Run the slice stage, then map playable events to object hit samples and BGM events to `Sample` events.
  2. Update the loss matrix: span audio is preserved with float32 WAV; stable-v14 millisecond timestamps and unsupported semantic structure report concrete losses. Lazer-v128 retains fractional timestamps.
  3. Test with a fixture that has one stem, three slices with a repeat, and two one-shots, plus a preview PCM comparison.
- **Invariants:** conversions from other formats to osu!mania are unchanged.
- **Reject/failure behavior:** a failure in the slice stage rejects the whole conversion.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'conversion|osu'`.
- **Evidence (2026-10-08):** fresh normal optimized core/audio/CLI build passed, followed by 109/109 conversion/osu/span/float tests (`t466-final-all-tests.log`). Decoder-OFF normal CLI/core build passed: audible BMSON with missing geometry rejects before outputs, soundless conversion succeeds, and pure conversion tests passed 17 cases/2515 assertions. Configured tidy compares existing findings without increases; every new helper/test is at most 25. Actual read-only Kick CLI writes 514 deduplicated float32 WAVs plus the `.osu`; full-tail reimport preview compares 7,008,868 frames with maximum absolute error 3.57628e-7, RMS 2.02681e-8 and zero samples above 2/32768, with all 514 voice/onset fingerprints equal (`kick-full-tail-preview-pcm.log`). Original chart and all 12 Ogg hashes are unchanged. Native 48 kHz and off-period continuation tests cover bounded phase-aligned preroll with fresh decoders; an existing reused-decoder reset transient remains outside this conversion-only fix. Logs/probes are under `/home/zardoru/devel/wormedit-scratch-builds/oct8-draw-conversion/conversion`. Atomic publication tests cover render failure, symlinks, retained/generated collisions, edited ownership, repeated export and unowned files. Root integrated the background app branch before legacy asset staging. Editor conversion losses publish with warning summary/detail; actual app dispatcher tests pass for both target families and directory/named outputs. Main build and 2,710/2,710 tests pass; native desktop interaction is unmeasured.
- **Out of scope:** osu! storyboard visuals.

## T-467 Convert BMSON to BMS when the sounds fit the ID capacity

- **Status:** done (2026-10-08; integrated with editor warning exports; main build and 2,710/2,710 tests pass)
- **Outcome:** a BMSON chart converts to BMS when distinct spans plus retained referenced sounds and any shared silent gameplay sample fit the free `#WAV` capacity. Each span gets one ID, packed columns map to BGM columns, and stem lanes map to BGM columns in lane order. Over capacity, the conversion is refused before anything is written, with the message "needs N sounds, BMS holds M".
- **Read first:** `src/formats/bms_writer.cpp`, `include/wormedit/core/resource_assignment.h` (`bms_identifier_capacity`), `include/wormedit/formats/slice_spans.h`, `src/formats/conversion.cpp`.
- **Files owned:** `src/formats/bms_writer.cpp`, `src/formats/conversion.cpp`, `tests/formats/conversion_tests.cpp`, `tests/formats/conversion_loss_matrix_tests.cpp`, `include/wormedit/formats/conversion.h` (explicit geometry/detached plans), `include/wormedit/formats/slice_spans.h`, `src/formats/slice_span_render.cpp` (decoder-only preparation/render helpers), `include/wormedit/formats/conversion_publication.h`, `src/formats/conversion_publication.cpp`, `src/formats/conversion_publication_storage.cpp` (atomic generated-WAV ownership and staging callback), `src/tools/wormedit_convert.cpp` (sliced-target CLI wiring), `tests/formats/conversion_publication_tests.cpp`, `tests/formats/slice_span_render_tests.cpp` (approved supporting transaction/PCM gates), `tests/tools/wormedit_convert_tests.cpp` (root-approved real CLI dotted/multi-chart directory regression), this card.
- **Clarification (root approved 2026-10-08):** capacity counts distinct audible spans, retained referenced WAV resources, and one shared one-frame zero float WAV iff unbound/EOF-silent gameplay needs a BMS binding. This synthetic artifact is declared separately from source spans and participates in atomic ownership/publication. Unused original channel resources are pruned only from the detached sliced export. T-282 is not implemented, so the current writer supports base-36 (1295 nonzero IDs), with no base-62 claim. The existing publication header/CLI/tests also support explicit directory semantics for dotted CLI output directories; app single-file paths retain their default interpretation.
- **Ownership/contract:** Sol owns production and build writes. Core conversion stays detached with no filesystem/decoder dependency and accepts explicit geometry; decoder-only helpers prepare/render spans. Generated WAVs and chart bytes publish through one sibling-directory transaction with ownership/rollback. Root owns manifests and the narrow app caller patch in `src/sys/compose_actions.cpp` after Draw releases that shared path; this worker does not edit it. Luna may append pure tests in `tests/formats/conversion_tests.cpp` only after an exclusive root-approved handoff.
- **Steps:**
  1. Count the spans and compare them with the capacity of the target profile: base-36 by default, and base-62 only if the beatoraja profile (T-282) allows it.
  2. Allocate IDs in natural span order, as T-351 does.
  3. Test: a chart that fits, a chart over capacity (refused atomically), and a stem with continuation points that becomes N sliced IDs.
- **Evidence (2026-10-08):** fresh normal optimized core/audio/CLI builds and final 93/93 conversion/BMS/span tests passed (`t467-last-tests.log`); all CLI tests passed 8 cases/59 assertions. Decoder-OFF normal core/CLI build passed, with 22 conversion cases/2555 assertions and the same eight CLI cases. Configured tidy on all nine changed implementation/test translation units (including headers) reports only unchanged baseline findings, including the CLI legacy 42/32 scores; every new function/test is at most 25. Core tests prove 1295 sounds fit and 1296 reject with the exact capacity message, counting four distinct spans, retained WAVs and shared EOF silence; unused original channel resources do not consume IDs, and generated silence is excluded from copying by resource identity so same-named legitimate music remains retained. CLI rejection leaves an existing destination marker as its sole file, before any decoder/render writes. Synthetic silence decodes to exactly one zero stereo frame. Native 48 kHz and off-period continuation reimports preserve strict preview PCM and authored note count with the explicit known 7-key profile; sparse BMS files can still require a conservative import profile choice. The writer’s existing natural free-ID and BGM-lane ordering is reused. Actual untouched Kick exports 514 WAVs/514 declarations into dotted directory `kick-bms-v1.2` as `chart-1.bme`; automatic reimport matches all 514 audible voice/onset tuples and 7,008,868 frames (maximum error 2.38419e-7, RMS 2.05836e-9, zero samples above 2/32768). Original chart/all 12 Ogg hashes remain unchanged. Dotted directory multi-chart publication and the actual CLI subprocess are covered; app file destinations keep their default semantics. Evidence/probes are in `/home/zardoru/devel/wormedit-scratch-builds/oct8-draw-conversion/conversion`; root owns background app wiring, main verification and cleanup. No GUI export claim.
- **Invariants:** BMS→BMS round trips are unchanged.
- **Reject/failure behavior:** a refusal writes nothing.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'conversion|bms_writer'`.
- **Out of scope:** BMS→BMSON.

## T-468 Pin StepMania's keysound model in a reference

- **Status:** done (2026-10-08; reference written from StepMania 5 commit `825467bcd81c812b33ad684dc04dd151b2d5dec3`; see `docs/references/stepmania-keysounds.md`, which recommends an SM/SSC `#KEYSOUNDS` import follow-up card)
- **Result:** Reference file `docs/references/stepmania-keysounds.md` added; no code changed. Gap confirmed: `src/formats/stepmania.cpp` has no keysound handling.
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
