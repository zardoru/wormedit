# Scoped BMS ownership and migration boundary

T-731, source audit completed 2026-10-23 against `799fa6b`. This is a design and
source review, not implementation or semantic-test evidence. The
[approved contract](../editing/bms-scoped-program.md) governs the decisions below;
[questions 21–24](../decisions/open-questions.md) were unanswered at the audited revision.
**Decision update (2026-10-23):** question 22 is now answered: fixed selectors are
separate settings-only nodes without Chart children, SKIP is typed case behavior,
and grouping/closing markers are structural syntax. T-733 is ready; the remaining
question-22 references below record the original audit boundary. No native files
or runtime code change in this card.

## Current authority and consumers

The current document deliberately mixes retained source and an editable active
projection. It is not the proposed scoped program.

| Path and exact entry | Present authority and migration consequence |
|---|---|
| `src/formats/bms.cpp`: `parse_bms_bytes`, `prepare_bms_source_lines`, `make_node`, `push_raw_statement` | Builds `chart_document_t::bms_control_tree` for all source lines, retaining encoded spans, line endings, close markers and order. IDs are allocated during parsing; byte offsets are not durable semantic identities. |
| Same file: `collect_active_statements`, `project_control_node`, `collect_active_statement_data` | Only root/common and selected control outcomes become semantic events, metadata and resources. Unselected definitions and endpoints remain source nodes. Import must decode those too, without enumerating selections as the storage model. |
| Same file: `bms_channel_materializer_t::materialize`, `pair_long_note_endpoints`, `finalize_bms_document` | Materializes notes/BGM in measures, BGA and timing in chart vectors; records provenance and imported timing/measure baselines. Hold pairing follows evaluated lane order and can combine endpoints from different source scopes. |
| `include/wormedit/core/document.h`: `chart_document_t`, `song_document_t` | Chart vectors, three `bms_*_projections` baseline lists, retained tree and `active_branch_selection` coexist. Resources/assets live in the song. A new owner must replace this semantic split, not add another writable member beside it. |
| `src/core/bms_branch_view.cpp`: `bms_branch_view_t::from`, `bms_branch_preview_view_t::from` | Validates and evaluates retained controls, exposes active statement IDs and selection differences. `src/formats/bms.cpp` also evaluates controls during import. T-734 must compare both existing consumers, then provide one semantic evaluator. |
| `src/formats/bms_branch_selection.cpp`: `rematerialize_bms_branch_selection`, `serialize_branch_source`, `preserve_editor_state` | Writes/reparses the current chart to change outcomes and selectively restores editor state. This is a legacy adapter, not a proven lossless whole-project migration: copied fields alone do not cover all current song/chart data. |
| `src/core/bms_branch_editor.cpp`: `apply_bms_branch_selection_command`, `apply_bms_branch_choice_command` | Outcome selection currently adopts a rematerialized document and commits “Change active BMS branch”. It is distinct from selecting an edit owner in the new model; question 24 owns the new session/history interaction. |
| Same file: `apply_bms_branch_statement_{edit,insert,delete}_command` | Calls the format's `edit_bms_inactive_branch_statement`, `insert_bms_inactive_branch_statement`, `delete_bms_inactive_branch_statement` and publishes through `publish_branch_edit`. These edit source bytes and repair provenance offsets. Retire modeled raw-statement editing at T-744; Text editing must reject semantic/control commands. |
| Same file: `apply_bms_branch_block_create_command`, `apply_bms_branch_block_remove_command`, `apply_bms_branch_leaf_move_command`, `preserve_guard` | Structural operations wrap/swap serialized source and reparse, requiring a single round-trippable BMS chart and rejecting other state. These are not empty-scope factories, general subtree deletion or unwrap. T-738–T-742 replace their semantics. |
| `src/formats/bms_writer.cpp`: `write_bms`, `normalized_write`, `append_preserved_branch_tree`, `emit_preserved_branch_lines` | May return unchanged source; otherwise emits normalized root data and patches retained branch statements using source claims. T-735 must serialize every semantic owner, including inactive ones; raw bytes become read-only provenance only. |
| `src/formats/native_project.cpp`: chart/song `to_json` / `from_json`, `build_native_project_parse`; `src/formats/native_project_migrations.cpp`: `default_migrations`, `migrate_document`, `normalize_legacy_bms_bgm` | Persists both active vectors and retained tree/baselines. Released migrations currently run through v13; T-737 must select the next version from the live table, not reserve 14 in advance. Preserve existing BGM normalization and shared asset references. |
| `src/core/command.cpp`: `commit_validated_document`, `document_command_transaction_t::finish_execution` | Owns timing-link reconciliation, validation, source-change flags, revision, Undo and journal observation. Scoped routing/projection must compose here and in shared staging, without per-command copies or alternate publication. |
| `src/sys/app/application_defaults.cpp`: `make_initial_chart`, `make_bms_initial_chart`, `make_initial_document`, `make_audio_project_document` | Fresh BMS charts have measures/layout/BPM but no retained tree. T-744 must create Root plus its first Chart without a selected source line. |
| `src/core/chart_editor.cpp`: `commit_empty_chart`, `apply_new_chart_command`, `apply_add_chart_command`, `apply_duplicate_charts_command`, `visit_chart_ids`, `history_id_ceiling` | New difficulties and duplication are additional creation paths. Every BMS difficulty gets its own program; duplication remaps the entire program and its references against current and retained history IDs. |

