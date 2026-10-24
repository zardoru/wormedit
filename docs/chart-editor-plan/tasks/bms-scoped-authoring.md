# Task cards for BMS scope node authoring

[Back to cards](../tasks.md). Contract: [BMS scoped program authoring](../editing/bms-scoped-program.md).
Prerequisites are in [model and migration](bms-scoped-model.md). Question 22 is answered;
questions 21, 23 and 24 remain explicit gates. T-744 owns public activation.

## T-738 Add Control Chart and Text nodes in selected scopes

- **Status:** done (2026-10-23)
- **Prerequisites:** T-737; [question 22](../decisions/open-questions.md) answered 2026-10-23.
- **Outcome:** typed commands insert valid control scopes, Chart children and opaque Text at explicit ordered positions, including an empty/new Root.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `docs/chart-editor-plan/decisions/open-questions.md`, `include/wormedit/core/bms_program.h`, `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`.
- **Files owned:** `include/wormedit/core/bms_program.h`, `src/core/bms_program.cpp`, `src/core/bms_program_edit.cpp`, `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, new `tests/core/bms_program_authoring_tests.cpp`, `tests/CMakeLists.txt`, `include/wormedit/formats/bms.h`, `src/formats/bms_program_writer.cpp`, `src/formats/bms_writer_internal.h`, `memory/chart-editor-status/resume.md`, this card.
- **Supporting ownership:** reuse the writer’s parser/codec policy through a public Text validation seam in `bms.h` / `bms_program_writer.cpp`; keep the existing writer line limit as an alias of the Text-owned limit in `bms_writer_internal.h`, and advance the resume entry after verification. Extend shared publication in `src/core/bms_program_edit.cpp` to validate implicit fixed-selector outcomes as well as explicit preview overrides, including hidden scopes. Existing append-only RANDOM/IF factories and legacy raw-source commands do not satisfy ordered typed insertion.
- **Steps:**
  1. After T-737, use the approved distinction between body scopes, separate SETRANDOM/SETSWITCH settings-only nodes, typed case SKIP behavior and automatic grammar markers.
  2. Add typed insertion requests with stable parent/anchor IDs and grammar-aware factories; every new scope receives its first empty Chart automatically.
  3. Add bounded Text insertion/editing through the existing parser classification; refuse attempts to hide modeled/control commands in opaque Text without a semantic owner.
  4. Test empty Root, nested RANDOM/IF and supported switch/fixed forms, several Chart siblings, ordered comments, invalid parent/anchor, limits and one-step Undo/Redo.
- **Invariants:** no selected raw line is required; existing content and siblings are untouched; every surviving scope has a Chart child.
- **Reject/failure behavior:** stale targets, invalid syntax/placement or encoding failure reject atomically with a named reason.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program-authoring]'`.
- **Out of scope:** RANDOM range changes (T-739), movement/deletion/unwrap (T-740–T-742), widgets (T-743).
- **Implementation:** explicit parent/after-child requests insert typed body scopes, empty Charts, fixed settings and encoded Text without a source-line selection. The shared immutable graph constructor checks grammar/anchors/budgets and creates the first Chart for every new body. Text editing shares export’s parser/codec classifier; anchored SKIP is authored on its existing case. History-wide identity reservation and the shared staged transaction provide atomic publication and Undo/Redo. Publication includes implicit fixed-selector outcomes in its existing bounded budget, so preview overrides cannot conceal invalid settings in hidden scopes. Production activation remains T-744.
- **Test-value checkpoint:** scoped node authoring/shared graph and transaction owners; baseline is T-737’s existing model/edit/writer/migration families. Reviewed all 12 new test bodies for observable ordered content, grammar, Text ownership/encoding, fixed selectors/SKIP, refusal atomicity, Undo/Redo, reserved IDs and persistence. Existing family bodies were retained conservatively, not re-audited; passing counts make no whole-suite value claim. Focused GCC evidence is 615 assertions / 12 authoring cases; the retained model/edit/writer/migration families plus authoring pass 4,562 assertions / 143 cases. Shared publication’s hidden fixed-selector checks cover both RANDOM and SWITCH. Clang passes the same focused families and the full 4,796-test suite; evidence below.

- **GCC verification (2026-10-23):** final `scripts/gate.sh --jobs 4` log `/tmp/t738-gate3.log` (details `/tmp/wormedit-gate.zS5muT`); explicit header consumers `/tmp/t738-header-complexity-final.log`; focused `/tmp/t738-authoring-final.log` and `/tmp/t738-focused-final.log`. Real output tails:

  ```text
  pass  build
  pass  ctest
  pass  whitespace
  pass  complexity
  100% tests passed out of 4796
  gate: passed
  complexity: passed (5 translation units)
  complexity: passed (46 translation units)
  All tests passed (615 assertions in 12 test cases)
  All tests passed (4562 assertions in 143 test cases)
  ```

- **Clang and review (2026-10-23):** `cmake --build build-clang --parallel 6` succeeds with one linker and no warnings in this affected-target rebuild (`/tmp/t738-clang-build.log`); `ctest --test-dir build-clang -j 4 --output-on-failure` passes (`/tmp/t738-clang-ctest.log`). Focused logs `/tmp/t738-clang-authoring.log` and `/tmp/t738-clang-focused.log` match GCC. Reviewed the code-style checklist: shared graph/parser/transaction owners, one Text line-limit fact, aggregate initialization, value equality and bounded helpers; no complexity allowances increased. Initial checks exposed test snapshot comparisons and oversized assertion bodies; final checks above use corrected snapshots and cohesive shared test checks. Real output tails:

  ```text
  100% tests passed out of 4796
  All tests passed (615 assertions in 12 test cases)
  All tests passed (4562 assertions in 143 test cases)
  ```
