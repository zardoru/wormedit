# Type, signature and cohesion audit (2026-10-12)

Read-only audit at commit `9087b40`. It follows the [gesture/verb audit](gesture-redundancy-2026-10-11.md).
That audit covered *workflows*; this one covers the *vocabulary* a reader has to learn:
- types and structs;
- parameter lists;
- copy-pasted helpers;
- how code is spread across files.

The goal is to make the code easier to navigate for someone who did not write it, **without changing
performance characteristics**. Every proposal here is a stack-only reference bundle, an alias, a
merge of identical code, or a file move. None adds heap allocation, virtual dispatch, `std::function`,
or a large copy on a per-frame or per-note path. Several proposals remove per-event work.

Marks: **V** = verified by Claude in source; **A** = agent finding, plausible, re-check before
editing; **R** = rejected or downgraded (see the end).
Probes:
- six Haiku agents: core contexts, projection contexts, twin types, helper copies, action
  vocabulary, chart scope;
- four Codex Luna agents: god-structs, module cohesion, dispatch layers, formats;
- AST mining with libclang over all 311 source TUs (8,784 function and 2,006 struct definitions).

## Summary

The architecture is sound: core, formats, sys and ui are separate layers, and the `WormUi*` C ABI
is a deliberate boundary. The difficulty is at the micro level, and it comes from five sources:

1. **No named "editing context" (EC, AC, PC).** The same 3–7 objects travel through hundreds of
   signatures:
   - `document_history_t + editor_interaction_state_t`: 109 functions;
   - `document_history_t + workspace_controller_t`: 117;
   - `chart_document_t + measure_index_t`: 159.

   Code that did bundle them did so locally. That produced 101 `*_context_t` structs, of which
   ~30 re-declare the same references. Twelve of them sit in `interaction.cpp` alone.
2. **Complexity-rule artifacts (CR).** `bb59e89` "Reduce C++ cognitive complexity across project"
   (2026-09-29, 88 files, +42.9k/−35.7k) satisfied the 25-point limit in places by moving
   branches into lambdas and `std::function` members. clang-tidy scores each lambda as its own
   function, so the metric passes, but the reader still faces one 415-line function. The worst
   case is the core action dispatcher.
3. **Helpers copied instead of shared (HL).** For example:
   - `error()` is defined in 28 files and `reject()` in 25;
   - `find_chart` in 10 places, with none in a header;
   - `effective_layout` in 6;
   - checked-math helpers in about 27 copies.

   Most copies are identical; a few have drifted.
4. **Shape twins (TW, AV).** Identical structs exist under different names: diagnostics, rects,
   result types, update/release action payloads, and the command registration/storage pair.
5. **Large files that mix families (MC, GS).** These are `interaction.cpp`, `compose_view.cpp`,
   `main.cpp`, `compose_input.cpp`, `compose_actions.cpp` and `frontend.h`. There are also four
   god-structs with 113–433 fields. These need the most churn and give the least payoff per
   line, so they come last.

## Root causes and findings

### EC — Core editing context (highest value)

| ID | Mark | Finding | Sites |
|---|---|---|---|
| EC1 | V | Twelve file-local contexts in core interaction re-declare `{history&, state&, const chart&[, const index&]}`. Of these, 9 also carry the index. | `interaction.cpp:2272, 2481, 3166, 3572, 3669, 4322, 4418, 4724, 4760, 4962, 5028`; `interaction_cursor.h:9` |
| EC2 | V | `dispatch_editor_action_impl` (`interaction.cpp:5629–6043`, 415 lines) builds **all** 12 contexts and ~45 `handle_*` lambdas on **every** action, before it knows which one it needs. Several contexts hold `std::function` members that forward to functions in the same file. | `interaction.cpp:5648–5950` |
| EC3 | V | The dispatcher ends in `switch (action.index())` with bare `case 0:`…`case 41:`. Nothing guards it: no `static_assert`, and the `default:` arm suppresses `-Wswitch`. Reordering `editor_ui_action_t` silently miswires actions. This is the only `switch` on a variant index in the tree. | `interaction.cpp:5951–6040` |
| EC4 | V | The identical cancel lambda, `dispatch_editor_action(history, state, cancel_editor_gesture_action_t{pointer_capture_lost})`, is written 5 times. | `interaction.cpp:5690, 5698, 5722, 5812, 5831` |
| EC5 | A | Each nested dispatch (cancel hooks, hold `dispatch_action`, scratch dispatches at `3396, 3724, 3736, 4626`) re-runs `make_view`, `find_chart` (twice: `5637`, `5644`), `build_measure_index` and all 12 contexts. **Keep the rebuild after a mutation**, because the document changed. The waste is building contexts the handler never uses. | `interaction.cpp:5637–5645` |
| EC6 | V | `find_chart` is called twice at dispatch entry with the same arguments. | `interaction.cpp:5637, 5644` |