## Authoritative semantic ownership

One difficulty owns one `bms_program_t` (T-732's planned type), its coordinate
domain and difficulty-wide editor settings. The program owns stable ordered
Control, Chart and Text nodes. Each modeled statement/event/declaration belongs
to exactly one Chart content node in its containing scope. A Control owns its
parameters and ordered children; it does not also own duplicate event vectors.
A Chart node is content within the difficulty, never a nested `chart_document_t`.
Shared song assets, MIDI assets, generator definitions, storyboards, sound groups and output groups retain their existing song owner. Per-difficulty metadata overrides and timing-link associations retain their explicit native owners; they must not be inferred from an active BMS header.

The following is the complete classification of the current importer dispatch,
plus native editor content that can be authored into a BMS difficulty. “Typed”
does not imply export support for a new BMS dialect.

| Family | Current decoder / consumer | New single owner and migration rule |
|---|---|---|
| Visible, invisible and mine channels, including profile-specific lanes | `is_materialized_note_family`, `bms_channel_materializer_t::add_note_slot`; writer `export_root_note`, `export_branch_note` | Chart owns typed note/channel payload and original identifier where semantically needed (including mine values). Derived note IDs map back to that owner, not merely `note_t::branch` or source order. Preserve unsupported channel payloads with diagnostics. |
| Long-note channel endpoints | `is_long_note_channel`, `emit_long_note_pair`, `pair_long_note_lane`; writer `export_root_hold_tail`, `export_branch_hold_note`, `validate_hold_endpoints` | Preserve both typed endpoint contributions at their actual Chart owners, lane, slot, resource token and exact position. A derived hold records all contributors; do not assign a root endpoint to its conditional partner's owner. Newly authored holds put both endpoints in the chosen Chart. Cross-owner projected edits require the approved owner checks and otherwise reject; no implicit transfer. Unpaired/malformed endpoints remain recoverable. |
| BGM `01`, repeated lines and zero slots | `add_bgm_event`, `next_bgm_column`; writer `export_chart_notes_and_bgm`, `export_branch_bgm_event` | Chart owns events and ordered source-line/column contributions; difficulty owns shared column identities. Preserve repeated lines and slot semantics, retaining native normalization rules instead of manufacturing audible zero events. |
| BGA `04`, `06`, `07`, `0A` | `add_bga_event`; writer `export_bga_events`, `export_branch_bga_event` | Chart owns every event with its explicit main/layer/poor/layer-2 identity and resource reference. All four families and inactive events migrate. |
| Initial `BPM`, hexadecimal `03`, extended `BPMxx` and `08` | `collect_bpm_statement`, `add_direct_bpm`, `add_extended_bpm`; writer `apply_branch_bpm_edits`, `bms_timing_projection_t::from` | Chart owns initial/extended declarations and event references as separate typed facts. Preserve duplicate declaration order and later-valid-value behavior; resolved BPM vectors are derived, not replacements for scoped declarations. |
| `STOPxx` and `09` | `collect_stop_statement`, `add_stop_event`; writer `apply_branch_pause_edits`, `validate_retained_pauses` | Chart owns declarations and references, exact values and BMS-tick source units. Preserve later-valid-definition behavior and inactive declarations. Do not convert ticks to seconds on migration. |
| `SCROLLxx` and `SC` | `collect_scroll_statement`, `add_scroll_event`; writer `append_projected_scrolls` | Chart owns signed exact declarations and references, including zero/negative values. Preserve current ordered replacement and profile export restrictions. |
| Measure ratio `mmm02` | `collect_measure_length_statement`, `validate_measure_geometry`; writer `append_measure_ratio_lines` and retained-tree patches | Chart owns each scoped length declaration, including inactive/overridden ones. Difficulty owns the shared measure identity/coordinate domain; evaluated lengths are derived. Never duplicate a complete measure universe under every Chart node. |
| Lane/profile interpretation: `PLAYER`, `OCT/FP`, channel mappings | `collect_metadata_statement`, `select_lane_profile`; `lane_migration.cpp`, `channel_map_editor.cpp` | Typed scoped declarations retain their source meaning. Difficulty owns stable lane identities, chosen supported profile and grid; projection interprets active declarations through existing profile rules. Reject geometry/profile mappings that cannot preserve all scoped content exactly. |
| Known metadata: `TITLE`, `SUBTITLE`, `ARTIST`, `SUBARTIST`, `GENRE`, `PLAYLEVEL`, `RANK`, `DEFEXRANK`, `TOTAL`, `VOLWAV`, `DIFFICULTY`, `COMMENT`, `URL`, `EMAIL` | `is_bms_metadata_command`, `collect_metadata_statement`; writer `collect_metadata_source_state`, `route_projected_branch_metadata`, `emit_metadata_header` | Each scoped occurrence is a typed Chart declaration, retaining order and spelling provenance. Resolved values are projection output; migration must not copy the selected branch's TITLE/ARTIST into an authoritative song-wide default. Shared project metadata and per-difficulty overrides remain distinct native facts. |
| `OFFSET`, `STAGEFILE`, `BANNER`, `BACKBMP`, `PREVIEW` | Same metadata collector; writer `metadata_field_output`, `emit_metadata_header` | Chart owns the exact offset or typed metadata/path declaration; shared assets/files may be referenced without changing declaration ownership. Preserve native explicit overrides separately, rejecting ambiguous legacy reconciliation. |
| `WAVxx`, `BMPxx` | `collect_resource_statement`, `bms_resource_parse_context_t::add`; writer `append_resource_declarations`, `edited_branch_resource_line` | Chart owns each declaration (namespace, slot, ordered occurrence, referenced shared resource/asset). Assets and reusable catalog data remain song-owned. Current parser keeps the **first active** resource declaration and warns on duplicates; BPM/STOP/SCROLL do not share that policy. Retain every inactive and duplicate declaration, even when not the effective binding. No new lexical shadowing rule. |
| Control syntax | `open_control_block`, `continue_if_branch`, `continue_switch_branch`, `close_control_block`, `process_skip_directive`; `project_control_node`, `bms_branch_view_t::from` | Root/RANDOM/IF scope parameters and child order are typed program data. Preserve ELSEIF/ELSE and SWITCH/CASE/DEF/fallthrough/fixed selectors. Full node mapping for SETRANDOM/SETSWITCH/SKIP/group/closing markers remains question 22; no default or extra executable bodies introduced here. |
| Comments, blank lines and commands with no implemented meaning | `push_raw_statement`, `retain_custom_header`; writer retained-line emission | Ordered Text owns exact opaque content and encoding provenance. A safe header token or writer allowlist entry alone is not implemented semantics: `LNTYPE`, `LNOBJ`, `LNMODE`, `BGA`, `PATH_WAV`, `MIDIFILE` need classification against the actual decoder, not a claim that long-note dialect semantics already exist. Keep unsupported forms lossless and diagnosed; do not demote any family above to Text. |
| Native-only timing, annotations and editing data | `chart_document_t` collections; marker/scroll-speed/anchor/semantic-layer editors, SV recipes | Native-authored pauses/delays, warps, SPEEDS, Kiai, anchors, bookmarks/sections, sound channels/clip events and recipes need one typed owner when scoped; editor-wide layer definitions/defaults and view settings keep their appropriate difficulty/session owner, while per-object membership follows the object. BMS export retains existing unsupported-feature/loss gates; migration must not drop native additions merely because the legacy BMS writer cannot express them. |

The resource collector scans active declarations before channel materialization;
do not replace its first-active binding with an invented “nearest preceding WAV”
rule. Preserve each family’s existing ordered evaluation under the selected
profile. Declaration identities and resolved resource identities are different
facts, even when two declarations point to the same file. For program-backed BMS,
the scoped slot assignment belongs to the declaration; any catalog slot label or
`target_identities` presentation is derived from that binding, not a second
independently editable WAV/BMP assignment.

## Identity, geometry and Text boundaries

- Allocate typed stable node IDs independently of line numbers and vector offsets;
  retain explicit parent and sibling order in one authoritative representation.
  Validate unique IDs, one Root, no cycles, legal grammar and at least one direct
  Chart child for every surviving scope. Scope selection resolves its first Chart
  child in program order, skipping Text and nested controls.
- Extend `src/core/id_ceiling.h`, `src/core/chart_object_ids.h` and chart duplication's
  visitors to include **inactive** node/content/declaration IDs and references.
  Allocation must consider current, Undo and Redo snapshots; exhaustion rejects.
  Prepared parsing may use a temporary ID map, published once and retained across
  native reload. Reparse order must never become the ongoing identity policy.
- All Chart children address the difficulty's shared measure/lane identities and
  resolution; sibling order never advances musical time. A scoped `02` declaration
  changes evaluated geometry, not sibling origin. Keep exact rational source
  positions until a shared grid is proven representable across all retained data;
  rounding, incompatible layout interpretation or cap exhaustion blocks migration.
  Structural grid edits must validate every affected owner, not just visible rows.
- Segment source in order: consecutive modeled statements belong to Chart content;
  comments/unknown statements become Text; a control or Text boundary ends that
  content run. Resume with another Chart when modeled content follows. Empty scopes
  get an empty first Chart without relocating preceding Text. Do not merge semantic
  runs across intervening Text/controls or move comments to the end of a scope.
  Detailed non-body directive boundaries wait for question 22.
- Repeated channel lines and declarations retain occurrence order inside content.
  Original bytes, spans and spellings are read-only provenance; invalid recognized
  material retains a diagnosed recovery backing, never silently becomes “unknown”.

## Editor entry inventory and routing

T-736 owns the shared resolver. It captures `(difficulty, scope, Chart)` plus the
existing document stamp at gesture/proposal entry, validates it at apply and routes
the working transaction to authoritative content. Existing-object operations use
their recorded owners; selecting a different scope never moves them. Hidden/stale,
ambiguous, cross-owner or unsupported writes reject atomically rather than falling
back to Root. A command that intentionally spans owners must have an explicit
operation; this card does not authorize one. New objects use the selected Chart.

| Entry family / actual current symbols | Required routing and consumers |
|---|---|
| Tap, keyboard, paint, hold and virtual tail | `note_placement.cpp`: `stage_editor_tap`, `place_tap`, `place_virtual_bms_tap`, `place_virtual_bms_hold`; `interaction.cpp`: `keyboard_entry_plan_t::apply`; `interaction_gesture_lifecycle.cpp`: `hold_gesture_t::apply`, `pending_hold_t::apply`, note-paint handling. Capture one target for the stroke/chord/hold, including both endpoints and any newly materialized measures. |
| Resource drag/drop and armed painting | `resource_assignment.cpp`: `apply_resource_placement`, `apply_bga_stroke`, BGM stroke operations, `stage_resource_assignment`; `resource_paint_gesture.cpp`: `pending_resource_paint_gesture_t::apply`; `hitsound_editor.cpp`: `apply_hitsound_edit`. New notes/BGM/BGA use the chosen owner; assignment/keysound painting edits the existing owners. Preserve combined one-Undo behavior. |
| Paste and external clipboard | `chart_clipboard.cpp`: `chart_paste_proposal_t::{from,revise,apply}`, `paste_chart_clipboard`, `append_paste_object`, `add_paste_bar_lines`; `sys/compose/views/compose_view.cpp`: `begin_compose_paste`, `revise_compose_paste`, `apply_compose_paste`. Capture target through preview/apply; clipboard provenance must not preserve a source document's owner IDs as destination ownership. |
| Move, resize, delete, quantize and time shift | `note_move.cpp`: `apply_note_move_plan`, `apply_duration_resize_plan`; `interaction.cpp`: selected deletion; `command.cpp`: `apply_quantize_command`; `quantize.cpp`: `quantization_plan_t::from`, `synchronize_quantized_selection`; `time_shift.cpp`: `stage_shift_selection`, `apply_shift_from_cursor`. Resolve every selected object and hold contributor. Remove source-order ownership heuristics only after equivalent owner checks cover these operations. |
| Note ↔ BGM / column edits | `note_to_bgm.cpp`: `stage_move_notes_to_bgm`, `stage_move_bgm_events_to_lane`; `bgm_columns.cpp`: `apply_bgm_column_count`. Preserve semantic owner through conversion/reorder; a lane/column move is not a scope move. |
| Timing palette, inspector, lane pseudonotes, spans, beat fitting and tap timing | `bpm_editor.cpp`: `stage_bpm_edit`, `beat_interval_drag_t::apply`; `pause_warp_editor.cpp`, `scroll_speed_editor.cpp`, `timing_anchor_editor.cpp`, `marker_editor.cpp`; `timing_lane_controller.cpp`: `timing_lane_pseudonote_preview_t::apply`, `move_timing_lane_event_group`, `delete_timing_lane_event_group`; `audio_offset_editor.cpp`: `apply_audio_offset_edit`, `audio_offset_drag_t::apply`; `tap_timing_apply.cpp`, `timing_reposition.cpp`, `sv_recipe.cpp`: `apply_sv_recipe_edit`. Distinguish declaration writes from event/reference writes; regenerate derived timing once through publication. |
| Measure, layout and channel map | `measure_editor.cpp`: `apply_measure_edit`, `add_bar_lines`, `remove_bar_lines`, `move_bar_lines`; `lane_layout_editor.cpp`, `lane_migration.cpp`, `channel_map_editor.cpp`. Shared geometry operations cannot mutate only the visible projection; preserve exact positions and inactive owners or reject. Existing conditional guards remain until equivalent validation exists. |
| Metadata and resources | `metadata_editor.cpp`: `apply_metadata_edit`, `apply_batch_chart_metadata_edit`; `resource_assignment.cpp`: `apply_resource_asset_import`, `apply_bms_wav_slot_assignment`, `apply_bms_wav_slot_batch_assignment`, `apply_bms_identifier_renumber`, `apply_resource_deletions`, `apply_resource_asset_relink`; `sound_import.cpp`: `apply_bulk_sound_import`; `note_label_editor.cpp`: `apply_note_label_edit`; `semantic_layer_editor.cpp`. Scoped declaration edits use target ownership; project-wide asset/catalog edits remain explicitly project-wide and inspect references in inactive content too. Never delete/rebind a shared asset as a side effect of changing a local declaration. |
| Timing links | `timing_links.cpp`: reconciliation and retained-BMS guards; `command.cpp`: `commit_validated_document`. Preserve the current refusal of conditional retained timing and unsafe retained-target rewrites until equivalent scoped support is demonstrated. This lane does not silently enable conditional timing links or add writes to a derived timing cache. |
| New/open/import/duplicate/load and history | `application_defaults.cpp`, `chart_editor.cpp`; `chart_import.cpp`: `apply_chart_import`, `collect_references`, `remap_references`, `import_one_chart`; `sys/project/project_open.cpp`, `formats/bms.cpp`, `native_project.cpp`, `command_journal.cpp`. Establish one program per BMS difficulty; remap inactive IDs and resource/asset references on duplication or chart import; save/load, recovery and Undo restore the same authoritative graph. |
| Old branch panel actions and view caches | `compose_actions.cpp`: `edit_compose_bms_branch_choice_action_t`, `edit_compose_bms_branch_statement_action_t`, `edit_compose_bms_branch_block_action_t`; `workspace.cpp`: `workspace_bms_branch_outline_view_t::from`; `sys/project/workspace_view.cpp`: `populate_compose_branch_outline`; `ui/nuklear/project/bms_branch_panel.cpp`: `render_branch_tree`, `render_node_statements`, `render_branch_block_buttons`. T-743 separates edit target from evaluation choices; T-744 removes modeled raw rows and byte-edit actions. |

View-only navigation, tool choice, zoom and ordinary selection stay outside document
commands. Their caches carry owner IDs and invalidate against the existing revision;
they never become semantic storage. The current gesture and transaction runners keep
stamp checking, lifecycle, publication and failure construction.

## One-way transition and public cutover

1. **T-732–T-736 prepare values and adapters.** Production legacy documents stay
   authoritative. Prepared scoped fixtures may be evaluated, written and edited in
   isolation, with no independently writable program attached to live legacy data.
   Every temporary adapter names T-744 as its removal owner.
2. **T-737 prepares native migration.** Read all retained statements, declarations,
   inactive endpoints and current native additions. Reconcile active edits against
   provenance and imported baselines before consuming the backing. A stale raw
   active line must not overwrite an edited semantic object; a projected object
   must not erase inactive alternatives. If matching cannot be proven unique,
   retain diagnosed recoverable data or refuse atomically. Never use a BMS
   write/reparse alone as the migration for native data it cannot represent.
3. Validate the complete graph and bounded affected evaluations against the legacy
   profile behavior, all IDs/references, exact positions, native additions and
   export representability. Unselected or capped evaluation is not equivalence
   evidence. Shared assets remain shared; no implicit garbage collection.
4. **T-744 cuts over together:** public import, new/add/duplicate difficulty, native
   load/save/recovery, edit routing, panel and default writer. Program-backed chart
   vectors become derived reader projections. Generic transaction staging must
   expose the real owner, not commit an independently edited cache then guess how
   to synchronize it. Persist one semantic owner, not both graph and projection.
5. Remove old semantic byte-edit entry points and branch patch/baseline authority.
   Keep unchanged-source passthrough only with verified unchanged semantics;
   retain encoding and opaque Text. Default export emits all scopes; flattening
   remains an explicit loss-policy operation.

Chart import currently gathers/remaps references from materialized vectors; extend
`chart_import.cpp::collect_references` and `remap_references` to include inactive
declarations and endpoint contributors before using them for program-backed imports.
Bulk sound import must carry the same declaration target through its detached batch;
note labels and audio-offset drags are writes even when entered through an inspector.

The exact retirement surface is the branch-editor public functions, three inactive
statement format functions, rematerialization adapter, old raw branch panel and
actions listed above; the writer's `apply_branch_bpm_edits`,
`apply_branch_pause_edits`, branch note/BGM/BGA slot patchers and
`edited_branch_resource_line`; document `bms_control_tree`, `active_branch_selection`
and baseline lists as **authoritative** storage; source-order guards in
`interaction.cpp`, `resource_assignment.cpp`, `quantize.cpp`, `channel_map_editor.cpp`,
`lane_migration.cpp`, `measure_editor.cpp`, `bpm_editor.cpp`, `pause_warp_editor.cpp`,
`time_shift.cpp`, `timing_reposition.cpp`, `timing_links.cpp`; and retained-tree
capability/loss checks in `formats/conversion.cpp` and `formats/osu_mania.cpp`.
Replace each with semantic owner/capability checks, preserving conservative refusals
where scoped support remains unavailable. Removal of the old field name alone is
not completion. Read-only provenance may remain without retaining writable meaning.

## Coverage review and remaining gates

The two tables were reviewed against every branch of
`collect_active_statement_data` / `bms_channel_materializer_t::materialize`, the
writer's metadata/resource/timing/note/BGM/BGA/control consumers, and all first-party
references to the retained tree and baseline lists. The editor inventory also
includes creation paths that have no retained-tree reference. Tests were indexed
as regression homes, not claimed to have been run or audited for value here:

- `tests/formats/bms_tests.cpp`, `bms_writer_tests.cpp`; fixtures
  `control-flow-mixed.bms`, `control-flow-fixed-values.bms`,
  `legacy-long-note.bml`, `repeated-bgm-ratio-stop-collision.bms`,
  `negative-orphan-control.bms`, profile and encoding fixtures.
- `tests/core/bms_branch_editor_tests.cpp`, `chart_editor_tests.cpp`,
  `chart_clipboard_tests.cpp`, `interaction_tests.cpp`, `resource_assignment_tests.cpp`,
  `metadata_editor_tests.cpp`, `chart_import_tests.cpp`, `sound_import_tests.cpp`,
  `note_label_editor_tests.cpp`, `audio_offset_editor_tests.cpp`,
  `timing_links_tests.cpp`, `command_journal_tests.cpp`;
  `tests/formats/native_project_tests.cpp`, `native_project_storage_tests.cpp`;
  `tests/ui/walkthrough_bms_event_harness_tests.cpp`.

T-733–T-744 must add inactive scoped metadata/resources, duplicate definitions,
cross-scope hold contributors, conditional geometry, adjacent Text and repeated
channel coverage to the agreed nested fixture matrix. Include mixed difficulties,
native-only additions, ID exhaustion/history, missing retained backing and fresh
Root creation. Compare all bounded relevant outcomes, not just the active notes.

Question 21 gates range shrink; 22 gates complete directive mapping and insertion
grammar; 23 gates selector unwrap; 24 gates inactive-scope field/playback and
inherited-context interaction. None is answered by this audit. No additional
product choice is introduced. **T-732 may proceed with the agreed Root/RANDOM/IF
value model; this card does not start T-732 or unblock the question-gated slices.**
