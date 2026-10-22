# Task cards: late compatibility (stage 8)

[Back to the task-card index](../tasks.md).

> **Deferred group (user decision, 2026-09-30):** these are late-stage ambitions, not MVP work. Every card here is `deferred`; do not start one unless the user explicitly asks for it by ID.

Stage 8 from [later work](../stages/later.md). The `.qua` format is documented in the [Quaver format reference](../../references/quaver-qua-format.md). The document model already has a
`roll` note kind and the SM/SSC codec reads and writes rolls; authoring and
ticked holds are missing. Apply the stale-plan check (rule 9) first.

## T-170 Author StepMania rolls

- **Status:** deferred (stage 7–8 ambition; was: ready)
- **Outcome:** in StepMania-family charts, a toolbar note-kind toggle makes hold entry (mouse Alt-drag and lane-key hold) create rolls instead of holds; rolls draw with a distinct striped body.
- **Read first:** `include/wormedit/core/document.h` (`roll`), `src/core/interaction.cpp` (hold entry), `src/ui/nuklear/compose_canvas.cpp` (hold body drawing), `src/formats/stepmania.cpp`.
- **Files owned:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `tests/core/interaction_tests.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_toolbar.cpp`.
- **Steps:** 1. Add the armed hold kind to the interaction controller; union rules treat roll and hold as incompatible (reject). 2. Tests: create roll, SSC export/re-import keeps it. 3. Drawing and toggle.
- **Invariants:** non-StepMania charts never offer rolls.
- **Reject/failure behavior:** union of a roll with a hold rejects.
- **Verify:** standard build/test; `-R 'stepmania'`.
- **Out of scope:** repeated-input playtest behavior.

## T-171 Read and write SSC `TICKCOUNTS`