**Proposal.** Add `src/core/interaction_context.h` (internal) with:

```cpp
struct chart_scope_t {                       // see CS1; also usable outside interaction
    const chart_document_t& chart;
    const measure_index_t& measures;
};
struct editor_edit_context_t {
    document_history_t& history;
    editor_interaction_state_t& state;
    chart_scope_t scope;
};
```

- Feature contexts that need extras extend this context and keep only those extras: `initial_view`
  and `absolute` for space/area selection, accumulators for move/resize.
- Replace the `std::function` hooks with plain free functions that take `editor_edit_context_t&`.
  The callbacks only exist to call back into this file.
- Replace the lambda table and index switch with one `std::visit` over an overload set of
  **named free functions**, `handle(editor_edit_context_t&, const place_tap_action_t&)` and so on.
  The compiler then checks the visit is exhaustive. Each handler stays under the complexity limit
  on its own, and no per-action context or closure is built for unrelated actions.
- **Perf:** this strictly reduces per-event work (no 12-context/45-closure/`std::function`
  construction per pointer event). The measure-index build per dispatch is unchanged; caching it by
  revision is out of scope.

### AC — Application dispatch context (sys)

| ID | Mark | Finding | Sites |
|---|---|---|---|
| AC1 | V | `workspace_frame_action_context_t` is a struct local to `main()` with 36 `decltype(x)& x_ref` members. It hides real types from the reader and from grep. | `main.cpp:4446–4485` |
| AC2 | V/A | The downstream contexts re-bundle overlapping parts of the same frame state: `compose_input_context_t` (16 fields), `compose_action_context_t` (13), `application_command_context_t` (7), `bmson_compose_context_t`, `compose_gesture_context_t`, `notification_frame_context_t`, `AudioOffsetDragContext`. The shared core is `{history, workspace, interaction_state, application_state, notifications}`. | `compose_input.cpp:1259`; `compose_actions.cpp:85`; `application_command_dispatch.cpp:156`; `compose_input.cpp:847, 1417`; `main.cpp:731, 3639` |
| AC3 | V | Unpacked parameter lists rebuild what the contexts already hold: `dispatch_compose_workspace_action` (14 params), `dispatch_compose_view_action` (12), `dispatch_registered_application_command` (11), `dispatch_project_export_action` (11), `dispatch_metadata_action` (11), `poll_frontend_events` (11), `dispatch_compose_input_action` and its `_impl` (10 each), `dispatch_project_chart_action` (10). The quad `WorkspaceAction + history + notifications + workspace` appears together in 28 functions. | `compose_actions.cpp:3507, 3572, 3606`; `main.cpp:1622, 4023`; `metadata_actions.cpp:389`; `compose_input.cpp:4231, 4408` |
| AC4 | A (Luna) | The dispatch hops themselves do real work: tab, chart, revision and capacity validation; routing; notifications. The pointer-to-core path has 10 hops and none is a pure forwarder. **Keep the hops; remove the re-passed environment.** | `compose_input.cpp:1259–4408` |

**Proposal.**
- Add `src/sys/application_dispatch_context.h`. It is a stack-only struct of references, built once
  per frame in `main()` from the live objects, with real type names. It holds:
  - history, workspace, compose interaction state (`std::optional<...>&`), application command
    state and notification sink;
  - asset base, resource-copy scheduler/runtime, export runtime and command registry;
  - preferences runtime, project-chart runtime, metadata asset runtime/drop state;
  - compose scene cache and viewport/navigation input controllers.
- It replaces `workspace_frame_action_context_t`. Each downstream context keeps only its
  event-specific fields (`ui_action`, derived geometry) and refers to the shared one.
  `command_invocation_context_t` stays separate, since it is a registry request with a focus
  snapshot.
- Lifetime: one synchronous frame dispatch. It must never be captured by callbacks or worker jobs.

### PC — Per-frame projection contexts (sys)

