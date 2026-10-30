# Task cards: late compatibility (stage 8)

[Back to the task-card index](../tasks.md).

> **Deferred group (user decision, 2026-09-30):** these are late-stage ambitions, not MVP work. Cards still marked `deferred` require explicit selection by ID. T-173 was selected by the owner on 2026-10-28. The owner then requested the paired Quaver reader/writer for a post-1.0 PR and explicitly authorized yaml-cpp; T-174/T-175 track that work.

Stage 8 from [later work](../stages/later.md). The `.qua` format is documented in the [Quaver format reference](../../references/quaver-qua-format.md). The document model already has a
`roll` note kind and the SM/SSC codec reads and writes rolls; authoring and
ticked holds are missing. Apply the stale-plan check (rule 9) first.

## T-170 Author StepMania rolls

- **Status:** deferred (stage 7–8 ambition; was: ready)
- **Outcome:** in StepMania-family charts, a toolbar note-kind toggle makes hold entry (mouse Alt-drag and lane-key hold) create rolls instead of holds; rolls draw with a distinct striped body.
- **Read first:** `include/wormedit/core/document.h` (`roll`), `src/core/interaction.cpp` (hold entry), `src/ui/nuklear/compose/compose_canvas.cpp` (hold body drawing), `src/formats/stepmania.cpp`.
- **Files owned:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `tests/core/interaction_tests.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose/compose_canvas.cpp`, `src/ui/nuklear/compose/compose_toolbar.cpp`.
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

- **Status:** done (owner approved 2026-10-28; fixtures and focused verification complete)
- **Completion evidence (2026-10-28):** Four original positive and ten negative CRLF fixtures with per-file expectations. Newly compiled fixture-inventory translation unit: 3 cases / 179 assertions passed; LF mutation rejected and restored. Existing Stage 0 CTest registrations: 3/3 passed; documentation: 5/5 passed after building the manual-reference tool. All 14 files parse as YAML; each negative isolates its documented constraint. clang-tidy 23.1.3 reports only the unchanged BMS-test complexity 29; baseline comparison passed with locally adapted Python bindings. Full core rebuild was not completed; the new test was compiled and linked separately against the existing Catch2 libraries. Parser implementation remains T-174.
- **Outcome:** hand-written positive and negative `.qua` fixtures with expected semantics in the fixture README, covering every construct in the format reference.
- **Read first:** `docs/references/quaver-qua-format.md`, `tests/fixtures/README.md`, `tests/core/fixture_inventory_tests.cpp`.
- **Files owned:** new `tests/fixtures/quaver/` (fixtures), `tests/fixtures/README.md`, `tests/core/fixture_inventory_tests.cpp`, `.gitattributes` (add `*.qua -text` so CRLF survives), this card.
- **Steps:**
  1. Positive: 4K with BPM change and 3/4 signature; 7K+scratch with long notes; keysounds + `CustomAudioSamples` + `SoundEffects`; denormalized and normalized SV with `InitialScrollVelocity`; `ScrollSpeedFactors`; a `TimingGroups` map with `$Global` and one custom `!ScrollGroup`; bookmarks; editor layers; mines with `QuaVersion: 1`; every `HitSound` flag.
  2. Negative: no hit objects; no timing points; sample index 0 and out of range; `EndTime <= StartTime`; `QuaVersion: 2`; unknown `Mode`.
  3. Write all fixtures by hand (CRLF, defaults omitted) and record expected semantics per file.
- **Invariants:** fixtures are original; do not copy Quaver.API test files.
- **Reject/failure behavior:** none (fixtures only).
- **Verify:** `ctest --test-dir build --output-on-failure -R 'Stage 0'`.
- **Out of scope:** parser.

## T-174 Read `.qua` files