- **Handoff:** T-740 is ready; T-739 remains blocked on question 21. No successor implementation is active.

## T-739 Edit RANDOM outcome ranges without implicit arm edits

- **Status:** done (2026-10-23)
- **Prerequisites:** T-738; [question 21](../decisions/open-questions.md).
- **Outcome:** a revision-checked command edits RANDOM's positive outcome range independently of child count and preview selection.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `docs/chart-editor-plan/decisions/open-questions.md`, `include/wormedit/core/bms_program.h`, `src/core/bms_branch_editor.cpp`, `tests/core/bms_program_authoring_tests.cpp`.
- **Files owned:** `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, `tests/core/bms_program_authoring_tests.cpp`, `include/wormedit/core/bms_program.h`, `src/core/bms_program.cpp`, `docs/chart-editor-plan/decisions/open-questions.md`, `docs/chart-editor-plan/editing/bms-scoped-program.md`, `memory/chart-editor-status/resume.md`, `docs/architecture.md`, this card.
- **Supporting ownership extension (coordinator, 2026-10-23):** graph parameter construction and reachability queries belong on `bms_program_t`; evaluation reconciliation belongs on its value type. The decision/contract/status files record approved questions 21 and 24 and the sequential T-739/T-743 handoff. All supporting paths are listed above before implementation edits.
- **Architecture extension (coordinator, 2026-10-23):** add `docs/architecture.md` to **Files owned** to describe range changes and shared evaluation reconciliation before snapshot publication.
- **Steps:**
  1. Follow approved question 21: preserve unreachable arms with a warning and clear invalid explicit preview choices without choosing a replacement.
  2. Add the scalar parameter edit with stable node identity, positive supported bounds and the approved reachability/selection diagnostics.
  3. Test grow, unchanged value, invalid value, shrink, sparse IF values, nested RANDOMs, export/reimport and Undo/Redo.
- **Invariants:** changing `n` does not add/delete/renumber IF arms, Text or Chart children; parameter and preview outcome are not conflated.
- **Reject/failure behavior:** invalid/stale/refused edits preserve parameter, descendants, preview state and history.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program-authoring]'`.
- **Out of scope:** automatic probability balancing or outcome enumeration changes (not requested); direct controls in the panel (T-743).

- **Completed:** stamped scalar range edits use the shared program staging/publication boundary. Graph queries identify unreachable IF/ELSEIF arms by their nearest RANDOM, independently of nested selectors. Shared evaluation reconciliation clears invalid explicit choices before constructing the immutable proposed owner, preserving valid/other choices and never rewriting fixed settings. Parameter and choice changes share one Undo step. Unreachable bodies, IDs, Text, values and order remain; no-op, invalid/wrong-owner, unbounded and invalid fixed-setting requests preserve the snapshot.
- **Bounded test-value checkpoint:** baseline 53 retained authoring cases; reviewed eight new bodies for growth, shrink/choice clearing, valid choice retention, no-op, invalid/wrong-owner/budget refusals, nested ownership, retained-body export/native reload and fixed-setting refusal. The family passes **2,004 assertions / 61 cases**; combined BMS families **5,951 / 192**. Old cases are retained conservatively, not newly reviewed as an entire suite. The generic native-reload helper was renamed with `clang-refactor local-rename` and checked for old-name remnants. New/affected functions pass complexity without allowance changes.
- **Verification tails (2026-10-23):** GCC `/tmp/t739-gate.log` and `/tmp/wormedit-gate.VQ1hyB`; explicit header consumers `/tmp/t739-header-complexity.log`; focused `/tmp/t739-authoring.log`, `/tmp/t739-focused.log`.
  ```text
  gate: 4 job(s), set explicitly; logs in /tmp/wormedit-gate.VQ1hyB
    pass  build
    pass  ctest
    pass  whitespace
    pass  complexity
    100% tests passed out of 4845
  gate: passed
  complexity: checked 3/3
  complexity: passed (3 translation units)
  complexity: checked 46/46
  complexity: passed (46 translation units)
  All tests passed (2004 assertions in 61 test cases)
  All tests passed (5951 assertions in 192 test cases)
  ```
- **Handoff:** question 24 is answered and T-743 is ready. Its authoring view must use target-node selection while leaving playback preview choices unchanged; confirmation widgets belong there. Public activation stays T-744.

## T-740 Move whole program subtrees in ordered scopes

- **Status:** done (2026-10-23)
- **Outcome:** one transaction moves a Control, Chart or Text subtree to an explicit valid parent/position while retaining its identities and ordered content.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `include/wormedit/core/bms_program.h`, `src/core/bms_branch_editor.cpp`, `tests/core/bms_program_authoring_tests.cpp`, `docs/references/bms-control-flow.md`.
- **Files owned:** `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, `src/core/bms_program.cpp`, `include/wormedit/core/bms_program.h`, `tests/core/bms_program_authoring_tests.cpp`, `memory/chart-editor-status/resume.md`, this card.
- **Supporting ownership:** add the immutable move request/factory to `include/wormedit/core/bms_program.h`, alongside graph construction and insertion; advance the resume entry after verification. Stale-plan search found only legacy leaf-body swaps, not scoped subtree moves. The prepared transaction/proposal runners and bounded publication validator already own staging, stamps, projection validation and Undo.
- **Steps:**
  1. Add an ordered move proposal identifying source subtree, destination scope and insertion anchor; describe affected definitions/projections before applying it.
  2. Reject cycles, invalid grammar, a surviving scope losing its last Chart, unresolved resources and invalid affected projections; do not silently rebind selectors or rebase rows.
  3. Test sibling reorder and legal reparenting with comments, nested controls, holds and scoped definitions, plus rejection atomicity and Undo/Redo.
- **Invariants:** Text moves with its block, not with an unrelated source-line index; unchanged nodes keep IDs, values and musical positions.
- **Reject/failure behavior:** a move with unprovable/invalid effects publishes neither a partial reparent nor cache/selection changes.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program-authoring]'`.
- **Out of scope:** copying/moving individual selected notes (T-93), drag/drop gestures (panel commands in T-743 provide the first UI).

