# Measure lane retrospective, 2026-10-17

Notes from implementing [T-654–T-657](../../docs/chart-editor-plan/tasks/measure-lane.md)
(`35c9d86`, `4fb603e`, `edd445a`, `d1289fc`). What made the change harder than
it needed to be, and what would have made the parts easier to find. These are
observations, not cards: input for a later refactoring pass, after the tasks
already in the pipeline. A real feature crossing core, sys and UI is what
exposes these pain points, so they are recorded while fresh.

The size of the problem: T-657's card owned 5 files and the commit touched 22.
T-656 owned 3 and touched 8. In both, most of the gap was locating the places
to change, not writing the change.

## Finding the code

1. **One concept, four names.** The editor calls a measure boundary a
   *bar line* (selection kind, measure editor), a *measure line* (grid,
   `editor_grid_line_kind_t::measure`), the *measure rail* (the left column,
   `compose_measure_rail_width`) and *measure labels* (BMS). The split/merge
   code says *split* and *merge*. Searching for one name never found the
   others. A short glossary in `docs/architecture.md` mapping each concept to
   its names in core, sys and UI would have saved most of the search.
2. **Projections dropped identity.** `WormUiComposeGridLineView` carried a
   display number (`measure_number`, index + 1) but not the measure ID, so the
   UI could not name the bar line it drew. I added `measure_id`. Frontend views
   of editable objects should carry the object's ID by default; a display
   number is not a key.
3. **Two area-selection paths.** Area selection uses projected rectangles from
   sys (`editor_area_geometry_snapshot_t`) when a snapshot exists and a
   row-range index otherwise. Which one applies, and that the scope flag
   matters only for the first, is visible only by reading
   `interaction_selection.cpp`. It belongs in the architecture "Where to look"
   table.
4. **Hidden consumers of a proposal.** The paste ghost preview rebuilds its
   own geometry in `paste_geometry` (`src/sys/compose_view.cpp`). Making paste
   split measures broke the ghost silently; I found it only by grepping for
   `report.objects`. Either the proposal should carry the geometry it produces,
   or a shared function should build the preview geometry from the report.
5. **Implicit determinism contract.** Paste `apply` replays the splits and
   relies on `next_measure_id` (max + 1) giving the same IDs as the proposal
   did. That holds because the stamp pins the revision, but nothing states it
   except my comment. A note on `next_measure_id`, or replaying from the
   proposal's own result, would make it explicit.

## Adding an object lane

6. **A lane is ten edits in ten files.** Making a canvas column selectable
   needed: a hit function in nuklear (like `compose_timing_marker_at` and
   `compose_bga_event_at`), an entry in `compose_pointer_start_hits_element`
   so a press doesn't start an area selection, a click handler, a new input
   kind, a sys dispatch case, an area-geometry appender, a draw pass and a
   view field. Each existing lane repeats this by hand. A lane descriptor that
   yields object rectangles from the view, used for drawing, picking,
   press-claiming and area geometry, would make the next lane one place.
   `compose_bar_line_bounds` is a small version of that idea for this lane.