- **Status:** done (post-1.0 draft PR #63, 2026-10-28; depends on fixture PR #56)
- **Outcome:** a bounded, transactional `.qua` reader imports every construct in the reference into the song document, retaining unmodeled data (timing groups, `LegacyLNRendering`, map IDs) as format fields for round trips.
- **Read first:** `docs/references/quaver-qua-format.md`, `src/formats/osu_mania.cpp` (millisecond-to-measure rewiring), `include/wormedit/formats/osu_mania.h`.
- **Files owned:** new `include/wormedit/formats/quaver.h`, new `src/formats/quaver.cpp`, new `tests/formats/quaver_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `src/formats/quaver_yaml.cpp`, `src/formats/quaver_internal.h`, `cmake/dependencies/yaml_cpp.cmake`, `cmake/third-party-notices.cmake`, `src/sys/project/project_open.cpp`, `src/sys/project/project_file_types.h`, `tests/sys/project_open_tests.cpp`, `src/ui/nuklear/app/frontend.cpp`, `docs/references/quaver-qua-format.md`, `tests/fixtures/README.md`, this card.
- **Steps:**
  1. YAML: pinned yaml-cpp 0.8.0, explicitly authorized by the owner on 2026-10-28 after the required dependency consultation; bounded event preflight rejects aliases and excess structure.
  2. Apply the reference's load rules (defaults, validation, version gate, stable sort).
  3. Rebuild measures from timing points with the osu!mania approach; convert denormalized SV explicitly.
  4. One test per fixture from T-173.
- **Invariants:** exact millisecond times retained as source spellings.
- **Reject/failure behavior:** any validation error publishes nothing.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'quaver'`.
- **Validation:** all 14 fixtures, native persistence and Quaver/conversion/loss tests pass (64 cases, 3,872 assertions); CLI accepts a valid `.qua` and rejects an invalid hold without publishing. The whole-project GATE builds and passes complexity/whitespace but has seven reproducible failures outside these owned files (four clipboard cases, one semantic-layer performance case, two render segfaults).
- **Implementation:** bounded YAML reader, load rules, timeline/lane/resource/marker mappings, retained Quaver-only fields, experimental project import. Supporting-file ownership expanded by the coordinator to wire admission, the file filter, dependency/license and regression coverage.
- **Out of scope:** writer (T-175), per-note timing groups in the editor.

## T-175 Write `.qua` files

- **Status:** done (post-1.0 draft PR #63, 2026-10-28; paired with T-174)
- **Outcome:** a deterministic writer emits CRLF YAML with default-valued fields omitted, `QuaVersion` computed, and semantic re-import equality for every positive fixture; the format is added to the conversion pipeline.
- **Read first:** `docs/references/quaver-qua-format.md` (write rules), `include/wormedit/formats/conversion.h`, `src/formats/conversion.cpp`.
- **Files owned:** `src/formats/quaver.cpp`, `include/wormedit/formats/quaver.h`, `tests/formats/quaver_tests.cpp`, `src/formats/conversion.cpp`, `include/wormedit/formats/conversion.h`, `tests/formats/conversion_tests.cpp`, `src/formats/quaver_writer.cpp`, `src/formats/quaver_internal.h`, `src/tools/wormedit_convert.cpp`, `src/formats/conversion_publication.cpp`, `tests/formats/conversion_publication_tests.cpp`, `docs/references/quaver-qua-format.md`, this card.
- **Steps:** 1. Writer. 2. Round-trip tests. 3. Loss diagnostics for BMS/SM-only features (stops, warps, BGA).
- **Invariants:** unedited imports re-export semantically equal.
- **Reject/failure behavior:** unrepresentable content rejects with a classified loss.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'quaver|conversion|loss'`.
- **Validation:** all 14 fixtures and the 64 Quaver/conversion/loss cases pass (3,872 assertions), including deterministic round trips, asset discovery, classified refusals and CLI reimport. The whole-project GATE has seven reproducible failures outside these owned files, recorded under T-174.
- **Implementation:** CRLF writer, stable output ordering, retained-data re-export, classified refusals and paired conversion/reimport; shared per-chart serialization and reimport acceptance remove duplicate pipeline plumbing. Supporting-file ownership expanded by the coordinator for publication, CLI and contract documentation. The PR remains a post-1.0 draft and the stable output catalog stays disabled.
- **Out of scope:** Quaver online metadata.
