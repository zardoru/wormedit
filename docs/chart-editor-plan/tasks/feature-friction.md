# Feature friction: refactors found by the measure lane

[Back to task cards](../tasks.md).

Building the measure lane ([T-654–T-657](measure-lane.md)) crossed core, sys and UI. T-657's card listed 5 files and its commit touched 22; T-656 listed 3 and touched 8. Most of the gap was finding the places to change. The [retrospective](../../../memory/coordination/oct17-measure-lane-retrospective.md) records each observation; these cards are its proposed fixes that no open card covers. Proposed fixes that fit an open card are recorded on that card instead: T-643, T-644, T-646 and T-653 in [state ownership](state-ownership.md).

All of these are **proposed** (2026-10-17) and wait for owner approval; none is scheduled. Each card says plainly how much it is worth. Two constraints apply throughout:
- The [Nuklear audit](../../audits/nuklear-2026-10-16.md) rejects stored callbacks, virtual dispatch and table-driven type erasure for canvas rendering. Sharing here means plain named functions over the view.
- D3 keeps the Compose edit toggles resetting per project, and D6 forbids splitting Nuklear UI state lifetimes. No card changes a lifetime.

## T-658 Write down the names and paths a cross-layer feature has to find

- **Status:** Proposed (2026-10-17; awaiting owner approval)
- **Value:** High for little cost: it is documentation only, and it removes most of the search time the measure lane spent.
- **Outcome:** `docs/architecture.md` has a glossary mapping each concept to its name per layer, and "Where to look" rows for area selection and paste; `docs/frontend-event-harness.md` describes the pointer press → candidate → release → click sequence; `AGENTS.md` says how to build in parallel; `docs/chart-editor-plan/tasks.md` asks card authors to map the nearest existing feature before listing **Files owned**.
- **Read first:** `docs/architecture.md`, `docs/frontend-event-harness.md`, `AGENTS.md`, the retrospective's observations 1, 3, 4, 7 and 15.
- **Files owned:** `docs/architecture.md`, `docs/frontend-event-harness.md`, `AGENTS.md`, `docs/chart-editor-plan/tasks.md`, `docs/chart-editor-plan/decisions/code-style.md`, this card.
- **Steps:**
  1. Stale-plan check: `grep -n "Glossary" docs/architecture.md`.
  2. Glossary, at least: bar line / measure line (`editor_grid_line_kind_t::measure`) / measure rail (`compose_measure_rail_width`) / measure labels (BMS) / split and merge (`measure_editor.h`); selection object / hit target / area geometry; proposal / report / ghost preview. Each entry names the type or file per layer.
  3. "Where to look" rows: area selection (projected rectangles from `cache_area_object_geometry` in `src/sys/compose_view.cpp` when a snapshot exists, otherwise the row-range index in `src/core/interaction_selection.cpp`; scope flags filter both); paste (proposal in `src/core/chart_clipboard.cpp`, ghost geometry in `paste_geometry`).
  4. Pointer sequence in `docs/frontend-event-harness.md`: a press starts an area-selection candidate unless `compose_pointer_start_hits_element` claims it; Nuklear's `pointer_clicked` is the *release*; the order in `process_compose_pointer_actions` decides which handler owns a click. Check each claim against the code before writing it.
  5. `AGENTS.md`: build with `cmake --build build --parallel "$(nproc)"` (`build/` uses the Makefile generator, which is serial by default).
  6. `tasks.md` rules: before writing **Files owned**, grep how the nearest existing feature is wired (for a lane: the BGA lane and timing markers) and list those files.
  7. `code-style.md`: a frontend view of an object the user can select or edit carries the object's ID; display numbers are not keys (see the proposed addition on T-653).
- **Invariants:** No code change. Every symbol named in the docs exists (grep each).
- **Reject/failure behavior:** A claim the code contradicts is corrected or dropped, not written.
- **Verify:** `ctest --test-dir build -R '^documentation\.' --output-on-failure`; grep every symbol the new text names.
- **Out of scope:** Renaming any symbol to make the names agree; switching `build/` to Ninja (an owner choice: it discards the warm build).

## T-659 Give the paste's chart changes one owner

