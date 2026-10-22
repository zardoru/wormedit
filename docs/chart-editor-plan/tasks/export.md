# Task cards: export and conversion

[Back to the task-card index](../tasks.md).

Stage 5 leftovers from [authoring/export](../stages/authoring-export.md) and the
[formats plan](../formats-persistence.md). Compose already exports an output
group through `convert_document` + `publish_conversion_group_atomic`
(`WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP` in `src/sys/compose/actions/compose_actions.cpp`).
Most remaining format work waits on user decisions; those cards are
`blocked-on-user`. Apply the stale-plan check (rule 9) first.

## T-110 Preview artifacts and losses before exporting

- **Status:** done (standalone card superseded by T-714 and T-726)
- **Completion:** T-714 made Preview use the same read-only plan as Export, including artifact
  names, staged copies, warnings, collision checks and missing-source refusals while preserving the
  no-write invariant. The remaining structured per-file sizes and complete diagnostics are now
  explicit T-726 acceptance criteria. Do not execute the stale implementation steps below.
- **Scope approval (user, 2026-10-03):** All supporting-file ownership extensions documented in this card’s audit and approval step are approved, including their integration/test linkage. This extends **Files owned** only for the existing outcome and supersedes the historical pending-approval statements below.
- **Stale-plan audit (2026-10-03):** The existing export worker converts and publishes immediately, and polling returns only a status string; there is no bounded retained preview summary. Completion needs unowned `src/sys/resource_job_state.h/.cpp` for worker/results/state, `src/sys/main.cpp` and `src/sys/application_frame_projection.h/.cpp` to pass the summary, and `src/sys/workspace_projection.cpp` for output-group projection; the owned `compose_view.cpp` projects chart scenes instead. Worker retention and revision-staleness checks also need an appropriate system/UI test owner and source-list ownership.
- **Outcome:** a **Preview export** button lists, for the selected output group, each artifact name, byte size, and every loss/diagnostic from `convert_document`, without writing files; Export stays a separate click.
- **Read first:** `include/wormedit/formats/conversion.h`, `src/sys/compose/actions/compose_actions.cpp` (export action near `convert_document`), `src/ui/frontend.h` (`compose_export_overwrite`).
- **Files owned:** `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_resources.cpp` or the unit that renders the export button, `tests/formats/conversion_tests.cpp`. User-approved supporting scope (2026-10-03): `src/sys/resource_job_state.h/.cpp`, `src/sys/main.cpp`, `src/sys/application_frame_projection.h/.cpp`, `src/sys/workspace_projection.cpp`.
- **Steps:**
  0. Scope extension approved by the user (2026-10-03).
  1. Run conversion on a snapshot off the frame thread (reuse the export worker path) and store a bounded summary (≤64 artifacts, ≤256 diagnostics) keyed by document revision.
  2. Render the summary; mark it stale when the revision changes.
- **Invariants:** preview never writes to disk.
- **Reject/failure behavior:** conversion failure shows the typed error list; Export remains available only if the preview succeeded for the current revision (show why otherwise).
- **Verify:** standard build/test; manual preview on a BMSON fixture that loses semantic layers.
- **Out of scope:** CLI (T-112).

## T-111 Fail tests on any unclassified export loss

- **Status:** done (2026-10-01; `tests/formats/conversion_loss_matrix_tests.cpp`, ctest name starts `core.loss matrix`)
- **Implementation notes:** converts every non-negative fixture to native project, BMS, SSC, SM, osu!mania and BMSON under the warn, allow and reject policies. A diagnostic is classified when it is a loss with the severity its policy implies, or a typed error that is not a request mistake. Pairs that already fail for another reason are not required to reach the loss gate. The test also checks that every successful output re-imported, that a lossy conversion under reject returns no outputs with `loss_rejected`, and that the matrix is not vacuous (some pairs convert, some with loss). No pair failed, so no new cards.
- **Outcome:** a test converts every checked-in fixture to every writer it claims to support and fails if any diagnostic lacks a loss classification.
- **Read first:** `include/wormedit/formats/conversion.h`, `tests/formats/conversion_tests.cpp`, `tests/fixtures/README.md`, `docs/chart-editor-plan/stages/authoring-export.md` (stage 5 exit condition).
- **Files owned:** new `tests/formats/conversion_loss_matrix_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Enumerate fixtures by directory; skip negative fixtures listed as such in the README.
  2. For each target, assert every diagnostic has a non-"unclassified" loss kind; tag the test name with `loss` so `-R 'loss'` selects it.
- **Invariants:** tests only; no production change.
- **Reject/failure behavior:** a failing pair is reported as a new card, not fixed here.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'loss'`.
- **Out of scope:** adding fixtures.

