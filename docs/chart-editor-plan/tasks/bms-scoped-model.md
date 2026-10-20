# Task cards for the scoped BMS program model

[Back to cards](../tasks.md). Contract: [BMS scoped program authoring](../editing/bms-scoped-program.md).
These are new outcomes, not a reopening of the historical T-90–T-92 evidence.
Execute prerequisites in order. T-744 owns public cutover and removal of temporary adapters.

## T-731 Specify scoped semantic ownership and the migration boundary

- **Status:** ready
- **Outcome:** a reviewed ownership map names the authoritative owner and migration rule for every modeled BMS command family and every editor entry path.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `include/wormedit/core/document.h`, `src/core/bms_branch_editor.cpp`, `src/formats/bms.cpp`, `src/formats/bms_writer.cpp`.
- **Files owned:** new `docs/chart-editor-plan/audits/bms-scoped-ownership.md`, `docs/chart-editor-plan/editing/bms-scoped-program.md`, `docs/chart-editor-plan/tasks/bms-scoped-model.md`, `docs/chart-editor-plan/tasks/bms-scoped-authoring.md`, `docs/chart-editor-plan/decisions/open-questions.md`, `memory/chart-editor-status/resume.md`.
- **Steps:**
  1. Trace import, evaluation, persistence, export and root/branch editing; record exact current symbols and paths, including inactive definitions, holds and new-project creation.
  2. Map notes, BGM/BGA, timing, geometry, metadata and resource declarations to one semantic owner; distinguish scoped declarations from shared assets and derived chart views.
  3. Specify stable node IDs, ordered children, the difficulty's shared geometry, Text segmentation and the one-way legacy transition; update later cards' exact supporting ownership from this trace.
  4. Record unresolved product choices as questions 21–24; stop on any additional question that changes the approved contract, rather than embedding an implementation default.
- **Invariants:** no semantic content is owned by two writable graphs; inactive data is included; a Chart content node is not a recursively embedded difficulty.
- **Reject/failure behavior:** this is a code-free audit; an unresolved mapping blocks its dependent implementation slice and changes no native song documents.
- **Verify:** `ctest --test-dir build -L documentation -j 8 --output-on-failure`; review the map against every command family and its named consumer.
- **Out of scope:** runtime implementation (T-732–T-744); automatic answers to product questions (owner decisions required).

## T-732 Define ordered Control Chart and Text program values

- **Status:** blocked-on T-731
- **Outcome:** a typed scoped program can represent Root, nested control scopes, several Chart children and ordered Text blocks with validated stable identities.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `docs/chart-editor-plan/audits/bms-scoped-ownership.md`, `include/wormedit/core/document.h`, `include/wormedit/core/vocabulary.h`, `src/core/chart_object_ids.h`.
- **Files owned:** new `include/wormedit/core/bms_program.h`, new `src/core/bms_program.cpp`, `include/wormedit/core/document.h`, `include/wormedit/core/vocabulary.h`, `src/core/document.cpp`, new `tests/core/bms_program_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, this card.
- **Steps:**
  1. Add the single program owner and typed node/content values using T-731's map; construction and queries live on their owning types.
  2. Add Root/RANDOM/IF scope factories that supply one empty Chart child, plus stable parent/order and first-Chart queries; leave non-body directive presentation to question 22.
  3. Validate unique IDs, cycles, parent references, ordered ownership, supported bounds and at least one Chart per surviving scope.
  4. Test empty Root, nested scopes, multiple Charts, Text ordering and invalid graphs; do not attach an independently writable mirror to production documents.
- **Invariants:** node identity is independent of byte spans and list positions; selecting a control resolves its first Chart, not the first child of any kind.
- **Reject/failure behavior:** invalid construction returns typed diagnostics without publishing partial nodes or identity changes.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program]'`.
- **Out of scope:** parsing (T-733), preview evaluation (T-734), persistence/cutover (T-737/T-744), full directive UI (T-743).

## T-733 Decode modeled content in every BMS scope