| ID | Mark | Finding | Sites |
|---|---|---|---|
| PC1 | A | Five compose projection contexts overlap. `flattened`, `chart` and `downscroll` appear in all 5; `view`, `history` and `application_state` in 3–4. | `compose_view.cpp:827, 2254, 3616, 3750`; `compose_waveform_overlays.cpp:68` |
| PC2 | V | The `view` member is always `flattened.view.compose`. The line `auto& view = flattened.view.compose;` appears 10 times. | `compose_view.cpp:1499, 1745, 3493, 5013, 5163 …` |
| PC3 | A | Three quantize functions with 10–11 params each thread the same `(index, request, lane_orders, pixels_per_row, scroll, height, downscroll)`. The `unordered_map<const layout_source*, vector<const lane_definition_t*>>` type is spelled out five times. | `compose_view.cpp:3158, 3180, 3212` |
| PC4 | V | `resolve_compose_paint_cell` has an unused parameter, `/*dead_spans*/`, at 3 call sites. | `compose_view.cpp:62–66` |
| PC5 | V | `project_application_frame(application_frame_projection_context_t context)` takes its 30-field context by value on every frame. | `application_frame_projection.h:70` |
| PC6 | A | `pixels_per_row` is recomputed as `pixels_per_standard_measure / divisions` at 7 sites. Verify that the scene-path value matches before deduplicating. | `compose_view.cpp:529, 2269, 2323, 3791, 3899, 4466, 4679` |

**Proposal.**
- Add `compose_frame_inputs_t`: flattened, history, cache, retained state, application state,
  timing stage, comparison rows, BGA move preview.
- Add `compose_chart_scope_t`: `chart_scope_t`, interaction view, downscroll and pixels per row.
- Keep the phase split: the interaction context is built before the interaction view exists. Each
  phase context holds `frame + scope + its own extras`.
- Add `quantize_projection_scope_t` and a named alias for the lane-order map.
- Pass the frame context by `const&`.

### CS — Chart scope and chart lookup (core-wide)

| ID | Mark | Finding | Sites |
|---|---|---|---|
| CS1 | A | `chart_document_t + measure_index_t` travel together in 159 functions (core 125, sys 24, formats 10), and 34 structs embed both. The index is always built from the same chart; the one divergence, `bpm_editor.cpp:583`, is benign. | full list in probe output; examples: `snap.cpp:11, 83, 97`; `chart_clipboard.cpp:25`; `timing.cpp:1201` |
| CS2 | V | `find_chart` is defined 10 times: `audition_resolver.cpp:388`, `audio_offset_editor.cpp:86`, `channel_map_editor.cpp:193` (iterator), `chart_editor.cpp:162` (iterator), `workspace.cpp:150`, `interaction.cpp:83/89`, `bookmark_chooser_projection.cpp:20`, `compose_bookmark_commands.cpp:22` (takes history), `compare_view.cpp:17`. None is in `include/`. There are 70 calls, plus aliases `chart_for`, `chart_by_id`, `chart_for_block`, `selected_chart` and ~132 inline `ranges::find` over charts. | as listed |
| CS3 | A | The "chart id + revision + measure index" cache-validity rule is hand-written in ~5 places; the compose cache reset spans ~30 fields. | `compose_view.cpp:3523`; `timing_view.cpp:259`; `interaction.cpp:2071`; `editor_scene.h:71`; `interaction.h:1060` |
| CS4 | A | `resource_assignment.cpp:1045` and `:1064` build the same index twice in one function. | |

**Proposal.**
- Declare `find_chart` (const and mutable overloads) in `document.h` and define it once. Delete
  the copies and aliases.
- Add `chart_scope_t` (EC) for new code and contexts. Migrate `snap.h`, interaction internals and
  timing helpers opportunistically, not as a big-bang over all 159 signatures.
- **Do not** bundle `song + chart`: the chart is often a working copy (`baked`, `working`,
  `candidate`), not the one contained in the song.

### HL — Copy-pasted helpers