## T-112 Decide the converter CLI name and schema

- **Status:** done (user decision 2026-09-30: separate executable `wormedit-convert` with its own JSON request schema; no rdconverter alias. Follow-up: T-281.)
- **Outcome:** answer recorded in `docs/chart-editor-plan/decisions/open-questions.md` (question 17).
- **Read first:** `docs/chart-editor-plan/decisions/open-questions.md`.
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** recorded; follow-up added.
- **Invariants:** none.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.

## T-113 Decide BMS dialect, StepMania, and osu! profile scope

- **Status:** done (user decision 2026-09-30; see Outcome)
- **Outcome:** question 3 is answered (user decision 2026-09-30: BMS exports target beatoraja first; LR2/legacy compatibility is a separate named profile; follow-up T-282). Question 18 is answered (user decision 2026-09-30): keep the pinned StepMania 5.1 and Etterna 0.75.1 profiles; a modern AMX profile is a much later ambition. osu!mania: pin the latest lazer format version (T-291). No other profiles now.
- **Read first:** `docs/chart-editor-plan/decisions/open-questions.md`, `docs/references/stepmania-warps-and-conversion.md`, `docs/references/osu-mania-version-profiles.md`.
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** 1. STOP and ask. 2. Record; add one card per new profile, each starting with "add paired positive/negative fixtures".
- **Invariants:** no profile is advertised without paired fixtures.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.

## T-114 Move provenance-backed BMS notes across measures

- **Status:** done (already implemented: `src/formats/bms_writer.cpp:2056`; temporary runtime probes verified root-note relocation, unrelated line bytes, occupied-slot rejection, and branch protection)
- **Outcome:** moving a BMS note whose source line is uniquely owned into another measure rewrites both measure lines on export instead of rejecting.
- **Read first:** `src/formats/bms_writer.cpp`, `include/wormedit/formats/bms_writer.h`, `tests/formats/bms_writer_tests.cpp`, `docs/chart-editor-plan/stages/authoring-export.md` (BMS dialect bullet).
- **Files owned:** `src/formats/bms_writer.cpp`, `tests/formats/bms_writer_tests.cpp`.
- **Steps:**
  1. Write the failing test first: import fixture, move one note from `#001` to `#002`, export, re-import, compare.
  2. Re-emit only the affected channel lines of both measures; untouched lines keep original bytes.