- **Implementation:** the immutable graph move preserves node/value identities and musical positions while changing only ordered parent child lists. Stamped proposals preview through the shared transaction and bounded publication validator; their report describes the subtree, all potentially affected Charts/declarations, and selected-projection Chart order before/after. IF and CASE selector owners remain unchanged; IF chains and anchored SKIP remain valid. Fixed-selector programs require unchanged control order/containment to prove bindings: Chart/Text moves and control shifts across only content can proceed, while unproven control rebinding refuses explicitly. Report construction lives on `bms_program_move_report_t::from`; conditional binding lookup lives on `bms_program_t::conditional_selector`. No implicit SKIP repair or musical rebasing occurs.
- **Test-value checkpoint:** scoped subtree movement/shared graph and proposal owners; baseline is T-738’s 12 authoring cases. Reviewed all 12 new bodies for observable ordering, complete value/ID preservation, read-only preview, legal nesting with comments/holds/declarations, refusal diagnostics/atomicity, selector/SKIP ownership, hidden collisions/references/holds, stale/no-op publication, depth caps and native reload. Inherited families remain conservatively retained, not re-audited; no whole-suite value claim. GCC evidence is 990 assertions / 24 authoring cases and 4,937 / 155 combined BMS cases; full GATE and complexity are green. Clang matches both focused runs and the full 4,808-test suite; evidence below.

- **GCC verification (2026-10-23):** final `scripts/gate.sh --jobs 4` log `/tmp/t740-gate-reviewed.log` (details `/tmp/wormedit-gate.H3xJom`); explicit header consumers `/tmp/t740-header-reviewed.log`; focused logs `/tmp/t740-authoring-reviewed.log` and `/tmp/t740-focused-reviewed.log`. Real output tails:

  ```text
  pass  build
  pass  ctest
  pass  whitespace
  pass  complexity
  100% tests passed out of 4808
  gate: passed
  complexity: passed (3 translation units)
  complexity: passed (46 translation units)
  All tests passed (990 assertions in 24 test cases)
  All tests passed (4937 assertions in 155 test cases)
  ```

- **Clang and review (2026-10-23):** affected-target `cmake --build build-clang --parallel 6` succeeds with one linker and no warnings in this rebuild (`/tmp/t740-clang-reviewed-build.log`); full `ctest --test-dir build-clang -j 4 --output-on-failure` passes (`/tmp/t740-clang-ctest.log`). Focused logs `/tmp/t740-clang-authoring.log` and `/tmp/t740-clang-focused.log` match GCC. Reviewed the code-style checklist: immutable graph ownership, type-owned report construction and selector query, shared proposal/transaction/publication runners, aggregate fields and value equality, bounded helpers and no increased complexity allowances. AST-aware renames used `clang-refactor`, followed by leftover searches. The initial fixture assumed empty Undo after setup; the final check compares the retained count. Real output tails:

  ```text
  100% tests passed out of 4808
  All tests passed (990 assertions in 24 test cases)
  All tests passed (4937 assertions in 155 test cases)
  ```
- **Handoff:** T-741 is ready for confirmed subtree deletion; T-739 remains blocked on question 21. No successor implementation is active. Production activation remains T-744.

## T-741 Delete scoped subtrees through explicit confirmation

- **Status:** done (2026-10-23)
- **Outcome:** confirmed deletion removes exactly the named subtree and its Chart/Text contents, while Cancel and stale confirmation leave the program intact.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, `src/sys/compose/actions/compose_measure_commands.cpp`, `tests/core/bms_program_authoring_tests.cpp`.
- **Files owned:** `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, `tests/core/bms_program_authoring_tests.cpp`, `include/wormedit/core/bms_program.h`, `src/core/bms_program.cpp`, `src/core/command.cpp`, `docs/architecture.md`, `memory/chart-editor-status/resume.md`, this card.
- **Supporting ownership:** put subtree queries and immutable removal on the program model (`bms_program.h` / `bms_program.cpp`), reuse that query for move reports, and reconcile deleted session targets in `src/core/command.cpp` across Commit/Undo/Redo. Keep `docs/architecture.md` true and advance the resume entry after verification. Stale-plan search found no prepared subtree deletion; retained-source deletion and measure removal do not satisfy this outcome.
- **Steps:**
  1. Prepare a typed deletion summary with stable subtree identity, revision and counts of descendant scopes, Charts, Text and semantic content.
  2. Apply only the confirmed proposal through the shared transaction; remove descendants, validate affected projections and resolve a deleted current target to the surviving parent's first Chart.
  3. Refuse Root removal and removal of a surviving scope's final Chart; retain assets used elsewhere and perform no implicit asset garbage collection.
  4. Test nonempty nested deletion, unrelated siblings, cancellation, stale confirmation, target repair and full one-step Undo/Redo.
- **Invariants:** deleting a control does not unwrap/promote children; no dangling owner/selection/resource references remain.
- **Reject/failure behavior:** failed/canceled confirmation publishes nothing; a structural failure rolls back the whole subtree change.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program-authoring]'`.
- **Out of scope:** confirmation widgets (T-743), unwrapping (T-742), project difficulty deletion (existing chart-management workflow).