| ID | Mark | Finding | Proposal |
|---|---|---|---|
| HL1 | A | `error(code, msg[, object])`: 28 definitions (18 identical, 9 with object); `reject(result&, code, msg)`: 25 definitions, 8 identical. About 196 inline `push_back(error(...))` calls. | Put one `error` in `document.h` beside `diagnostic_t` and one `reject` in `command.h` beside `document_command_result_t`. Migrate **atomically**: a surviving local copy makes unqualified calls ambiguous. Keep the timing and audition overloads, since their return types differ. |
| HL2 | V | `effective_layout`: 6 copies, the same algorithm (`interaction.cpp:310`, `interaction_selection.cpp:33`, `interaction_hit_test.cpp:52`, `interaction_cursor.cpp:36`, `document.cpp:628`, plus the quantize-geometry variant `quantize.cpp:253`). `editable_lane`: 5 identical copies. `position_at`: 4 forwarders to `snap.h` `position_at_absolute_row`. `maximum_viewport_scroll`: 3 copies. | Keep one inline definition each in a core header. The quantize variant takes a row-of template callable, not a `std::function`; this is on the hit-test path. |
| HL3 | V | Object-ID allocation: `visit_chart_object_ids` ×3, `next_chart_object_id` ×8 in two forms, `chart_object_id_count` ×6 (exact vs. capped at 2, both intended), `object_id_allocation_t` ×5 in two shapes. **All 7 scanners cover the same 16 vectors today**, so there is no live collision (`sv_recipes` use `recipe_id_t`). The risk is drift when a vector is added. | One header template `visit_chart_object_ids(chart, Visitor&&)`, one allocation struct, and one `next_chart_object_id(chart, limit = max)`. |
| HL4 | A | Checked math, ~27 copies: `magnitude` ×5 although `timing_arithmetic.h:18` already exists (`timing.cpp` shadows it); `checked_multiply` ×9 (optional and bool-out forms); `checked_add` ×7; `checked_signed_multiply` ×3; `checked_lcm` ×4. **`stepmania.cpp:132` lcm returns 0 on zero input; the other copies return nullopt.** | Add `src/core/checked_math.h` (inline constexpr). Preserve the StepMania zero case at its call site unless a deliberate fix is recorded. |
| HL5 | A | Text helpers: `valid_utf8` ×10 (bool vs. first-invalid-offset), `path_from_utf8` ×5 plus inverse ×4, `ascii_fold` ×3, identical `parse_unsigned`/`parse_integer` pairs. `trim` ×5 **with different whitespace sets** (StepMania strips CR/LF, osu! strips CR, storyboard strips space/tab only). | Add `src/core/text_util.h`, and put `parse_integer<T>` in the existing `numeric_text.h`. Merge `trim` only after a per-format fixture check. Keep the strict `bms_clipboard` variants and the measure `parse_unsigned` overflow enum. |
| HL6 | V | `diagnostic_feedback_severity` ×4 and `set_feedback_severity` ×4 are byte-identical (`compose_label_search`, `compose_measure_commands`, `compose_selection_transform`, `recipe_form`). The shared `projection_diagnostic_severity` already exists (`projection_notification_feedback.h:26`); a sixth copy is in `workspace_edit_feedback.h:19`. Luna L2 adds that notification raising has no sys home. | Use one mapping. Make `projection_notification_feedback.h` (or a renamed `notification_feedback.h`) the documented sys entry point for raising feedback. |
| HL7 | A | The UI compose action `raise()` is copied into 5 panels. The measures panel skips `compose_input_project_id`. | Add a UI-only `stamp_compose_input` / `raise_compose_action` helper. Confirm whether the missing project id is intended. |
| HL8 | A | `validate_position` ×4 with the same intent; the `marker_navigation.cpp:125` copy is an **unrelated** function with the same name (it returns a row). | One geometry validator; rename the marker-navigation one. |

### TW — Shape twins

