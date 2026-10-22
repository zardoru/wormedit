# Feature friction: refactors and tooling found by the measure lane

[Back to task cards](../tasks.md).

Building the measure lane ([T-654–T-657](measure-lane.md)) crossed core, sys and UI. T-657's card listed 5 files and its commit touched 22; T-656 listed 3 and touched 8. Most of the gap was finding the places to change. The [retrospective](../../../memory/coordination/oct17-measure-lane-retrospective.md) records each observation; these cards are its proposed fixes that no open card covers. Proposed fixes that fit an open card are recorded on that card instead: T-621, T-643, T-644, T-646 and T-653 in [state ownership](state-ownership.md). T-665–T-672 came from a second pass over how to make pending cards easier to land and problems easier to catch, in both tooling and code. One codebase question went to the owner as [open question 20](../decisions/open-questions.md), answered 2026-10-17: the C ABI requirement is relaxed in favor of a slow C++ migration when touched ([code-style.md, "Frontend boundary"](../decisions/code-style.md)).

All of these were **proposed** (2026-10-17) for owner approval; each card's Status says whether it is done. Each card says plainly how much it is worth. Two constraints apply throughout:
- The [Nuklear audit](../../audits/nuklear-2026-10-16.md) rejects stored callbacks, virtual dispatch and table-driven type erasure for canvas rendering. Sharing here means plain named functions over the view.
- D3 keeps the Compose edit toggles resetting per project, and D6 forbids splitting Nuklear UI state lifetimes. No card changes a lifetime.

## T-658 Write down the names and paths a cross-layer feature has to find

- **Status:** done
- **Evidence (2026-10-19):** `docs/architecture.md` now has the per-layer glossary, verified area
  selection and paste lookup rows, a four-feature owner index, five measure-lane-derived addition
  recipes and the four fixed navigation prompts with comparison conditions. The event-harness page
  records the qualified press/candidate/release/click order. Card authoring now requires tracing the
  nearest feature before ownership, the standard wiring pattern follows the C++ frontend boundary,
  and editable views carry stable IDs. `AGENTS.md` already had the T-665 job-count/GATE rule and did
  not need an edit.
- **Value:** High for little cost: it is documentation only, and it removes most of the search time the measure lane spent.
- **Outcome:** `docs/architecture.md` has a compact feature-to-owner index naming public entry,
  capability, workflow/session owner, core operation, completion and regression home for paste,
  quantize, note/BGM conversion and measure editing; its glossary maps each concept to its name per
  layer. `docs/frontend-event-harness.md` describes the pointer press → candidate → release → click
  sequence; `AGENTS.md` says how to build in parallel; `docs/chart-editor-plan/tasks.md` asks card
  authors to map the nearest existing feature before listing **Files owned**.