- **Status:** deferred (stage 7–8 ambition; was: ready)
- **Outcome:** SSC `#TICKCOUNTS` imports into a chart-scoped tick-count timing segment list and exports identically; `.sm` export reports a loss when non-default tick counts exist.
- **Read first:** `src/formats/stepmania.cpp`, `include/wormedit/formats/stepmania.h`, `include/wormedit/core/document.h` (typed timing events), `tests/formats/stepmania_tests.cpp`.
- **Files owned:** `include/wormedit/core/document.h`, `src/core/document.cpp`, `src/formats/stepmania.cpp`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `tests/formats/stepmania_tests.cpp`, `tests/formats/native_project_tests.cpp`, new fixture `tests/fixtures/stepmania/tickcounts.ssc`.
- **Steps:** 1. Fixture and failing test first. 2. `tick_count_event_t { position, ticks_per_beat }`. 3. Codec + native JSON (schema migration if needed).
- **Invariants:** hold endpoints never change when tick segments change.
- **Reject/failure behavior:** zero or negative tick counts reject on import with a diagnostic.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'stepmania|native_project'`.
- **Out of scope:** tick lane UI and preview (T-172).

## T-172 Edit and preview tick counts

- **Status:** deferred (stage 7–8 ambition; was: blocked-on T-171)
- **Outcome:** a Ticks timing lane in Timing/Compose edits tick-count segments; player preview draws tick marks along holds.
- **Read first:** `include/wormedit/core/timing_lane_controller.h`, `src/ui/nuklear/timing_panel.cpp`, `src/core/player_preview.cpp`.
- **Files owned:** `include/wormedit/core/timing_lane_controller.h`, `src/core/timing_lane_controller.cpp`, `tests/core/timing_lane_controller_tests.cpp`, `src/sys/timing/timing_view.cpp`, `src/ui/nuklear/timing_panel.cpp`, `src/core/player_preview.cpp`, `tests/core/player_preview_tests.cpp`.
- **Steps:** 1. New lane kind in the pseudo-note controller. 2. Tick positions computed exactly over hold intervals. 3. Preview drawing.
- **Invariants:** as T-171.
- **Reject/failure behavior:** timing lock applies.
- **Verify:** standard build/test.
- **Out of scope:** judgement.

## T-173 Add paired Quaver `.qua` fixtures

- **Status:** deferred (stage 7–8 ambition; was: ready)
- **Outcome:** hand-written positive and negative `.qua` fixtures with expected semantics in the fixture README, covering every construct in the format reference.
- **Read first:** `docs/references/quaver-qua-format.md`, `tests/fixtures/README.md`, `tests/core/fixture_inventory_tests.cpp`.
- **Files owned:** new `tests/fixtures/quaver/` (fixtures), `tests/fixtures/README.md`, `tests/core/fixture_inventory_tests.cpp`, `.gitattributes` (add `*.qua -text` so CRLF survives).
- **Steps:**
  1. Positive: 4K with BPM change and 3/4 signature; 7K+scratch with long notes; keysounds + `CustomAudioSamples` + `SoundEffects`; denormalized and normalized SV with `InitialScrollVelocity`; `ScrollSpeedFactors`; a `TimingGroups` map with `$Global` and one custom `!ScrollGroup`; bookmarks; editor layers; mines with `QuaVersion: 1`; every `HitSound` flag.
  2. Negative: no hit objects; no timing points; sample index 0 and out of range; `EndTime <= StartTime`; `QuaVersion: 2`; unknown `Mode`.
  3. Write all fixtures by hand (CRLF, defaults omitted) and record expected semantics per file.
- **Invariants:** fixtures are original; do not copy Quaver.API test files.
- **Reject/failure behavior:** none (fixtures only).
- **Verify:** `ctest --test-dir build --output-on-failure -R 'Stage 0'`.
- **Out of scope:** parser.

## T-174 Read `.qua` files

- **Status:** deferred (stage 7–8 ambition; was: blocked-on T-173)
- **Outcome:** a bounded, transactional `.qua` reader imports every construct in the reference into the song document, retaining unmodeled data (timing groups, `LegacyLNRendering`, map IDs) as format fields for round trips.
- **Read first:** `docs/references/quaver-qua-format.md`, `src/formats/osu_mania.cpp` (millisecond-to-measure rewiring), `include/wormedit/formats/osu_mania.h`.
- **Files owned:** new `include/wormedit/formats/quaver.h`, new `src/formats/quaver.cpp`, new `tests/formats/quaver_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. YAML: parse with a bounded subset parser (block mappings, block sequences, scalars, the `!ScrollGroup` tag) or add a pinned YAML dependency; STOP and ask before adding a dependency.
  2. Apply the reference's load rules (defaults, validation, version gate, stable sort).
  3. Rebuild measures from timing points with the osu!mania approach; convert denormalized SV explicitly.
  4. One test per fixture from T-173.
- **Invariants:** exact millisecond times retained as source spellings.
- **Reject/failure behavior:** any validation error publishes nothing.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'quaver'`.
- **Out of scope:** writer (T-175), per-note timing groups in the editor.

## T-175 Write `.qua` files

- **Status:** deferred (stage 7–8 ambition; was: blocked-on T-174)
- **Outcome:** a deterministic writer emits CRLF YAML with default-valued fields omitted, `QuaVersion` computed, and semantic re-import equality for every positive fixture; the format is added to the conversion pipeline.
- **Read first:** `docs/references/quaver-qua-format.md` (write rules), `include/wormedit/formats/conversion.h`, `src/formats/conversion.cpp`.
- **Files owned:** `src/formats/quaver.cpp`, `include/wormedit/formats/quaver.h`, `tests/formats/quaver_tests.cpp`, `src/formats/conversion.cpp`, `include/wormedit/formats/conversion.h`, `tests/formats/conversion_tests.cpp`.
- **Steps:** 1. Writer. 2. Round-trip tests. 3. Loss diagnostics for BMS/SM-only features (stops, warps, BGA).
- **Invariants:** unedited imports re-export semantically equal.
- **Reject/failure behavior:** unrepresentable content rejects with a classified loss.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'quaver|conversion|loss'`.
- **Out of scope:** Quaver online metadata.