- **Implementation:** a shared subtree query reports scopes, Charts, encoded Text, fixed settings and semantic content (typed entries, nested native identities and case SKIP behavior). The private deletion payload retains the original stamp and exact node summary; only an explicit typed Confirm decision stages removal, and retargeted confirmation envelopes reject. Shared publication validates all bounded outcomes, preserves catalogs and removes evaluation choices only for deleted selectors. Commit/Undo/Redo repair a deleted session target through its previous ancestry; valid targets stay selected and unknown ancestry clears. Session selection remains outside semantic Undo. Root/final-Chart removal, dangling IF/SKIP, unresolved references and partial holds refuse without mutation. No implicit SKIP repair, unwrapping or catalog collection occurs.
- **Test-value checkpoint:** subtree deletion/shared graph, proposal and publication owners; baseline is T-740’s 24 authoring cases. Reviewed all 12 new bodies for typed counts, exact subtree/sibling preservation, catalog preservation, Cancel, stale/forged confirmation, target repair and selection validity through Undo/Redo, structural/semantic refusals and native reload. Existing family bodies were retained conservatively, not re-audited; counts do not establish whole-suite value. GCC evidence is 1,338 assertions / 36 authoring cases and 5,285 / 167 combined BMS cases. Full GATE and complexity pass; Clang matches the focused runs and full 4,820-test suite; evidence below.

- **GCC verification (2026-10-23):** final `scripts/gate.sh --jobs 4` log `/tmp/t741-gate-final.log` (details `/tmp/wormedit-gate.eOZM9x`); explicit header consumers `/tmp/t741-header-complexity.log`; focused logs `/tmp/t741-authoring-final.log` and `/tmp/t741-focused-final.log`. Real output tails:

  ```text
  pass  build
  pass  ctest
  pass  whitespace
  pass  complexity
  100% tests passed out of 4820
  gate: passed
  complexity: passed (4 translation units)
  complexity: passed (46 translation units)
  All tests passed (1338 assertions in 36 test cases)
  All tests passed (5285 assertions in 167 test cases)
  ```

- **Clang and review (2026-10-23):** affected-target `cmake --build build-clang --parallel 6` succeeds with one linker and no warnings in this rebuild (`/tmp/t741-clang-build.log`); `ctest --test-dir build-clang -j 4 --output-on-failure` passes (`/tmp/t741-clang-ctest.log`). Focused logs `/tmp/t741-clang-authoring.log` and `/tmp/t741-clang-focused.log` match GCC. Reviewed the code-style checklist: type-owned shared subtree query, immutable graph mutation, private confirmation identity, shared publication/Undo target reconciliation, documented runner semantics, named aggregate fields and bounded helpers. No complexity allowances increased. Initial catalog comparisons used types without equality and were replaced with native catalog snapshots; oversized test assertion blocks were consolidated. One initial clang-tidy crash did not recur in stable reruns or the final gate/header checks; none were skipped. Real output tails:

  ```text
  100% tests passed out of 4820
  All tests passed (1338 assertions in 36 test cases)
  All tests passed (5285 assertions in 167 test cases)
  ```
- **Handoff:** T-742 is complete below following the question-23 answer. T-739 and T-743 retain their user-decision gates (questions 21 and 24). Confirmation widgets remain T-743 and production activation remains T-744.

## T-742 Unwrap controls as an explicit structural operation

- **Status:** done (2026-10-23)
- **Prerequisites:** T-741; [question 23](../decisions/open-questions.md).
- **Outcome:** a confirmed valid unwrap replaces one wrapper with its ordered children without deleting content; affected dependent branches become explicit selectorless, always-applying scopes only after confirmation.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `docs/chart-editor-plan/decisions/open-questions.md`, `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, `tests/core/bms_program_authoring_tests.cpp`.
- **Files owned:** `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, `tests/core/bms_program_authoring_tests.cpp`, `docs/chart-editor-plan/decisions/open-questions.md`, `docs/chart-editor-plan/editing/bms-scoped-program.md`, `memory/chart-editor-status/resume.md`, this card, `include/wormedit/core/bms_program.h`, `src/core/bms_program.cpp`, `src/formats/bms_program_writer.cpp`, `src/formats/native_project_bms_codec.inc`, `include/wormedit/core/document.h`, `src/formats/native_project_migrations.cpp`, `docs/architecture.md`.
- **Supporting-file extension (coordinator, 2026-10-23):** the decision, model contract and resume files above record the owner's question-23 answer and the resulting implementation gate. Runtime representation and codec ownership extensions must be recorded before implementation edits.
- **Runtime ownership extension (coordinator, 2026-10-23):** add `include/wormedit/core/bms_program.h`, `src/core/bms_program.cpp`, `src/formats/bms_program_writer.cpp`, `src/formats/native_project_bms_codec.inc`, `include/wormedit/core/document.h` and `src/formats/native_project_migrations.cpp` to **Files owned**. These implement selectorless scope identity, validated promotion, faithful export and schema-v15 persistence/migration. Existing projection traversal already evaluates a nonconditional body scope unconditionally; verify this through command tests. CASE conversion explicitly removes selector-specific SKIP behavior while retaining all bodies; report its count. Fixed-setting control rebinding that cannot be proven safe refuses conservatively.
- **Runner documentation extension (coordinator, 2026-10-23):** `docs/architecture.md` records confirmed conversion and the shared staging/preview helpers used by move, delete and unwrap rather than adding a third copy of their execution ceremony.
- **Implementation boundaries:** direct CASE/DEF unwrap refuses because its promoted contents would still be subject to SWITCH entry/fallthrough; the containing SWITCH can be unwrapped with explicit conversion. Fixed-setting programs refuse unproven consumption changes. Neither refusal drops content. Nested conditions independent of the removed selector stay conditional; affected following IF continuations become bare rather than testing a changed predicate prefix. External SKIP anchors retain their lexical position through promotion.
- **Bounded test-value checkpoint:** baseline is the 36 retained T-738/T-740/T-741 authoring cases. Reviewed the 17 added T-742 bodies for ordered promotion, confirmation distinction/cancellation, selector and continuation conversion, independent nested conditions, outer-selector non-rebinding, CASE/SKIP conversion, lexical SKIP anchors, simultaneous/hidden collisions, target identity, native migration/reload and Undo/Redo. Existing move/delete cases are retained to exercise shared staging/preview extraction; their previous review is not a new whole-suite-value claim. The complete authoring family passes 1,810 assertions / 53 cases; combined BMS families pass 5,757 / 184. Complexity passes for five changed TUs and 46 explicit header consumers without increased allowances. Conclusions apply to the reviewed new bodies, not the value of every retained test.
- **Steps:**
  1. Follow question 23's approved policy: when promotion would remove or change a surviving branch's selector, offer explicit conversion of those affected branches into selectorless, always-applying scopes, or Cancel with no change. Never infer whole-program flattening from the word unwrap.
  2. Propose the literal ordered child promotion and its reachability, definition and collision effects; require confirmation before an apply.
  3. Validate grammar, surviving Chart invariants and every affected bounded projection; retain child IDs and positions.
  4. Test valid body-scope unwrap, selector cases under the approved policy, Text order, canceled/stale confirmation, collisions and Undo/Redo.