| ID | Mark | Finding | Proposal |
|---|---|---|---|
| TW1 | A | Four object diagnostics are identical apart from the code enum: `diagnostic_t`, `timing_diagnostic_t`, `sound_slice_diagnostic_t`, `audition_resolve_diagnostic_t`. Four format diagnostics `{severity, code, message, std::string path}` (`native_project.h:21`, `bms_writer.h:29`, `bms_branch_selection.h:27`, `bms.h:87`). The StepMania and osu! parse diagnostics are identical, and so are their write diagnostics. | `object_diagnostic_t<Code>`, `path_diagnostic_t<Code>`, `text_parse_diagnostic_t<Code>`, with the old names kept as aliases. Keep each code enum. Keep `native_project_storage_diagnostic_t` (it uses `fs::path`) and the per-format location fields. |
| TW2 | V | `editor_field_rect_t == player_preview_rect_t` in core and `WormUiComposeRect == WormUiPlayerPreviewRect` in the ABI. The `convert_rect` lambda is copied 4× in `compose_view.cpp` (`1016, 1115, 1237, 1454`), plus `compose_waveform_overlays.cpp:99` and `player_preview_presenter.cpp:158`. | Core `rect_t` with aliases, and one `to_frontend_rect()`. Keep the ABI structs; a `typedef` between the two identical ABI rects is layout-neutral. |
| TW3 | V | The compose and metadata file-dialog slots are identical code under two names: the state struct plus begin/complete/take/open (`compose_state.cpp:163, 283–322`; `metadata_state.cpp:11, 181–260`). | `file_dialog_slot_t` in `src/ui/nuklear/file_dialog_slot.h`. |
| TW4 | V | `command_registration_t` and `command_t` share 14 of 15 fields. `register_command` copies all 14 by hand (`command_registry.cpp:410–424`). Only `current_shortcuts` differs: optional on input, resolved on storage. | `command_definition_t definition` as a member of both. |
| TW5 | V | `hold_placement_plan_t` (`interaction.cpp:757`) and `pending_hold_placement_preview_t` (`interaction.h:491`) are identical except for how `removals` is held. The converter copies the vector into `make_shared` on every hold-preview update. | One type; the plan builds the shared vector once (this removes a copy). |
| TW6 | A | The SDL preview and audition start results and status enums are identical. The preview and audition session render results, with their 8-value status enums, are identical too. | Template or alias in `audio/session_render.h`. Keep the session classes. |
| TW7 | A | `worm_ui_nuklear_compose_color_t` is `nk_colorf` and is used by one panel. `channel_map_apply_result_t` equals `lane_migration_apply_result_t`. `id_source_t` is duplicated in `stepmania.cpp:47` and `bmson.cpp:437`. | Delete, template, or move to one home respectively. Low priority. |

### AV — Action vocabulary

| ID | Mark | Finding | Proposal |
|---|---|---|---|
| AV1 | V | Identical update/terminal payload pairs in `editor_ui_action_t`: note duration (`interaction.h:180/184`), note move (`197/207`), area selection (`320/326`), semantic paint (`379/385`). Alt-pointer and note-paint differ only by `source_geometry`. `compose_input.cpp:920–923` picks release vs. update with a ternary over identical arguments. The terminal phase has three names: `release_`, `end_`, `commit_`. | One vocabulary: begin / update / commit / cancel. Merge the identical pairs into `<family>_update_action_t{phase, payload}` (42 → 38 alternatives). Keep the distinct `begin_*` payloads, which carry data update does not. |
| AV2 | V | `toggle_semantic_layer_membership_action_t` has **no production constructor** (only the variant entry and 1 test). assign/remove/toggle/add-selected share one payload. A file-local `membership_operation_t` already exists (`semantic_layer_editor.cpp:282`). `interaction.cpp:4942–4951` builds an assign and then reassigns the variant, copying vectors. | Add `semantic_layer_membership_edit_t{…, operation}`, promote the enum and drop the orphan toggle alternative. Keep clear and replace. |
| AV3 | A | `insert_` and `replace_format_metadata_field_action_t` have the same payload. sys already maps a C enum onto them. | One field edit with a mode; keep erase. |
| AV4 | A | The variant is named `editor_ui_action_t` but lives in `interaction.h` and is dispatched by `dispatch_editor_action`, which returns `editor_interaction_result_t`. Three names exist for one concept. | Rename to `editor_interaction_action_t`. Mechanical, but ~100 source and ~576 test references. |
| AV5 | A | Small candidates: `workspace_select_audio_asset`/`workspace_select_visual_asset` → one action with a slot enum; recovery inspect/discard/quarantine → one action with a kind enum. | Low priority. |

### MC / GS — Module cohesion and god-structs (largest churn, do last)