- **Status:** blocked-on-user
- **Prerequisites:** T-732; [question 22](../decisions/open-questions.md) for complete source-directive to scoped-node mapping.
- **Outcome:** a prepared program contains typed active and inactive content while retaining only comments and semantically unknown commands as Text.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `docs/chart-editor-plan/audits/bms-scoped-ownership.md`, `src/formats/bms.cpp`, `include/wormedit/formats/bms.h`, `tests/fixtures/bms/control-flow-mixed.bms`.
- **Files owned:** `src/formats/bms.cpp`, `include/wormedit/formats/bms.h`, new `src/formats/bms_program_import.cpp`, `include/wormedit/core/bms_program.h`, new `tests/formats/bms_program_import_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, this card.
- **Steps:**
  1. STOP until question 22's node mapping is answered; reuse the existing parser/codec and T-731 command classification to decode typed declarations and events in every scope without requiring each outcome to be selected first.
  2. Segment ordered content into Chart/Text/control nodes without moving text across boundaries; preserve exact rows, scoped definitions and both hold endpoints.
  3. Retain malformed/unsupported constructs with diagnostics and safe backing; never discard recognized inactive commands before they have equivalent typed owners.
  4. Test nested selectors, inactive WAV/BPM/STOP definitions, adjacent comments, repeated channels, encoding limits and empty scopes through the prepared import adapter.
- **Invariants:** import is lossless across all retained outcomes; known commands are not mislabeled Text as a migration shortcut.
- **Reject/failure behavior:** failed preparation leaves the legacy document authoritative and untouched; no partial program is published.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program-import]'`.
- **Out of scope:** changing BMS dialect semantics (existing profile fixtures govern), malformed-flow repair proposals (T-287), activating the new public owner (T-744).

## T-734 Derive playable chart projections from scoped programs

- **Status:** blocked-on T-733
- **Outcome:** the same program and evaluation environment produce a deterministic chart projection with semantic-owner identities on editable objects.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `include/wormedit/core/bms_program.h`, `src/core/bms_branch_view.cpp`, `src/formats/bms_branch_selection.cpp`, `docs/references/bms-control-flow.md`.
- **Files owned:** `include/wormedit/core/bms_program.h`, new `src/core/bms_program_projection.cpp`, `include/wormedit/core/bms_branch_view.h`, `src/core/bms_branch_view.cpp`, `include/wormedit/formats/bms_branch_selection.h`, `src/formats/bms_branch_selection.cpp`, new `tests/core/bms_program_projection_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, this card.
- **Steps:**
  1. Evaluate the typed program using existing profile rules for root/common content, RANDOM/IF, fixed selectors and switch fallthrough.
  2. Resolve scoped declarations in BMS program order and derive notes, timing, resources and geometry without a second writable semantic store.
  3. Return stable owner IDs for visible content and bounded diagnostics for inactive/invalid targets; keep edit-target choice separate from evaluation inputs.
  4. Compare all bounded fixture outcomes with the legacy evaluator, including nested scopes, holds, resource overrides and several Chart children.
- **Invariants:** sibling Chart order does not concatenate musical time; hidden objects cannot be mutated through stale visible selections.
- **Reject/failure behavior:** invalid/capped evaluation returns diagnostics and does not replace the last accepted document or projection.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program-projection]'`.
- **Out of scope:** deciding inactive-scope preview UX (question 24/T-743), scoped writes (T-736), changing the supported BMS profile language.

## T-735 Serialize complete scoped programs from semantic owners

- **Status:** blocked-on T-734
- **Outcome:** default BMS output serializes all scopes and their current typed content, with Text retained in place and no duplicate raw semantic statements.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `include/wormedit/core/bms_program.h`, `src/formats/bms_writer.cpp`, `include/wormedit/formats/bms_writer.h`, `tests/formats/bms_writer_tests.cpp`.
- **Files owned:** `include/wormedit/core/bms_program.h`, `src/formats/bms_writer.cpp`, `include/wormedit/formats/bms_writer.h`, new `src/formats/bms_program_writer.cpp`, new `tests/formats/bms_program_writer_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, this card.
- **Steps:**
  1. Emit control nodes and ordered typed content from the program owner, preserving inactive scopes and Text without consulting stale modeled raw lines as authority.
  2. Keep exact unchanged-source passthrough as a read-only optimization; after edits regenerate modeled commands and retain safe spelling/encoding provenance where applicable.
  3. Keep control-flow preservation as the default; explicit flattening uses the shared projection and existing loss policy, while plain timing remains ordinarily editable/exportable.
  4. Compare import/export/reimport for every bounded outcome, nested/fixed selectors, comments, scoped resources and holds; test unsupported units, limits and unrepresentable output refusal.
- **Invariants:** no inactive data loss, duplicate commands, silent timing approximation or selection-dependent default output.
- **Reject/failure behavior:** unsupported or ambiguous serialization produces no partial output and leaves the authored program unchanged.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program-writer]'`.
- **Out of scope:** native persistence (T-737), public adapter retirement (T-744), new target dialects (existing compatibility-profile cards).