- **Invariants:** unwrap is not delete, implicit renumbering, sibling coalescing, or a hidden active-projection export.
- **Reject/failure behavior:** cancellation, unconfirmed conversion, invalid/rebound conditional structure or unresolved content rejects without changing the wrapper or any child. Confirmed conversion retains affected branch bodies and order; it never selects one outcome or discards alternate bodies.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program-authoring]'`.
- **Out of scope:** lossy branch flattening (existing explicit export policy), widgets (T-743).

- **Completed:** immutable stamped proposals preview all affected branches, ordered contents, Chart reachability and potential definition changes. Ordinary promotion confirmation cannot authorize unconditional conversion. Confirmed conversion retains all bodies and IDs, removes only the wrapper and explicitly reported CASE/SKIP behavior, and preserves unrelated selector bindings and external SKIP anchors. Cancel, forged/stale confirmation, invalid structures, unproven fixed settings and hidden collisions publish nothing. Bare scopes persist in native schema v15 (v14 migrates unchanged) and export without conditional markers or a false flatten-loss warning. Move/delete/unwrap share staging and preview execution; shared publication owns bounded validation and Undo/Redo.
- **Review:** applied the code-style checklist: model construction/queries stay on their types, report construction lives on the report, and the third proposal path extracts existing execution ceremony instead of copying it. No baseline allowances or suppression changes. Initial verification corrected an assertion that confused a retained SKIP anchor with an importer-appended empty Chart; export-equivalence assertions were extracted into a cohesive helper after the complexity check found a 32-point test body. A capped header scan timed out on the existing Compose action TU; its isolated retry and the final complete 46-TU scan pass.
- **Verification tails (2026-10-23):** GCC GATE `/tmp/t742-gate-final.log`, detail directory `/tmp/t742-gate-run/wormedit-gate.IPHwic`; explicit header consumers `/tmp/t742-header-complexity-verified.log`; authoring `/tmp/t742-authoring-verified.log`; combined families `/tmp/t742-focused-verified.log`.
  ```text
  gate: 4 job(s), set explicitly; logs in /tmp/t742-gate-run/wormedit-gate.IPHwic
    pass  build
    pass  ctest
    pass  whitespace
    pass  complexity
    100% tests passed out of 4837
  gate: passed
  complexity: checked 5/5
  complexity: passed (5 translation units)
  complexity: checked 46/46
  complexity: passed (46 translation units)
  All tests passed (1810 assertions in 53 test cases)
  All tests passed (5757 assertions in 184 test cases)
  ```
- **Handoff:** T-739 and T-743 retain questions 21 and 24 respectively. Confirmation widgets remain T-743; production activation remains T-744. No successor implementation is active.

## T-743 Present the branch panel as a scope authoring tree