| ID | Mark | Finding |
|---|---|---|
| MC1 | A | `frontend.h` (3,319 lines) is the intended ABI, but it holds every surface. Split it into per-surface C headers (workspace/project, metadata/audio, compose, timing/preview, notifications/options) with `frontend.h` as an umbrella, keeping layout unchanged. |
| MC2 | A | `interaction.cpp` (6,146 lines): move the placement verbs (`place_tap`, hold planning, virtual BMS placement) to `note_placement.cpp`, move/resize planning to `note_move.cpp`, gesture lifecycles to `interaction_gestures.cpp`, and area selection into `interaction_selection.cpp`. Do this **after EC**, so the moved code already takes `editor_edit_context_t`. |
| MC3 | A | `compose_view.cpp` (5,240), `compose_input.cpp` (4,469), `compose_actions.cpp` (3,699) and `main.cpp` (4,799) each hold several action or projection families. Keep the coordinator and dispatcher in place and move each family to its own file (waveform/fit input, timing-lane gestures, resource import/export, options/shortcuts, project/chart import, audio-offset actions…). Do this **after AC/PC**. |
| GS2 | V | **Dead and write-only fields exist** (this corrects Luna L1, which found none). Eight fields are declared and never accessed anywhere in `src/`:<br>• `compose_state.h:465–467`: `semantic_layer_name_id`, `semantic_layer_name_revision`, `semantic_layer_color_id`;<br>• `compose_state.h:642–643`: `resource_import_path`, `resource_import_error`;<br>• `frontend.h:3067, 3069`: `compose_sound_group_id`, `compose_sound_group_name`;<br>• `compose_view.h:480`: `semantic_layer_palette_target`.<br>About 20 more have exactly one access in `src/`, typically a projection write that no UI code reads:<br>• `WormUiComposeView`: `semantic_layer_page`, `semantic_layer_page_count`, `sv_graph_minimum`/`maximum`, `selected_visual_asset_id`, `quantize_q_bound`, and `source_playhead_*`, which only tests read;<br>• `worm_ui_nuklear_compose_state_t`: the six `keysound_/bga_renumber_{project,chart,revision}` stamps and `bga_event_move_anchor_y_pixels`;<br>• `WormUiWorkspaceAction`: `visual_asset_id`, `compose_quantize_preset_selected`;<br>• `editor_interaction_view_model_t`: `can_move_backward`/`forward`, `active_lane_name`;<br>• `compose_scene_cache_t`: test-probe counters such as `static_projection_reused_frames`, `selected_source_scanned`/`reused`. | Delete the 8 dead fields, keeping C ABI field order otherwise. For each write-only field, decide: delete it, mark it as a test probe, or wire up the missing reader. A one-access field may be the unfinished half of a feature, so a missing reader is a possible bug. Do this in T-591, since it is cheap and independent of the grouping. |
| GS1 | A | `WormUiComposeView` (433 fields), `worm_ui_nuklear_compose_state_t` (391), `WormUiWorkspaceAction` (240), `compose_scene_cache_t` (113) and `application_command_state_t` (69, inheriting 28 `std::function` services) can each be grouped into inline POD sub-structs by purpose. This needs a user decision (see below). |

### FM — Formats

| ID | Mark | Finding |
|---|---|---|
| FM1 | A | `stepmania.cpp:1042, 1131, 1211` (10–11 params) thread parser state by hand. Add a stack-only `stepmania_parse_state_t` (options, id source, event counter, keysound context, diagnostics, profile). |
| FM2 | A | `conversion.cpp` helpers repeat `request + limits + result` at 9+ sites. Add a private `conversion_context_t`. |
| FM3 | A | The short `native_project.cpp` functions (107 of 201 are ≤4 lines) are explicit schema serializers with compatibility behaviour. **Keep them.** |

## Necessary complexity (keep as it is)

- **The C ABI boundary.** The `WormUi*` structs, field order, capacities and frame-owned pointer
  lifetimes stay. Only aliases or `typedef`s between layout-identical structs are allowed.
- **Per-domain code enums and strong ID types.** Examples are `diagnostic_code_t`,
  `timing_error_code_t`, `object_id_t`, `recipe_id_t` and `layer_id_t`. Object IDs are unique only
  within a family (`interaction.h:257`), so the ten `delete_*{chart, object_id}` actions stay
  separate.
- **Distinct `begin_*` gesture payloads.** They carry `reference_note`, `displace_to_bgm` and
  capture geometry. A single phase-plus-union design would let an update carry missing data.
- **Real dispatch hops** (AC4) and **projection phases** (PC). Interaction is computed before the
  final view.
- **Format quirks.** These include:
  - `field_t` and `source_line_t` per format;
  - trim and base-36 differences;
  - the StepMania lcm zero case;
  - the strict `bms_clipboard` parsing;
  - the native-project serializers;
  - `timing_arithmetic`'s 128-bit math.
