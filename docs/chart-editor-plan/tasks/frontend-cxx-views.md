# Frontend C++ views: close the boundary migration

[Back to task cards](../tasks.md).

The C ABI stopped being a requirement on 2026-10-17 ([code-style.md, "Frontend boundary"](../decisions/code-style.md)). Until now the move to plain C++ view types was gradual and held by four ceremony-ratchet rows. On 2026-10-18 the owner asked for a closing lane instead, under the rule "A migration finishes in its card, or it ratchets". These cards convert every remaining `WormUi*` C surface and take the rows to 0. They also replace [T-653](state-ownership.md#t-653-map-each-c-abi-type-through-one-ui_codec): a C++ view needs no codec.

**Counts at `7160520`:** 133 `typedef struct WormUi` (ratchet `frontend_c_struct`), 77 `WORM_UI_*_CAPACITY` (`frontend_fixed_capacity`), 183 `*_count;` fields (`frontend_pointer_count_field`), 1 `ui_codec<WormUi…>` (`frontend_ui_codec`). By header: `compose.h` 64 structs and 93 count fields; `metadata_audio.h` 13 and 24; `timing_preview.h` 13 and 22; `project.h` 10 and 12; `options_notifications.h` 9 and 7; `actions.h` 8 and 16; `runtime.h` 6 and 1; `workspace.h` 6 and 8; `catalog.h` 4 and 0. `common.h` holds 69 of the capacities.

**Order:** T-690 first, because it proves the recipe on the smallest surfaces. T-691–T-694 can then run in parallel when their sys and UI files don't overlap; coordinate `application_frame_view.*` and `src/ui/nuklear/frontend.cpp`. T-695–T-698 split `compose.h` and run one at a time, because they share `src/sys/compose_view.cpp` and `compose_panel.cpp`. T-699 closes the lane.

## Conversion recipe (every card)

The worked example is T-672: `compose_measure_lane_view_t` in `src/ui/frontend/compose_measure_lane.h`.

1. **Type:** a C++ struct named `<area>_<thing>_view_t` (or `_request_t` / `_action_t` for payloads) replaces the C struct. It uses core vocabulary from `core/vocabulary.h` (enums, strong IDs, `rational_t`) instead of raw integers. It holds no document, history, index or editor state, and includes no backend header (`cmake/check_ui_boundary.cmake`).
2. **Lists:** an owned `std::vector` replaces each pointer-plus-count pair and its capacity. Text becomes `std::string`, or `std::string_view` when the producer owns stable text for the frame.
3. **Filling:** sys refills the view in place each frame (`clear()`, then push), so steady state allocates nothing. Nothing may copy a view per frame. A view that becomes non-trivially copyable leaves `frame_span` storage. The root workspace view is persistent (owner, 2026-10-20): the application owns one, keeps it across frames and fills it with `view.refill(context)`; nothing builds a fresh root per frame with `from` and assigns it, because that discards every vector's capacity and brings back the per-frame allocations that T-272 tried to fix with an arena. Producers of nested views take the nested view by reference and refill it; they do not return it by value.
4. **Queries:** a question the frontend asks of the view becomes a member on the type, e.g. `view.measure_lane.is_selected(id)`.
5. **Editable objects carry their ID** (from T-653's proposed addition): a view of a selectable or editable object includes the object's ID, and display numbers are never used as keys.
6. **Capacities:** a capacity that only sized storage is deleted, along with its truncation flag and "list truncated" path. A capacity that bounds real work (render budget, input size, format limit) moves to the consumer or core validation as a named constant, recorded on the card. Nothing is silently unbounded that was bounded before.
7. **Codecs:** a `ui_codec` specialization and the free `*_from_ui` / `*_to_ui` converters for a converted surface are deleted. Sys builds the C++ view with `build_<x>_view` or a member, and parses actions with `T::from`.
8. **Tests:** update the frontend, event-harness and sys tests that construct the old struct. Add one steady-state test per card: building the view twice with the same data shows a zero delta in `frame_heap_allocation_snapshot()` (`src/sys/frame_timing.h`; debug builds) on the second build.
9. **Ratchet:** lower `frontend_c_struct`, `frontend_fixed_capacity`, `frontend_pointer_count_field` and `frontend_ui_codec` in the same checkpoint that lowers their counts. Never raise one.

**Card fields shared by T-690–T-698**, so each card lists only its differences:
- **Read first:** this section, `src/ui/frontend/compose_measure_lane.h`, `docs/chart-editor-plan/decisions/code-style.md` ("Frontend boundary"), `docs/architecture.md`, the card's header.
- **Files owned:** the card's headers, plus their sys producers, `src/ui/nuklear` consumers and tests, as listed by step 1's grep and recorded on the card; `src/ui/frontend/common.h` (only the capacities this card retires); `cmake/ceremony_ratchet.txt`; this card.
- **Invariants:** the same pixels, hit targets, text, ordering and actions as before. Removing a truncation path is the only intended behavior change: a list that used to stop at its capacity now shows every row, and that difference is recorded. No per-frame allocation or copy in steady state.
- **Reject/failure behavior:** a producer failure still yields the same empty, disabled or error view as today. An oversized input is still rejected wherever a real bound existed (recipe step 6).
- **Verify:** GATE; `ctest --test-dir build -R '^documentation\.' -V` shows the lowered rows; `ctest --test-dir build -L event-harness --output-on-failure`; the coordinator runs `scripts/smoke-compose-virtual-x.sh` on the affected screen and inspects the PNG.

## T-690 Convert the catalog, workspace and runtime surfaces

- **Status:** done (2026-10-20)
- **Value:** Proves the recipe on 16 small structs before the large surfaces.
- **Outcome:** `catalog.h`, `workspace.h` and `runtime.h` contain no `typedef struct WormUi`; their count fields and their capacities in `common.h` are gone. The application owns one persistent frontend workspace view and refills it each frame; `frontend_workspace_view_t::from(const application_frame_projection_context_t&)` is gone.
- **Read first:** the shared list, plus `src/sys/workspace_view.h` (`frame_span`, `rebind_view_pointers`), `src/sys/application_frame_view.cpp` (per-frame `from`).
- **Files owned:** the shared list for `src/ui/frontend/catalog.h`, `workspace.h` and `runtime.h`, plus `src/sys/workspace_view.h/.cpp`, `src/sys/application_frame_view.h/.cpp` and `src/sys/application.cpp` (view ownership and the per-frame call only).
- **Steps:**
  1. Stale-plan check and inventory: `grep -rn "WormUi<Name>" src tests` for each struct in the three headers; record producers, consumers, the capacities each uses and the `frame_span` storage behind each list. `grep -rn "frontend_workspace_view_t::from\|rebind_view_pointers" src tests`; record every caller.
  2. Make the root persistent (recipe step 3): the application owns one `frontend_workspace_view_t` and calls `refill(context)` on it each frame; replace the per-frame `from(context)` and its assignment. Tests construct a view and call `refill`. Keep `rebind_view_pointers` while unconverted C surfaces still point into `frame_span` storage; it now runs once per refill instead of after every move. Checkpoint with the steady-state allocation test before converting any header.
  3. Convert per the recipe, one header per checkpoint. `WormUiWorkspaceView` becomes the C++ root view; unconverted nested C structs stay members of it, as in T-672.
  4. Record any new conventions the recipe lacked as a proposed addition on this file's recipe section (coordinator transfers it).
- **Invariants:** the shared list.
- **Reject/failure behavior:** the shared list.
- **Verify:** the shared list.
- **Out of scope:** the other headers (T-691–T-698); deleting `frame_span` itself (T-699).

### T-690 implementation checkpoints (2026-10-20)

- Inventory: catalog producers live in `src/sys/catalog_view.h`, with dropdown consumers in
  the Nuklear catalog helper, setup, metadata, timing, sound import and Compose controls.
  The two string-catalog structs have no callers. Catalog rows borrow process-lifetime labels;
  the converted catalog owns its row list and retains capacity in each persistent view.
- Workspace producers are `workspace_view.cpp`, `compare_view.cpp`, `menu_command_view.cpp`,
  `preferences_runtime.cpp` and the command-menu/close-confirmation projections in
  `application.cpp`. Consumers are the root frontend, menu bar, command menu and compare panel,
  plus frontend/sys fixtures. Summary text was arena-backed; compare lists were cache-backed;
  other root lists were inline arrays. The nested metadata, Compose and audio lists still use
  `frame_span` and remain scheduled for T-691–T-699.
- Runtime producers/consumers: `frame_timing.cpp`, the frontend diagnostic overlay,
  `sdl_backend.cpp` GPU metrics, Compose canvas test bounds and event-harness fixtures.
  The 240-sample rolling timing history is a real work bound owned by `frame_timing_capacity`;
  statistical sample counts remain statistics rather than list-length plumbing.
- Persistent-root checkpoint, before converting headers: the application now retains the main
  workspace and both compare-pane roots. `refill(context)` replaces the per-frame root factory;
  `from(source, ...)` remains for once-built standalone snapshots. The new persistent-refill
  regression passes **5 assertions**, including zero warmed allocation delta, retained selected
  bar-line capacity and cleared inactive compare state. Build/test log:
  `/tmp/wormedit-t690-persistence-build.log`; the application build subsequently passed.
- Coordinated supporting-file ownership: catalog consumers in the other frontend headers may
  change their catalog member types only; runtime users in the SDL backend, canvas and test
  harness may change their runtime value types only. These extensions are required to compile
  the converted T-690 surfaces and do not convert the other cards' payloads.

- Converted all 16 legacy structs in these three headers (including deleting the two unused
  string-catalog structs). Catalogs use their actual enum IDs, owned row vectors and member label
  queries. Runtime timing samples and graph storage persist; statistical sample counts remain.
  Test text capture now retains labels beyond the old 128-byte storage buffer.
- Root lists own storage and refill in place. Summary (16), menu (64), command-menu (512),
  compare chart/diagnostic (256) and recovery row/diagnostic (4 each) storage cutoffs are removed.
  Recovery keeps a four-row/four-diagnostic **viewport budget**, with scrolling through every
  controller-retained row. The controller still owns its 64-row/64-diagnostic maximum retention
  limits (diagnostic default 32); recent-file preferences retain their existing 1–100 bound.
  Command-menu results keep their named 20-match render budget; runtime keeps the 240-sample
  history budget. Removed truncation on these lists is intentional.
- Supporting ownership extensions: `src/ui/frontend/file_jobs.h` borrows process-lifetime phase
  labels (all four existing producers select literals), avoiding owned-string snapshot allocation;
  `src/sys/shortcut_rows.*` retains first-chord and joined palette text in the existing shared cache;
  `src/ui/nuklear/chrome.h` shares its combo implementation with a typed span/label projection.
  Compare uses that overload without recreating a label-pointer array or changing popup layout.
- The frontend workspace render entry accepts the persistent root by mutable reference/pointer
  only to fill its presentation-only Compose input-occlusion flag. This removes the previous
  per-frame Compose copy while retaining all existing popup/pointer input guards.
- The recovery-height query now lives on the workspace view and replaces three duplicated
  formulas. Complexity verification of the seven sys owners passes; both former score-26
  `compose_surface_height` allowances are removed after the shared query lowers them below 26.
  No complexity allowance was increased. Ratchets lower **133→117**, **77→73**, **183→174**;
  the single timing codec stays for T-693.

- Final verification: **GATE passes 4,351/4,351**, build, whitespace and complexity across
  48 changed translation units (`/tmp/wormedit-gate.gOnv6y`). The new runtime, menu, recovery
  and retained-root allocation tests pass. The code-style review checklist found no new parallel
  vocabulary, outcome/transaction plumbing, copied helper or increased ratchet/baseline allowance.
- Isolated real-window QA inspected startup, Project chart actions/duplication, Compose, View
  menu shortcuts, the timing overlay and both Compare panes with its chart picker. Captures:
  `/tmp/wormedit-t690-{landing,two-charts,compose,timing-overlay,compare,compare-catalog}.png`.
  Header-layout edits initially overlapped compilation and left inconsistent object layouts;
  touching the shared headers after freezing edits forced a complete consumer rebuild, after
  which the real-window checks passed. No broad projection-allocation or frame-time gain is
  claimed from these converted-owner allocation tests.

## T-691 Convert the project and options/notifications surfaces

- **Status:** done (2026-10-20)
- **Outcome:** `project.h` and `options_notifications.h` contain no C structs; their capacities (recovery rows, project paths, notification history and options lists) are deleted or recorded as real bounds.
- **Read first:** the shared list.
- **Files owned:** the shared list for `src/ui/frontend/project.h` and `options_notifications.h`.
- **Steps:**
  1. Inventory as in T-690.
  2. Convert, one header per checkpoint.
  3. The project chart table views carry chart IDs (recipe step 5).
- **Reject/failure behavior:** the shared list.
- **Verify:** the shared list.
- **Invariants:** the shared list. Recovery and notification history keep their existing retention limits as named bounds.
- **Out of scope:** the project-open publication and export jobs (T-684/T-685 behavior stays).

### T-691 coordinated inventory (2026-10-20)

- Producers/consumers and supporting ownership: `src/sys/project_view.*`,
  `chart_import_session.*`, `options_runtime.*`, `notification_history_runtime.*`,
  `shortcut_rows.*`; Nuklear `setup_panel.*`, `setup_state.*`, `chart_import_dialog.*`,
  `options_window.*`, `shortcuts_panel.*`, `notification_panel.*`, `notification_toasts.*`
  and `popup_lifecycle.h`, plus their existing core/sys/frontend/event-harness tests.
  Shared `workspace_view.*`, `application.cpp`, `frontend.cpp` and the workspace header
  integrate retained child views. Core vocabulary relocates existing pure enums and preference
  limits from their original headers without changing values. The metadata editor's existing
  256-chart admission bound becomes `maximum_batch_chart_metadata_charts` for all consumers.
- Storage cutoffs to retire: project chart/group lists (256), chart-import notes (24),
  shortcut rows (512), and boundary options/notification arrays. Preserve core preference text
  4096 bytes, 64 external players, registry admission 512, notification retention 256,
  visible toasts 3, output-preview processing 512, project-table display text 4096 bytes
  and condensed missing-file summaries 8. Project-path and recovery
  destination input limits remain in the enclosing action migration (T-694).
- Recovery rows own path/summary/actions, reuse row storage, and query `can(core_action)`;
  the former text sidecar and bitmask vocabulary are removed. Options validation and shortcut
  conflict/chord projections use their existing owners with keyed reuse, including direct public
  draft changes. Notification projection refills the retained root after reset, avoiding a copy.
- Supporting tests include the existing popup lifecycle/menu, menu bar and BMS walkthrough
  event harnesses. Their fixtures now own row lists and carry the same strong IDs as production.
  The project action parser is `project_chart_command_t::from`, using the shared result type and
  retaining the previous failure text and row-error publication.


## T-692 Convert the metadata and audio surfaces

- **Status:** done (2026-10-20)
- **Outcome:** `metadata_audio.h` contains no C structs. The metadata text capacities (`WORM_UI_METADATA_*`) are either deleted or turned into the format limits that the metadata core already validates.
- **Read first:** the shared list.
- **Files owned:** the shared list for `src/ui/frontend/metadata_audio.h`, plus `src/sys/metadata_view.cpp` and `src/sys/metadata_actions.cpp`.
- **Steps:** 1. Inventory as in T-690, and for each `_BYTES` or `_CAPACITY` limit, note whether core validation already enforces it. 2. Convert. 3. Edits that used to be rejected by a UI buffer limit are still rejected by core with the same diagnostic, or the difference is recorded for the owner.
- **Invariants:** the shared list.
- **Reject/failure behavior:** the shared list.
- **Verify:** the shared list.
- **Out of scope:** metadata form behavior and validation rules.

### T-692 coordinated inventory (2026-10-20)

- Supporting ownership: metadata/audio producers and consumers, `sound_list_view.*`,
  `sound_usage_view.*`, `waveform_view.*`, `metadata_drop_state.*`, their existing tests,
  shared `workspace_view.*`/`frontend.cpp`, and `include/wormedit/core/metadata_limits.h`.
  The limits type moves unchanged from `metadata_editor.h` into this lightweight header.
  Sound-usage enums move unchanged from sys into core vocabulary.
- Preserve actual metadata input/format limits: title/artist/chart text 16384 bytes,
  4096 tags of 4096 bytes, format names 1024 bytes, values 1048576 bytes,
  16384 fields/group, output paths 4096 bytes and total metadata 32 MiB.
  Retire boundary storage limits for format fields (128), format targets (14) and assets (128).
  Retain sound query 512, usage query 1024, waveform pixel/render 4096,
  channel-map admission 256 and batch-chart admission 256 as named real bounds.
- Approved supporting extension: `include/wormedit/audio/waveform.h`,
  `src/audio/waveform.cpp` and `src/audio/waveform_viewport.cpp` gain in-place refill while
  snapshot factories use the same unchanged math. The projection retains intermediate columns,
  resizes/refills owned frontend columns and fixes repeated-refill column accumulation.
  Retained keyed sound/usage queries avoid rebuilding their temporary maps each frame.
- `src/sys/metadata_projection_cache.h` owns retained formatting/query caches for supplemental
  metadata paths, output asset directories, channel/lane preview summaries and lane-profile
  targets. Borrowed hit keys allocate nothing; changed paths, destination kinds, preview content,
  document stamps and project session generations invalidate the applicable value. Sound/usage
  caches also carry session generation, so reopening the same ID/revision refreshes their rows.
  The shared application frame context passes that existing generation to the retained root.
- The nested output row still belongs to T-698. Its owned outer list removes the old row cutoff,
  while its arena-backed labels retain complete-prefix failure and the existing warning on arena
  exhaustion. Other converted metadata text is owned independently of that arena.


## T-693 Convert the timing and preview surfaces

- **Status:** done (2026-10-20)
- **Outcome:** `timing_preview.h` contains no C structs, and `ui_codec<WormUiTimingLaneKind>` is gone (`frontend_ui_codec` 0).
- **Read first:** the shared list.
- **Files owned:** the shared list for `src/ui/frontend/timing_preview.h`, plus `src/sys/timing_view.cpp`, `src/sys/timing_lane_ui.h` and `src/sys/player_preview_presenter.cpp`.
- **Steps:** 1. Inventory as in T-690. 2. Convert; the timing-lane kind becomes the core enum. 3. Delete the codec and its callers' conversions.
- **Reject/failure behavior:** the shared list.
- **Verify:** the shared list.
- **Invariants:** the shared list. Player-preview frame pacing ([frame-pacing.md](frame-pacing.md)) must not regress; record `frame_timing` heap summaries before and after.
- **Out of scope:** timing edits themselves (proposal path).

### T-693 coordinated inventory (2026-10-20)

- Supporting ownership includes timing/preview sys producers, player-preview Nuklear rendering
  and texture consumers, existing timing/preview tests, external-player application projection
  and local editor buffers, shared root reset, and core vocabulary's former enum owners.
  The shared Compose timing-lane enum is replaced with the existing core enum; the timing codec
  and its duplicate conversion table are removed without converting Compose geometry (T-695).
- Six preview output vectors move from presenter side storage into the retained view.
  IDs use core types; image pixels borrow immutable decoded resources retained by the presenter.
  Timing/stage status owns text; external definitions borrow stable preference text.
- Retire storage-only lane 64 and hold 4096 rejection paths and unused timing-speed 4096/grid
  16384 capacities. Preserve note rendering/evaluation 4096, storyboard evaluation 27,
  texture composition 6 and decoder image dimension 16384 as named real budgets.
  Timing-marker capacity 16384 still serves unconverted Compose storage and belongs to T-695.
- Before measurement: isolated Compose fixture with Player preview and timing overlay,
  `/tmp/wormedit-t693-frame-before.txt`: warmed workspace projection 71 new / 9248 requested
  bytes, Frame view 17 / 5000, Timing view 0; whole-frame measurements include unrelated work.


- After measurement with the identical isolated fixture and steps, no compiler work running:
  `/tmp/wormedit-t693-frame-after.txt` records warmed workspace projection **65 new / 8646 bytes**
  versus **71 / 9248** before. Frame view remains **17 / 5000** (the unchanged core preview-scene
  factory), and Timing view remains **0 / 0**. Final 240-frame mean/p99 were 17.323/19.839 ms
  after versus 20.654/26.303 ms before. Single-run Xvfb timings do not establish a pacing gain;
  both preview and timing-overlay captures were inspected with matching contents.

### T-691–T-693 coordinated verification checkpoint

- Final **GATE passes 4,361/4,361**, build, whitespace and complexity across **172 changed
  translation units** (`/tmp/wormedit-gate.Iv9GMV`). Separate event-harness verification passes
  **590/590** (`/tmp/wormedit-maintenance-verified-events.log`); verbose documentation verification
  passes **5/5** and prints all four lowered rows. The code-style checklist and the three card
  reviews found no remaining concrete issue after the recorded fixes.

- Warmed populated project/options/shortcut, supplemental metadata and output-suggestion tests
  pass **61 assertions in four cases**, including zero allocation deltas and source/session
  invalidation. Shortcut conflict reuse additionally keys focus scope, context, tab and lane.
- The converted export draft vector exposed an existing Preview/Delete read of unopened editor
  state. `setup_panel.cpp` now serializes draft fields only for Create/Update; Preview/Delete
  retain the shared group identity and stamps. The existing event-harness workflow covers this.
- Startup QA found a nullable idle Tap timing status. The runtime now publishes an empty
  `std::string_view`; a regression covers startup, valid taps and reset.
- Isolated real-window checks inspect Project, Metadata, Audio Slices, General options,
  Input & shortcuts, External players, chart import, notification history/toasts, Player preview
  and the timing overlay. Screenshots are under `/tmp/wormedit-maintenance-*.png` and
  `/tmp/wormedit-t693-{preview,timing}-after.png`.
- Ratchets now read **64 C structs / 50 fixed capacities / 121 count fields / 0 codecs**.
  This includes the explicitly partial eight-declaration T-694 checkpoint below. The remaining
  Compose output-row arena adapter is tracked by T-698; no whole-pipeline zero-allocation claim
  is made. Read-only card reviews found no remaining concrete ownership, lifetime or cache-key
  issue after the shortcut scope fix. The shortcut scope regression uses a cohesive assertion
  helper; the notification overlay test also splits its assertions, removing its former score-26
  allowance. No complexity allowance was increased.


## T-694 Convert the action payloads

- **Status:** partial (declaration checkpoint; owned payloads and strong IDs remain)
- **Outcome:** `actions.h` contains no C structs; action payloads are C++ types the frontend fills and sys reads with `T::from`, with no fixed-size arrays.
- **Read first:** the shared list.
- **Files owned:** the shared list for `src/ui/frontend/actions.h`, plus the sys dispatchers that read each payload (step 1).
- **Steps:** 1. Inventory every payload with its writers in `src/ui/nuklear` and readers in `src/sys`. 2. Convert one action family per checkpoint. 3. Payload IDs use strong ID types.
- **Reject/failure behavior:** the shared list.
- **Verify:** the shared list.
- **Invariants:** the shared list. Dispatch routing, stale-stamp checks and diagnostics are unchanged.
- **Out of scope:** what any action does; command routes (`application_command_route_t`).

### T-694 declaration checkpoint (2026-10-20)

- **Done so far:** inventoried eight payloads and their readers/writers; compiler-resolved
  renames give the declarations and consumers consistent C++ names in `wormedit::ui` while
  T-691/T-693 convert nested payloads. Timing target and viewport input are plain value records.
  A small Sonnet read-only audit checked the retained input/admission bounds.
- **Remaining:** convert the other payload families' pointer/count lists and text buffers to
  owned storage, parse payloads on their types, and carry strong IDs through their producers
  and dispatchers. Preserve Compose/viewport admission 16 and shortcut admission 64 including
  overflow cancellation. Preserve path/import/recovery input bounds and missing metadata
  payload diagnostics. The declaration spelling checkpoint alone does not complete this card;
  remaining capacities/count fields stay ratcheted.


## T-695 Convert the Compose field geometry views

- **Status:** ready
- **Value:** The hottest per-frame path, so this is where the steady-state allocation test matters most.
- **Outcome:** the field geometry structs in `compose.h` are C++ views with owned lists refilled in place: rect, paste ghost, band, lane, grid line, lane layout segment, timing preview rect, dead/Kiai/speed spans, SV graph point, recipe area, difference, hold, note, cursor, timing marker, quantize ghost.
- **Read first:** the shared list.
- **Files owned:** the shared list for those structs in `src/ui/frontend/compose.h`, plus `src/sys/compose_view.cpp`, `src/sys/compose_view_internal.h`, `src/ui/nuklear/compose_canvas.cpp` and the field renderer (`src/ui/field/field_renderer.*`).
- **Steps:** 1. Inventory as in T-690. 2. Convert one primitive family per checkpoint. 3. Record frame-time and `frame_timing` heap summaries for the smoke chart before and after.
- **Reject/failure behavior:** the shared list.
- **Verify:** the shared list.
- **Invariants:** the shared list. The GPU field renderer's input stays a contiguous span.
- **Out of scope:** BGM, sound and bookmark views (T-696–T-698); `WormUiComposeView` itself (T-698).

## T-696 Convert the Compose BGM, waveform and stem views

- **Status:** blocked-on T-695
- **Outcome:** the BGM lane, event, draft, bar mark/bookmark, move preview, waveform row/overview row, stem waveform and stem cut views are C++ views.
- **Read first:** the shared list.
- **Files owned:** the shared list for those structs, plus `src/sys/compose_bgm_bar.cpp`, `src/sys/bmson_waveform_view.cpp` and `src/sys/waveform_view.cpp`.
- **Steps:** 1. Inventory. 2. Convert one family per checkpoint.
- **Reject/failure behavior:** the shared list.
- **Verify:** the shared list.
- **Invariants:** the shared list. Waveform pyramids stay owned by the audio cache; views borrow their rows for the frame.
- **Out of scope:** waveform generation.

## T-697 Convert the Compose sound, keysound and BGA views

- **Status:** blocked-on T-696
- **Outcome:** the resource, sound picker, keysound row/display/advance/selection/layer, sound group, keysound fallback, sound slot, BGA row/event views and the BGA and keysound dock requests are C++ types.
- **Read first:** the shared list.
- **Files owned:** the shared list for those structs, plus `src/ui/nuklear/compose_sounds_panel.cpp` and `src/ui/nuklear/compose_bga_panel.cpp`.
- **Steps:** 1. Inventory. 2. Convert one family per checkpoint.
- **Invariants:** the shared list.
- **Reject/failure behavior:** the shared list.
- **Verify:** the shared list.
- **Out of scope:** sound import and slicing behavior.

## T-698 Convert the remaining Compose views and `WormUiComposeView`

- **Status:** blocked-on T-697
- **Outcome:** `compose.h` contains no C structs. The remaining views (semantic layers, bookmarks, sections, audio bookmarks/sections, branch rows and choices, recipe form, bookmark chooser, lane key binding, shortcut event, output group, measure row, quantize preset/position/report) and `WormUiComposeView` itself are C++ types, and the T-672 member sits in a C++ parent.
- **Read first:** the shared list.
- **Files owned:** the shared list for `src/ui/frontend/compose.h`, plus `src/sys/compose_view.cpp`, `src/ui/nuklear/compose_panel.cpp` and `compose_inspector.cpp`.
- **Steps:** 1. Inventory. 2. Convert the leaf views one family per checkpoint, then `WormUiComposeView`. 3. Shortcut events keep USB HID usages as a typed enum.
- **Invariants:** the shared list.
- **Reject/failure behavior:** the shared list.
- **Verify:** the shared list.
- **Out of scope:** shortcut profile behavior.

## T-699 Retire the C boundary leftovers

- **Status:** blocked-on T-698 and T-691–T-694
- **Outcome:** all four frontend ratchet rows are 0 and their rows are deleted. `common.h` keeps only real bounds and SDL forward declarations. `frame_span` storage for frontend views is gone, along with `rebind_view_pointers` and the side storage in `frontend_workspace_view_t`: what remains is the persistent C++ root view that T-690 introduced, holding only view data. The `ui_codec` naming row and T-653 are retired, and code-style.md's "Frontend boundary" says the migration is finished.
- **Read first:** this file, `src/sys/workspace_view.h`, `src/sys/application_frame_view.*`, code-style.md ("Frontend boundary", naming table).
- **Files owned:** `src/ui/frontend/common.h`, `src/ui/frontend.h`, `src/sys/workspace_view.h/.cpp`, `src/sys/application_frame_view.h/.cpp`, `cmake/ceremony_ratchet.txt`, `docs/chart-editor-plan/decisions/code-style.md`, `docs/architecture.md`, T-653's status line in `state-ownership.md`, this card.
- **Steps:**
  1. Stale-plan check: all four rows read 0. `grep -rn "frame_span" src` lists only users that are not frontend views (`src/core/arena.cpp`, storyboard preview); record them.
  2. Delete frontend-only frame storage, `rebind_view_pointers` and dead capacities; keep the arena for its remaining users. If `frontend_workspace_view_t` then holds nothing beyond the root view, fold it into that type.
  3. Delete the ratchet rows. Update code-style.md (migration finished; remove the `ui_codec` row; C++ views are the only form) and the "Where to look" table in `docs/architecture.md`. Set T-653 to `superseded by T-690–T-699`.
- **Invariants:** no behavior change; same frame-time budget.
- **Reject/failure behavior:** a remaining user of a deleted storage type stops the step; record it and keep the type.
- **Verify:** GATE; `ctest --test-dir build -R '^documentation\.'`; the coordinator's Xvfb smoke of every workspace tab.
- **Out of scope:** replacing Nuklear (the Nuklear audit decides that).