- **Status:** complete
- **Prerequisites:** T-738–T-742; [question 24](../decisions/open-questions.md).
- **Outcome:** the panel exposes Root, control scopes, Chart and Text nodes, indicates the exact edit target, and offers validated node commands with distinct Delete/Unwrap confirmations.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `docs/chart-editor-plan/decisions/open-questions.md`, `src/ui/nuklear/project/bms_branch_panel.cpp`, `src/sys/project/workspace_view.cpp`, `src/sys/compose/actions/compose_actions.cpp`.
- **Files owned:** new `src/ui/frontend/bms_program.h`, `src/ui/frontend/compose.h`, `src/ui/frontend/actions.h`, `include/wormedit/core/workspace.h`, `src/core/workspace.cpp`, `src/sys/project/workspace_view.h`, `src/sys/project/workspace_view.cpp`, `src/sys/compose/views/compose_view.h`, `src/sys/compose/views/compose_view.cpp`, `src/sys/compose/actions/compose_actions.cpp`, new `src/sys/bms_program_runtime.h`, new `src/sys/bms_program_runtime.cpp`, `src/ui/nuklear/project/bms_branch_panel.h`, `src/ui/nuklear/project/bms_branch_panel.cpp`, new `tests/ui/bms_program_event_harness_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `include/wormedit/core/document.h`, `src/core/document.cpp`, `include/wormedit/core/bms_program.h`, `include/wormedit/core/bms_program_edit.h`, `include/wormedit/core/bms_branch_editor.h`, `include/wormedit/core/command.h`, `src/core/bms_program.cpp`, `src/core/bms_program_projection.cpp`, `src/core/bms_program_edit.cpp`, `src/core/bms_branch_editor.cpp`, `src/core/command.cpp`, `src/core/interaction.cpp`, `src/core/sv_recipe.cpp`, `src/core/timing_lane_controller.cpp`, `src/sys/app/application_frame_view.cpp`, `src/sys/app/application.cpp`, `src/sys/project/compare_view.h`, `src/sys/project/compare_view.cpp`, `src/sys/compose/compose_source_key.h`, `tests/core/bms_program_edit_tests.cpp`, `tests/core/bms_program_authoring_tests.cpp`, new `tests/core/bms_program_authoring_view_tests.cpp`, `docs/architecture.md`, `memory/chart-editor-status/resume.md`, `src/core/note_to_bgm.cpp`, `src/core/marker_editor.cpp`, `include/wormedit/core/interaction_state.h`, `src/sys/compose/input/compose_input.cpp`, `src/sys/compose/views/compose_scene_view.cpp`, `src/ui/nuklear/compose/compose_canvas.cpp`, `src/ui/nuklear/compose/compose_panel.cpp`, new `src/ui/nuklear/chrome/text_block.h`, `src/ui/nuklear/chrome/notification_panel.cpp`, `src/core/note_move.cpp`, `src/core/interaction_internal.h`, `src/core/interaction_gesture_lifecycle.cpp`, `src/sys/timing/timing_view.h`, `src/sys/timing/timing_view.cpp`, this card.
- **Steps:**
  1. Follow approved question 24: separate node authoring from playback choices, show inherited content read-only, clear object selection on target changes and retain valid session targets through semantic Undo; migrate the touched branch view/action boundary to C++ values with one runtime owner for session target/drafts.
  2. Draw the ordered scope tree and exact target path; selecting controls targets first Chart, selecting another Chart overrides it, and meaningful commands do not appear as raw-statement scope rows.
  3. Expose Add, move, parameter edit, Text editing, Delete and Unwrap through typed core capabilities; RANDOM count and preview value have separate labels/actions, and destructive actions show their prepared summaries.
  4. Cover new/empty Root without retained source, nested scope/alternate-Chart targeting, keyboard capture isolation, resizing, Cancel, stale targets and Undo/Redo in the event harness; inspect isolated Xvfb at normal and 800×700 sizes.
- **Invariants:** frontend widgets do not own program semantics or mutations; missing targets never write to a hidden leaf or silently fall back to Root.
- **Reject/failure behavior:** show typed diagnostics, keep rejected drafts recoverable and release capture; no data change before confirmation.
- **Verify:** GATE; `build/tests/wormedit_frontend_event_harness_tests '[bms-scope]'`; isolated `scripts/smoke-compose-virtual-x.sh /tmp/bms-scope.png compose` with the nested native fixture prepared by the card, then inspect the PNG.
- **Out of scope:** final public-owner activation/old adapter deletion (T-744), optional inactive ghost overlays or drag/drop trees (not required for this first UI).

- **Supporting ownership extension (coordinator, 2026-10-23):** the additional paths above provide a detached authoring projection, explicit history/transaction read context, read-only inherited ownership checks, prepared-only workspace access, cache identity, typed parameter/preview edits, shared frame/runtime wiring and focused core tests. They are necessary for the approved separate authoring view to work without changing playback evaluation or publishing an edited projection. `chart_document_t` receives only a derived, nonserialized authoring-origin marker for source-cache identity. Existing active-preview APIs remain available; public import/owner cutover and broad legacy adapter removal stay T-744. Further supporting paths require another recorded extension before edits.

- **Warning presentation (owner, 2026-10-23):** show scope warnings with yellow text and a warning icon, including unreachable retained arms and read-only partial-hold context. Keep their diagnostic severity distinct from errors.

- **Selection ownership extension (coordinator, 2026-10-23):** `include/wormedit/core/interaction_state.h` owns clearing object and retained range selection when the authoring target changes; preserve cursor and viewport state and clear transient selection history in the runtime.

- **Additional read-path ownership (coordinator, 2026-10-23):** `src/core/note_to_bgm.cpp` and `src/core/marker_editor.cpp` plan against the current transaction view; route those reads through the explicit interactive chart lookup so inactive target geometry is honored.

- **Frontend supporting ownership (coordinator, 2026-10-23):** `src/sys/compose/input/compose_input.cpp` reads the explicit authoring context; `src/sys/compose/views/compose_scene_view.cpp` and `src/ui/nuklear/compose/compose_canvas.cpp` expose and mute read-only note context; `src/ui/nuklear/compose/compose_panel.cpp` keeps the scope panel usable at 800×700. These retain existing semantic ownership and isolated-window verification.

- **Wrapped-text ownership extension (coordinator, 2026-10-23):** extract the existing notification detail line renderer into `src/ui/nuklear/chrome/text_block.h` and consume it from `src/ui/nuklear/chrome/notification_panel.cpp` and the scope panel. This keeps warning and confirmation text fully readable in narrow panels without duplicating the wrapping implementation.

- **Gesture-capture ownership extension (coordinator, 2026-10-23):** `src/core/note_move.cpp`, `src/core/interaction_internal.h` and `src/core/interaction_gesture_lifecycle.cpp` pass the existing edit context into scoped move/resize publication and use the shared edit runner. This preserves the captured owner when terminal cleanup removes the gesture state; it avoids rebuilding a transaction against a later node selection.

- **Timing projection ownership extension (coordinator, 2026-10-23):** `src/sys/timing/timing_view.h` and `src/sys/timing/timing_view.cpp` read the authoring context, key geometry by the existing scope-aware scene identity, and initialize a valid empty header for Text/settings nodes. The isolated native fixture exposed a legacy-only lookup that rejected the whole real window; add a frontend projection regression before repeating the captures.

- **Completed (2026-10-24):** a project-owned runtime presents Root, controls, fixed settings, ordered Charts, Text and bare scopes. Node selection is separate from playback; scopes resolve their first direct Chart and explicit Chart selection remains exact. The detached field protects inherited content, shows inherited notes gray, and warns about partial hold context without inventing pairs. Selection changes clear objects/ranges and transient selection history; valid nodes remain selected through Undo/Redo. Captured move/resize transactions and source reads retain their original context.
- **Authoring boundary:** Add, move, scalar parameters, UTF-8 Text drafts, explicit preview choices, Delete and Unwrap use typed core commands/proposals. Rejected drafts remain recoverable; stale drafts require Reload. Delete, promotion and unconditional conversion have distinct captured confirmation operations. A stale Delete confirmation cannot authorize Unwrap. RANDOM range and playback choice have separate controls. Yellow text and a drawn warning triangle identify unreachable retained arms and scope warnings, including selected rows.
- **Bounded test-value review:** baseline authoring **61 cases** and scoped editor **52 cases** were retained. Reviewed **14 new core bodies** and **14 new scope harness bodies** covering inactive/unreachable views, inherited refusal, captured reads, partial holds, empty Root, native workspace binding, node selection, drafts, confirmations, alternate Charts, Undo repair, text capture, narrow rendering and real Compose/timing projection/cache invalidation. Three existing direct-edit cases now assert inherited refusal before deliberately selecting the recorded owner; the captured drag regression remains intact. Other retained tests were rerun for compatibility, not claimed as newly reviewed whole-suite value.
- **Verification:** GCC **GATE passes 4,873/4,873**, with whitespace and **31 changed C++ TUs** passing (`/tmp/t743-gate-5.log`, details `/tmp/wormedit-gate.fWRVNs`). Focused authoring-view/editor checks pass **887 assertions / 66 cases** (`/tmp/t743-core-focused-final.log`); scope harness passes **162 / 14** after strengthening the visible-note/read-only assertions (`/tmp/t743-scope-harness-final.log`). No test-listing failures were accepted.
- **Header complexity:** the initial 67-consumer scan identified one new row-renderer score of 26; extracted the cohesive row renderer. The final gate and **13 explicit header/gesture consumers** pass with no increased allowances (`/tmp/t743-header-recheck.log`). The first extra scan timed out on the large frame-timing consumer under its tighter memory cap; the complete 13-file recheck passed with more single-worker headroom.
- **Visual evidence:** generated `/tmp/t743-visual/nested.wormedit` using `WORMEDIT_BMS_SCOPE_FIXTURE=/tmp/t743-visual/nested.wormedit build/tests/wormedit_frontend_event_harness_tests '[bms-scope]'` after creating its directory. Inspected real isolated Xvfb captures at **1280×720** (`/tmp/t743-scope-normal.png`) and **800×700** (`/tmp/t743-scope-800.png`), selecting unreachable ELSEIF 3 while IF 1 remains the playback choice. Warning text/icon, selected-node/content paths and scrolling remain readable. The real-window check exposed and fixed the timing summary's legacy-only lookup; the new projection regression covers that boundary and same-revision target geometry changes. No test windows were opened on Niri.
- **Handoff:** T-744 is ready, not started. Public BMS import/new-project activation and retiring the legacy raw-statement adapter remain there; this card exposes prepared scoped native fixtures without enabling a second writable authority.

## T-744 Activate scoped ownership and retire raw statement authority

- **Status:** partial (step 1 identity foundation; public activation remains gated)
- **Prerequisites:** T-731–T-743, including the native migration and all product answers.
- **Outcome:** public BMS import/new-project/load/edit/save/export use one scoped program owner, with no independently writable retained-statement semantic graph or legacy panel adapter.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `docs/chart-editor-plan/audits/bms-scoped-ownership.md`, `src/core/bms_branch_editor.cpp`, `src/formats/native_project_migrations.cpp`, `src/ui/nuklear/project/bms_branch_panel.cpp`.
- **Files owned:** `docs/chart-editor-plan/tasks/bms-scoped-model.md`, `docs/chart-editor-plan/tasks/bms-scoped-authoring.md`, `docs/chart-editor-plan/editing/bms-scoped-program.md`, `docs/chart-editor-plan/tasks/bms-branches.md`, `docs/chart-editor-plan/tasks/compose-docks.md`, `memory/chart-editor-status/resume.md`, `src/core/bms_program.cpp`, `src/core/bms_branch_editor.cpp`, `src/formats/bms.cpp`, `src/formats/bms_writer.cpp`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `src/sys/app/application_defaults.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/sys/project/workspace_view.cpp`, `src/ui/nuklear/project/bms_branch_panel.cpp`, `tests/core/bms_program_edit_tests.cpp`, `tests/formats/bms_program_migration_tests.cpp`, `tests/formats/bms_program_writer_tests.cpp`, `tests/ui/bms_program_event_harness_tests.cpp`, `include/wormedit/core/bms_branch_editor.h`, `include/wormedit/core/bms_branch_view.h`, `include/wormedit/formats/bms.h`, `include/wormedit/formats/bms_branch_selection.h`, `src/core/bms_branch_view.cpp`, `src/formats/bms_branch_selection.cpp`, `src/core/command.cpp`, `src/core/chart_editor.cpp`, `src/core/id_ceiling.h`, `src/core/chart_object_ids.h`, `src/core/interaction.cpp`, `src/core/interaction_internal.h`, `src/core/resource_assignment.cpp`, `src/core/quantize.cpp`, `src/core/channel_map_editor.cpp`, `src/core/lane_migration.cpp`, `src/core/measure_editor.cpp`, `src/core/bpm_editor.cpp`, `src/core/pause_warp_editor.cpp`, `src/core/time_shift.cpp`, `src/core/timing_reposition.cpp`, `src/core/timing_links.cpp`, `src/core/workspace.cpp`, `src/formats/conversion.cpp`, `src/formats/osu_mania.cpp`, `src/sys/compose/actions/compose_action_outcome.cpp`, `src/ui/frontend/actions.h`, `src/ui/frontend/compose.h`, `src/ui/nuklear/compose/compose_state.h`, `src/ui/nuklear/project/bms_branch_panel.h`, `tests/core/bms_branch_editor_tests.cpp`, `tests/core/chart_editor_tests.cpp`, `tests/core/timing_links_tests.cpp`, `tests/sys/application_defaults_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `tests/formats/bms_tests.cpp`, `tests/formats/bms_writer_tests.cpp`, `tests/formats/conversion_tests.cpp`, `tests/formats/native_project_tests.cpp`, `tests/core/command_journal_tests.cpp`, `tests/ui/walkthrough_bms_event_harness_tests.cpp`, `src/core/chart_import.cpp`, `src/core/sound_import.cpp`, `tests/core/chart_import_tests.cpp`, `tests/core/sound_import_tests.cpp`.
- **Cutover inventory (T-731, 2026-10-23):** retire the exact functions and authority listed in the [one-way transition audit](../audits/bms-scoped-ownership.md#one-way-transition-and-public-cutover), including source-order guards and non-BMS conversion loss checks. Preserve refusals until replacement capabilities are verified. The supporting files above cover new/add/duplicate difficulty, all inactive IDs, legacy statement/selection APIs, panel feedback, migration/recovery and export; no implicit widening of supported dialects or conditional timing links.
- **Supporting ownership (2026-10-24):** `include/wormedit/core/bms_program.h`, `src/core/bms_program_identity.cpp`, `src/formats/native_project_bms_migration.inc`, `CMakeLists.txt`: replace migration's serialized-key ID rewriting with a typed program identity operation shared by creation/import/duplication. Public activation remains gated on the complete cutover inventory.
- **Done so far (step 1 checkpoint, 2026-10-24):** typed identity remapping covers scoped declarations/references, native exact positions and child IDs, IF continuations and SKIP anchors; lane vocabulary, Text/provenance and project sound-group membership remain intact. Explicit legacy preparation uses this operation. Shared typed resource/asset/recipe/chart ceilings include prepared owners, and ordinary chart creation/duplication in mixed projects uses complete current/Undo/Redo ceilings. GCC GATE passes **4,882/4,882**; focused migration **1,002 assertions / 28 cases**, combined BMS **8,766 / 243**, scope event harness **162 / 14**. Complexity passes for three changed TUs, five explicit header consumers and the native migration TU; Clang compiles all four directly affected TUs. No complexity allowances changed. Public activation and its acceptance captures remain pending.
- **Remaining:** finish step 1 before public activation: preserve native difficulty settings in the scoped owner, create fresh BMS Roots and support full-program difficulty duplication/import with shared pools, and reconcile edited legacy native data without relying on unchanged-source equivalence. Then perform steps 2–4's coordinated public import/load/edit/export activation, legacy semantic-path retirement and complete acceptance matrix. Existing production ownership and conservative capability refusals remain until those replacements are verified.
- **Bounded checkpoint test review:** reviewed nine new migration-test bodies for the shared identity operation and mixed-project allocation boundary, including all 14 existing native fixture variants, inactive structure, export equivalence, invalid maps, native references, sound groups, exhaustion and recipe allocation. The 19 existing migration bodies are retained, not newly reviewed for value. These counts make no claim about the whole suite.
- **Steps:**
  1. Activate the validated public import/native schema/new-project paths and scoped edit routing together; reconcile old command callers through the audited migration, never two writable authorities.
  2. Remove every temporary T-744 adapter/guard and the retained raw-statement panel as a semantic editing entry; retain read-only provenance and opaque Text where still required.
  3. Run the full contract fixture matrix across import, alternate outcomes, edits, node operations, native reload and default BMS export; verify untouched non-BMS documents and existing project difficulties.
  4. Reconcile historical T-90–T-93/T-287/T-399 statuses only against demonstrated outcomes; update the resume line with real gate/visual evidence and no premature completion claims.
- **Invariants:** program data has one authoritative owner; source caches and projected charts are derived; stable identities and every inactive outcome survive migration.
- **Reject/failure behavior:** failed migration, unsupported scoped content or an incomplete command-family adapter blocks cutover instead of discarding data or enabling partial production authoring.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program],[bms-program-import],[bms-program-projection],[bms-program-writer],[bms-program-edit],[bms-program-migration],[bms-program-authoring]'`; `build/tests/wormedit_frontend_event_harness_tests '[bms-scope]'`; isolated Xvfb acceptance captures and native save/reload comparison.
- **Out of scope:** individual-note move/copy between scopes (T-93), opt-out malformed-flow repair proposals (T-287), new BMS dialects and an inactive ghost overlay (separate future work).

Checkpoint verification logs: `/tmp/t744-gate-2.log` (details `/tmp/wormedit-gate.WPQXTP`),
`/tmp/t744-migration-final.log`, `/tmp/t744-bms-contract-final.log`,
`/tmp/t744-scope-harness-1.log`, `/tmp/t744-header-1.log`,
`/tmp/t744-native-complexity.log`, `/tmp/t744-clang-1.log`.
Actual output tails:

```text
  pass  build
  pass  ctest
  pass  whitespace
  pass  complexity
  100% tests passed out of 4882
gate: passed
All tests passed (1002 assertions in 28 test cases)
All tests passed (8766 assertions in 243 test cases)
All tests passed (162 assertions in 14 test cases)
complexity: passed (5 translation units)
complexity: passed (1 translation units)
```
