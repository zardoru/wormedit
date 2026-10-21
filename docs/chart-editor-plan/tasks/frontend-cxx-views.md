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

- **Status:** done
- **Outcome:** `actions.h` contains no C structs; action payloads are C++ types the frontend fills and sys reads with `T::from`, with no fixed-size arrays.
- **Read first:** the shared list.
- **Files owned:** the shared list for `src/ui/frontend/actions.h`, plus the sys dispatchers that read each payload (step 1), `cmake/complexity_baseline.txt` (reviewed test-limit decrease), and the exact coordinated inventory below.
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
- **Remaining at that checkpoint (implemented below):** convert the other payload families' pointer/count lists and text buffers to
  owned storage, parse payloads on their types, and carry strong IDs through their producers
  and dispatchers. Preserve Compose/viewport admission 16 and shortcut admission 64 including
  overflow cancellation. Preserve path/import/recovery input bounds and missing metadata
  payload diagnostics. The declaration spelling checkpoint alone does not complete this card;
  remaining capacities/count fields stay ratcheted.


### T-694 ownership and payload checkpoint (2026-10-20)

- **Done so far:** action-owned strings and vectors replace borrowed payloads, count pairs and
  direct fixed buffers. Metadata retains optional missing-text semantics and owns channel-map
  rows; sound import/group payloads own nested members. Obsolete metadata, sound and measure
  pointer staging and the harness's deep-copy adapters are retired. Actual identities carry
  core strong IDs; measure-list values remain chart positions, including virtual tail measures,
  and are documented as positions rather than being mislabeled as stable measure IDs.
- **Bounds:** Compose and viewport ingestion remain 16 each; shortcut ingestion remains 64,
  with the existing overflow cancellation. These are named admission constants, independent of
  vector storage. Project paths retain 4096 bytes and recovery destinations 511 bytes; their
  bounds moved to vocabulary, and recovery rejects embedded nulls and overlength owned strings.
  Domain import, tag, channel-map, batch and quantization-name validation remains in place.
- **Supporting ownership approved by the coordinator:** move resource-role and quantization
  vocabulary declarations from `document.h` to `vocabulary.h` without changing values; share
  import intent in a neutral header; remove obsolete UI/harness staging; carry typed values
  through the existing dispatch and request owners. No command route or edit semantics change.
  The nested `WormUiTimingRecipeForm` is declared in `compose.h` and explicitly remains owned
  by T-698; this card removes arrays declared by `actions.h`, not that later card's form.
- **Verification:** GATE passes build, **4,367/4,367** tests, whitespace and complexity across
  **132 changed translation units** (`/tmp/wormedit-gate.5J66eB`). The separate event harness
  passes **591/591**. Owned-copy regressions cover metadata, sound, branch text and measure
  positions; the warmed sound-list setter reports zero additional allocations and requested
  bytes. Oversized Compose/viewport batches cancel active input without changing history;
  shortcut ingestion and shared capture reject overflow before binding. No complexity allowance
  increased; the metadata reset test limit drops **52→48** after its retired staging assertion
  is removed. Existing domain factories and validators remain the text parsing owners.
- **Real-window checks:** isolated Xvfb captures cover the populated Compose fixture, timing
  focus, empty Audio Slices, fresh-project creation and Project/Metadata/Compose navigation
  (`/tmp/t694-{compose,timing,audio,project,metadata,created-compose}.png`). Applying a song
  title and Undo restore both text and clean state (`/tmp/t694-metadata-{applied,undone}.png`).
  The smoke helper's default wizard coordinates predate T-728; fixture arguments and explicit
  current wizard coordinates were used, without changing the helper in this card.
- **Clang limitation:** `cmake --build --preset linux-clang --parallel 2` still stops at the
  same three missing `stepmania` initializers in unchanged `src/core/lane_profiles.cpp`
  (1139, 1148, 1151; identical in HEAD). Log: `/tmp/t694-clang-build.log`.