- **Status:** Proposed (2026-10-17; awaiting owner approval)
- **Value:** Modest. It prevents one class of silent preview drift. Today only bar-line splits are affected, so it is worth doing only if paste gains more geometry effects.
- **Outcome:** One member of `chart_paste_report_t` (for example `apply_geometry(chart_document_t&) const -> diagnostic_list_t`) performs every structural change a paste makes. The proposal, `chart_paste_proposal_t::apply` and the sys ghost preview (`paste_geometry` in `src/sys/compose_view.cpp`) all call it instead of each calling `add_bar_lines`.
- **Read first:** `include/wormedit/core/chart_clipboard.h`, `src/core/chart_clipboard.cpp` (`add_paste_bar_lines`, `apply`), `src/sys/compose_view.cpp` (`paste_geometry`), T-656's Done note in [measure-lane.md](measure-lane.md).
- **Files owned:** `include/wormedit/core/chart_clipboard.h`, `src/core/chart_clipboard.cpp`, `src/sys/compose_view.cpp`, `tests/core/chart_clipboard_tests.cpp`, this card.
- **Steps:**
  1. Stale-plan check: `grep -n "add_bar_lines" src/core/chart_clipboard.cpp src/sys/compose_view.cpp` (expect three call sites).
  2. Add the member and route the three call sites through it.
  3. Add a test that a pasted bar line and an object in its new measure produce the same measure IDs in the proposal, the committed chart and the ghost geometry.
- **Invariants:** Same pasted documents, IDs, diagnostics and one Undo step. Older clipboard payloads paste unchanged.
- **Reject/failure behavior:** A split failure rejects the paste as today; the ghost keeps showing the rejected candidate's objects.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`; `ctest --test-dir build -L event-harness`; `clang-tidy -p build src/core/chart_clipboard.cpp`.
- **Out of scope:** Changing what paste does; the ID-allocation contract (recorded on T-643).

## T-660 Put canvas object geometry and hit testing in one header

- **Status:** Proposed (2026-10-17; awaiting owner approval; runs after T-645, which edits the same press path)
- **Value:** The highest of these cards. The next selectable lane becomes one bounds function, one line in the hit query, one draw pass and one dispatch case, instead of edits in about ten files. It is also the largest and riskiest: input routing order is a contract.
- **Outcome:** `src/ui/compose_canvas_geometry.h` (renamed from, or including, `compose_timing_geometry.h`) holds one `compose_<kind>_bounds(view, item)` per non-note canvas object kind (timing markers and bar lines already exist; BGA events, BGM events and bookmarks move in) and one query `compose_canvas_object_at(view, x, y) -> std::optional<editor_selection_object_t>`, written as a fixed sequence of named calls. `compose_pointer_start_hits_element`, the click handlers and hover read that query; sys `append_area_*_geometry` builds rectangles from the same bounds functions. Rail coordinates are named constants.
- **Read first:** `src/ui/compose_timing_geometry.h`; `src/ui/nuklear/compose_input_panel.cpp` (`compose_pointer_start_hits_element`, `process_compose_pointer_actions`); `src/ui/nuklear/compose_input_internal.h` (`compose_bgm_event_at`, `compose_bga_event_at`); `src/ui/nuklear/compose_bookmark_rename.cpp` (`compose_bookmark_at`); `src/ui/nuklear/compose_canvas.cpp` (`compose_timing_marker_at`); `src/sys/compose_view.cpp` (`append_area_*_geometry`); the Nuklear audit's "rejected over-unification" section; T-645.
- **Files owned:** `src/ui/compose_timing_geometry.h` (or its renamed successor), `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_input_internal.h`, `src/ui/nuklear/compose_object_input.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_bookmark_rename.cpp`, `src/ui/nuklear/compose_viewport_input.cpp`, `src/ui/nuklear/compose_area_menu.cpp`, `src/sys/compose_view.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/bga_lane_event_harness_tests.cpp`, `tests/ui/markers_event_harness_tests.cpp`, this card.
- **Steps:**
  1. Stale-plan check: `grep -rn "compose_canvas_object_at" src`. Then inventory every non-note hit function, its rectangle, its callers and its priority when rectangles overlap; save the table on this card.
  2. Move each bounds computation into the header without changing it; each existing `*_at` function calls its bounds function.
  3. Add `compose_canvas_object_at` with the inventoried priority order; switch `compose_pointer_start_hits_element` to it.
  4. Switch the click and hover callers one kind per checkpoint, keeping their own policies (modifiers, double-click, Draw mode).
  5. Build sys area geometry from the same bounds functions where it does not already.
  6. Replace the literal rail values (`42.0`, `22.0` in `compose_viewport_input.cpp` and `compose_area_menu.cpp`) with named constants.