- **Read first:** `docs/architecture.md`, `docs/frontend-event-harness.md`, `AGENTS.md`, the retrospective's observations 1, 3, 4, 7 and 15.
- **Files owned:** `docs/architecture.md`, `docs/frontend-event-harness.md`, `AGENTS.md`, `docs/chart-editor-plan/tasks.md`, `docs/chart-editor-plan/decisions/code-style.md`, this card.
- **Steps:**
  1. Stale-plan check: `grep -n "Glossary" docs/architecture.md`.
  2. Glossary, at least: bar line / measure line (`editor_grid_line_kind_t::measure`) / measure rail (`compose_measure_rail_width`) / measure labels (BMS) / split and merge (`measure_editor.h`); selection object / hit target / area geometry; proposal / report / ghost preview. Each entry names the type or file per layer.
  3. "Where to look" rows: area selection (projected rectangles from `cache_area_object_geometry` in `src/sys/compose/views/compose_view.cpp` when a snapshot exists, otherwise the row-range index in `src/core/interaction_selection.cpp`; scope flags filter both); paste (proposal in `src/core/chart_clipboard.cpp`, ghost geometry in `paste_geometry`).
  4. Pointer sequence in `docs/frontend-event-harness.md`: a press starts an area-selection candidate unless `compose_pointer_start_hits_element` claims it; Nuklear's `pointer_clicked` is the *release*; the order in `process_compose_pointer_actions` decides which handler owns a click. Check each claim against the code before writing it.
  5. Done by T-665: `AGENTS.md` states the job-count rule and points at `scripts/gate.sh`. Check it still reads correctly.
  6. `tasks.md` rules: before writing **Files owned**, grep how the nearest existing feature is wired (for a lane: the BGA lane and timing markers) and list those files.
  7. `code-style.md`: a frontend view of an object the user can select or edit carries the object's ID; display numbers are not keys (see the proposed addition on T-653).
  8. "Adding an X" recipes in `docs/architecture.md`, each a short list of every touchpoint with file and symbol: a selection object kind, a Compose action, a toolbar button, a Compose view field (written the C++ way under `code-style.md` "Frontend boundary", from T-672's worked example; the old C-struct path described only for touching existing fields), a selectable canvas lane. Write each by tracing a recent real addition (`bar_line`, `WORM_UI_COMPOSE_ACTION_TOGGLE_MEASURE_LANE`, the measure-lane toggles and lane). Where a recipe step could instead be a compile error, note it for T-661/T-669 rather than relying on the recipe.
  9. Record the four fixed navigation exercise prompts from the workflow-navigation audit beside
     the index. The baseline runs after this card and before the quantize pilot, with identical
     model, effort, tools and starting context for the post-pilot comparison.
- **Invariants:** No code change. Every symbol named in the docs exists (grep each).
- **Reject/failure behavior:** A claim the code contradicts is corrected or dropped, not written.
- **Verify:** `ctest --test-dir build -R '^documentation\.' --output-on-failure`; grep every symbol the new text names.
- **Out of scope:** Renaming any symbol to make the names agree; switching `build/` to Ninja (an owner choice: it discards the warm build).

## T-722 Route keyboard and frontend quantize through one typed workflow

- **Status:** done (2026-10-19)
- **Completion:** `src/sys/compose/actions/compose_quantize.cpp` now owns capability, stamped editor initialization,
  preview toggling, typed selection normalization, report retention and application outcomes.
  Registered Apply passes the stored typed request directly; the area route alone decodes the
  frontend payload. Application still forces collision rejection, disables preview-only resolution
  promotion, clears legacy object IDs and attaches current note/BGM selection. Invalid stored
  settings, report contents, collision policy, promotion policy, Undo, diagnostics and rebind
  failure have production-path regressions. The sys frontend-action-builder ratchet is 3 → 2, with
  the two project drop/replacement adapters retained.
- **Assessment:** The repeated quantize trace had no wrong turn or internal frontend detour, versus
  one wrong file guess before, but inspected 13 files versus 9. Untouched controls varied from
  −4 to +3 files and from zero to three wrong guesses. This is a useful structural cleanup, not
  evidence that the extraction broadly improved navigation; see the baseline audit's verdict.
- **Verification:** Focused core/sys quantize checks pass **1,358 assertions in 82 cases**,
  including invalid stored settings and retained collision/promotion policy. Final GATE passes
  **4,230/4,230** with build, CTest, whitespace and changed-C++ complexity clean
  (`/tmp/wormedit-gate.1kQR7r`). Documentation passes 5/5. Affected clang-tidy has no new findings;
  it reports only recorded existing complexity in `build_speed_graph` and unrelated legacy test
  cases. The four fresh Luna medium post-change traces found all mutation and completion boundaries.
- **Baseline:** The fixed before-refactor observation is
  [`workflow-navigation-baseline-2026-10-19.md`](../../audits/workflow-navigation-baseline-2026-10-19.md).
  Its keyboard trace inspected nine files and found the typed request → frontend action → typed
  request round trip. The lexical sys frontend-action construction inventory is three sites: this
  quantize detour and two retained project drop/replacement adapters in `application.cpp`.
- **Outcome:** `compose_quantize` is the named sys workflow owner for capability, editor-state
  initialization/preview toggling and typed selection application. A registered command passes its
  stored `quantize_request_t` directly; only the real frontend route decodes
  `WormUiWorkspaceAction`. Both routes retain one typed application body for selection attachment,
  report storage, core mutation and outcome construction.
- **Read first:** `docs/architecture.md`; `docs/chart-editor-plan/decisions/code-style.md`;
  `docs/audits/workflow-navigation-2026-10-18.md`; `src/sys/compose/actions/compose_actions.cpp`;
  `src/sys/compose/actions/compose_area_actions.cpp`; `src/sys/compose/actions/compose_quantize_request.h`;
  `src/sys/commands/application_command_dispatch.cpp`; `src/core/command.cpp`.
- **Files owned:** new `src/sys/compose/actions/compose_quantize.h`, `src/sys/compose/actions/compose_quantize.cpp`;
  `src/sys/compose/actions/compose_actions.h`, `src/sys/compose/actions/compose_actions.cpp`,
  `src/sys/compose/actions/compose_actions_internal.h`, `src/sys/compose/actions/compose_area_actions.h`,
  `src/sys/compose/actions/compose_area_actions.cpp`, `src/sys/compose/actions/compose_quantize_request.h`,
  `src/sys/commands/application_command_dispatch.cpp`, `tests/sys/application_commands_tests.cpp`,
  `tests/sys/frame_timing_tests.cpp`,
  `CMakeLists.txt`, `tests/CMakeLists.txt`, `cmake/ceremony_ratchet.txt`, `docs/architecture.md`,
  this card, the baseline audit and `memory/chart-editor-status/resume.md`.
- **Steps:**
  1. Recheck all request producers/consumers and the three local sys frontend-action builders.
     Record the construction ratchet at three before removal and two after it; the row is a lexical
     guard, not proof of intent.
  2. Add the named sys owner. Move capability, quantize-editor initialization, preview toggle and
     typed application/report completion into it. Keep the existing core command and result types.
  3. Decode frontend settings once in the area-action adapter. Pass stored typed settings directly
     from the registered command; remove `invoke_compose_quantize_command` and its fabricated
     frontend action.
  4. Preserve the old decoder's application policy in the typed owner: validate divisions and
     enum ranges, force collision rejection and resolution promotion off, clear legacy object IDs,
     and attach the current typed selection without mutating stored panel settings.
  5. Strengthen the registered-command regression for applied report contents and add invalid
     stored-settings rejection. Run focused checks, GATE and affected clang-tidy/complexity.
  6. Repeat all four fixed navigation exercises under the recorded conditions. Record whether the
     result justifies this extraction before recommending the pattern elsewhere.
- **Invariants:** Same command IDs, capability projection and invocation recheck; same preview
  stamp/toggle lifetime; current note/BGM selection; collision and resolution-promotion policy;
  report row ordering/count/truncation; one transaction and Undo; accepted no-ops, diagnostics,
  feedback delivery/order and producer rebind policy. Frontend action values and UI projection do
  not change.
- **Reject/failure behavior:** Invalid typed or frontend settings reject before preview/mutation;
  core rejection retains its first diagnostic and does not store an applied report. Rebind failure
  still reports the successful edit before the document error. If the repeated trace does not find
  the correct mutation and completion boundaries, do not use this extraction as a template.
- **Verify:** focused `application_commands`, `compose_action_notification` and quantize tests;
  `scripts/gate.sh --agents 1`; `clang-tidy -p build` and `scripts/check-complexity.py` on affected
  C++ consumers; four fixed fresh-Luna navigation exercises; `git diff --check`.
- **Out of scope:** Converting the frontend action payload (T-694), changing quantize behavior or
  preview projection, moving quantize tests wholesale (T-671), paste/measure extraction, or the two
  retained project drop/replacement frontend adapters.

## T-659 Give the paste's chart changes one owner

- **Status:** deferred (proposed 2026-10-17; start only if the owner names T-659)
- **Value:** Modest. It prevents one class of silent preview drift. Today only bar-line splits are affected, so it is worth doing only if paste gains more geometry effects.
- **Outcome:** One member of `chart_paste_report_t` (for example `apply_geometry(chart_document_t&) const -> diagnostic_list_t`) performs every structural change a paste makes. The proposal, `chart_paste_proposal_t::apply` and the sys ghost preview (`paste_geometry` in `src/sys/compose/views/compose_view.cpp`) all call it instead of each calling `add_bar_lines`.
- **Read first:** `include/wormedit/core/chart_clipboard.h`, `src/core/chart_clipboard.cpp` (`add_paste_bar_lines`, `apply`), `src/sys/compose/views/compose_view.cpp` (`paste_geometry`), T-656's Done note in [measure-lane.md](measure-lane.md).
- **Files owned:** `include/wormedit/core/chart_clipboard.h`, `src/core/chart_clipboard.cpp`, `src/sys/compose/views/compose_view.cpp`, `tests/core/chart_clipboard_tests.cpp`, this card.
- **Steps:**
  1. Stale-plan check: `grep -n "add_bar_lines" src/core/chart_clipboard.cpp src/sys/compose/views/compose_view.cpp` (expect three call sites).
  2. Add the member and route the three call sites through it.
  3. Add a test that a pasted bar line and an object in its new measure produce the same measure IDs in the proposal, the committed chart and the ghost geometry.
- **Invariants:** Same pasted documents, IDs, diagnostics and one Undo step. Older clipboard payloads paste unchanged.
- **Reject/failure behavior:** A split failure rejects the paste as today; the ghost keeps showing the rejected candidate's objects.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`; `ctest --test-dir build -L event-harness`; `clang-tidy -p build src/core/chart_clipboard.cpp`.
- **Out of scope:** Changing what paste does; the ID-allocation contract (recorded on T-643).

## T-660 Put canvas object geometry and hit testing in one header

- **Status:** deferred (old universal-picker sketch requires replacement scope; start only if the owner names T-660)
- **Reevaluation (owner direction, 2026-10-18):** [T-679](canvas-input-ownership.md) completed the proven BGM/bookmark duplication cleanup. BGA/timing/bar-line bounds were already shared; pointer and area selection have intentionally different shapes. [T-682](extension-friction.md) now inventories specialized-lane extension sites. Retain the following outcome as the historical proposal until that inventory supplies its replacement scope; the approximate ten-file benefit was not re-established by the recent bounded review.
- **Value (historical proposal):** The highest of these cards. The next selectable lane becomes one bounds function, one line in the hit query, one draw pass and one dispatch case, instead of edits in about ten files. It is also the largest and riskiest: input routing order is a contract.
- **Outcome:** `src/ui/compose_canvas_geometry.h` (renamed from, or including, `compose_timing_geometry.h`) holds one `compose_<kind>_bounds(view, item)` per non-note canvas object kind (timing markers and bar lines already exist; BGA events, BGM events and bookmarks move in) and one query `compose_canvas_object_at(view, x, y) -> std::optional<editor_selection_object_t>`, written as a fixed sequence of named calls. `compose_pointer_start_hits_element`, the click handlers and hover read that query; sys `append_area_*_geometry` builds rectangles from the same bounds functions. Rail coordinates are named constants.
- **Read first:** `src/ui/compose_timing_geometry.h`; `src/ui/nuklear/compose_input_panel.cpp` (`compose_pointer_start_hits_element`, `process_compose_pointer_actions`); `src/ui/nuklear/compose_input_internal.h` (`compose_bgm_event_at`, `compose_bga_event_at`); `src/ui/nuklear/compose_bookmark_rename.cpp` (`compose_bookmark_at`); `src/ui/nuklear/compose_canvas.cpp` (`compose_timing_marker_at`); `src/sys/compose/views/compose_view.cpp` (`append_area_*_geometry`); the Nuklear audit's "rejected over-unification" section; T-645.
- **Files owned:** `src/ui/compose_timing_geometry.h` (or its renamed successor), `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_input_internal.h`, `src/ui/nuklear/compose_object_input.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_bookmark_rename.cpp`, `src/ui/nuklear/compose_viewport_input.cpp`, `src/ui/nuklear/compose_area_menu.cpp`, `src/sys/compose/views/compose_view.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/bga_lane_event_harness_tests.cpp`, `tests/ui/markers_event_harness_tests.cpp`, this card.
- **Steps:**
  1. Stale-plan check: `grep -rn "compose_canvas_object_at" src`. Then inventory every non-note hit function, its rectangle, its callers and its priority when rectangles overlap; save the table on this card.
  2. Move each bounds computation into the header without changing it; each existing `*_at` function calls its bounds function.
  3. Add `compose_canvas_object_at` with the inventoried priority order; switch `compose_pointer_start_hits_element` to it.
  4. Switch the click and hover callers one kind per checkpoint, keeping their own policies (modifiers, double-click, Draw mode).
  5. Build sys area geometry from the same bounds functions where it does not already.
  6. Replace the literal rail values (`42.0`, `22.0` in `compose_viewport_input.cpp` and `compose_area_menu.cpp`) with named constants.
- **Invariants:** Same action kinds, counts and order for every existing event-harness case; same overlap priority; notes keep their core hit index. No stored callbacks, virtual dispatch or per-frame allocation (Nuklear audit).
- **Frontend boundary (2026-10-17):** write the bounds functions against what they need (a rectangle source and the view geometry), not the whole `WormUiComposeView`, so a surface converted to C++ types (T-672 and later) can call them unchanged.
- **Reject/failure behavior:** Any change in which object a press or click selects stops the card and goes to the owner with a failing-before harness test.
- **Verify:** `ctest --test-dir build -L event-harness`; `ctest --test-dir build --output-on-failure`; `clang-tidy -p build` on each touched `.cpp` with no new or raised findings; isolated Xvfb capture via `scripts/smoke-compose-virtual-x.sh`.
- **Out of scope:** Note hit testing; capture continuation (T-641); hover/paint gate queries (T-645); drawing code beyond calling the shared bounds.

## T-661 Make additions to scopes and action enums fail at compile time

- **Status:** Done (2026-10-17; owner asked for it directly)
- **Value:** Small but cheap. Each of the two traps cost real time once: a new scope flag is silently false at positional construction sites, and an appended action enum value aborts test discovery instead of failing the build.
- **Outcome:** `editor_selection_scope_t` has named presets (at least `every_object()`), and no positional construction remains. The Compose action and input enums end with a `_COUNT` sentinel, every table sized from an enum uses it, and a `static_assert` ties each route table to it.
- **Read first:** `include/wormedit/core/interaction.h` (`editor_selection_scope_t`), `src/sys/compose/input/compose_input_internal.h:1145`, `src/sys/commands/application_command_dispatch.cpp:484`, `src/ui/frontend/actions.h`, `src/sys/compose/actions/compose_actions.cpp` (`compose_kind_count`, `build_compose_action_routes`), `cmake/ceremony_ratchet.txt`.
- **Files owned:** `include/wormedit/core/interaction.h`, `src/sys/compose/input/compose_input_internal.h`, `src/sys/commands/application_command_dispatch.cpp`, `src/ui/frontend/actions.h`, `src/sys/compose/actions/compose_actions.cpp`, `cmake/ceremony_ratchet.txt`, this card.
- **Steps:**
  1. Stale-plan check: `grep -rn "editor_selection_scope_t{true" src` and `grep -n "_COUNT" src/ui/frontend/actions.h`.
  2. Add the presets; convert both positional sites; add a ratchet row for positional construction at 0.
  3. Append the sentinels (end-of-enum, so existing values do not move); size `compose_kind_count` from it; make the route table build `constexpr` so a duplicate or out-of-range route fails to compile.
  4. Grep for other arrays sized by a named last enumerator and convert them.
- **Invariants:** No enum value changes; same routes; same selections from Select All and area selection.
- **Reject/failure behavior:** If the route table cannot be `constexpr` without restructuring the route modules, keep the runtime build and add only the `static_assert` on size; record why.
- **Verify:** GATE (T-665), or by hand: `cmake --build build --parallel <jobs>`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -R '^documentation\.'`, with `<jobs>` chosen as in T-665.
- **Out of scope:** Re-routing actions (T-636 is done); renaming scope fields.
- **Done:** `editor_selection_scope_t::every_object()` (all five kinds) replaces the positional `{true, true, true, true, true}` in `src/sys/compose/input/compose_input_internal.h`; Select All's scope in `src/sys/commands/application_command_dispatch.cpp` is now the designated `every_object_but_bar_lines`, since it deliberately excludes bar lines (they are selectable only through the measure lane). Ratchet row `positional_selection_scope` at 0. `WORM_UI_COMPOSE_ACTION_COUNT` ends the action enum and sizes the route index (`compose_action_kind_count` in `src/sys/compose/actions/compose_action_routes.h`). The combined route index cannot be `constexpr` because its seven modules live in different files, so (per this card's reject clause) each module instead `static_assert`s `is_valid_route_module`: every kind below the count, none repeated within the module. A repeat across modules is still checked at startup, and T-665's gate now reports that failure clearly. No input-kind table is sized by an enum, so the input enum got no sentinel; the other `+ 1` sizes found were byte capacities for a terminating NUL. Evidence: build passes; a scratch duplicate route in `compose_bga_actions.cpp` failed with "static assertion failed"; a scratch positional scope failed `documentation.ceremony_ratchet` ("1 > maximum 0"); both reverted; complexity unchanged (only the existing `build_speed_graph` finding).

## T-662 Let event-harness tests find widgets by tooltip

- **Status:** Done (2026-10-18; bounded T-644 toolbar checkpoint completed first)
- **Value:** Medium. Adding one toolbar button broke three tests that click fixed x positions; every toolbar change will keep doing that.
- **Outcome:** The harness records each icon widget's bounds and tooltip during a frame, and tests call `harness.click_widget("Hide sounds list")`. Toolbar tests no longer hard-code x positions.
- **Read first:** `tests/ui/nuklear_event_harness.h`, `docs/frontend-event-harness.md`, `src/ui/nuklear/chrome.h` (`IconButton`, `DrawnIconButton`), the testing hook `worm_ui_frontend_compose_viewport_for_testing`.
- **Files owned:** `tests/ui/nuklear_event_harness.h`, `tests/ui/nuklear_event_harness.cpp`, `src/ui/nuklear/chrome.h` (reference; hook may use its shared native cell), `src/ui/nuklear/components.cpp` (test-only recording at the shared native icon cell), new `src/ui/nuklear/icon_widget_recording.h` (C++ test snapshot), new `tests/ui/widget_lookup_event_harness_tests.cpp` (lookup contracts), `tests/CMakeLists.txt` (test-only define), `tests/ui/compose_hover_timing_event_harness_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/bga_lane_event_harness_tests.cpp`, `tests/ui/inspector_resource_workflow_tests.cpp`, `docs/frontend-event-harness.md`, this card.
- **Steps:**
  1. Stale-plan check: `grep -rn "click_widget" tests/ui`.
  2. Record at the shared native icon cell (both icon families), then add the recording hook, compiled only for tests or behind the existing testing hooks; no production allocation. It is a new frontend surface, so write it in C++ types (`code-style.md` "Frontend boundary"), not as a `WormUi*` C struct.
  3. Add `click_widget` and migrate every toolbar click by x position (`grep -rn ", 122" tests/ui`).
- **Invariants:** No production behavior or per-frame allocation change; same assertions in migrated tests.
- **Reject/failure behavior:** A tooltip that is not unique fails the lookup loudly rather than picking one.
- **Verify:** `ctest --test-dir build -L event-harness`; `ctest --test-dir build --output-on-failure`.
- **Out of scope:** Non-toolbar pixel clicks inside the field (they test geometry on purpose); T-644's pruning.
- **Execution scope (2026-10-18):** Missing/duplicate tooltips fail loudly. Capture only visible clipped bounds, including disabled icons for hover/rejection checks; refresh per frame. The hook is compiled only in the event-harness target, with no production recorder or allocations. Add focused lookup-contract coverage and exercise existing compact/wide, conditional-icon and disabled-input cases. Verify GCC/Clang GATE and the header consumers explicitly.

- **Implementation/evidence (2026-10-18):** The shared `NativeIconCell` records both icon families only under the event-harness target's `WORMEDIT_TEST_ICON_WIDGETS` definition. A C++ `icon_widget_snapshot_t` owns tooltip strings and uses existing `ui/field/rect.h` intersection over SDL rectangles; a frame scope clears the snapshot and installs/restores its sink. `widget_center`, `hover_widget` and `click_widget` reject missing/ambiguous matches, retain disabled icons for rejection checks and exclude clipped-away cells. Production symbol inspection found no recorder symbols. All identified hard-coded top Compose toolbar positions in the four owned test files were migrated; field/overview geometry coordinates remain intentional.
- **Verification:** Final GCC GATE `/tmp/wormedit-gate.kzqQJF` and Clang GATE `/tmp/wormedit-gate.E8BLhf` each report `100% tests passed out of 4128` and `gate: passed`; each complexity stage checked all seven changed C++ consumers with no new/increased allowances. Event harness **531/531** (`/tmp/t662-final-events.log`); focused reviewed toolbar plus lookup cases **115 assertions / 11 cases** (`/tmp/t662-final-focused.log`). Three new cases cover uniqueness errors, clipped visibility and real-frontend per-frame refresh; the existing color-mode case runs at two widths with and without the conditional measure-lane icon. Isolated Compose fixture smoke `/tmp/t662-compose.png` was inspected: loaded title, toolbar, M1–M3 and notes render. Ceremony maxima are unchanged. Final documentation checks passed **3/3** (`/tmp/t662-final-docs.log`). Gate resource measurements are in `/tmp/t662-final-{gcc,clang}-timing.log`; these include rebuilds and establish no speed improvement.

## T-663 Keep the Compose edit toggles in one struct

- **Status:** deferred (proposed 2026-10-17; start only if the owner names T-663)
- **Value:** Small, and the least certain of these cards. It removes per-field mirroring and the second write in each toggle, but the benefit grows only if more toggles arrive. Skip it if no new toggle is planned.
- **Outcome:** `edit_snap_enabled`, `y_lock` and `bar_line_time` form one `editor_edit_settings_t` stored in `project.compose_view` (where D3 puts them) and copied whole into the interaction state at the existing idle-frame point. Toggles write only `compose_view`; the view reads the struct.
- **Read first:** `src/sys/compose/views/compose_view.h` (`compose_view_state_t`), `src/sys/compose/views/compose_view.cpp` (the `!retained_state->is_pending()` copy and `view.y_lock`), `src/sys/compose/actions/compose_actions.cpp` (the snap, Y-lock and bar-line time toggles), `include/wormedit/core/interaction.h`, D3 in [state ownership](state-ownership.md) (T-601).
- **Files owned:** `src/sys/compose/views/compose_view.h`, `src/sys/compose/views/compose_view.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `src/core/note_move.cpp`, `tests/sys/compose_action_notification_tests.cpp`, this card.
- **Steps:**
  1. Stale-plan check: `grep -rn "editor_edit_settings_t" include src`.
  2. Add a test first: changing snap or Y-lock while a gesture is pending takes effect only after the gesture ends (today's behavior from the idle-frame copy).
  3. Introduce the struct; replace the three per-field copies with one; toggles write `compose_view` only.
- **Invariants:** Same reset behavior (D3), same lifetimes (D6), same timing of when a toggle reaches a pending gesture.
- **Reject/failure behavior:** If any core path needs a toggle value before the idle-frame copy, keep its current immediate write and record why.
- **Verify:** `ctest --test-dir build --output-on-failure`; `clang-tidy -p build` on each touched `.cpp`.
- **Out of scope:** Persisting these settings (D3); other `compose_view` fields.

## T-664 Give the Compose smoke check a chart with real measures

- **Status:** Done (2026-10-18)
- **Value:** Small. The default smoke project has one real measure, so bar-line or measure work needs a hand-picked project and trial clicks.
- **Outcome:** `scripts/smoke-compose-virtual-x.sh` can open a checked-in multi-measure chart, takes an optional `STEPS=<file>` hook that replaces its final capture (so scripted interactions no longer need a `sed`-edited copy of the script), and its header comment notes the field origin in the real window (x ≈ 49 at 1280×720, behind the tool column) and two known traps: a steps file leaving an unsaved dialog needs `kill -9 $app_pid` to keep cleanup from waiting, and Ctrl+Alt chords never reach the app under Xvfb (use the event harness for those).
- **Read first:** `scripts/smoke-compose-virtual-x.sh`, `tests/fixtures/README.md`.
- **Files owned:** `scripts/smoke-compose-virtual-x.sh`, new `tests/fixtures/native/compose-smoke.wormedit`, `tests/fixtures/README.md`, this card. Coordinator inventory (2026-10-18): the existing BMS fixture is rejected by the app's native command-line project path, so use a native fixture generated by the existing converter from eight synthetic measures, one tap per measure and no audio resources. Conversion and re-import validation pass. Preserve default smoke behavior; only the optional STEPS path replaces the final capture.
- **Steps:**
  1. Stale-plan check: whether an existing fixture (for example under `tests/fixtures/osu/`) already has several measures and opens through the script's project argument; if one does, document it instead of adding a file.
  2. Otherwise add a small synthetic chart (about eight measures, a few notes, no audio) with its README row.
  3. Add the `STEPS` hook: when set, source the file (with `click_at`, `$window_id`, `$output` and `$app_pid` available) instead of the final `import`.
  4. Note the field origin, the fixture and the two traps in the script's header comment.
- **Invariants:** Default script behavior unchanged; isolated Xvfb only.
- **Reject/failure behavior:** If the script cannot open the fixture's format, use a native project fixture instead.
- **Verify:** Run the script with the fixture and inspect the PNG: several measure labels and bar lines are visible. Run it once with a two-line `STEPS` file (one click, one capture) and inspect that PNG too.
- **Evidence (2026-10-18):** The existing BMS fixture was captured but did not load (native JSON startup path), so it is not counted as a passing chart check. The generated native fixture contains eight real measures and eight soundless taps, with no external resources; the converter reports re-import validation. Both `scripts/smoke-compose-virtual-x.sh /tmp/t664-measures.png compose tests/fixtures/native/compose-smoke.wormedit` and the same command with `STEPS=/tmp/t664-steps.sh` succeed. Both PNGs were opened and inspected: the default shows M1–M3 and notes; the two-line click/capture hook changes 192 to 240 pixels/measure and shows M1–M2. Bash syntax checks pass. Final documentation verification is recorded in the commit body.
- **Out of scope:** Scripted interaction steps beyond opening the chart.

## T-665 Define GATE as one script

- **Status:** Done (2026-10-17; owner asked for it directly)
- **Value:** High. T-645–T-650 say "Verify: GATE", but nothing in the repository defines GATE. Every other card repeats a hand-run Verify block. During T-657 a static-initialization abort made ctest print "Error listing tests", which a search for "tests failed" did not catch, so a whole test binary silently did not run.
- **Outcome:** `scripts/gate.sh` builds in parallel, runs ctest and fails on any test-listing error as well as on failures, runs the event-harness and documentation labels, runs `git diff --check`, runs the complexity comparison from T-666 on changed `.cpp` files once that exists, and prints one pass/fail summary with log paths. `docs/chart-editor-plan/tasks.md` defines GATE as this script.
- **Read first:** `AGENTS.md` (Build and verify), `docs/chart-editor-plan/tasks.md` (rules), the Verify fields of T-641 and T-645, `scripts/check-staged-cpp.py` (existing script style).
- **Files owned:** `scripts/gate.sh` (new), `docs/chart-editor-plan/tasks.md`, `AGENTS.md`, this card.
- **Steps:**
  1. Stale-plan check: `ls scripts/ | grep -i gate`; `grep -rn "GATE" docs AGENTS.md`.
  2. Write the script: build directory as an argument (default `build`), logs under a temporary directory it prints, nonzero exit on any failed stage. Treat "Error listing tests" and "Not Run" in ctest output as failures.
  2a. Job count (owner, 2026-10-17: the gate is often not allowed the whole computer). Default: `min(8, nproc / agents)`, at least 1, where `agents` is the number of concurrent runs sharing the machine (`--agents N` or `WORMEDIT_GATE_AGENTS`, default 1). `--jobs N` (or `WORMEDIT_GATE_JOBS`) overrides it; `--whole-machine` uses `nproc` and is only for runs the owner has said may use the whole computer. Use the same count for the build and for `ctest -j`, and print the chosen count and why in the summary.
  3. Define GATE in `tasks.md` and point `AGENTS.md` at the script.
  4. Prove the listing-error detection: temporarily add a failing static initializer in a scratch test, run the script, see it fail, then remove it.
- **Invariants:** No test, build flag or source change. The script only reads the tree and writes logs outside it. By default it never uses more than 8 jobs.
- **Reject/failure behavior:** Any stage failure fails the whole gate with that stage named; the script never reports success after a stage it could not run.
- **Verify:** Run `scripts/gate.sh` on a clean tree (passes) and on the scratch listing failure from step 4 (fails, naming the stage); check the printed job count on this machine for the default, `--agents 4` and `--whole-machine`; `ctest --test-dir build -R '^documentation\.'`.
- **Out of scope:** Xvfb captures (they need a display and stay manual); the complexity baseline itself (T-666); CI configuration.
- **Done:** `scripts/gate.sh [build-dir] [--agents N] [--jobs N] [--whole-machine] [--print-jobs]` builds, runs every CTest test once (the event-harness and documentation labels are part of the full run, so they are not run twice), runs `git diff --check HEAD`, and prints one summary with log paths. It fails when ctest's output contains "Error listing tests", "Not Run" or "Subprocess aborted", even if no test reports failure. Job count defaults to `min(8, nproc / agents)`, at least 1 (8 here); `--print-jobs` showed 8 by default, 4 with `--agents 4`, 1 with `--agents 32`, 16 with `--whole-machine`, 3 with `--jobs 3`, and rejected `--agents 0`. GATE is defined in `docs/chart-editor-plan/tasks.md` rule 4 and in `AGENTS.md`, which also states the job-count rule. Not yet included: the complexity comparison, which waits for T-666. Evidence: clean tree passed (4,038/4,038); with a scratch aborting static initializer in `tests/sys/player_preview_status_tests.cpp`, ctest stopped at "Error listing tests … Subprocess aborted" without printing a pass line, and the gate failed naming the ctest stage; scratch reverted.

## T-666 Check "no new or raised complexity" against a checked-in baseline

- **Status:** Done (2026-10-18)
- **Value:** Medium-high. Every card requires "no new or increased clang-tidy complexity findings", and today the comparison is by memory. T-657 pushed three functions from at or below 25 to 26–28, which I noticed only because I happened to run the check; a function already over the limit getting worse would be easy to miss.
- **Outcome:** `cmake/complexity_baseline.txt` lists every function currently above 25 with its score. A script runs clang-tidy's cognitive-complexity check on given (or changed) `.cpp` files and fails on a new function above 25 or an increase over the baseline; it prints decreases so the baseline can be lowered in the same commit, as the ceremony ratchet does.
- **Read first:** `AGENTS.md` (C++ function complexity), `cmake/ceremony_ratchet.txt` and `cmake/check_ceremony_ratchet.cmake` (the ratchet convention), `docs/chart-editor-plan/decisions/code-style.md` ("Ratchets").
- **Files owned:** `cmake/complexity_baseline.txt` (new), `scripts/check-complexity.py` (new), `AGENTS.md`, this card; coordinator-approved supporting paths (2026-10-18): `scripts/gate.sh` and `docs/chart-editor-plan/tasks.md` integrate and explain the existing gate's planned T-666 stage; new `tests/scripts/check_complexity_tests.py` verifies attribution and comparison failure behavior; `tests/CMakeLists.txt` registers those checks with CTest. Baseline measurement also includes compiled test translation units so existing Catch warnings are measured rather than silently ignored. No application C++ changes; source probes are temporary and reverted.
- **Steps:**
  1. Stale-plan check: `ls cmake scripts | grep -i complex`.
  2. Generate the baseline from a full run over `src/` with the Threshold 25 configuration; key entries by file and function name, not line.
  3. Write the comparison script (changed files from `git diff --name-only` by default) and document it in `AGENTS.md`.
  4. Prove it: raise a scratch function's complexity, see the failure, revert.
- **Invariants:** No source change; the baseline matches today's findings exactly.
- **Reject/failure behavior:** A function the script cannot attribute (renamed or moved) is reported as new rather than skipped.
- **Verify:** Run the script on a clean tree (passes) and on the step 4 change (fails); `ctest --test-dir build -R '^documentation\.'`.
- **Implementation (2026-10-18):** Measured 585 compiled C++ source files across `src/` and `tests/`, including compilation-database variants and owned headers, at `feb3b83` with clang-tidy 23.1.1 / threshold 25. The JSON-lines baseline has 779 measured allowances (6 in production paths, 773 in test paths), not claims about test value. Win32 codepage and optional Lua test sources are not compiled in this Linux app build and have no inferred allowance. Function keys use Clang USRs; Catch keys use macro literals rather than counters; callbacks use a unique named-variable/call context. Ambiguous contexts reject even when only one sibling is over the warning threshold. PyYAML and libclang handle diagnostics and C++ structure; no C++ parser is hand-written.
- **Behavior:** Changed tracked/untracked `.cpp` files are checked by default, explicit inputs and `--all` are supported, and `--base-ref` / GATE's `--complexity-base` select committed changes. Missing inputs/compile commands, compiler failures, version drift and ambiguous attribution fail. Header edits require explicit consumers or `--all`; dependency inference is not claimed. Decreases are printed for review; complete candidate output cannot overwrite the tracked baseline or accept increases.
- **Verification (2026-10-18):** Full `scripts/check-complexity.py --all --jobs 8` passes all 585 inputs (`/tmp/t666-full-check.log`). Temporary source probes reject a new 26-point function, an increase 26→27, a generated macro function, ambiguous callbacks and a compiler error. A real Catch insertion changes generated counters and line positions while the existing findings still match; original source bytes were restored. The stronger callback uniqueness guard passes all existing over-limit callback owners (`/tmp/t666-lambda-check.log`) and its below-threshold-sibling regression. `python3 tests/scripts/check_complexity_tests.py` passes 13 tests, including three real-Clang identity cases; focused Clang-build template checking passes (`/tmp/t666-clang-check.log`).
- **Gate evidence:** GCC `scripts/gate.sh --jobs 4 --complexity-base HEAD^` passes **4,123/4,123**, including eight real changed-file complexity inputs (`/tmp/wormedit-gate.SFYpY1`). Linux-clang GATE also passes **4,123/4,123** (`/tmp/wormedit-gate.1ncDeZ`). Final post-edit documentation/tooling and whitespace checks are recorded in the commit body. No existing C++ violation was changed.
- **Out of scope:** Fixing existing violations; other clang-tidy checks.

## T-667 Test the measure editor and row moves with random edit sequences

- **Status:** deferred (proposed 2026-10-17; start only if the owner names T-667)
- **Value:** Medium. `measure_editor` and `time_shift` share one strong invariant: a keep-rows edit leaves every object's absolute row unchanged and the document valid. Example tests cover only the cases their author thought of; random sequences find the rest cheaply.
- **Outcome:** A seeded property test applies random sequences of `add_bar_lines`, `remove_bar_lines`, `move_bar_lines` and `stage_shift_selection` (both bar-line time policies) to small generated charts, and checks after each step: `validate_document` is clean; under keep-song-time every non-bar-line object keeps its absolute row; a rejected edit leaves the chart unchanged. A failure prints its seed and step list.
- **Read first:** `include/wormedit/core/measure_editor.h`, `include/wormedit/core/time_shift.h`, `tests/core/measure_editor_tests.cpp`, `tests/core/time_shift_tests.cpp`, `tests/slow_tests.cmake`.
- **Files owned:** `tests/core/measure_geometry_property_tests.cpp` (new), `tests/CMakeLists.txt`, `tests/slow_tests.cmake`, this card.
- **Steps:**
  1. Stale-plan check: `grep -rln "seed" tests/core | head`.
  2. Generator: 1–8 measures of mixed lengths, notes and holds across bar lines, a few timing events and a lane layout change; fixed seeds in the normal suite, a larger run labelled slow.
  3. Write the invariants; fix any real bug found in a separate commit with its own regression test, and record it here.
- **Invariants:** Deterministic for a seed; the normal-suite part stays under a second.
- **Reject/failure behavior:** A failing seed becomes an ordinary example test once understood.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`; the slow label once.
- **Out of scope:** Propagation correctness beyond "valid and deterministic" (its expected rows need their own examples); other editors.

## T-668 Let the documentation check catch stale references and statuses

- **Status:** deferred (proposed 2026-10-17; start only if the owner names T-668)
- **Value:** Medium. Cards cite `file.cpp:1258` lines that drift, the task index's free-ID line had gone stale (it said "above T-628" while T-657 existed), and status values mix forms ("ready", "Ready (owner approved…)", "done (date)"), so nothing can list what is ready.
- **Outcome:** `documentation.chart_editor_state` also fails on a backticked repository path in a card that does not exist, warns on `path:line` citations, checks that every Status begins with one word from a fixed set (`Proposed`, `Ready`, `In progress`, `Partial`, `Blocked`, `Done`), and checks the free-ID line against the highest card number. A small script lists Ready cards with their "after" constraints.
- **Read first:** `cmake/check_chart_editor_docs.cmake`, `docs/chart-editor-plan/tasks.md`.
- **Files owned:** `cmake/check_chart_editor_docs.cmake`, `docs/chart-editor-plan/tasks.md`, `scripts/list-ready-cards.py` (new), and the card files whose Status lines need the leading word normalized (wording after the first word unchanged), this card.
- **Steps:**
  1. Stale-plan check: read the current checker's card-field section.
  2. Normalize existing Status lines to the fixed leading words, changing only case and the first word.
  3. Add the path-existence check (skipping paths a card marks as new) and the `:line` warning.
  4. Add the free-ID check and the ready-card script.
- **Invariants:** No card content changes beyond the Status leading word.
- **Reject/failure behavior:** A path that legitimately does not exist yet is written with "(new)" after it; the checker skips those.
- **Verify:** `ctest --test-dir build -R '^documentation\.' --output-on-failure`; introduce a broken path in a scratch card and see the failure.
- **Out of scope:** Rewriting `resume.md`; changing required card fields.

## T-669 Reach zero compiler warnings, then make them errors on our targets

- **Status:** Done (2026-10-17; owner asked for it directly)
- **Value:** High for its cost. When an enum gains a value, `-Wswitch` is what points at every switch that must handle it. The current build already prints warnings (unused variables at `src/sys/application.cpp:3048`, `src/sys/compose/actions/compose_actions.cpp:3646`, `src/sys/compose/input/compose_input.cpp:1246`, and missing-field-initializer warnings), so a new one scrolls past unseen.
- **Outcome:** Wormedit's own targets build with zero warnings under GCC and the linux-clang preset, and `-Werror` (`/WX` for MSVC) is on for those targets only, never for dependencies.
- **Read first:** `CMakeLists.txt` (the `target_compile_options` lines), `CMakePresets.json`, `AGENTS.md`.
- **Files owned:** `CMakeLists.txt`, and every source file with a warning, recorded on this card after step 1's inventory, this card.
- **Steps:**
  1. Stale-plan check: `grep -n "Werror" CMakeLists.txt`. Inventory: a full clean build of `build/` and the linux-clang preset, saving every warning by kind and file.
  2. Fix warnings one kind per commit. Behavior-neutral only: an unused variable that hides a real bug goes to the owner instead.
  3. Turn on `-Werror` for the project's targets.
- **Invariants:** No behavior change; the test suite passes after each commit.
- **Reject/failure behavior:** A warning that cannot be fixed neutrally is suppressed locally with a comment naming why, never by a global flag.
- **Verify:** Clean builds of both configurations with zero warnings; `ctest --test-dir build --output-on-failure`.
- **Out of scope:** Warnings in dependencies; adding new warning flags beyond `-Wall -Wextra -Wpedantic`; the Windows cross build unless its toolchain is available.
- **Inventory (step 1):** clean builds printed 1,819 GCC 16 and 860 clang 23 warnings. In our files, 1,770 GCC lines (786 source lines) were `-Wmissing-field-initializers`; clang splits these into 781 `-Wmissing-designated-field-initializers` (designated initializers leaving members to their defaults, the project idiom) and 51 positional braces that stop early. Our files also had 15 `-Wdangling-reference` (GCC false positives on reference-returning JSON helpers), 9 `-Wpedantic` on `__int128`, 5 unused variables, 5 unused parameters, 2 sign-compare, 2 range-loop copies, 1 redundant move, 1 `-Wclass-memaccess`, and 1 ignored `[[nodiscard]]` in a test. The rest were in dependencies (SDL, libotoworm, stb_vorbis, `<ciso646>`).
- **Owner decision (2026-10-17):** designated initializers that rely on default member initializers are allowed, an exception to this card's "never a global flag" rule. Clang turns off only `-Wmissing-designated-field-initializers`; GCC, which has no separate switch, turns off `-Wmissing-field-initializers`. Recorded in `code-style.md` ("Name the members you set").
- **Done:** `cmake/warnings.cmake` defines `wormedit_enable_warnings(target)`, used by all six targets that had their own flag block (`wormedit`, `wormedit_core`, `wormedit-convert`, `wormedit_visual_assets`, `wormedit_visual_decoder`, `wormedit_video_decoder`). It applies `-Wall -Wextra -Wpedantic` (or `/W4 /permissive-`), the designated exemption, and `-Werror`/`/WX` under `WORMEDIT_WARNINGS_AS_ERRORS` (default ON; existing build trees that cached it OFF today were reset). Fixes, all behavior-neutral: the 51 positional initializers became designated ones naming the members they already set, so omitted members keep the same initialization; the five unused variables were leftover aliases or revisions already checked elsewhere (the BGA move's `is_timing_input_current`), so none hid a bug; six pass-through parameters were dropped (`post_audio_offset_feedback`, `clear_audio_offset_drag`, `post_audio_offset_diagnostic`, `import_bga_feedback`, `import_keysound_feedback`, `build_reused_scalar_scene_view`) and one interface parameter left unnamed; the BMSON JSON helpers and `extension_matches` carry `WORMEDIT_NO_DANGLING` from the new `include/wormedit/core/compiler.h` (`[[gnu::no_dangling]]` where supported); `__int128` aliases use `__extension__`; range loops bind by reference; sign compares cast explicitly (the same conversion as before); `return std::move` became `return`; `nk_sdl` lost two `{}` initializers that never ran (the struct is raw-allocated and zeroed), which makes it trivial; the recipe test now asserts the ignored result. Files: `CMakeLists.txt`, `cmake/warnings.cmake`, `include/wormedit/core/compiler.h`, `docs/chart-editor-plan/decisions/code-style.md`, `src/core/{bmson_sound_channel_editor,chart_clipboard,document,editor_scene,interaction,interaction_gesture_lifecycle,lane_migration,note_move,timing}.cpp`, `src/formats/{bms,bmson,osu_mania,stepmania}.cpp`, `src/sys/{application,application_command_dispatch,audio_source_registration,compose_actions,compose_bga_actions,compose_hover_timing,compose_input,compose_resource_input,compose_selection_transform,compose_sound_actions,compose_view,notification_history_runtime,project_open,resource_job_state,waveform_analysis_runtime}.cpp`, `src/sys/{compose_input_internal,compose_view_internal,stem_waveform_window}.h`, `src/ui/nuklear/{sdl_backend,sound_import_state,sound_slot_batch}.cpp`, `tests/sys/recipe_form_tests.cpp`. Deviation: one commit instead of one per kind, because several files mix kinds; only the final state was tested. Evidence: `build/` (GCC) and `build-clang/` (clang) both build with `-Werror` on; GATE passes, 4,038/4,038; the headless configuration builds.
- **Follow-up for the owner:** GATE builds only GCC, which cannot tell positional from designated omissions, so a positional brace that stops early is caught only by a `linux-clang` build. Adding a clang build stage to `scripts/gate.sh` would close that gap (it costs a second full build).

## T-670 Split `interaction.h` by role

- **Status:** Done (2026-10-17). After the step 1 measurement the owner chose the full split (c): "we still have to maintain the code; we can pay the cost today instead of deferring it."
- **Frontend tie-in (2026-10-17):** under the relaxed boundary rule, C++ view types should reuse core enums rather than raw integers, so they need vocabulary without the editor state. Four Nuklear files already include all of `document.h` or `interaction.h` only for enums, IDs, rationals and `editor_bga_lane_index`: `src/ui/nuklear/sound_import_state.h`, `compose_object_input.cpp`, `compose_bga_panel.cpp` and `compose_canvas.cpp`. The vocabulary header is worth doing even if step 1's measurement stops the wider split.
- **Value:** Uncertain until step 1 for the full split; the vocabulary header alone is clearly useful. `include/wormedit/core/interaction.h` holds about 780 declarations with 46 includers, so most core header edits rebuild most of the tree and the header is hard to navigate. T-655 made it a little worse by including `time_shift.h` from it. The split pays off only if most includers need a small part.
- **Outcome:** Separate headers for interaction types (selection objects, scope, state), actions, and queries/views, with `interaction.h` kept as an umbrella during migration; includers move to the narrowest header they need, and the umbrella's includer count drops. Among them, a light **vocabulary header** (enums such as `note_kind_t`, `visual_event_kind_t`, `editor_selection_object_kind_t`, strong IDs, and small pure queries such as `editor_bga_lane_index`) that the frontend and C++ boundary types may include under the "Frontend boundary" rule in `code-style.md`, without pulling in documents or editor state.
- **Read first:** `include/wormedit/core/interaction.h`, `docs/architecture.md`, `docs/chart-editor-plan/decisions/code-style.md`.
- **Files owned:** `include/wormedit/core/interaction.h`, `include/wormedit/core/document.h` (moving vocabulary out only), new `include/wormedit/core/interaction_*.h` headers and the vocabulary header (new), the frontend files named above, and the includers changed in step 3, recorded on this card, this card.
- **Steps:**
  1. Stale-plan check, then measure: for each includer, which declaration groups it uses; time a rebuild after touching `interaction.h`. Count frontend includers (`src/ui`) separately. **STOP** and report to the owner if fewer than half the includers would avoid a group; in that case still do step 1a.
  1a. Extract the vocabulary header (from `interaction.h` and `document.h`), keeping both as includers of it so nothing else changes; point the frontend files listed above at it.
  2. Move declarations into role headers without changing them; the umbrella includes all of them.
  3. Point includers at the narrowest headers; re-time the rebuild and record both times here.
- **Invariants:** No declaration changes; identical behavior and tests.
- **Reject/failure behavior:** A cyclic dependency between the new headers stops the split at that boundary; keep those declarations together.
- **Verify:** `cmake --build build --parallel <jobs>` (chosen as in T-665; use the same job count for both timings); `ctest --test-dir build --output-on-failure`; recorded before and after rebuild times.
- **Out of scope:** Changing any type or action; T-621's folder moves.
- **Measured (step 1):** 46 direct includers. A real declaration change to `interaction.h` recompiles 283 objects in `build/` in 4 min 24 s at 8 jobs (touching the file alone takes 9 s because ccache hits, so time with a real change). The declarations fall into vocabulary, actions, gesture captures, `editor_interaction_state_t`, cursor helpers, hit-test queries and dispatch functions, but they are not independent: the state holds the gesture captures, and those take action types, so anyone using the state needs vocabulary, actions, gestures and state together. Counting that, the direct includers that could skip a group: queries 39/46, dispatch 33/46, cursor 25/46, but actions/gestures/state only 11/46. In objects, edits to the state chain would still rebuild 626 of the 638 dependents, the cursor helpers 576, the hit-test queries 318 and the dispatch functions 88. So the card's stop test passes by count, but the edits that matter most (new actions, gesture and state fields) would rebuild 98% of the dependents either way, and the full split would buy navigation, not build time. **Owner choice:** (a) stop here; (b) a narrow split that moves only the hit-test and query declarations into their own header, which halves rebuilds for edits there; (c) the full role split for navigation.
- **Done (step 1a):** `include/wormedit/core/vocabulary.h` holds `signed_rational_t`, `note_kind_t`, `visual_event_kind_t`, `sound_kind_t`, `bar_line_time_t`, `editor_selection_object_kind_t`, `editor_bga_lane_mask_all` and `editor_bga_lane_index`, moved unchanged from `document.h`, `time_shift.h` and `interaction.h`, which now include it, plus the strong IDs through `id.h`. `compose_bga_panel.cpp`, `compose_canvas.cpp`, `compose_object_input.cpp` and `sound_import_state.h` include it instead of `document.h`/`interaction.h` (`sound_import_state.h` still gets `document.h` through `sound_import.h`). Supporting file: `include/wormedit/core/time_shift.h` (moving `bar_line_time_t`, which T-672 needed). Evidence: GATE passes, 4,038/4,038.
- **Done (steps 2–3):** `interaction.h` became seven role headers, moved line for line (a check confirmed every non-blank line survived; the only one dropped was an orphan comment with no declaration after it). Each includes the one before it: `interaction_types.h` (gesture kinds, viewport limits, selection scope and objects, area geometry, paint cells, scroll direction, row span) → `interaction_actions.h` (action structs and `editor_interaction_action_t`) → `interaction_gestures.h` (pending captures, previews, `active_gesture_t` helpers) → `interaction_state.h` (state, view model, setup, result, measure geometry). Three leaf headers sit on the state: `interaction_cursor_pose.h` (cursor and viewport placement; named so it does not collide with `src/core/interaction_cursor.h`), `interaction_queries.h` (inspection, position mapping, hit testing, hover, area queries) and `interaction_dispatch.h` (dispatch, note-kind arming, selection operations, pruning). Each header compiles on its own and includes only the standard headers it uses. The 44 direct includers now name the narrowest headers they need; 24 more files that had used cursor, dispatch, query or `time_shift.h` declarations only through the umbrella now include them. `resource_assignment.h` now includes `command.h` directly. Once nothing included the umbrella, it was deleted rather than kept, so new code cannot pull in everything by habit; `docs/cursor-movement.md` and the `cmake/ceremony_ratchet.txt` comment now name `interaction_cursor_pose.h` (the ratchet count is unchanged). **Re-timed** at 8 jobs with a real declaration change: state chain (and therefore actions, gestures, types) 223 compiles in 3 min 48 s; queries 150 in 3 min 15 s; cursor pose 163 and dispatch 81 objects by dependency count; before, 283 compiles in 4 min 24 s for any change. Time falls less than object count because the heaviest translation units (`compose_view.cpp`, the large test files) need the queries and dispatch headers anyway.

## T-671 Share test helpers and put new tests by feature

- **Status:** partial (bounded pointer-helper checkpoint done 2026-10-18; broader fixture/move work remains)
- **Done so far:** `wormedit::test::mouse_button` and `mouse_motion` now own construction for the four scoped files; identical vector-returning `click_at` scripts in the main and inspector tests share the harness implementation. Differently named local symbols were renamed with `clang-refactor local-rename` before removing their definitions. Motion ordering, zero window IDs, click counts and explicit NONE modifiers are preserved.
- **Remaining:** Audit/consolidate other pointer and keyboard families in bounded checkpoints, extract the sys fixture and move existing measure-lane tests (steps 3–4). This checkpoint does not claim project-wide helper consolidation.
- **Value:** Medium, cheap. Event-harness helpers are copied across test files under different names (`button_event` and `mouse_button`, `motion_event` and `mouse_motion`, `click_at` and `click`). Test files are named by history rather than feature: T-657's sys tests landed in `compose_action_notification_tests.cpp` and `frame_timing_tests.cpp` only because those files had the fixtures and includes, and each of these files holds 100–250 test cases.
- **Outcome:** One set of pointer and keyboard helpers in `tests/ui/nuklear_event_harness.h`, and one sys dispatch fixture in `tests/support/` (or the existing `tests/sys/*_fixture.h`), used by every test file. New feature tests go in a file named for the feature; the testing doc says so. Existing tests move only where a file holds a clearly separate feature, such as the measure-lane cases.
- **Read first:** `tests/ui/nuklear_event_harness.h`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/bga_lane_event_harness_tests.cpp`, `tests/sys/application_dispatch_fixture.h`, `docs/chart-editor-plan/testing.md`, T-644.
- **Files owned:** `tests/ui/nuklear_event_harness.h`, `tests/ui/nuklear_event_harness.cpp`, `tests/support/` (new fixture headers), `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/bga_lane_event_harness_tests.cpp`, `tests/ui/inspector_resource_workflow_tests.cpp`, `tests/ui/compose_hover_timing_event_harness_tests.cpp` (exact pointer-helper checkpoint), `tests/CMakeLists.txt`, `docs/chart-editor-plan/testing.md`, this card.
- **Steps:**
  1. Stale-plan check and inventory: `grep -rn "auto button_event\|auto mouse_button\|auto motion_event\|auto mouse_motion\|auto click_at\|auto click(" tests`.
  2. Move one helper family into the harness header per commit, deleting local copies.
  3. Remaining after this bounded checkpoint: extract the sys dispatch fixture used by the BGA and measure-lane input tests.
  4. Remaining after this bounded checkpoint: move the measure-lane sys and harness cases into `tests/sys/measure_lane_tests.cpp` and `tests/ui/measure_lane_event_harness_tests.cpp` as the example; document the rule.
- **Execution scope (2026-10-18):** Share mouse-button/motion construction across these four files and the identical two-frame vector click script in the main/inspector files. Preserve click counts, per-frame motion and modifiers. BGA's fixed-array click driver remains distinct because typed consumers use its array result; wheel/keyboard variants and other test files remain later checkpoints. Use AST-aware renaming before removing differently named duplicates. One pointer-helper family per commit.
- **Invariants:** Same assertions and the same number of test cases; no production change.
- **Reject/failure behavior:** Helpers that differ in behavior (for example click counts or timing) stay distinct under different names, with the difference recorded.
- **Verify:** `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; compare case counts before and after.
- **Out of scope:** Deleting tests (T-644); finding widgets by tooltip (T-662).

- **Verification (2026-10-18):** Same final GCC/Clang GATE and event-harness results as T-662. T-644 first changed **4,126 → 4,125** total cases and **529 → 528** event-harness cases by its documented merge/removal. This helper extraction removes no cases or assertions; T-662 then adds exactly three lookup contract cases, giving **4,128** total / **531** event-harness cases. Existing field geometry, wheel/keyboard variants and BGA fixed-array consumers retain their policies. No test moves or sys-fixture changes were included. Documentation now directs new cases to feature-named files and shared pointer helpers.

## T-672 Convert the measure-lane view fields to C++ types as the worked example

- **Status:** Done (2026-10-17; owner asked for it directly)
- **Value:** Medium. The "Frontend boundary" rule says new and reworked surfaces use C++ types, but nobody has done one yet; without an example, the first person to follow the rule designs the pattern in the middle of other work. These fields are recent, contained and read only by measure-lane code.
- **Outcome:** The measure-lane data the frontend reads (`measure_lane_visible`, `bar_line_time_propagates`, `selected_bar_lines`/`selected_bar_line_count`, and the bar-line identity now carried as `measure_id` on `WormUiComposeGridLineView`) comes from a C++ view type, using core vocabulary (`measure_id_t`, `bar_line_time_t`) instead of raw integers. The card records the pattern: where the type lives, how sys builds it, how Nuklear receives it each frame, and what it removed.
- **Read first:** `docs/chart-editor-plan/decisions/code-style.md` ("Frontend boundary"); T-657's Done note in [measure-lane.md](measure-lane.md); `src/ui/frontend/compose.h`; `src/sys/compose/views/compose_view.cpp` (where the measure-lane fields are set); `src/sys/project/workspace_view.h` (`selected_bar_line` frame storage); `src/sys/compose/views/compose_scene_view.cpp` (`grid_line_measure_id`); `src/ui/compose_timing_geometry.h` (`compose_bar_line_bounds`, `is_compose_bar_line_selected`); `src/ui/nuklear/compose_canvas.cpp` (static scene cache).
- **Files owned:** `src/ui/frontend/compose.h`, a new C++ boundary header under `src/ui/frontend/`, `src/sys/compose/views/compose_view.cpp`, `src/sys/project/workspace_view.h`, `src/sys/compose/views/compose_scene_view.cpp`, `src/ui/compose_timing_geometry.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/compose_object_input.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/sys/frame_timing_tests.cpp`, `docs/chart-editor-plan/decisions/code-style.md` (the pattern, once proven), this card.
- **Steps:**
  1. Stale-plan check: `grep -rn "measure_lane_visible" src/ui/frontend`.
  2. Inventory, and record the answers here before any edit: how `WormUiComposeView` reaches Nuklear each frame and who owns its storage; whether the static scene cache (`static_scene`) copies views and would need the new type; how the event harness builds a view; whether the Windows/MSVC build depends on C layout anywhere on this path. **STOP** and report to the owner if any answer makes the conversion larger than these files.
  3. Add the C++ view type with core vocabulary and point the measure-lane readers at it; remove the C fields and the `selected_bar_line` frame storage.
  4. Write the pattern into `code-style.md` "Frontend boundary" as a short example, and record on this card what the conversion removed (fields, storage, conversions).
- **Invariants:** Same measure-lane behavior, drawing, hit testing and area selection; all existing measure-lane tests pass unchanged in what they assert; no per-frame allocation added.
- **Reject/failure behavior:** If the frontend cannot hold a C++ view without restructuring frame ownership, stop at step 2 with the findings; the rule stays, and this card becomes the design question for the owner.
- **Verify:** `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `clang-tidy -p build` on each touched `.cpp`; isolated Xvfb capture of the lane shown with a selected bar line.
- **Out of scope:** Converting any other surface; the rectangle-function work (T-660); the vocabulary header (T-670), though this card may use it if it exists.
- **Inventory (step 2):** `WormUiComposeView` is a member of `WormUiWorkspaceView`, which `frontend_workspace_view_t` (sys) owns. It is rebuilt every frame and passed to Nuklear by pointer; the side vectors that keep pointer-plus-count arrays alive live beside it in the same object. The static scene cache keeps a second, retained `frontend_workspace_view_t` and points `static_scene` at its view; the measure lane is drawn from the current view every frame, outside the cached pass, so the cache needs nothing new. The event harness builds a `WormUiWorkspaceView` value and sets fields directly. No `ui_codec` specialization, `memcpy`, `offsetof` or C compilation touches this view, so neither MSVC nor any other build depends on its layout. Grid lines live in trivially-copyable frame spans, and a strong ID keeps them trivially copyable. Nothing here required a STOP.
- **Done:** `wormedit::ui::compose_measure_lane_view_t` (`src/ui/frontend/compose_measure_lane.h`) is the `measure_lane` member of `WormUiComposeView`: `visible`, `bar_line_time` (`bar_line_time_t`) and an owned `std::vector<measure_id_t> selected_bar_lines`, with `is_selected(measure_id_t)` on the type. `prepare_compose_projection_state` in `compose_view.cpp` refills it in place; Nuklear and `compose_timing_geometry.h` read `view.measure_lane`. `WormUiComposeGridLineView::measure_id` is now `measure_id_t` (`0` became an empty ID), and the bar-line select input still carries `measure_id.value` because the action payload in `actions.h` was out of scope. Removed: four C fields (`measure_lane_visible`, `bar_line_time_propagates`, `selected_bar_lines`, `selected_bar_line_count`), the `selected_bar_line` frame storage in `workspace_view.h`, the enum-to-bool conversion of the bar-line time policy, and the free `is_compose_bar_line_selected`. Cost: `WormUiComposeView` is no longer trivially copyable. It is copied nowhere per frame, and the vector allocates as often as the storage it replaced (the frame object is new each frame). The pattern is in `code-style.md`, "Frontend boundary". Evidence: GATE passes, 4,038/4,038; clang-tidy on the touched `.cpp` files reports only existing findings in untouched functions; isolated Xvfb on the template chart shows the lane toggle and the bar-line time button, a Draw-placed M2 handle drawn thin blue-gray after Escape and gold once clicked.
