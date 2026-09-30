# Task cards: export and conversion

[Back to the task-card index](../tasks.md).

Stage 5 leftovers from [authoring/export](../stages/authoring-export.md) and the
[formats plan](../formats-persistence.md). Compose already exports an output
group through `convert_document` + `publish_conversion_group_atomic`
(`WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP` in `src/sys/compose_actions.cpp`).
Most remaining format work waits on user decisions; those cards are
`blocked-on-user`. Apply the stale-plan check (rule 9) first.

## T-110 Preview artifacts and losses before exporting

- **Status:** ready
- **Outcome:** a **Preview export** button lists, for the selected output group, each artifact name, byte size, and every loss/diagnostic from `convert_document`, without writing files; Export stays a separate click.
- **Read first:** `include/wormedit/formats/conversion.h`, `src/sys/compose_actions.cpp` (export action near `convert_document`), `src/ui/frontend.h` (`compose_export_overwrite`).
- **Files owned:** `src/sys/compose_actions.cpp`, `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_resources.cpp` or the unit that renders the export button, `tests/formats/conversion_tests.cpp`.
- **Steps:**
  1. Run conversion on a snapshot off the frame thread (reuse the export worker path) and store a bounded summary (≤64 artifacts, ≤256 diagnostics) keyed by document revision.
  2. Render the summary; mark it stale when the revision changes.
- **Invariants:** preview never writes to disk.
- **Reject/failure behavior:** conversion failure shows the typed error list; Export remains available only if the preview succeeded for the current revision (show why otherwise).
- **Verify:** standard build/test; manual preview on a BMSON fixture that loses semantic layers.
- **Out of scope:** CLI (T-112).

## T-111 Fail tests on any unclassified export loss

- **Status:** ready
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

- **Status:** blocked-on-user
- **Outcome:** answer to open question 17 recorded, then a card for a headless CLI target that reads a JSON request and calls the same conversion + publication services.
- **Read first:** `docs/chart-editor-plan/decisions/open-questions.md` (question 17), `docs/chart-editor-plan/formats-persistence.md` (converter paragraph).
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** 1. STOP: ask question 17 and the JSON schema scope. 2. Record; add the follow-up card.
- **Invariants:** none.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.

## T-113 Decide BMS dialect, StepMania, and osu! profile scope

- **Status:** blocked-on-user
- **Outcome:** answers to open questions 3 (BMS dialect target) and 18 (StepMania profiles), plus which osu!mania implementation profiles beyond stable v14 / lazer v128 to pin; each answer becomes a fixture-first follow-up card.
- **Read first:** `docs/chart-editor-plan/decisions/open-questions.md`, `docs/references/stepmania-warps-and-conversion.md`, `docs/references/osu-mania-version-profiles.md`.
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** 1. STOP and ask. 2. Record; add one card per new profile, each starting with "add paired positive/negative fixtures".
- **Invariants:** no profile is advertised without paired fixtures.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.

## T-114 Move provenance-backed BMS notes across measures

- **Status:** ready
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

- **Status:** blocked-on-user
- **Outcome:** answers for default tap duration, PPQ cap, track grouping, and loss policy (from `memory/chart-editor-status/open-decisions.md`), then a card for a MIDI writer over lane `midi_destination_t` mappings.
- **Read first:** `memory/chart-editor-status/open-decisions.md`, `include/wormedit/core/document.h` (`midi_destination_t`).
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** 1. STOP and ask the four defaults. 2. Record; add the writer card.
- **Invariants:** none.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.

## T-116 Decide label export and non-gameplay round-trip scope

- **Status:** blocked-on-user
- **Outcome:** answers to open questions 1, 2, and 4, then cards for any writer changes they require.
- **Read first:** `docs/chart-editor-plan/decisions/open-questions.md`, `docs/chart-editor-plan/document-model.md` (labels paragraph).
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** 1. STOP and ask. 2. Record; add follow-up cards.
- **Invariants:** no invented label syntax in any target.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.
