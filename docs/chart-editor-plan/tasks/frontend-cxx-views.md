# Frontend C++ views: close the boundary migration

[Back to task cards](../tasks.md).

The C ABI stopped being a requirement on 2026-10-17 ([code-style.md, "Frontend boundary"](../decisions/code-style.md)). Until now the move to plain C++ view types was gradual and held by four ceremony-ratchet rows. On 2026-10-18 the owner asked for a closing lane instead, under the rule "A migration finishes in its card, or it ratchets". These cards convert every remaining `WormUi*` C surface and take the rows to 0. They also replace [T-653](state-ownership.md#t-653-map-each-c-abi-type-through-one-ui_codec): a C++ view needs no codec.

**Counts at `7160520`:** 133 `typedef struct WormUi` (ratchet `frontend_c_struct`), 77 `WORM_UI_*_CAPACITY` (`frontend_fixed_capacity`), 183 `*_count;` fields (`frontend_pointer_count_field`), 1 `ui_codec<WormUi…>` (`frontend_ui_codec`). By header: `compose.h` 64 structs and 93 count fields; `metadata_audio.h` 13 and 24; `timing_preview.h` 13 and 22; `project.h` 10 and 12; `options_notifications.h` 9 and 7; `actions.h` 8 and 16; `runtime.h` 6 and 1; `workspace.h` 6 and 8; `catalog.h` 4 and 0. `common.h` holds 69 of the capacities.

**Order:** T-690 first, because it proves the recipe on the smallest surfaces. T-691–T-694 can then run in parallel when their sys and UI files don't overlap; coordinate `application_frame_view.*` and `src/ui/nuklear/frontend.cpp`. T-695–T-698 split `compose.h` and run one at a time, because they share `src/sys/compose_view.cpp` and `compose_panel.cpp`. T-699 closes the lane.

## Conversion recipe (every card)

The worked example is T-672: `compose_measure_lane_view_t` in `src/ui/frontend/compose_measure_lane.h`.

1. **Type:** a C++ struct named `<area>_<thing>_view_t` (or `_request_t` / `_action_t` for payloads) replaces the C struct. It uses core vocabulary from `core/vocabulary.h` (enums, strong IDs, `rational_t`) instead of raw integers. It holds no document, history, index or editor state, and includes no backend header (`cmake/check_ui_boundary.cmake`).
2. **Lists:** an owned `std::vector` replaces each pointer-plus-count pair and its capacity. Text becomes `std::string`, or `std::string_view` when the producer owns stable text for the frame.
3. **Filling:** sys refills the view in place each frame (`clear()`, then push), so steady state allocates nothing. Nothing may copy a view per frame. A view that becomes non-trivially copyable leaves `frame_span` storage.
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

- **Status:** ready
- **Value:** Proves the recipe on 16 small structs before the large surfaces.
- **Outcome:** `catalog.h`, `workspace.h` and `runtime.h` contain no `typedef struct WormUi`; their count fields and their capacities in `common.h` are gone.
- **Read first:** the shared list, plus `src/sys/workspace_view.h` (`frame_span`).
- **Files owned:** the shared list for `src/ui/frontend/catalog.h`, `workspace.h` and `runtime.h`.
- **Steps:**
  1. Stale-plan check and inventory: `grep -rn "WormUi<Name>" src tests` for each struct in the three headers; record producers, consumers, the capacities each uses and the `frame_span` storage behind each list.
  2. Convert per the recipe, one header per checkpoint.
  3. Record any new conventions the recipe lacked as a proposed addition on this file's recipe section (coordinator transfers it).
- **Invariants:** the shared list.
- **Reject/failure behavior:** the shared list.
- **Verify:** the shared list.
- **Out of scope:** the other headers (T-691–T-698); deleting `frame_span` itself (T-699).

## T-691 Convert the project and options/notifications surfaces

- **Status:** blocked-on T-690
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

## T-692 Convert the metadata and audio surfaces

- **Status:** blocked-on T-690
- **Outcome:** `metadata_audio.h` contains no C structs. The metadata text capacities (`WORM_UI_METADATA_*`) are either deleted or turned into the format limits that the metadata core already validates.
- **Read first:** the shared list.
- **Files owned:** the shared list for `src/ui/frontend/metadata_audio.h`, plus `src/sys/metadata_view.cpp` and `src/sys/metadata_actions.cpp`.
- **Steps:** 1. Inventory as in T-690, and for each `_BYTES` or `_CAPACITY` limit, note whether core validation already enforces it. 2. Convert. 3. Edits that used to be rejected by a UI buffer limit are still rejected by core with the same diagnostic, or the difference is recorded for the owner.
- **Invariants:** the shared list.
- **Reject/failure behavior:** the shared list.
- **Verify:** the shared list.
- **Out of scope:** metadata form behavior and validation rules.

## T-693 Convert the timing and preview surfaces

- **Status:** blocked-on T-690
- **Outcome:** `timing_preview.h` contains no C structs, and `ui_codec<WormUiTimingLaneKind>` is gone (`frontend_ui_codec` 0).
- **Read first:** the shared list.
- **Files owned:** the shared list for `src/ui/frontend/timing_preview.h`, plus `src/sys/timing_view.cpp`, `src/sys/timing_lane_ui.h` and `src/sys/player_preview_presenter.cpp`.
- **Steps:** 1. Inventory as in T-690. 2. Convert; the timing-lane kind becomes the core enum. 3. Delete the codec and its callers' conversions.
- **Reject/failure behavior:** the shared list.
- **Verify:** the shared list.
- **Invariants:** the shared list. Player-preview frame pacing ([frame-pacing.md](frame-pacing.md)) must not regress; record `frame_timing` heap summaries before and after.
- **Out of scope:** timing edits themselves (proposal path).

## T-694 Convert the action payloads

- **Status:** blocked-on T-690
- **Outcome:** `actions.h` contains no C structs; action payloads are C++ types the frontend fills and sys reads with `T::from`, with no fixed-size arrays.
- **Read first:** the shared list.
- **Files owned:** the shared list for `src/ui/frontend/actions.h`, plus the sys dispatchers that read each payload (step 1).
- **Steps:** 1. Inventory every payload with its writers in `src/ui/nuklear` and readers in `src/sys`. 2. Convert one action family per checkpoint. 3. Payload IDs use strong ID types.
- **Reject/failure behavior:** the shared list.
- **Verify:** the shared list.
- **Invariants:** the shared list. Dispatch routing, stale-stamp checks and diagnostics are unchanged.
- **Out of scope:** what any action does; command routes (`application_command_route_t`).

## T-695 Convert the Compose field geometry views

- **Status:** blocked-on T-690
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
- **Outcome:** all four frontend ratchet rows are 0 and their rows are deleted. `common.h` keeps only real bounds and SDL forward declarations. `frame_span` storage for frontend views is gone. The `ui_codec` naming row and T-653 are retired, and code-style.md's "Frontend boundary" says the migration is finished.
- **Read first:** this file, `src/sys/workspace_view.h`, `src/sys/application_frame_view.*`, code-style.md ("Frontend boundary", naming table).
- **Files owned:** `src/ui/frontend/common.h`, `src/ui/frontend.h`, `src/sys/workspace_view.h/.cpp`, `src/sys/application_frame_view.h/.cpp`, `cmake/ceremony_ratchet.txt`, `docs/chart-editor-plan/decisions/code-style.md`, `docs/architecture.md`, T-653's status line in `state-ownership.md`, this card.
- **Steps:**
  1. Stale-plan check: all four rows read 0. `grep -rn "frame_span" src` lists only users that are not frontend views (`src/core/arena.cpp`, storyboard preview); record them.
  2. Delete frontend-only frame storage and dead capacities; keep the arena for its remaining users.
  3. Delete the ratchet rows. Update code-style.md (migration finished; remove the `ui_codec` row; C++ views are the only form) and the "Where to look" table in `docs/architecture.md`. Set T-653 to `superseded by T-690–T-699`.
- **Invariants:** no behavior change; same frame-time budget.
- **Reject/failure behavior:** a remaining user of a deleted storage type stops the step; record it and keep the type.
- **Verify:** GATE; `ctest --test-dir build -R '^documentation\.'`; the coordinator's Xvfb smoke of every workspace tab.
- **Out of scope:** replacing Nuklear (the Nuklear audit decides that).