- **Ratchets:** **64 C structs / 45 capacities / 107 count fields / 0 codecs**. The two
  remaining action count fields describe BGM column configuration and the semantic-layer
  window, rather than pointer/count lists. No frame-time improvement is claimed.

Verification output tails:

```text
  pass  build
  pass  ctest
  pass  whitespace
  pass  complexity
  100% tests passed out of 4367
gate: passed
100% tests passed out of 591
100% tests passed out of 5
4: -- Ceremony ratchet 'frontend_c_struct': 64
4: -- Ceremony ratchet 'frontend_fixed_capacity': 45
4: -- Ceremony ratchet 'frontend_pointer_count_field': 107
4: -- Ceremony ratchet 'frontend_ui_codec': 0
```

The exact coordinated producer, consumer, vocabulary and regression inventory is:

- **Vocabulary and frontend contract:** `include/wormedit/core/document.h`, `include/wormedit/core/vocabulary.h`, `src/ui/frontend/actions.h`, `src/ui/frontend/common.h`, `src/ui/frontend/sound_import.h`.
- **Application producers and dispatch:** `src/sys/application.cpp`, `src/sys/application_command_dispatch.h`, `src/sys/application_frame_services.cpp`, `src/sys/application_timing_stage.cpp`, `src/sys/application_timing_stage.h`, `src/sys/audio_offset_actions.h`, `src/sys/catalog_view.h`, `src/sys/compose_actions.cpp`, `src/sys/compose_area_actions.cpp`, `src/sys/compose_bga_actions.cpp`, `src/sys/compose_bgm_bar.cpp`, `src/sys/compose_bookmark_actions.cpp`, `src/sys/compose_input.cpp`, `src/sys/compose_input_internal.h`, `src/sys/compose_measure_actions.cpp`, `src/sys/compose_quantize_request.h`, `src/sys/compose_recipe_actions.cpp`, `src/sys/compose_resource_actions.cpp`, `src/sys/compose_resource_input.cpp`, `src/sys/compose_sound_actions.cpp`, `src/sys/compose_sound_actions.h`, `src/sys/compose_timing_input.cpp`, `src/sys/compose_view.cpp`, `src/sys/compose_waveform_fit_input.cpp`, `src/sys/metadata_actions.cpp`, `src/sys/metadata_asset_runtime.cpp`, `src/sys/preferences_runtime.cpp`, `src/sys/recovery_notification_feedback.cpp`, `src/sys/shortcut_rows.cpp`, `src/sys/shortcut_rows.h`, `src/sys/sound_drop_transaction.cpp`, `src/sys/sound_import_staging.cpp`, `src/sys/sound_import_staging.h`, `src/sys/sound_usage_actions.cpp`, `src/sys/transport_player_actions.cpp`.
- **Frontend consumers and staging:** `src/ui/nuklear/audio_panel.cpp`, `src/ui/nuklear/bms_branch_panel.cpp`, `src/ui/nuklear/bmson_lane_mode_ui.cpp`, `src/ui/nuklear/chart_import_dialog.cpp`, `src/ui/nuklear/command_menu.cpp`, `src/ui/nuklear/compare_panel.cpp`, `src/ui/nuklear/compose_action_stamp.h`, `src/ui/nuklear/compose_area_menu.cpp`, `src/ui/nuklear/compose_area_menu.h`, `src/ui/nuklear/compose_bga_panel.cpp`, `src/ui/nuklear/compose_bmson_tab.cpp`, `src/ui/nuklear/compose_bookmark_chooser.cpp`, `src/ui/nuklear/compose_bookmark_rename.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_keyboard_input.cpp`, `src/ui/nuklear/compose_layers_panel.cpp`, `src/ui/nuklear/compose_markers.cpp`, `src/ui/nuklear/compose_measure_resize_input.cpp`, `src/ui/nuklear/compose_measures_panel.cpp`, `src/ui/nuklear/compose_menu_command.h`, `src/ui/nuklear/compose_note_label_inline.cpp`, `src/ui/nuklear/compose_note_menu.cpp`, `src/ui/nuklear/compose_object_input.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_recipe_form.cpp`, `src/ui/nuklear/compose_recipe_menu.cpp`, `src/ui/nuklear/compose_resources.cpp`, `src/ui/nuklear/compose_snap_grid.cpp`, `src/ui/nuklear/compose_sound_picker.cpp`, `src/ui/nuklear/compose_sounds_panel.cpp`, `src/ui/nuklear/compose_state.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_timing_input.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/compose_viewport_input.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/keysound_list_state.cpp`, `src/ui/nuklear/keysound_list_state.h`, `src/ui/nuklear/metadata_audio_section.cpp`, `src/ui/nuklear/metadata_difficulty_section.cpp`, `src/ui/nuklear/metadata_format_fields.cpp`, `src/ui/nuklear/metadata_panel.cpp`, `src/ui/nuklear/metadata_song_section.cpp`, `src/ui/nuklear/metadata_state.cpp`, `src/ui/nuklear/metadata_state.h`, `src/ui/nuklear/setup_panel.cpp`, `src/ui/nuklear/sound_import_dialog.cpp`, `src/ui/nuklear/sound_import_runtime.cpp`, `src/ui/nuklear/sound_import_runtime.h`, `src/ui/nuklear/sound_import_state.cpp`, `src/ui/nuklear/sound_import_state.h`, `src/ui/nuklear/sound_slot_batch.cpp`, `src/ui/nuklear/sound_slot_batch.h`, `src/ui/nuklear/timing_settings_panel.cpp`, `src/ui/nuklear/timing_stage_editor.cpp`.
- **Regression consumers:** `tests/sys/application_commands_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `tests/sys/compose_area_dispatch_fixture.h`, `tests/sys/compose_beat_fit_tests.cpp`, `tests/sys/compose_bgm_bar_tests.cpp`, `tests/sys/compose_notification_tests.cpp`, `tests/sys/compose_quantize_preview_tests.cpp`, `tests/sys/frame_projection_arena_tests.cpp`, `tests/sys/frame_timing_tests.cpp`, `tests/sys/global_drop_notification_tests.cpp`, `tests/sys/manual_sound_group_command_tests.cpp`, `tests/sys/metadata_notification_tests.cpp`, `tests/sys/minimap_center_tests.cpp`, `tests/sys/recovery_notification_tests.cpp`, `tests/sys/resource_notification_tests.cpp`, `tests/sys/shortcut_rows_tests.cpp`, `tests/sys/sound_properties_actions_tests.cpp`, `tests/sys/sound_usage_actions_tests.cpp`, `tests/ui/audio_sounds_workflow_event_harness_tests.cpp`, `tests/ui/bga_advance_workflow_tests.cpp`, `tests/ui/bga_lane_event_harness_tests.cpp`, `tests/ui/bmson_lane_mode_event_harness_tests.cpp`, `tests/ui/bookmark_chooser_event_harness_tests.cpp`, `tests/ui/bookmark_rename_event_harness_tests.cpp`, `tests/ui/command_menu_event_harness_tests.cpp`, `tests/ui/compose_beat_fit_tests.cpp`, `tests/ui/compose_bgm_layer_tests.cpp`, `tests/ui/compose_hover_timing_event_harness_tests.cpp`, `tests/ui/compose_measure_resize_tests.cpp`, `tests/ui/dock_select_all_event_harness_tests.cpp`, `tests/ui/dock_shortcut_event_harness_tests.cpp`, `tests/ui/gesture_lifecycle_event_harness_tests.cpp`, `tests/ui/hidden_notification_history_event_harness_tests.cpp`, `tests/ui/hitsound_palette_event_harness_tests.cpp`, `tests/ui/inspector_resource_workflow_tests.cpp`, `tests/ui/lane_keys_event_harness_tests.cpp`, `tests/ui/live_resource_table_event_harness_tests.cpp`, `tests/ui/manual_sound_group_event_harness_tests.cpp`, `tests/ui/markers_event_harness_tests.cpp`, `tests/ui/menu_bar_event_harness_tests.cpp`, `tests/ui/minimap_center_event_harness_tests.cpp`, `tests/ui/note_label_inline_event_harness_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/ui/nuklear_event_harness.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/nuklear_metadata_state_tests.cpp`, `tests/ui/popup_menu_event_harness_tests.cpp`, `tests/ui/preview_audio_export_event_harness_tests.cpp`, `tests/ui/selected_sound_layers_menu_tests.cpp`, `tests/ui/snap_grid_event_harness_tests.cpp`, `tests/ui/sound_import_state_tests.cpp`, `tests/ui/sound_picker_event_harness_tests.cpp`, `tests/ui/sound_slot_batch_event_harness_tests.cpp`, `tests/ui/sound_source_copy_event_harness_tests.cpp`, `tests/ui/sound_usage_event_harness_tests.cpp`, `tests/ui/tag_grips_event_harness_tests.cpp`, `tests/ui/tap_timing_panel_event_harness_tests.cpp`, `tests/ui/timing_settings_tests.cpp`, `tests/ui/walkthrough_bms_event_harness_tests.cpp`, `tests/ui/walkthrough_osu-stepmania_event_harness_tests.cpp`.

## T-695 Convert the Compose field geometry views

- **Status:** done (2026-10-20)
- **Value:** The hottest per-frame path, so this is where the steady-state allocation test matters most.
- **Outcome:** the field geometry structs in `compose.h` are C++ views with owned lists refilled in place: rect, paste ghost, band, lane, grid line, lane layout segment, timing preview rect, dead/Kiai/speed spans, SV graph point, recipe area, difference, hold, note, cursor, timing marker, quantize ghost.
- **Read first:** the shared list.
- **Files owned:** the shared list for those structs in `src/ui/frontend/compose.h`, plus `src/sys/compose_view.cpp`, `src/sys/compose_view_internal.h`, `src/ui/nuklear/compose_canvas.cpp` and the field renderer (`src/ui/field/field_renderer.*`); coordinator-approved supporting files are enumerated in the T-695 inventory and conversion checkpoints below.
- **Steps:** 1. Inventory as in T-690. 2. Convert one primitive family per checkpoint. 3. Record frame-time and `frame_timing` heap summaries for the smoke chart before and after.
- **Reject/failure behavior:** the shared list.
- **Verify:** the shared list.
- **Invariants:** the shared list. The GPU field renderer's input stays a contiguous span.
- **Out of scope:** BGM, sound and bookmark views (T-696–T-698); `WormUiComposeView` itself (T-698).

### T-695 inventory and ownership extensions (2026-10-20)

- The stale-plan check found all 18 primitive structs still C structs, with geometry lists
  borrowed from arena spans, cache vectors or paste/quantize staging. Scene projection lives in
  `compose_scene_view.cpp`, scalar reuse and layout segments in `compose_view.cpp`, timing
  geometry in `compose_marker_view.cpp`/`compose_waveform_overlays.cpp`, and quantize ghosts in
  `compose_selection_view.cpp`. Consumers include canvas drawing, note/timing/viewport input,
  menu hit tests and frontend validation, with sys geometry/lifetime and UI harness fixtures.
- Coordinator-approved supporting ownership: `src/sys/workspace_view.h/.cpp` for retained root
  lists and exact grid rows; `src/sys/compose_scene_view.cpp`, `compose_selection_view.cpp`,
  `compose_marker_view.cpp`, `compose_waveform_overlays.cpp`, `compose_draw_source_view.cpp`,
  `compose_view.h`, `frontend_geometry.h` and `application.cpp` for their primitive signatures,
  storage and consumption. Retained graph output buffers also require
  `src/sys/compose_dead_spans.h/.cpp`, `compose_sv_graph.h/.inl`,
  `compose_recipe_areas.h/.inl` and `compose_speed_spans.h`.
- Frontend supporting ownership includes `src/ui/compose_timing_geometry.h`, the primitive
  references in `src/ui/frontend/actions.h`, `timing_preview.h` and `options_notifications.h`,
  and `src/ui/nuklear/compose_canvas.h`, `compose_panel.cpp`, `compose_timing_input.cpp`,
  `compose_recipe_menu.cpp`, `compose_state.h/.cpp`, `frontend.cpp`, `compose_input_panel.cpp`,
  `compose_viewport_input.cpp`, `compose_measure_resize_input.cpp`, `compose_note_menu.cpp` and
  `compose_object_input.cpp`. `sdl_backend.h/.cpp` owns per-surface retained tile extraction
  scratch so main and Compare surfaces have explicit, separate lifetimes.
- Regression ownership includes `tests/sys/frame_timing_tests.cpp`, `timing_lane_ui_tests.cpp`
  and the UI fixtures constructing these primitives. Core vocabulary extraction owns
  `include/wormedit/core/vocabulary.h`, `document.h` and `interaction_gestures.h`: existing
  stage, recipe-curve and move-conflict enum values move without changing their meaning.
- Baseline: populated `tests/fixtures/native/compose-smoke.wormedit`, isolated Xvfb at
  1280×720, identical 12-second pointer-motion script `/tmp/t695-profile-steps.sh`.
  Last warmed 240-frame block: **18.574987 ms average / 24.849622 ms p99**, projection
  **48 allocations / 3,646 requested bytes per frame**, scene **11 / 1,071**.
  Full phase/heap summary: `/tmp/t695-before-timing.txt`; image `/tmp/t695-before.png`.
  Wall times include software-renderer/VSync scheduling while the independent Clang build
  was active; no frame-time improvement will be inferred from this single sample.

### T-695 conversion checkpoints (2026-10-20)

- Primitive checkpoint: all 18 types use C++ names, default initialization and value equality;
  editable IDs and modeled enums use core vocabulary. Paste ghosts distinguish note glyphs from
  timing lanes. Rectangle construction and validity are members; the free rectangle converter
  and `src/sys/frontend_geometry.h` are removed. Its unused include also leaves
  `src/sys/player_preview_presenter.cpp` (supporting signature cleanup only).
- Storage checkpoint: 20 lists now belong to the persistent Compose view, with an optional cursor.
  Root reset preserves their capacity; source producers clear/refill them directly. Exact grid
  rows retain a paired vector, and graph builders reuse retained work/output buffers. Timing
  labels retain their strings; note labels and hitsound metadata bind to source identity and
  revision. Retained sound labels additionally include the existing project-session generation,
  with a same-ID/revision reopen regression. `src/sys/compose_source_key.h` owns that existing
  shared source key.
- Rendering checkpoint: main and Compare surfaces own separate retained canvas scratch through
  the SDL backend. Tile drawing copies only drawing scalars, refills the selected primitive pass,
  and consumes contiguous ranges. Actual scratch/vector/hash capacities are charged against
  retention budgets, including the shared hitsound payload once per source.
- The 15 retired macros include the real scene work budget, now
  `compose_scene_primitive_work_limit` (**32,768**) in `compose_view.cpp`;
  `compose_scene_grid_work_limit` remains **16,384**. The static source stays within **16 MiB**;
  the renderer keeps its existing **4 MiB** geometry scratch reservation. Recipe sampling
  limits and unconverted BGM/bookmark/sound bounds are unchanged. Artificial output cutoffs for
  geometry, timing spans/graphs/markers and layout segments are gone; oversized layout lists
  now show every segment. The regression explicitly covers **2,049** segments.
- Additional UI ownership from compilation/review: `src/ui/nuklear/theme.h` and `arrow_glyph.h`
  supply typed overloads for existing drawing helpers; `options_window.cpp` only consumes the
  typed marker ID. The exact UI fixture set is `tests/ui/nuklear_event_harness_tests.cpp`,
  `compose_beat_fit_tests.cpp`, `compose_measure_resize_tests.cpp`,
  `gesture_lifecycle_event_harness_tests.cpp`, `bga_lane_event_harness_tests.cpp`,
  `popup_menu_event_harness_tests.cpp`, `notification_hit_region_event_harness_tests.cpp`,
  `compose_stem_lane_render_tests.cpp`, `walkthrough_bms_event_harness_tests.cpp`,
  `theme_tests.cpp` and `nuklear_compose_state_tests.cpp`.
- User-requested Clang repair additionally owns `src/core/lane_profiles.cpp`: its three
  non-StepMania output descriptors explicitly initialize their absent policy with `std::nullopt`.
  Existing defaults and output choices are preserved.

### T-695 measurements and visual receipt (2026-10-20)

Identical populated smoke chart, window, pointer-motion script and last warmed 240-frame block:

| Metric | Before | After |
| --- | ---: | ---: |
| Frame wall average / p99 (ms) | 18.574987 / 24.849622 | 17.678153 / 21.533841 |
| Frame CPU average / p99 (ms) | 3.895152 / 5.423063 | 3.937724 / 5.453363 |
| Workspace projection allocations / requested bytes per frame | 48 / 3,646 | 47 / 3,518 |
| Compose scene allocations / requested bytes per frame | 11 / 1,071 | 10 / 879 |

The full frame still contains unrelated allocations. The populated direct geometry-producer
regression measures **zero count and byte delta** on its second refill, covering notes, holds,
custom hitsounds, long sound labels, grid/layout rows, markers, spans and graphs. CPU/wall samples
include software rendering, VSync and concurrent verification; no general timing improvement is
claimed. Full phase/heap reports are `/tmp/t695-before-timing.txt` and
`/tmp/t695-after-timing.txt`.

- Isolated Xvfb Compose captures `/tmp/t695-before.png` and `/tmp/t695-after.png` are
  **pixel-identical** (ImageMagick AE **0**), and both were inspected. Timing focus was inspected
  in `/tmp/t695-timing-focus.png`. A disposable project copy was duplicated through the real UI;
  `/tmp/t695-compare.png` shows both populated Compare fields with separate cache owners.
- Geometry/grid tests pass **14 cases / 4,407 assertions**; timing geometry passes **5 cases /
  26,930 assertions**. The dedicated populated refill test passes **14 assertions**, including
  reopened-project labels (`/tmp/t695-owned-allocation-final.log`).
- Both changed timing producer clang-tidy runs pass. Production/header complexity checks pass;
  the new timing test was simplified after its initial macro-expanded score exceeded 25.
  The old speed-graph score-29 allowance is removed from `cmake/complexity_baseline.txt` after
  reviewing the cohesive extraction; no allowance is increased. The shared arena assertion
  remains for unconverted bookmark/section views, while geometry has its own owned-storage check.
- Full `linux-clang` build and its final incremental build pass with no first-party diagnostics;
  all **4,370 Clang tests pass**, including the final session-key fix
  (`/tmp/t695-clang-ctest-session-final.log`). Existing output-catalog
  tests also pass **12 cases / 1,982 assertions**, checking the absent non-StepMania policies.
- Final GATE passes build, **4,370/4,370 tests**, whitespace and complexity across **149 changed
  translation units** (`/tmp/wormedit-gate.m1y9nR`). The separate event-harness run passes
  **591/591** (`/tmp/t695-events.log`).

## T-696 Convert the Compose BGM, waveform and stem views

- **Status:** done (2026-10-20)
- **Outcome:** the BGM lane, event, draft, bar mark/bookmark, move preview, waveform row/overview row, stem waveform and stem cut views are C++ views.
- **Read first:** the shared list.
- **Files owned:** the shared list for those structs, plus `src/sys/compose_bgm_bar.cpp`, `src/sys/bmson_waveform_view.cpp` and `src/sys/waveform_view.cpp`.
- **Steps:** 1. Inventory. 2. Convert one family per checkpoint.
- **Reject/failure behavior:** the shared list.
- **Verify:** the shared list.
- **Invariants:** the shared list. Waveform pyramids stay owned by the audio cache; views borrow their rows for the frame.
- **Out of scope:** waveform generation.

### T-696 inventory and ownership (2026-10-20)

- **Types:** BGM lane/event/draft/move preview, waveform and overview rows, BGM bar marks/bookmarks, stem waveform pieces and cuts. The root owns eleven retained vectors and an optional move preview. Stem piece spans borrow retained audio projection rows; waveform pyramids remain audio-cache-owned.
- **Supporting ownership (coordinator):** `src/sys/application_frame_view.cpp`, `src/sys/bmson_waveform_view.cpp`, `src/sys/compose_draw_source_view.cpp`, `src/sys/compose_scene_view.cpp`, `src/sys/compose_view.cpp`, `src/sys/compose_view.h`, `src/sys/compose_view_internal.h`, `src/sys/stem_waveform_window.h`, `src/sys/waveform_view.cpp`, `src/sys/workspace_view.cpp`, `src/sys/workspace_view.h`, `src/ui/compose_timing_geometry.h`, `src/ui/frontend/common.h`, `src/ui/frontend/compose.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_canvas.h`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_note_menu.cpp`, `src/ui/nuklear/compose_object_input.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/compose_viewport_input.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/sdl_backend.cpp`, `tests/sys/frame_timing_tests.cpp`, `tests/ui/bga_lane_event_harness_tests.cpp`, `tests/ui/bmson_lane_mode_event_harness_tests.cpp`, `tests/ui/compose_beat_fit_tests.cpp`, `tests/ui/compose_bgm_layer_tests.cpp`, `tests/ui/compose_hover_timing_event_harness_tests.cpp`, `tests/ui/compose_stem_lane_render_tests.cpp`, `tests/ui/gesture_lifecycle_event_harness_tests.cpp`, `tests/ui/minimap_center_event_harness_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/nuklear_sdl_backend_tests.cpp`, `tests/ui/playback_status_event_harness_tests.cpp`, `tests/ui/walkthrough_bms_event_harness_tests.cpp`. These paths cover declarations, producers, persistent reset, cache accounting, canvas/input consumers and existing regression fixtures. `memory/chart-editor-status/resume.md` advances the queue after verification.
- **Parallel ownership:** waveform/stem producer agent owns `src/sys/waveform_view.cpp`, `src/sys/bmson_waveform_view.cpp`, retained enumeration overloads in `src/sys/stem_waveform_window.h` and the direct-projection cache in `compose_view.h`; consumer agent owns the affected UI paths and UI tests; regression agent owns `tests/sys/frame_timing_tests.cpp`. Coordinator owns shared declarations, remaining sys producers, cache/reset integration, ratchets, documentation and build writes.
- **Storage versus work:** retire eight BGM/waveform storage-capacity macros and the stem-cut 4096 cutoff. Preserve the audio alignment 4096-row work limit, 512 overview samples, 1024 BGM bar samples/measure-mark sampling budget, scene primitive budget and retained CPU/GPU memory budgets at their consumers. BGM bar bookmarks and BGM lane/event view storage no longer truncate independently.
- **Warm ownership:** scene text borrows source-stamped retained labels/filenames. BGM mix and visible stem/packed-piece enumeration use retained scratch; interval queries reuse index scratch. Canvas static passes retain only their selected BGM geometry and drawing metadata.
- **Reviewed scope extension:** `include/wormedit/audio/block_cache.h`, `src/audio/block_cache.cpp` and `tests/core/audio_block_cache_tests.cpp` add a mutex-protected content revision for direct sample-point projection invalidation. Equal-sized PCM replacement and source removal can leave entry/frame/eviction totals unchanged; the new retained projection must still refresh. This changes cache observability, not waveform generation. The direct projection key also includes session, chart/timing, waveform source, viewport and style. A focused selection-switch regression covers rebuilding stem pieces when selected-channel status changes without scrolling.