7. **Pointer routing is order-dependent and undocumented.** A press may start
   an area-selection candidate in `compose_input_panel.cpp`; the click is
   handled later, on *release* (Nuklear's `pointer_clicked` means release), in
   `compose_object_input.cpp`; and the handler order in
   `process_compose_pointer_actions` decides who wins. A paragraph in
   `docs/frontend-event-harness.md` describing the press → candidate → release
   → click sequence would have saved a round of reading.
8. **Magic rail coordinates.** The rail width is `compose_measure_rail_width`,
   but `42.0` and `22.0` still appear literally in
   `compose_viewport_input.cpp` and `compose_area_menu.cpp`. I replaced the one
   in the measure-click handler; the others remain.
9. **Positional scope construction.** `editor_selection_scope_t{true, true,
   true, true}` compiles unchanged when a member is added, so a new flag is
   silently false there. Designated initializers or named presets
   (`every_object()`) would make additions show up at every construction site.

## Registration and layout traps

10. **A route table sized by the last enum value.** `compose_kind_count` is
    `WORM_UI_COMPOSE_ACTION_ADD_SELECTED_SOUNDS_TO_LAYER + 1`. Appending two
    actions compiled cleanly, then aborted on an `assert` at static
    initialization. That crashed test *discovery* (`--list-tests`), so ctest
    reported an error listing tests rather than a failing test. A `_COUNT`
    sentinel in the enum, or a `static_assert` tying the table size to it,
    would catch it at compile time.
11. **Hand-counted toolbar layout.** `layout_compose_toolbar` takes literal
    icon counts per group (`8` tools), and `dock_toggles_at_end` has a
    hand-measured width threshold. Both had to change with the buttons, in a
    different function. Deriving the counts from the same button list would
    keep them in step.
12. **Tests that click by pixel.** Three event-harness tests clicked toolbar
    buttons at fixed x positions and broke when one button was added before
    them. The harness can already find drawn text (`drawn_text_center`);
    finding buttons by tooltip or ID would make these tests survive layout
    changes.
13. **Three copies of view toggles.** Y-lock and now the bar-line time policy
    live in `application_state.project.compose_view`, are copied into the
    retained interaction state each idle frame, and are copied again into
    `WormUiComposeView`. A toggle must write two of them. One owner plus
    projection would remove that.

## Tests and tooling

14. **A deliberately invalid shared fixture.** `clipboard_song()` names a
    sound resource the song lacks and reuses one object ID across a note and a
    BGM event (on purpose, for a collision test). Any new test that goes
    through `validate_document` fails with an unrelated message. A valid base
    fixture plus a named collision variant would avoid that.
15. **Serial builds.** `build/` uses the Makefile generator, so a plain
    `cmake --build build` compiles one file at a time. One header edit
    (`chart_clipboard.h`, `measure_editor.h`) took over 20 minutes before I
    noticed; with `-j16` it is a few minutes. `interaction.h` is included in 39
    places, so most core header edits rebuild most of the tree. AGENTS.md could
    say `-j16`, or the preset could use Ninja.
16. **Visual checks need a richer default fixture.** The smoke script's
    untitled template has one real measure, so bar-line work cannot be seen
    without bringing a project. In the real window the field also starts
    behind the left tool column (x ≈ 49), unlike harness coordinates, so the
    first scripted clicks missed. A checked-in multi-measure fixture and the
    field origin printed by the script would make scripted captures
    first-try.

## Draft solutions

Now cards (proposed, awaiting approval): S1 + S11 → T-658, S3 → T-659 (its ID
contract → T-643), S4 → T-660, S5 + S6 → T-661, S8 → T-662, S9 → T-663,
S12 → T-664, all in [feature friction](../../docs/chart-editor-plan/tasks/feature-friction.md);
S2 → T-653 (inventory only; the fix changes ABI layout, which T-653 excludes),
S7 → T-646, S10 → T-644, as proposed additions in
[state ownership](../../docs/chart-editor-plan/tasks/state-ownership.md).

Checked against the open refactoring cards (T-621, T-633, T-639, T-641–T-653) and
the decisions they carry. Two constraints shape the drafts:
- The Nuklear audit (`docs/audits/nuklear-2026-10-16.md`) rejects stored callbacks,
  virtual dispatch and table-driven type erasure for canvas rendering. Shared code
  here means plain named functions over the view, not a lane framework.
- D3 keeps Y-lock, snap and similar toggles resetting per project, and D6 forbids
  splitting Nuklear UI state lifetimes. Nothing below changes a lifetime.

Coverage legend: **new** (no card covers it), **extends T-xxx** (fits an open
card's scope as an addition), **after T-xxx** (separate, but sequence behind it).

### S1. Navigation notes in the docs (observations 1, 3, 4, 7) — new, docs only

- Add a **glossary** to `docs/architecture.md`: each concept and its name per
  layer. Start with: bar line / measure line / measure rail / measure labels /
  split and merge; selection object / hit target / area geometry; proposal /
  report / ghost.
- Add "Where to look" rows for: area selection (projected rectangles from
  `cache_area_object_geometry` when a snapshot exists, else the row-range index;
  scope flags filter both), and paste (proposal in `chart_clipboard.cpp`, ghost
  geometry in `paste_geometry`).
- Add a "Pointer sequence" section to `docs/frontend-event-harness.md`: press may
  start an area candidate unless `compose_pointer_start_hits_element` claims it;
  `pointer_clicked` is the *release*; the handler order in
  `process_compose_pointer_actions` decides ownership.
- Cost: an hour. Can land any time; the documentation test checks links.

### S2. Editable objects in views carry their ID (observation 2) — extends T-653

- Rule for `code-style.md`: a frontend view of an object the user can select or
  edit carries the object's ID; display numbers are not keys.
- During T-653's inventory of ABI ↔ core mappings, list views whose items lack
  their ID and add it in the same `ui_codec<…>::from`.
- Grid lines already gained `measure_id`; the inventory is how to find the rest.

### S3. One owner for what a paste does to the chart (observations 4, 5) — new; the ID part extends T-643

- Give `chart_paste_report_t` one member, e.g. `apply_geometry(chart_document_t&)`,
  that performs the bar-line splits. The proposal, `apply`, and the sys ghost
  (`paste_geometry`) all call it instead of each calling `add_bar_lines`.
- In T-643, when measure IDs move onto the shared reducer, state the contract
  on it: allocation is a pure function of the document, which is what lets a
  revision-stamped proposal replay to the same IDs. Add a test that replays a
  paste proposal and compares measure IDs.

### S4. Canvas object geometry in one header (observations 6, 8) — after T-645

- Rename `src/ui/compose_timing_geometry.h` to `compose_canvas_geometry.h` (or
  add the latter) and keep one `compose_<kind>_bounds(view, item)` per object
  kind there: timing markers and bar lines already are; BGA events, BGM events
  and bookmarks move in.
- Add one query, `compose_canvas_object_at(view, x, y) ->
  std::optional<editor_selection_object_t>`, as a fixed sequence of named calls.
  `compose_pointer_start_hits_element`, the click handlers and hover read it.
- Build sys area geometry (`append_area_*_geometry`) from the same bounds
  functions; most already do.
- Replace the literal `42.0`/`22.0` rail coordinates with named constants.
- After T-645, because both edit the press/claim path in
  `compose_input_panel.cpp`. Notes stay with their existing hit index.
- Outcome: the next selectable lane is one bounds function, one line in the
  query, one draw pass and one dispatch case.

### S5. Named scope presets (observation 9) — new, small

- Add `editor_selection_scope_t::every_object()` (and
  `with_bar_lines()` if wanted) and use designated initializers at the two
  positional sites (`compose_input_internal.h:1145`,
  `application_command_dispatch.cpp` `every_object`).
- Add a ceremony-ratchet row for positional `editor_selection_scope_t{true` at 0.

### S6. Enum-sized tables fail at compile time (observation 10) — new, small

- Append `WORM_UI_COMPOSE_ACTION_COUNT` (and an input-kind count if a table
  uses one) to the C ABI enums; end-of-enum sentinels don't move existing values.
- Size `compose_kind_count` from the sentinel and `static_assert` every route
  table against it. Building the table stays `constexpr`-checkable, so a
  duplicate route fails the build instead of aborting test discovery.

### S7. Toolbar layout counts from the renderers (observation 11) — extends T-646

- Next to each `render_*_group`, a `*_icon_count(view)` function; the layout
  sums them, and `dock_toggles_at_end` computes its threshold from the same
  counts instead of a measured constant.
- T-646 already owns `compose_toolbar.cpp` and the icon shell; this is a step
  in it.

### S8. Harness finds widgets by tooltip (observation 12) — new; pairs with T-644

- Record each icon widget's bounds and tooltip during a harness frame (a testing
  hook like `worm_ui_frontend_compose_viewport_for_testing`) and add
  `harness.click_widget("Hide sounds list")`.
- Migrate the toolbar tests that click by x position (at least the three that
  moved this time). T-644 is pruning tests in the same files; do this after
  its pass so no migrated test is then deleted.

### S9. Compose toggles in one struct (observation 13) — new, no lifetime change

- Group `edit_snap_enabled`, `y_lock` and `bar_line_time` into one
  `editor_edit_settings_t` that lives in `project.compose_view` (where D3 puts
  it) and is copied whole into the interaction state at the existing idle-frame
  point. Core keeps reading it from the state.
- Toggles write only `compose_view`; the per-field mirror lines and the second
  write in each toggle go away. The view reads the struct too.
- Same lifetimes and reset behaviour as today (D3, D6). Needs a test that a
  toggle mid-gesture still waits for the idle frame, as now.

### S10. Valid shared test fixtures (observation 14) — extends T-644

- Make `clipboard_song()` valid and add `clipboard_song_with_typed_id_collision()`
  for the one test that needs the collision. Fixtures that build documents
  `REQUIRE(validate_document(song).empty())` unless named as invalid.
- T-644 step 4 already rewrites test helpers in these files.

### S11. Build guidance (observation 15) — new, owner choice

- AGENTS.md: build with `cmake --build build --parallel "$(nproc)"`.
  (Give the number: with Make, `--parallel` alone can mean unlimited jobs.)
- Optional, owner's call because it discards the warm build: reconfigure `build/`
  with Ninja like every preset already does (only `build/` uses Make).
- Measure before splitting headers: `interaction.h` has 39 includers; an
  `interaction_fwd.h` only pays off if most of them need only names.

### S12. A smoke fixture with real measures (observation 16) — new, small

- Check in a small native project (8 measures, a few notes, no audio) under
  `tests/fixtures/` and let `scripts/smoke-compose-virtual-x.sh` take
  `--fixture measures`.
- Note the field origin (x ≈ 49 at 1280×720, behind the tool column) in the
  script's header comment next to the existing coordinate notes.

### Process note

T-657's card listed 5 files and touched 22. A planning step that greps for the
parallel implementations of the nearest existing feature (here: the BGA lane
and timing markers) before writing "Files owned" would have predicted most of
them. Worth a line in `docs/chart-editor-plan/tasks.md`.