- **Invariants:** branch-owned notes still reject; other lines byte-identical.
- **Reject/failure behavior:** occupied destination slot rejects with a diagnostic.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'bms'`.
- **Out of scope:** shared timing declarations.

## T-115 Decide Standard MIDI export defaults

- **Status:** deferred (user decision 2026-09-30: MIDI export is a later ambition; was: blocked-on-user)
- **Outcome:** answers for default tap duration, PPQ cap, track grouping, and loss policy (from `memory/chart-editor-status/open-decisions.md`), then a card for a MIDI writer over lane `midi_destination_t` mappings.
- **Purpose (user, 2026-09-30):** a quality-of-life tool for advanced uses (driving storyboard generation, importing chart rhythm into Blender or other tools), not an interchange target; when un-deferred, favor faithful timing and per-lane tracks over playback fidelity.
- **Read first:** `memory/chart-editor-status/open-decisions.md`, `include/wormedit/core/document.h` (`midi_destination_t`).
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** 1. STOP and ask the four defaults. 2. Record; add the writer card.
- **Invariants:** none.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.

## T-116 Decide label export and non-gameplay round-trip scope

- **Status:** done (user decision 2026-09-30; see Outcome)
- **Outcome:** questions 1 and 2 are answered (user decision 2026-09-30: labels are private author comments stored only in the native project; every exporter omits them with a non-fatal loss note, so no writer change is needed beyond that note). Question 4 is answered (user decision 2026-09-30): non-gameplay data must round-trip robustly for BMS and osu!mania (T-288, T-289); SM/SSC keeps only what the importer already preserves (no new writers; SSC is too ambitious for now).
- **Read first:** `docs/chart-editor-plan/decisions/open-questions.md`, `docs/chart-editor-plan/document-model.md` (labels paragraph).
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** 1. STOP and ask. 2. Record; add follow-up cards.
- **Invariants:** no invented label syntax in any target.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.

## T-281 Add the `wormedit-convert` command-line converter

- **Status:** done (2026-10-01; `wormedit-convert` target in `CMakeLists.txt`, `src/tools/wormedit_convert.cpp`, `tests/tools/wormedit_convert_tests.cpp`; five tool tests pass with the two GCC 13 test fixes of PR #5 applied locally)
- **Implementation notes:** usage is `wormedit-convert <request.json | ->`. Request fields: `input`, `output_directory` (must exist), `target` (`native_project`, `bms`, `stepmania`, `osu_mania`, `bmson`), `target_profile`, `stepmania` (`file_profile`, `timing_syntax`, `implementation_profile`), `charts` (zero-based indices), `branches`, `encoding`, `loss_policy`, `overwrite`. Quantization and staged assets are not exposed yet. Exit codes: 0 success, 1 conversion rejected or write failed, 2 invalid request, 3 internal error; the JSON result always carries `ok`, `artifacts`, and `diagnostics`. Output names are checked and every destination is verified before any file is written. Input is opened with `sys::open_project_file`, so BMS lane profile detection is whatever the editor does at open time. Found while testing: a plain BMS chart converted with a forced encoding exports no notes, (cause not investigated; the tool only calls the shared open and conversion code).
- **Outcome:** a separate headless executable `wormedit-convert` reads a JSON request (input file, target family and policies, chart selection, output directory, overwrite policy), runs the same `convert_document` and publication services as the editor, and prints a JSON result with artifacts and classified losses; exit code 0 only on success (user decision 2026-09-30).
- **Read first:** `include/wormedit/formats/conversion.h`, `include/wormedit/formats/conversion_publication.h`, `src/sys/project/project_open.cpp` (input parsing), `docs/chart-editor-plan/formats-persistence.md` (converter paragraph).
- **Files owned:** new `src/tools/wormedit_convert.cpp`, new `tests/tools/wormedit_convert_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Request/response schema with bounded parsing (nlohmann/json, as elsewhere). 2. Headless target (no SDL, no Nuklear). 3. Tests run the binary on fixtures (BMS → SSC, SM → osu!mania) and compare re-imported semantics.
- **Invariants:** identical output to the editor's export for the same request.
- **Reject/failure behavior:** invalid requests print a JSON error and exit non-zero without writing files.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'wormedit_convert'`.
- **Out of scope:** an rdconverter-compatible argument mode.

## T-282 Add a beatoraja BMS export profile

- **Status:** done (2026-10-21)
- **Completion:** default beatoraja and explicit LR2 player policies now share conversion and CLI selection independently of lane maps. Modern exports preserve LNMODE/PREVIEW and materialize exact SCROLL/SC (including zero and negative values); LR2 rejects unsupported statements and recipe-baked scrolls with classified diagnostics. Legacy untouched lines transcode without normalization by default; preservation is opt-out, and CP932 failures reject. Ten paired fixtures cover extensions, timing and Japanese text. The pinned player jar decodes actual UTF-8/CP932 exports with all eight notes and the expected scroll multipliers; see [the source audit](../../references/bms-export-profiles.md) for durable probe artifacts and the retained-branch/input-profile limits.
- **Verification:** GATE passes **4,400/4,400**, build, whitespace and eight-TU complexity (`/tmp/t282-gate-final.log`, `/tmp/wormedit-gate.oi7klU`); focused player profiles **92 assertions / 5 cases**, explicit BMS/loss filter **70/70**, and four CLI rejection cases pass. The final Clang preset build and **4,400/4,400** tests also pass (`/tmp/t282-clang-build-final2.log`, `/tmp/t282-clang-ctest-final.log`). Final documentation checks pass **5/5**. No complexity allowance increased.
- **Outcome:** BMS export defaults to a named `beatoraja` profile (user decision 2026-09-30): it may emit beatoraja-supported extensions (for example `#LNMODE`, `#SCROLL`/SC channel, `#PREVIEW`, extended BPM/STOP) where the document uses them, and an `lr2_compatible` profile restricts output to the classic command set with explicit loss diagnostics.
- **Read first:** `src/formats/bms_writer.cpp`, `include/wormedit/formats/bms_writer.h`, `docs/references/bms-command-memo.md`, `include/wormedit/formats/conversion.h`.
- **Files owned:** `src/formats/bms_writer.cpp`, `include/wormedit/formats/bms_writer.h`, `src/formats/conversion.cpp`, `tests/formats/bms_writer_tests.cpp`, new fixtures under `tests/fixtures/bms/` for each profile, `tests/fixtures/README.md`, `tests/core/fixture_inventory_tests.cpp`, `src/formats/bms.cpp`, `src/formats/text_codec_result.h`, `src/formats/bms_timing_directive.h`, `include/wormedit/formats/bms_export_profile.h`, `include/wormedit/formats/conversion.h`, `src/tools/wormedit_convert.cpp`, `tests/formats/fixture_encoding.h`, `tests/formats/fixture_validation_tests.cpp`, `tests/formats/conversion_loss_matrix_tests.cpp`, `docs/references/bms-export-profiles.md`, this card and `memory/chart-editor-status/resume.md`.
- **Supporting ownership (2026-10-21):** materialize/export authored `SCROLL`/`SC` values so conversion reimport retains their semantics; share the existing BMS decoder codec for untouched-source transcoding. Keep the existing explicit low-level encoding override; profile defaults apply when no override is supplied. Conversion accepts named player profiles and a typed BMS profile independent of its existing lane-profile choice; the CLI exposes `bms_profile`. Share the duplicate fixture encoding lookup before adding the new CP932 fixture to both broad validation suites.
- **Steps:** 1. Fixture-first: one paired fixture per extension, with the beatoraja behavior cited from beatoraja's source (record the commit). 2. Profile enum in the writer options; default `beatoraja`. 3. `lr2_compatible` converts or rejects each extension with a classified loss. 4. Text encoding (user decision 2026-09-30): the `beatoraja` profile writes UTF-8 for all normalized output and offers "convert untouched source lines to UTF-8" (default on for this profile, so a re-exported Shift-JIS chart becomes UTF-8). Check beatoraja's charset detection in its source (record the commit) to decide whether to write a BOM; add a fixture that beatoraja's detection reads as UTF-8. The `lr2_compatible` profile writes CP932 through iconv and rejects characters CP932 cannot represent.
- **Invariants:** unedited imports still re-export byte-identically under the profile they were imported with.
- **Reject/failure behavior:** extensions the chosen profile cannot express reject with classified losses.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'bms|loss'`.
- **Out of scope:** other players' profiles.

## T-288 Round-trip BMS non-gameplay data robustly

- **Status:** done (2026-10-01; fixture `tests/fixtures/bms/non-gameplay-roundtrip.bme`, writer test "BMS round trip keeps every non-gameplay construct across a far edit"; core suite 1002/1002 with the two GCC 13 test fixes of PR #5 applied locally)
- **Gaps found:** every listed construct already survived normalization except the `;` and `//` comment lines and unknown `#` headers, which were dropped with a warning. The writer now re-emits those root-level lines verbatim, interleaved with metadata by source order; text that is not single-line UTF-8 is still omitted with the warning. Non-UTF-8 sources are transcoded like the rest of the file (CP932 test). Untouched files were already byte-identical (`preserve_unchanged_source`); after an edit the normalized channel lines are semantically, not byte, equal.
- **Outcome:** BMS import → edit → export preserves every non-gameplay construct listed below, verified by paired fixtures: `#BMP`/BGA base, layer, layer2, and poor channels; `#BGA` definitions; `#STAGEFILE`, `#BANNER`, `#BACKBMP`, `#PREVIEW`; `#SUBTITLE`, `#SUBARTIST`, `#GENRE`, `#COMMENT`, `#DIFFICULTY`, `#PLAYLEVEL`, `#RANK`, `#DEFEXRANK`, `#TOTAL`, `#VOLWAV`, `#LNTYPE`/`#LNOBJ`/`#LNMODE`; `;` and `//` comment lines; unknown `#` headers in source order (user decision 2026-09-30, question 4).
- **Read first:** `src/formats/bms.cpp`, `src/formats/bms_writer.cpp`, `tests/formats/bms_writer_tests.cpp`, `docs/references/bms-command-memo.md`, `tests/fixtures/README.md`.
- **Files owned:** `src/formats/bms.cpp`, `src/formats/bms_writer.cpp`, `tests/formats/bms_tests.cpp`, `tests/formats/bms_writer_tests.cpp`, new fixtures under `tests/fixtures/bms/`, `tests/fixtures/README.md`, `tests/core/fixture_inventory_tests.cpp`.
- **Steps:** 1. One fixture containing every construct above. 2. Test: import, edit one note far away, export, compare every listed line (byte-equal where untouched, semantically equal where normalized). 3. List the gaps found in the report, then fix each one.
- **Invariants:** untouched lines byte-identical under the import profile.
- **Reject/failure behavior:** constructs that cannot be preserved are reported as classified losses, never dropped silently.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'bms|loss|round.?trip'`.
- **Out of scope:** BGA editing UI.

## T-289 Round-trip osu!mania non-gameplay data robustly

- **Status:** done (2026-10-01; fixture `tests/fixtures/osu/non-gameplay-roundtrip.osu`, test `osu!mania keeps events, samples, colours and unknown keys through an edit`)
- **Implementation notes:** import → edit → export now keeps what the fixture holds. Gaps found and fixed in `src/formats/osu_mania.cpp`: background and video offsets (were written as `0,0`), `Sample` event layer and volume (were forced to layer 1, volume 100; stored per event id as `osu.event.<id>.offset` / `.sample` retained fields), and the sample set, index and volume of red lines the writer adds at measure starts (were `0,0,100`, which changed the hitsound mix until the next source point; they now copy the active source point). Already preserved before this card: `[General]` sample settings and unknown keys, `[Colours]`, break periods, storyboard lines in source order, timing-point sample fields and effects, hit-object hitsound and hitsample fields. The existing structured-storyboard test now expects `Sample,500,0,...` because the source layer is kept.
- **Known remaining differences (not fixed):** comment lines (`//...`) are dropped; background/video rows come after break and storyboard rows instead of in source order; hit-sample fields are written inside double quotes (existing writer behavior, covered by tests), which this card did not verify against other osu! readers.
- **Outcome:** osu!mania import → edit → export preserves `[Events]` (background, video with offset, breaks, storyboard lines in source order), `[Colours]`, `[General]` sample settings (`SampleSet`, `SampleVolume`, `AudioLeadIn`, `PreviewTime`, `Countdown`, `SpecialStyle`, `WidescreenStoryboard`), timing-point sample sets/volumes/effects, hit-object hitsample fields, and unknown keys, verified by paired fixtures (user decision 2026-09-30, question 4).
- **Read first:** `src/formats/osu_mania.cpp`, `include/wormedit/formats/osu_mania.h`, `tests/formats/osu_mania_tests.cpp`, `docs/references/osu-mania-version-profiles.md`.
- **Files owned:** `src/formats/osu_mania.cpp`, `tests/formats/osu_mania_tests.cpp`, new fixtures under `tests/fixtures/osu/`, `tests/fixtures/README.md`, `tests/core/fixture_inventory_tests.cpp`.
- **Steps:** as T-288 for osu!mania.
- **Invariants:** unedited files re-export semantically equal; storyboard lines keep order.
- **Reject/failure behavior:** as T-288.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'osu|loss|round.?trip'`.
- **Out of scope:** storyboard editing (deferred).

## T-291 Pin the latest osu!mania format version

- **Status:** done (already implemented: `src/formats/osu_mania.cpp:1996`; current encoder commit and v128 evidence verified 2026-10-02 in `docs/references/osu-mania-version-profiles.md`)
- **Outcome:** the lazer profile is pinned to the latest `osu file format vN` emitted by the current osu! (lazer) encoder, with a fixture and the encoder commit recorded; stable v14 stays readable (user decision 2026-09-30).
- **Read first:** `docs/references/osu-mania-version-profiles.md`, `src/formats/osu_mania.cpp` (profile selection), `tests/fixtures/osu/`.
- **Files owned:** `docs/references/osu-mania-version-profiles.md`, `src/formats/osu_mania.cpp`, `include/wormedit/formats/osu_mania.h`, `tests/formats/osu_mania_tests.cpp`, new fixture under `tests/fixtures/osu/`, `tests/fixtures/README.md`, `tests/core/fixture_inventory_tests.cpp`.
- **Steps:** 1. Read the current lazer `LegacyBeatmapEncoder` (record commit and version number). 2. If newer than v128, add a profile for it with a fixture and make it the default export profile. 3. Keep v128 and v14 reading.
- **Invariants:** existing fixtures keep passing.
- **Reject/failure behavior:** unknown future versions still reject transactionally.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'osu'`.
- **Out of scope:** osu! standard/taiko/catch.