- **Invariants:** Same action kinds, counts and order for every existing event-harness case; same overlap priority; notes keep their core hit index. No stored callbacks, virtual dispatch or per-frame allocation (Nuklear audit).
- **Reject/failure behavior:** Any change in which object a press or click selects stops the card and goes to the owner with a failing-before harness test.
- **Verify:** `ctest --test-dir build -L event-harness`; `ctest --test-dir build --output-on-failure`; `clang-tidy -p build` on each touched `.cpp` with no new or raised findings; isolated Xvfb capture via `scripts/smoke-compose-virtual-x.sh`.
- **Out of scope:** Note hit testing; capture continuation (T-641); hover/paint gate queries (T-645); drawing code beyond calling the shared bounds.

## T-661 Make additions to scopes and action enums fail at compile time

- **Status:** Proposed (2026-10-17; awaiting owner approval)
- **Value:** Small but cheap. Each of the two traps cost real time once: a new scope flag is silently false at positional construction sites, and an appended action enum value aborts test discovery instead of failing the build.
- **Outcome:** `editor_selection_scope_t` has named presets (at least `every_object()`), and no positional construction remains. The Compose action and input enums end with a `_COUNT` sentinel, every table sized from an enum uses it, and a `static_assert` ties each route table to it.
- **Read first:** `include/wormedit/core/interaction.h` (`editor_selection_scope_t`), `src/sys/compose_input_internal.h:1145`, `src/sys/application_command_dispatch.cpp:484`, `src/ui/frontend/actions.h`, `src/sys/compose_actions.cpp` (`compose_kind_count`, `build_compose_action_routes`), `cmake/ceremony_ratchet.txt`.
- **Files owned:** `include/wormedit/core/interaction.h`, `src/sys/compose_input_internal.h`, `src/sys/application_command_dispatch.cpp`, `src/ui/frontend/actions.h`, `src/sys/compose_actions.cpp`, `cmake/ceremony_ratchet.txt`, this card.
- **Steps:**
  1. Stale-plan check: `grep -rn "editor_selection_scope_t{true" src` and `grep -n "_COUNT" src/ui/frontend/actions.h`.
  2. Add the presets; convert both positional sites; add a ratchet row for positional construction at 0.
  3. Append the sentinels (end-of-enum, so existing values do not move); size `compose_kind_count` from it; make the route table build `constexpr` so a duplicate or out-of-range route fails to compile.
  4. Grep for other arrays sized by a named last enumerator and convert them.