## T-736 Resolve chart edits to one scoped semantic owner

- **Status:** blocked-on T-735
- **Outcome:** edits on program-backed documents enter the existing transaction with a stable scope/Chart target and mutate only their intended semantic owner.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `docs/chart-editor-plan/audits/bms-scoped-ownership.md`, `include/wormedit/core/command.h`, `src/core/command.cpp`, `src/core/interaction_edit_commit.h`.
- **Files owned:** new `include/wormedit/core/bms_program_edit.h`, new `src/core/bms_program_edit.cpp`, `include/wormedit/core/command.h`, `src/core/command.cpp`, `src/core/interaction_edit_commit.h`, `include/wormedit/core/interaction_state.h`, `src/core/interaction.cpp`, new `tests/core/bms_program_edit_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, this card; T-731 must record exact command-family supporting extensions before implementation.
- **Steps:**
  1. Introduce one typed edit-target resolver carrying difficulty, scope and Chart IDs through shared runners, with root/first-Chart defaults and stale-target validation.
  2. Route every T-731 inventoried creation path, including notes/holds, paste, BGM/BGA, timing and scoped definitions, to that owner; existing objects retain their recorded owner, and mismatched-owner writes reject unless a separate explicit operation is requested.
  3. Rebuild affected derived projections through shared publication; do not teach each chart edit its own copy/stamp/result/provenance lifecycle.
  4. Test root, RANDOM, IF and alternate-Chart targets, stale/hidden selections, sibling isolation, scoped resources and one-step Undo/Redo in prepared program-backed fixtures.
- **Invariants:** selection changes do not relocate existing objects; an unavailable target never falls back silently to root; no writes go to a derived cache.
- **Reject/failure behavior:** target, collision, resource or projection failures roll back the complete existing transaction.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program-edit]'`.
- **Out of scope:** UI selection/preview policy (question 24/T-743), structural commands (T-738–T-742), production activation (T-744).

## T-737 Persist scoped programs and migrate legacy native data

- **Status:** blocked-on T-736
- **Outcome:** versioned native codecs migrate legacy active and inactive BMS data into one scoped program and reload it without identity or semantic loss.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `docs/chart-editor-plan/audits/bms-scoped-ownership.md`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `tests/formats/native_project_tests.cpp`.
- **Files owned:** `include/wormedit/core/document.h`, `src/core/document.cpp`, `include/wormedit/formats/native_project.h`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.h`, `src/formats/native_project_migrations.cpp`, new `tests/formats/bms_program_migration_tests.cpp`, `tests/CMakeLists.txt`, this card.
- **Steps:**
  1. Select the next schema from the actual migration table; specify one serialized owner for program nodes, Chart content, Text and scoped declarations.
  2. Build the migration using prepared import/projection/routing adapters, preserving all inactive data, shared assets, exact positions and stable/remapped identities across history.
  3. Test legacy native/BMS fixtures, newly authored empty Root, multiple Charts, malformed retained material, mixed-difficulty projects and save/reload/Undo/Redo.
  4. Keep the new codec/migration explicitly exercised in tests until T-744's coherent public cutover; do not introduce independently writable legacy/new document mirrors.
- **Invariants:** typed data has one owner; old source is not removed before equivalence is proven; defaults for non-BMS documents remain unchanged.
- **Reject/failure behavior:** unmappable or invalid legacy data is retained/reported or the migration is refused atomically; never flatten or drop it to make migration succeed.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program-migration]'`.
- **Out of scope:** enabling a partially migrated production graph (T-744), malformed repair UI (T-287), node authoring widgets (T-743).