- **Song and chart as separate parameters.** The chart is often a working copy.
- **Lifetime separation.** The per-frame view, persistent Nuklear state and retained sys cache
  are kept apart, even where values repeat. Merging them risks stale input or dangling frontend
  pointers.
- **The measure-index rebuild after a nested mutation** (EC5).
- **Gesture capture booleans.** Only clearly exclusive flag sets may become phase enums, and only
  after a transition review.

## Rejected or downgraded

| Claim | Verdict |
|---|---|
| The index switch and `handle_*` lambdas are necessary for type safety (Haiku H1) | **R.** `std::visit` over named handlers keeps each type distinct and adds an exhaustiveness check; the bare-int switch has no guard. |
| The hand-written ID scans miss vectors and can collide (implied by H2) | **R as a live bug.** All 7 scanners cover the same 16 vectors; `sv_recipes`/`semantic_layers` use other ID types. Kept as a drift risk. |
| `native_project.cpp` short functions are boilerplate for a table or macro | **R** (Luna L4). They encode compatibility behaviour. |
| Rect types across layers should stay fully distinct (Luna L2) | **Downgraded.** The ABI structs stay, but the two core rects and the copied converters merge (TW2). |
| Cache the measure index across nested dispatches | **Out of scope.** It is a revision-semantics change and needs a measurement first. |
| No dead god-struct fields (Luna L1) | **R.** A token scan plus grep found 8 never-accessed fields and ~20 write-only ones (GS2). |

## Guidance for future code

Proposed as an addition to `AGENTS.md`, for the user to accept:

> Do not satisfy the cognitive-complexity limit by moving branches into lambdas or `std::function`
> members inside the same function: clang-tidy scores lambdas separately, so the metric passes while
> the reader still faces one long function. Extract named free functions that take an existing
> context type (`editor_edit_context_t`, `application_dispatch_context_t`, `compose_frame_inputs_t`).
> Before adding a file-local helper, grep for an existing one in `include/wormedit/core` and
> `src/core/*.h`.

## Suggested card breakdown

Execute in order. Each card is behaviour-preserving: no product semantics change, all tests stay
green, and the complexity rule is respected. Before editing, each card re-greps its sites against
current main.

| Card | Scope | Findings | Churn |
|---|---|---|---|
| T-591 | Shared core helpers: `error`/`reject`, `find_chart`, `effective_layout`/`editable_lane`/`position_at`/`maximum_viewport_scroll`, object-ID cluster, severity mapping and the documented sys feedback entry point; dead and write-only fields | HL1, HL2, HL3, HL6, HL8, CS2, GS2 | medium, mechanical |
| T-592 | Core editing context: `chart_scope_t` and `editor_edit_context_t`; named handlers with `std::visit`; delete the lambda table, index switch, duplicated cancel hooks, `std::function` members and the double `find_chart` | EC1–EC6, CS4 | medium, contained to `interaction*` |
| T-593 | Action vocabulary: phase naming, merged identical update/terminal pairs, semantic membership edit (drop the orphan toggle), metadata field mode, `command_definition_t`, hold plan/preview merge | AV1–AV3, AV5, TW4, TW5 | medium; tests heavy |
| T-594 | Application dispatch context: replace the `decltype` bag; shrink the 10–14 param dispatchers; downstream contexts keep only event extras | AC1–AC4 | medium |
| T-595 | Projection contexts: frame inputs and chart scope; quantize scope; `const&` frame context; unused param; one rect converter | PC1–PC6, TW2 (sys side) | medium |
| T-596 | Shape twins: diagnostic templates and aliases, core `rect_t`, file-dialog slot, audio session result templates, color/apply-result/`id_source_t` | TW1–TW3, TW6, TW7 | low–medium |
| T-597 | Checked math and text utilities; formats parse-state and conversion contexts | HL4, HL5, HL7, FM1, FM2 | medium; fixture-sensitive |
| T-598 | File moves for navigation: split `frontend.h` into surface headers; split `interaction.cpp` and the sys family files; no logic change | MC1–MC3 | high churn, low risk |
| T-599 | **Decision-gated.** God-struct grouping into inline sub-structs | GS1 | very high churn; needs user go-ahead |
| — | Optional, user decision: rename `editor_ui_action_t` → `editor_interaction_action_t` | AV4 | ~676 refs |