- **Invariants:** No enum value changes; same routes; same selections from Select All and area selection.
- **Reject/failure behavior:** If the route table cannot be `constexpr` without restructuring the route modules, keep the runtime build and add only the `static_assert` on size; record why.
- **Verify:** `cmake --build build --parallel "$(nproc)"`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -R '^documentation\.'`.
- **Out of scope:** Re-routing actions (T-636 is done); renaming scope fields.

## T-662 Let event-harness tests find widgets by tooltip

- **Status:** Proposed (2026-10-17; awaiting owner approval; after T-644's pruning of the same test files)
- **Value:** Medium. Adding one toolbar button broke three tests that click fixed x positions; every toolbar change will keep doing that.
- **Outcome:** The harness records each icon widget's bounds and tooltip during a frame, and tests call `harness.click_widget("Hide sounds list")`. Toolbar tests no longer hard-code x positions.
- **Read first:** `tests/ui/nuklear_event_harness.h`, `docs/frontend-event-harness.md`, `src/ui/nuklear/chrome.h` (`IconButton`, `DrawnIconButton`), the testing hook `worm_ui_frontend_compose_viewport_for_testing`.
- **Files owned:** `tests/ui/nuklear_event_harness.h`, `tests/ui/nuklear_event_harness.cpp`, `src/ui/nuklear/chrome.h` (testing hook only), `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/bga_lane_event_harness_tests.cpp`, `tests/ui/inspector_resource_workflow_tests.cpp`, `docs/frontend-event-harness.md`, this card.
- **Steps:**
  1. Stale-plan check: `grep -rn "click_widget" tests/ui`.
  2. Add the recording hook, compiled only for tests or behind the existing testing hooks; no production allocation.
  3. Add `click_widget` and migrate every toolbar click by x position (`grep -rn ", 122" tests/ui`).
- **Invariants:** No production behavior or per-frame allocation change; same assertions in migrated tests.
- **Reject/failure behavior:** A tooltip that is not unique fails the lookup loudly rather than picking one.
- **Verify:** `ctest --test-dir build -L event-harness`; `ctest --test-dir build --output-on-failure`.
- **Out of scope:** Non-toolbar pixel clicks inside the field (they test geometry on purpose); T-644's pruning.

## T-663 Keep the Compose edit toggles in one struct

- **Status:** Proposed (2026-10-17; awaiting owner approval)
- **Value:** Small, and the least certain of these cards. It removes per-field mirroring and the second write in each toggle, but the benefit grows only if more toggles arrive. Skip it if no new toggle is planned.
- **Outcome:** `edit_snap_enabled`, `y_lock` and `bar_line_time` form one `editor_edit_settings_t` stored in `project.compose_view` (where D3 puts them) and copied whole into the interaction state at the existing idle-frame point. Toggles write only `compose_view`; the view reads the struct.
- **Read first:** `src/sys/compose_view.h` (`compose_view_state_t`), `src/sys/compose_view.cpp` (the `!retained_state->is_pending()` copy and `view.y_lock`), `src/sys/compose_actions.cpp` (the snap, Y-lock and bar-line time toggles), `include/wormedit/core/interaction.h`, D3 in [state ownership](state-ownership.md) (T-601).
- **Files owned:** `src/sys/compose_view.h`, `src/sys/compose_view.cpp`, `src/sys/compose_actions.cpp`, `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `src/core/note_move.cpp`, `tests/sys/compose_action_notification_tests.cpp`, this card.
- **Steps:**
  1. Stale-plan check: `grep -rn "editor_edit_settings_t" include src`.
  2. Add a test first: changing snap or Y-lock while a gesture is pending takes effect only after the gesture ends (today's behavior from the idle-frame copy).
  3. Introduce the struct; replace the three per-field copies with one; toggles write `compose_view` only.
- **Invariants:** Same reset behavior (D3), same lifetimes (D6), same timing of when a toggle reaches a pending gesture.
- **Reject/failure behavior:** If any core path needs a toggle value before the idle-frame copy, keep its current immediate write and record why.
- **Verify:** `ctest --test-dir build --output-on-failure`; `clang-tidy -p build` on each touched `.cpp`.
- **Out of scope:** Persisting these settings (D3); other `compose_view` fields.

## T-664 Give the Compose smoke check a chart with real measures

- **Status:** Proposed (2026-10-17; awaiting owner approval)
- **Value:** Small. The default smoke project has one real measure, so bar-line or measure work needs a hand-picked project and trial clicks.
- **Outcome:** `scripts/smoke-compose-virtual-x.sh` can open a checked-in multi-measure chart, and its header comment notes the field origin in the real window (x ≈ 49 at 1280×720, behind the tool column).
- **Read first:** `scripts/smoke-compose-virtual-x.sh`, `tests/fixtures/README.md`.
- **Files owned:** `scripts/smoke-compose-virtual-x.sh`, one new fixture under `tests/fixtures/`, `tests/fixtures/README.md`, this card.
- **Steps:**
  1. Stale-plan check: whether an existing fixture (for example under `tests/fixtures/osu/`) already has several measures and opens through the script's project argument; if one does, document it instead of adding a file.
  2. Otherwise add a small synthetic chart (about eight measures, a few notes, no audio) with its README row.
  3. Note the field origin and the fixture in the script's header comment.
- **Invariants:** Default script behavior unchanged; isolated Xvfb only.
- **Reject/failure behavior:** If the script cannot open the fixture's format, use a native project fixture instead.
- **Verify:** Run the script with the fixture and inspect the PNG: several measure labels and bar lines are visible.
- **Out of scope:** Scripted interaction steps beyond opening the chart.