- **Allocation-driven supporting scope:** `src/core/timing.cpp` replaces temporary vectors in private exact-rational factor cancellation with deduced stack arrays, retaining every caller and exact result. `include/wormedit/audio/source_registry.h`, `src/audio/source_registry.cpp`, `src/sys/project_audio_sources.h` and `src/sys/project_audio_sources.cpp` add retained-output binding queries; existing return-by-value APIs delegate. Stem projection retains the output and its owned string capacities. These changes remove allocations traced from the actual warmed projection paths; waveform generation remains unchanged.

### T-696 verification receipt

- Initial documentation: **5/5 passed** (`/tmp/t696-doc-initial.log`), ratchets **36 C structs / 22 capacities / 74 count fields / 0 codecs**.
- Default `clang-tidy -p build` on `src/audio/block_cache.cpp`, `src/sys/compose_scene_view.cpp`, `src/sys/compose_view.cpp`, `src/sys/waveform_view.cpp` and `src/sys/bmson_waveform_view.cpp`: exit 0, no diagnostics (`/tmp/t696-tidy-production.log`).
- Focused allocation/ownership regressions: **6 cases / 346 assertions passed** (`/tmp/t696-owned-tests2.log`), including zero warmed allocation count and bytes for populated BGM/waveform/stem and direct sample-point paths. Source/style/viewport changes, equal-sized PCM replacement/removal and selected-channel switching refresh the appropriate retained results. The retired BGM storage cutoff test publishes all 32,769 admitted events; scene admission keeps its separate primitive budget.
- Cache content revision: **1 case / 15 assertions passed** (`/tmp/t696-cache-revision-test.log`). Additional default clang-tidy on `src/core/timing.cpp`, `src/audio/source_registry.cpp` and `src/sys/project_audio_sources.cpp` exits 0 with no reported findings (`/tmp/t696-tidy-allocation-fixes.log`).
- `cmake --preset linux-clang`, preset build and `ctest --preset linux-clang -j 4 --output-on-failure`: **4,373/4,373 passed** (`/tmp/t696-clang-build-final.log`, `/tmp/t696-clang-ctest.log`). Separate final event harness: **591/591 passed** (`/tmp/t696-events-final.log`).
- Final isolated Xvfb captures inspected: `/tmp/t696-after.png`, `/tmp/t696-waveform-after.png`, `/tmp/t696-stem-after.png`, `/tmp/t696-bar-after.png`. Compose, populated waveform and stem captures are pixel-identical to the original binary. The song-bar layout has two differing RGB pixels at a stem-waveform edge; its song-bar rows and layout are identical. Raw comparison: `/tmp/t696-visual-comparison.txt`. Fixtures: tracked `tests/fixtures/native/compose-smoke.wormedit` and disposable existing `/tmp/feedback-waveform/chart.wormedit`, `/tmp/bgm-mix-stem-project/stem-mix-verification-1.wormedit`; bar toggle script `/tmp/t696-bar-steps.sh`.
- Test complexity review extracted coherent cache lifecycle and selected-stem publication helpers without weakening assertions. `cmake/complexity_baseline.txt` lowers the reviewed existing stem-test allowance **42 → 38**; no allowance increases. Focused comparison passes both affected test files (`/tmp/t696-test-complexity.log`). Final Clang rebuild/test after this test-only extraction again passes **4,373/4,373** (`/tmp/t696-clang-build-tests-final.log`, `/tmp/t696-clang-ctest-final.log`).
- Final `scripts/gate.sh --jobs 4`: **passed** — build, **4,373/4,373 tests**, whitespace and complexity across **34 changed translation units** (`/tmp/t696-gate3-run.log`, `/tmp/wormedit-gate.x9DjjZ`, complexity details `/tmp/wormedit-complexity.as8lp5kh`). No new/increased complexity violations; the single reviewed allowance is lowered to 38.

## T-697 Convert the Compose sound, keysound and BGA views

- **Status:** ready
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
