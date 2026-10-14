# State ownership and lifetime audit (2026-10-12)

Read-only audit at commit `bf16d82`. It follows the [type/signature audit](type-redundancy-2026-10-12.md),
whose cards T-591–T-600 are complete. That work named the *edit* context. This audit covers what
is still hard to read: **who owns the application's state, and when each piece is reset**.

Every proposal is behaviour-preserving unless it appears under [Decisions](#decisions-for-the-owner).
None adds heap allocation, virtual dispatch, `std::function` or a large copy on a per-frame or
per-event path. One allocation of the application object at startup is the only new allocation.

Marks: **V** = verified by Claude in source; **A** = agent finding, plausible, re-check before
editing; **R** = rejected or downgraded.
Probes:
- four Haiku agents: reset functions, lambdas in `main()`, data-mixin inheritance, construction and
  teardown graph;
- four Codex Luna agents: `main()` ownership inventory, application command state lifetimes, UI
  state and scene cache lifetimes, context width and callback capture.

Part 2 (requested by the owner during the audit) covers the **command contract and vocabulary**:
the copied command steps, revision checks repeated across layers, duplicated fact tables and how
constructors are named. Its probes were two Haiku agents (constructor naming, duplicated tables) and
two Codex Luna agents (the edit transaction, revision checks across layers).

## Summary

The application has no owner. `int main()` (`src/sys/main.cpp:3790–4425`, ~635 lines) holds about
110 locals that together *are* the application, then runs a ~315-line frame loop and a hand-written
shutdown. Everything else in this audit follows from that:

1. **Reference mirrors instead of an owner (OW).** `application_dispatch_context_t` has 36 `*_ref`
   members that point back into `main()`. Its full aggregate initializer is written out twice. The
   frame contexts wrap or inherit it again.
2. **Mixed lifetimes force allowlist resets (LT).** `application_command_state_t` holds app-lifetime
   callbacks (as a base class), session preferences and project edit state side by side. The project
   reset therefore builds a fresh object and copies 11 survivors back by hand. That list has already
   drifted: one display-derived field is missing from it, which is a real bug (LT2).
3. **Project boundaries written twice (PB).** Opening a project and recovering a session both assign
   the same eight project-identity fields by hand. The two copies have already diverged.
4. **Shutdown is not RAII (SD).** The ordered cancel/join/close sequence runs only on a normal loop
   exit. Some worker joins happen after `SDL_Quit`.
5. **Stored callbacks capture `main()` locals (CB).** The command registry, transport services,
   resident-sample listener and audition reconciliation store `[&]` closures over dozens of locals.
   Their lifetimes are safe only because of the current declaration order. Nothing states that.

Part 2 adds four more causes:

6. **The command steps are copied, not shared (CT).** 183 functions return `document_command_result_t`,
   and each writes the same steps by hand:
   - stamp the result with the current revision (41 identical lines);
   - reject a stale `expected_revision` (68 hand-written checks);
   - look up the chart, then copy the document (`auto proposed = history.document;` 58 times);
   - change the copy, then commit (93 `commit_document_command` calls).
7. **"The document changed" is checked at every layer (RV).** There are 386 revision comparisons and
   145 different wordings of the stale message. A typical compose action is checked at the
   dispatcher, again in its handler, and then once more in core against the *live* revision, a
   comparison that can never fail.
8. **The same fact is written out in several places (FT).** Examples are the new-project lane counts,
   playback-rate labels, BMS encodings and output-format families. Several are joined to sys by a
   list index sent across the C ABI, and some hide capabilities the core already has.
9. **Constructor names don't follow a vocabulary (NV).** Seven prefixes are used for "returns a new
   value", plus a few noun-named constructors (`audio_project_document`).

## Findings

### OW — Ownership of the application state

- **OW1 V — `main()` is the owner.** About 110 locals:
  - platform and runtime;
  - document, history and workspace;
  - project and session flags and paths;
  - schedulers and job runtimes;
  - the audio chain (behind `#if WORMEDIT_HAS_AUDIO_DECODER`);
  - scene caches and input controllers;
  - preferences, the command registry and options.

  The Luna 1 inventory gives each local's lifetime class and dependency edges
  (scratchpad `lifetime/luna1-report.md`, A1).
- **OW2 V — The 36-reference aggregate is written out twice.** Once inside the stored registry
  invoker (`main.cpp:4030`, rebuilt on every command invocation), and once per frame (`main.cpp:4096`).
  The two differ only by `#if` wrapping.
- **OW3 V — Data-mixin inheritance.** The codebase has exactly two internal derivations without
  virtuals:
  - `application_command_state_t : application_command_services_t` (`compose_view.h:408`), which is
    sliced on purpose in the reset (LT1);
  - `workspace_frame_action_context_t : application_dispatch_context_t` (`main.cpp:4088`). It adds
    one `audition_reconciliation_ref`, which is reachable only through `template <typename Context>`
    helpers (`main.cpp:2240–2262`). Under `#else` it is a `using` alias of the base instead.

  The nlohmann SAX derivations are real polymorphism and stay.
- **OW4 A — Startup mixes object validity with side effects.** Construction is interleaved with:
  - recovery-scan install;
  - smoke seeding;
  - preference application and frontend setters;
  - decoder budgets;
  - transport command wiring;
  - audio output open;
  - registry construction.

  These belong in a `start()` step that runs after the members exist (Luna 1, A5).
- **OW5 A — The frame loop is already phase-shaped.** It has ten phases, partly marked by
  `frame_timing().enter(...)`:
  1. build contexts;
  2. poll events;
  3. dropped imports;
  4. jobs and autosave;
  5. audio reconcile;
  6. prepare the workspace model;
  7. validate;
  8. project the frontend view;
  9. dispatch the action;
  10. persist preferences and render.

  Luna 1 (A4) lists each phase's line ranges and which subsystems it reads and writes. The 28-line
  close-confirmation callback (`main.cpp:4372–4401`) is the largest inline block left.

- **OW6 V — Wide signatures stand in for a missing owner** (raised by the owner).
  - **76 sys functions take seven or more parameters.**
  - **Seven take three or more objects from the audio chain** (sources, preview session, output and
    state, audition output, object audition). For example, `process_sound_audition_request`
    (`compose_audition_requests.cpp:129`) takes the request, history and five audio objects, plus
    notifications.
  - **The widest functions** are the transport wiring in `main.cpp`: `apply_loop_selection` (15),
    `configure_transport_commands` (14), `configure_transport_play_pause` (13) and
    `configure_transport_seek_commands` (10).
  - **They hide their types.** Eight `main.cpp` functions take 2 to 14 `auto&` parameters each, 69 in
    total across the codebase. There are also 24 `template <typename Context>` helpers, so neither a
    reader nor grep can see what is passed.

  With `audio_runtime_t` and the other subsystems, these become member functions of the subsystem
  that owns the state, or take `audio_runtime_t&` plus the document. The transport services are then
  wired by an `audio_runtime_t` member using `[this]`. T-603 and T-605 must remove every `auto&` and
  `Context` template in `main.cpp`.

### AS — Project audio sources (raised by the owner)

- **AS1 V — The registry mirrors the document, but nothing pairs them.**
  `audio::decoded_source_registry_t` (`source_registry.h`) caches decoders keyed by the document's
  `asset_id_t`. It only means something relative to `song_document_t::audio_assets` plus `asset_base`.
  The bookkeeping that keeps it in sync lives outside it, as four loose `main()` locals:
  `audition_source_project`, `audition_asset_inventory`, `audition_source_generation` and
  `audition_reconciled_revision` (`main.cpp:3910–3936`).

  These are bundled with the document, the registry and the metronome bindings into
  `audition_source_reconciliation_context_t`, which has 7 references, 3 `std::function` callbacks
  and 2 optional pointers (`audio_source_registration.h:~110`) and is passed by value.
- **AS2 V — 20 declarations take both** a `song_document_t`/`document_history_t` and the registry
  (about 14 distinct functions):
  - registration in `audio_source_registration.cpp`;
  - `chart_preview_t::compile_if_needed` and `play_pause`, `registered_music_asset`;
  - the four `process_*_request` functions in `compose_audition_requests.cpp`;
  - `append_packed_waveform_assets`.

  Each caller must know the two are in sync. For example, `process_sound_audition_request` calls
  `await_pending()` and then resolves resource → asset → binding by hand.
- **AS3 A — Possible stale window.** Reconciliation runs in the frame's audio phase
  (`main.cpp:4197`), before action dispatch. An action later in the same frame that changes assets
  and then auditions would see the registry from before the edit until the next frame. This was
  inferred from the phase order and not reproduced.
- **AS4 — Layering is right; ownership is missing.** The registry belongs in `src/audio`, below the
  editor: it also holds generated PCM sources and serves `decode_block` from worker threads. Keep it
  free of `song_document_t`. What's missing is a sys owner that binds it to the project:

  ```cpp
  // src/sys/project_audio_sources.h — a member of audio_runtime_t (T-603), declared after the session.
  class project_audio_sources_t {
  public:
      void reconcile(const song_document_t&, const std::filesystem::path& asset_base); // no-op if revision and project unchanged
      [[nodiscard]] auto binding_for(const song_document_t&, resource_id_t) const -> std::optional<audio::decoded_source_binding_t>;
      [[nodiscard]] auto clips_for(const song_document_t&, resource_id_t) const -> std::optional<std::vector<audio::audition_clip_t>>;
      audio::decoded_source_registry_t& registry(); // for workers and generated sources
  private:
      audio::decoded_source_registry_t registry_;
      std::optional<song_id_t> project_;
      std::vector<audition_asset_entry_t> inventory_;
      std::uint64_t generation_{}, reconciled_revision_{};
  };
  ```

  - The four loose locals and the reconciliation context go away.
  - The three callbacks (stop playback, reset waveform, invalidate preview) become calls between
    sibling members of `audio_runtime_t`.
  - Consumers take `project_audio_sources_t&` instead of the document plus the registry.
  - Queries take the document explicitly, so nothing stores a document reference that a project
    replacement could invalidate.
  - Whether queries also reconcile on a revision mismatch (closing AS3) is decision D12.

### MT — The metronome has no home (raised by the owner)

- **MT1 V — Where its parts live today.** The parts below are mostly in the right place; each owns a
  layer:
  - beat accents and scheduling are in `core/timing` (`metronome_accent_t`,
    `preview_metronome_event_t`, `include_metronome`, count-in pulses; `timing.h:385–473`);
  - the voice mix and the enable/volume atomics are in `audio/preview_bridge`.

  Those placements are reasonable: the rhythm is a timing fact, and the mixing happens in audio.
- **MT2 V — The sound itself is hidden in registration.** `register_metronome_sources`
  (`audio_source_registration.cpp:97`) synthesizes the three accent clicks: sine bursts at 1.1, 1.5
  and 1.9 kHz with an exp(−90t) envelope, 45 ms long, at fixed gains. It registers them as generated
  sources and fills a `preview_metronome_source_binding_t` vector. It runs again on every
  registration and reconciliation, because generated sources share the registry's generation.
- **MT3 V — No owner for its state.**
  - The bindings vector is a loose `main()` local (`preview_metronome_bindings`), threaded through
    38 references in `main.cpp`, `chart_preview_state.*` and `audio_source_registration.*`.
  - The settings (enabled, available, volume, count-in) sit in
    `application_command_state_t::transport` (`compose_view.h:578–582`).
  - Three `std::function` services push those settings into the bridge (`compose_view.h:392–394`).
    The result is two sources of truth, the sys copy and the bridge atomics, kept in sync by hand in
    `toggle_transport_metronome` and `apply_timing_settings_action`.
- **MT4 — Proposal.**
  - `src/audio/metronome.{h,cpp}` with `make_metronome_clicks()`, a pure PCM synthesis function that
    can be tested on its own.
  - A `metronome_t` member of `audio_runtime_t` that owns the bindings and
    `register_into(registry, generation)`; `project_audio_sources_t` calls it after a reset.
  - The settings move into `metronome_t`, as session preferences with a single source of truth;
    the bridge is updated through member calls.
  - The `metronome_bindings` parameters and the three `std::function` services go away.
  - Scheduling stays in `core/timing` and mixing in `preview_bridge`.

  Coordinate with T-601: four of the 11 fields that survive a project reset are metronome settings.

### LT — Mixed lifetimes in the application command state

- **LT1 V — Allowlist reset.** `reset_application_project_edit_state`
  (`application_command_dispatch.cpp:35–50`) does three things:
  1. builds `application_command_state_t next`;
  2. moves the services base over with `static_cast<application_command_services_t&>`;
  3. copies back **11** fields: `compose_view.editor_preferences`,
     `compose_view.minimap_click_moves_paused_playhead`, and nine `transport.*` fields (rate index,
     keep pitch, keep-pitch available, seek posts feedback, metronome enabled and available, timing
     settings visible, metronome volume, count-in).

  Anything not on the list resets on every project open. This is the only "fresh object, copy back"
  reset in `src/`.
- **LT2 V — Bug: the scratch side reverts after a project open.** `compose_view.scratch_on_right` is
  resolved from the display preferences and the field skin
  (`update_scratch_presentation`, `main.cpp:938`). Only three callers set it: startup (3982),
  preference change (981) and options (996). It is not on the LT1 list, so a project replacement
  resets it to `{}`.

  By default the preference is "use the skin's side" (`scratch_positions` 0,
  `options_runtime.cpp:261`). After any project open or recovery, the field therefore falls back to
  the chart's authored placement until the user touches preferences. The value reaches the editor on
  every frame (`compose_input.cpp:2258`, `compose_view.cpp:1309`).
- **LT3 V — Groups mix lifetimes.**
  - `compose_view` holds session preferences next to project-scoped maps (snap denominators, BGM
    scroll).
  - `transport` holds preferences, capability flags (`*_available`) and chart-bound seek records.
  - Several fields that look like preferences currently reset per project: `edit_snap_enabled`,
    `y_lock`, `compose_timing_focus`, `compose_timing_lane_visibility_mask`, `player_preview_visible`,
    `player_preview_skin`, `compose_editor_right_pixels`.

  Luna 2 proposes buckets (APP-LIFE-3): services, session preferences, project, chart, gesture,
  frame. **The regrouping must follow today's reset behaviour, not those proposed buckets.** Fields
  that reset today go into `project`; whether they should persist is decision D3.
- **LT4 A — Chart switches reset nothing centrally.** `open_project_chart` resets only the editor
  interaction state (`main.cpp:1151`). Chart-bound fields rely on per-use chart and revision stamps.
  This is correct today but implicit. Name a `chart` group, and do not add a chart reset in this batch.
- **LT5 A — About 750 qualified accesses** to the ten current groups across `src/sys` (Luna 2,
  APP-LIFE-6). The regroup is mechanical but broad.

### PB — Project-boundary duplication

- **PB1 V — Two hand-written project boundaries.** `replace_project_session` (`main.cpp:2289–2297`)
  and the recovery path (`main.cpp:2750–2758`) each assign:
  - `session_loaded`, `native_project_loaded`, `fresh_blank_project`;
  - the pending audio and BMS paths;
  - `active_project_path`, `asset_base`, `scanned_primary_version`;

  and both then call `reset_project_runtime_state`.
- **PB2 V — The copies have diverged.** Replacement clears `external_player_runtime.source_path`
  (2296); recovery does not. The external player can keep the previous project's source path after
  a recovery (decision D2).
- **PB3 V — `reset_project_runtime_state<Context>`** (`main.cpp:2240–2262`) is the project-lifetime
  boundary, spread across 14 locals:
  - chart runtime (generation bump first);
  - audio-offset drag;
  - interaction state;
  - timing edit, timing grid and the two scene caches;
  - viewport escape and navigation cancel;
  - metadata drop;
  - command state (LT1);
  - audition reconcile.

  Keep the generation bump first: async consumers compare generations.

### SD — Shutdown and teardown

- **SD1 V — The explicit sequence** (`main.cpp:4411–4423`):
  1. cancel copy, metadata and export jobs;
  2. join the copy scheduler;
  3. `metadata_asset_runtime.cleanup(asset_base)` (needs the join first, `metadata_asset_runtime.h:55`);
  4. `export_runtime.shutdown()`;
  5. close the preview, then the audition output;
  6. clean up the external player;
  7. `shutdown_application_runtime` (ends in `SDL_Quit`);
  8. remove the crash marker.

  `application_runtime_t` has no destructor.
- **SD2 V — No RAII.** An exception out of the frame loop skips SD1 entirely, including SDL teardown
  and output close.
- **SD3 A — Some joins happen after `SDL_Quit`.** Destructors of the autosave scheduler, the player
  image and video jobs (which cancel and wait) and the waveform runtime scheduler run after step 7.
  This is suspect if a decoder touches SDL (decision D5).
- **SD4 A — Reverse declaration order already covers a lot.** Notifications are destroyed last; the
  comment at `main.cpp:3792` requires that. Outputs go before sessions, which go before transports,
  which go before caches. The resident loader goes before the probe scheduler. Its destructor does
  not join, so the scheduler must be destroyed after it
  (`audio_source_registration.h` header comment).

  The Haiku teardown table lists, for each runtime class, whether it can be copied or moved and what
  its destructor does.

### CB — Stored callbacks and context width

- **CB1 V — Stored `[&]` closures over `main()` locals:**
  - the resident-sample listener (`main.cpp:3903`). It runs on probe workers and touches both
    transports.
  - audition-reconciliation stop, reset and invalidate (3925–3930);
  - `external_player` (3990);
  - about 20 transport services in `configure_transport_commands` (578–762);
  - `tap_clock` (4018);
  - the registry invoker (4026–4071), which captures about 38 locals.

  `external_player_runtime` is declared *after* the command state whose callback captures it. That
  is safe only because nothing calls the callback during destruction.
- **CB2 A — Consumers use small subsets of the 36 references.** Luna 4 (CW-1) gives a usage matrix by
  subsystem. Command handlers mostly need history, workspace, interaction, command state and
  notifications. Metadata needs history, workspace, drop state and runtime, asset base and the copy
  scheduler. The notification frame helper needs only the notification center and history, plus
  three navigation references.
- **CB3 V — Naming leftovers.** The `_ref` suffix throughout `application_dispatch_context_t` exists
  because it is a borrowed view. `external_player_runtime_t` is defined in the dispatch-context
  header.

### UR — Reset entry points in UI and sys

- **UR1 V — The same four Nuklear session resets are repeated three times** (`frontend.cpp:616–619`,
  `730–733`, `757–760`).
- **UR2 V — A per-frame "boundary" reset.**
  `if (view && !view->project_loaded) worm_ui_nuklear_reset_sound_import();` runs on every frame
  with no project loaded (`frontend.cpp:3245`). Each call locks `picker.mutex` and increments
  `picker.generation` (`sound_import_runtime.cpp:65–76`). The struct mixes three lifetimes: the
  picker (app), the submitted batches (project) and pending (request). The behaviour is harmless as
  far as the probe could tell, but this is a transition written as a poll.
- **UR3 A — Scene cache invalidation is a long manual reset** (`compose_view.cpp:1128`), with similar
  but non-uniform key checks at other consumers (`compose_input.cpp:401`, `:1747`,
  `compose_minimap_navigation.cpp:83`). Use one `ensure_scene_key` and one `invalidate_scene_cache()`.
- **UR4 A — Scattered boundary calls.** `invalidate_playing_seek_records` has 7 callers and
  `clear_beat_fit_capture` has 10. Each must be called at every seek, playback or gesture boundary.
  Leave these as they are unless a card touches those paths.
- **UR5 V — Two "audition on click" settings.** The Sounds panel toggle
  `dock_layout.sounds_preview_on_click` (`compose_state.h:590`, UI-only, not persisted) is separate
  from `editor_preferences.audition_on_click` (persisted; used by note placement and resource input).
  They may be intentionally distinct (catalog list versus chart), but nothing says so (decision D4).
- **UR6 A — `worm_ui_nuklear_compose_state_t` resets only at frontend init and shutdown.** It mixes
  gesture captures, chart and project editing buffers, and durable dock and layout choices. Luna 3
  proposes shared `project_stamp_t`/`chart_stamp_t` types and per-boundary UI resets. That is large,
  and it is a separate decision (D6).

### CT — The document command steps (part 2)

- **CT1 V — The counts.** 183 functions return `document_command_result_t`
  (`include/wormedit/core/command.h:28`). 41 open with the identical line
  `result{.revision = history.document.revision}`. 68 hand-write the stale `expected_revision`
  check, 58 copy with `auto proposed = history.document;`, and 93 end in `commit_document_command`.
  The names `apply_` (156) and `validate_` (111) look like a pair, but only one name exists in both
  forms. `validate_*` are private helpers, not a step of the contract.
- **CT2 A — The shapes found.** Luna 5 sampled 45 commands across core:
  - 31 follow the full sequence: stamp → stale check → preconditions → whole-document copy →
    mutate → optional fix-up (e.g. `extend_native_charts_to_audio_end`) → commit;
  - 8 resolve the chart or target before copying;
  - 4 commit a document that was prepared elsewhere.

  Deviations:
  - multi-chart edits (`apply_batch_chart_metadata_edit`);
  - staged edits that use a scratch history (`move_notes_to_bgm`, timing and SV previews);
  - commands that return success with no change;
  - results that carry an extra payload.
- **CT3 A — 72 `*_result_t` types in core.** Only nine are a command result plus a payload:
  `quantize_command_result_t`, `redivision_command_result_t`, `duplicate_chart_result_t`,
  `chart_import_result_t`, `time_shift_result_t`, `selected_note_to_bgm_result_t`, two
  `bms_branch_*_command_result_t`, and `proposal_apply_result_t<Issue>`. The rest are genuinely
  different (plans, proposals, parses, queries, storage, dispatch) and stay separate.
- **CT4 A — Copy cost.** The usual path already copies the whole `song_document_t` once. The
  transaction must copy lazily, on first `copy_document()`, so that rejections before the copy stay
  free, exactly as today.

### RV — Revision and staleness checks (part 2)

- **RV1 V — A triple check.** A meter edit is checked three times:
  1. `dispatch_compose_view_action` checks the action's chart and revision
     (`compose_actions.cpp:~2305`);
  2. `set_cursor_measure_meter` checks the same stamp again (`compose_marker_actions.cpp:570`);
  3. `apply_cursor_measure_meter` passes `history.document.revision` as the expected revision to
     `apply_measure_edit` (`compose_measure_commands.cpp:129`), which then compares the revision with
     itself.
- **RV2 A — Classifying the 386 comparisons** (Luna 6, extrapolated from traced paths):

  | Class | Count | Meaning |
  |---|---|---|
  | Boundary | ~60 | first check of an action stamped by the UI; keep, once |
  | Redundant | ~95 | the same synchronous path with no change since the boundary |
  | Self-comparison | ~70 | the "expected" value is the live revision |
  | Legitimate later | ~161 | the stamp was captured earlier: a gesture from a previous frame, a preview or proposal, an async result, a cache key, or a change made earlier in the same handler |

  Claude counted about 185 call arguments of the form `X.document.revision`, about 70 of them into
  `apply_`/`move_`/`insert_`/`delete_` commands (`apply_measure_edit` ×8,
  `apply_workspace_action` ×8, `apply_resource_asset_import` ×4, …).
- **RV3 V — Batches make some checks real.** One compose input action carries a batch of inputs
  checked against a single entry stamp (loop at `compose_input.cpp:2033`). When an earlier input
  changes the document, the checks for later inputs are legitimate. The boundary check therefore
  belongs **per batch item**, not per action.
- **RV4 A — Entry points without a shared boundary.** Metadata and workspace dispatch have none;
  their first real check is in core. Workspace's check that the document matches the stored project
  and revision (`workspace.cpp:1130`) is a consistency check and stays.
- **RV5 A — Stamp-carrying structs and message templates.** There are up to 81 structs carrying
  `(chart|project|object, revision)` stamp fields in sys, core and ui (an upper bound; the C ABI is
  excluded). The 145 messages cluster into six templates: ignored because the document changed;
  expired; stale target; reopen or reselect; changed during a long operation; a core "revision is
  stale" diagnostic.

### FT — Facts written in several places (part 2)

- **FT1 V — New-project lane counts** live in three hand-synced places:
  - UI labels `"4K"…"14K"` (`frontend.cpp:2821`);
  - the sys index-to-count table `{4,…,10,14}` (`main.cpp:2189`);
  - `supported_lane_count` (`application_defaults.cpp:19`);

  plus a fourth mapping, `step_type`. The UI sends a **list index** across the ABI
  (`new_project_layout_index`). Meanwhile `make_osu_mania_profile` accepts 1–18
  (`lane_profiles.cpp:814`), and DP and 18K profiles exist (`make_bms_double_scratch_profile`,
  `make_pms_bme_style_18k_profile`), but none of these is offered (decision D7).
- **FT2 V — Playback rate.** Core has `playback_rates` and `playback_rate_label`
  (`playback_rate.h:7`, `.cpp:19`). The UI keeps its own reversed label array and does `8 - index`
  (`frontend.cpp:2059`). sys hardcodes `< 8` (`main.cpp:782`), and the default index is 8
  (`compose_view.h:572`).
- **FT3 V — BMS text encodings** are listed in the UI (`frontend.cpp:2875`) and in sys
  (`main.cpp:2572`), joined by a `i+1` index sent across the ABI.
- **FT4 V — Format families.**
  - The output-group UI has a 5-name array whose index is cast straight to the enum
    (`setup_panel.cpp:439`, `:469`).
  - Metadata offers all 7 families, from an array copied twice in one file
    (`metadata_format_fields.cpp:229`, `:757`).
  - There are display-name switches in `workspace_projection.cpp:150` and
    `chart_import_session.cpp:11`.
- **FT5 A — Smaller cases.**
  - Three UI arrays of timing-lane labels are indexed by the core enum's order, with no
    static_assert.
  - `text_encoding_t` name switches are repeated in `bms.cpp` and `bms_writer.cpp`.
  - `enum_at_most(…, quaver)` is used as the family count.
  - The default snap set `{4,8,16,32,48,64}` appears three times.
  - The inspector's grid list is closed and does not come from core.

- **NV3 V — `project_` means two things** (raised by the owner).
  - About 45 sys functions use `project_` as a verb ("project the document into view data"):
    `project_bga_list`, `project_sound_list`, `project_waveform`, `project_application_frame`, … .
  - At least five use it as the noun, the song project: `project_export_available`
    (`main.cpp:1383`), `project_export_command` (`application_command_dispatch.cpp:780`),
    `project_replacement_busy` (`main.cpp:2441`), `project_transition_ready` (`main.cpp:2303`) and
    `project_import_warning` (`project_import_feedback.h:9`).
  - Others read either way, e.g. `project_chart_row`, which is a projection for the project view.

  `project_bga_list` reads like "the project's BGA list". Every projection is a read-only
  derivation: document + view request → frontend data. The pipeline is: core document → sys
  projection into C++ types, testable headless (e.g. `bga_list_t`) → flattening into `WormUi*` C ABI
  views that point into runtime-owned storage (e.g. `bga_dock_runtime.cpp:99–132`) → Nuklear. The
  only consumers are the frontend and tests.

- **NV4 V — Yes/no checks have no prefix** (raised by the owner). There are 1,589 functions that
  return `bool`; 484 of them are actions reporting success (`apply_`, `set_`, …). Among the actual
  yes/no checks, **about 286 have a state-shaped name with no `is_`/`has_`/`can_` prefix**, against
  103 that have one. Examples: `stem_ready` (`bmson_waveform_projection.cpp:164`), `bga_placeable`,
  `bgm_move_enabled`, `audio_offset_drag_matches`, `chart_row_exists`, `advance_eligible`. A few put
  the verb in the middle: `bga_stack_is_valid`, `bms_span_is_valid`. The rule, for free functions
  and members alike:
  - `is_<subject>_<state>()` (`is_stem_ready`, `is_bga_placeable`, `is_bga_stack_valid`);
  - `has_<thing>()` for possession;
  - `can_<verb>()` for capability;
  - "still valid for the current document" checks (`*_matches`) become `is_<x>_current`.

  Comparators passed to std algorithms keep `*_less` and `*_equal`.
- **NV5 V — Types built only by a free function.** 112 `make_` functions return 66 types. **56 types
  have exactly one factory**, for example `make_document_history` → `document_history_t` (746 calls,
  most in tests), `make_default_editor_command_registry` → `command_registry_t`, `make_grid` →
  `grid_t`, `make_lane_map`, `make_bms_channel_map`, `make_compose_resource_catalog`. Going to the
  type's definition doesn't show how it is built. The proposal is a static named factory on the type:
  `document_history_t::from(document, maximum_undo, saved)`, returning `std::optional<T>` or a
  result when construction can fail. A static factory, not a constructor, because:
  - these are aggregates built with designated initializers everywhere (`{.id = …}`), and a
    user-declared constructor would remove that;
  - a named `from`/`with_defaults` can fail by returning `optional`.

  Real constructors stay for classes with private invariants, as today. Types with *many* factories
  stay as free functions in a catalog: the 35 `lane_input_profile_t` profiles belong to the T-611
  layout catalog. Result types (`*_result_t`) are not candidates.
- **NV6 V — Find-or-add accessors are not constructors.** `stem_entry(cache, channel)`
  (`bmson_waveform_projection.cpp:170`) finds a channel's entry in
  `compose_scene_cache_t::waveform.stem_waveforms` or appends one, returning a reference into the
  container. That is not `stem_waveform_cache_t::from`, which would build a new value. It belongs as a
  member of the container's group: `cache.waveform.stem_for(channel)`. Scan for the same shape
  (`find`, else `emplace_back`, then return a reference) and give each one a `*_for(key)` member on
  its owner.

### RW — Position ↔ row conversion has a home nobody found (raised by the owner)

- **RW1 V — Core already owns both directions.** `absolute_row(index, position)`
  (`document.h:1017`, `document.cpp:1320`, with checked addition) and
  `position_at_absolute_row(index, row)` (`snap.h:57`, `snap.cpp:120`). They are free functions in
  two different headers, and neither header's name says "rows", so sys and core code wrote their
  own.
- **RW2 V — Copies of position → row:**

  | Copy | Difference from core |
  |---|---|
  | `row_for_position` (`bookmark_chooser_projection.cpp:20`) | same, without the overflow check |
  | `absolute_row_of` (`compose_bookmark_commands.cpp:84`) | **no `row_capacity` check**: an out-of-range row would map into the next measure (behaviour difference; see below) |
  | `position_row` (`sv_recipe.cpp:61`) | wraps core and also accepts the chart-end position (a legitimate variant) |
  | `row_of` member (`time_shift.cpp:30`), `row_for` (`timing.cpp:2615`, `interaction_internal.h:419`), `row_of` (`interaction_hit_test.cpp:274`) | the same rule through private id→entry hash maps; `timing.cpp` also bounds by `total_rows` |
  | `position_row_or_diagnostic` (`document.cpp:619`) | core plus a diagnostic (fine) |
  | `absolute_row_for` (`stepmania.cpp:1509`) | different rule, uniform measures; format-specific, so keep it |

- **RW3 V — The inverse copy is exported from sys.** `local_position_for_row`
  (`compose_bookmark_commands.cpp:45`, declared in its header) is token-for-token
  `position_at_absolute_row`, and it is used by `compose_playhead_skip.cpp` and
  `compose_waveform_fit_input.cpp`. `sv_recipe.cpp:45` `position_at_row` adds the chart-end variant.
- **RW4 V — Why the private maps exist.** `measure_index_t` (`document.h:985`) is only
  `{entries, total_rows}`, so a lookup by measure id is a linear scan. Five hot callers built their
  own id→entry lookup:
  - `interaction_internal.h:415`;
  - `marker_navigation.cpp:66` (`measure_lookup_t`, `build_measure_lookup`);
  - `timing.cpp:2615`;
  - `time_shift.cpp:30`;
  - `interaction_hit_test.cpp:255`.
- **RW5 — Proposal.** Give the concept its home on the type:
  - `measure_index_t` gains `row_of(position)`, `position_at(row)` and `entry_for(measure_id)`
    members using today's algorithms. The existing free functions become one-line forwarders, or
    are renamed, under T-612/T-617.
  - One shared `measure_lookup_t::from(index)` (hash map by id, with the same `row_of` and
    `entry_for`) replaces the five private maps. Only the callers that build a map today build this
    one, so there is **no new allocation**. `measure_index_t` itself must not gain a map, because
    it is rebuilt on every editor action.
  - `allow_chart_end` becomes an explicit parameter or a `row_of_or_end` member.
  - Delete the sys copies.
  - `absolute_row_of` migrates to the checked version only if the card shows its inputs are always
    within capacity (bookmark positions are document-validated). Otherwise record the difference and
    keep the behaviour.

### AP — The sys action handler steps (raised by the owner)

- **AP1 V — Every compose action handler repeats the same steps.** The 49 handlers that take
  `compose_action_context_t&` (across `compose_actions.cpp` and the `compose_{area,bga,marker,resource,sound}_actions.cpp`
  files) each hand-write:
  - a precondition: `editor_interaction_has_pending_gesture` (57 here, 94 across sys);
  - a stamp check: `compose_input_document_revision` (71; mostly the redundant class from RV2);
  - the core command;
  - `rebind_workspace` after a mutation (37 here, 55 across sys), with a two-message failure path
    (`compose_mutation_rebind_failure`);
  - feedback: `compose_action_warning` (159), `publish_compose_feedback` (30), `compose_action_error`
    (11).

  `set_cursor_measure_meter` (`compose_marker_actions.cpp:566`) shows all five steps in 25 lines.
- **AP2 — Proposal: handlers return an outcome, and one place finishes it.** This is the sys
  counterpart of the core transaction (CT), with no callbacks and no type erasure:

  ```cpp
  struct compose_action_outcome_t {            // a value, built by the handler
      std::string message;
      notification_severity_t severity{notification_severity_t::success};
      notification_source_t source{notification_source_t::edit};
      bool mutated{};                          // true: the dispatcher rebinds the workspace
      static auto blocked(std::string, notification_source_t) -> compose_action_outcome_t;
      static auto from(const document_command_result_t&, std::string success,
                       std::string failure) -> compose_action_outcome_t;
  };
  // on compose_action_context_t:
  [[nodiscard]] auto require_no_gesture(std::string_view what) const -> std::optional<compose_action_outcome_t>;
  // in the dispatcher, once per action:
  auto finish_compose_action(compose_action_context_t&, compose_action_outcome_t) -> std::string; // rebind if mutated, then publish
  ```

  - The stamp check leaves the handlers; T-609's boundary covers it.
  - The gesture precondition becomes one declared call.
  - Rebinding and feedback happen once, in `finish_compose_action`, which keeps today's two-message
    rebind-failure text.
  - A handler then reads as "check, perform, describe".
  - The same outcome type serves the compose input handlers (5 rebinds) and the 5 rebinds in
    `main.cpp`.
- **AP3 V — `compose_marker_actions.cpp` is a grab-bag.** Under the "marker" name it holds bookmark
  edits, beat sections, audio markers, note-label search, the selection transform, measure edits, the
  recipe form, the meter and moving a bar line. Split it by concept when the handlers move to
  outcomes: bookmarks, measures/meter, recipe form, selection transform.

- **AP4 V — `area_action_context_t` is a second, smaller compose action context**
  (`compose_area_actions.cpp:17`; raised by the owner). The compose dispatcher unpacks its context
  into loose arguments (`action, history, state, notifications, timing_locked`;
  `compose_actions.cpp:2145`), and `dispatch_compose_area_action` repacks them into a new struct with
  its own feedback helpers:
  - `area_feedback` has the same body as `publish_compose_feedback`;
  - `area_rejection` is `compose_action_warning`;
  - `area_diagnostic` posts diagnostics.

  The wrapper `dispatch_compose_area_with_quantize_report` (`compose_actions.cpp:2113`) adds two more
  inline preconditions (paste session, active gesture) with their own `if (notifications)` posts. In
  T-618 the area handlers take `compose_action_context_t&` and return `compose_action_outcome_t`; the
  struct and its helpers go away. Check the other 26 sys `*_context_t` for the same "unpack, then
  rebuild a subset" shape.
- **AP5 V — The "hint or normal post" choice is copied seven times, and the sink is optional.** The
  `severity == hint ? post_transient_hint : post` branch is hand-written in:
  - `application_command_dispatch.cpp:111`;
  - `compose_actions.cpp:66` and `:74`;
  - `compose_area_actions.cpp:28`;
  - `compose_navigation_input.cpp:64`;
  - `main.cpp:248`;
  - `transport_player_actions.cpp:12`.

  Separately, `std::optional<notification_sink_t>` serves as a "post or stay silent" switch. It is
  declared 47 times, with 23 guarded posts in sys.

  The proposal adds `notification_sink_t::post_feedback(severity, source, message)` to
  `core/notifications.h`; it does the hint routing. It also adds a muted sink,
  `notification_sink_t::muted()`, whose posts do nothing. Every caller then posts unconditionally,
  and the optionals and guards go away. Both changes are cheap: the sink is a small handle.

- **AP6 V — The outcome type has already been invented three times** (raised by the owner, from
  `fail()` in `compose_bookmark_commands.cpp:18`):
  - `bookmark_command_outcome_t` (`compose_bookmark_commands.h:25`): `accepted`,
    `document_changed`, `status`, `diagnostics`, `feedback_posted`;
  - `bookmark_rename_outcome_t` (`bookmark_rename_commands.h:12`): the same fields, but `severity`
    in place of `feedback_posted`;
  - `bms_block_outcome_t` (`compose_actions_internal.h:535`): a `document_command_result_t`, a
    `message` and `attempted`.

  Each has its own local builder, e.g. `fail(status)` and the HL1-era `reject(std::string)` in
  `bookmark_rename_commands.cpp:20`. These are local versions of `compose_action_outcome_t` (AP2),
  and they confirm its shape. T-618 replaces all three with it: `document_changed` becomes
  `mutated`, and `feedback_posted` goes away because posting happens once, in
  `finish_compose_action`.
- **AP7 V — Core builders of a rejected result under other names.** `rejected(revision, code, message)`
  appears in `bmson_sound_channel_editor.cpp:19` and `note_to_bgm.cpp:18`, and seven inline
  `return {.applied = false, …}` sites build a rejected `document_command_result_t` by hand. T-591
  shared `reject()` and `error()` by *name*, so these were missed. The CT transaction's `reject()`
  replaces them all.
- **AP8 — Why the earlier sweeps missed these.** HL1 (`error`/`reject`) and HL5 (`ascii_fold`) were
  found by name, so copies under other names (`fail`, `rejected`, `fold_ascii`, `folded`, …)
  survived. T-612, T-617 and T-618 must find copies by **what they do**: the result shape built,
  the code pattern, or the struct fields. Each card records its search patterns.

### FL — Folders instead of prefixes (raised by the owner)

- **FL1 V — Prefixes are standing in for folders.**
  - `src/sys` is one flat folder of **209 files (122 distinct names)**. The prefixes already name
    the groups: `compose_` 46 names, `application_` 10, `audio_` 5, `sound_` 5, `sdl_` 4, `project_`
    4, `metadata_` 4, `timing_` 3, `workspace_` 3, and a long tail.
  - `src/ui/nuklear` holds **166 files**, 36 of them `compose_`.
  - `src/core` has 97 files whose clusters are small (`interaction_` 8, `lane_` 6), so it can stay
    flat for now.
- **FL2 — Proposal: folders that match the subsystem owners.** For `src/sys`:

  | Folder | Contents |
  |---|---|
  | `sys/app/` | `main`, `application_*`, `frame_*`, `window`, `thread_priority`, `crash_backtrace`, `platform_*`, `macos_menu`, `frontend_*` |
  | `sys/commands/` | `application_command*`, `menu_command_projection`, `shortcut_rows`, `platform_shortcut_profile` |
  | `sys/preferences/` | `preferences_runtime`, `options_runtime`, `external_player` |
  | `sys/project/` | `project_*`, `chart_import_session`, the recovery notices, `workspace_*` |
  | `sys/audio/` | `audio*`, `sdl_*`, `transport_player_actions`, `tap_timing_runtime`, `inline_preview_cursor`, `preview_cursor_frame`, `chart_preview_state`, `waveform_*`, `stem_waveform_window`, plus the new `project_audio_sources` and `metronome` |
  | `sys/compose/` | the 46 `compose_*` names, with `actions/`, `input/` and `views/` subfolders (the `views/` hold the `build_*_view` projections, D13) |
  | `sys/resources/` | `sound_*`, `resource_*`, `bga_*` |
  | `sys/metadata/`, `sys/timing/`, `sys/bookmarks/` | their prefixes; `bookmarks/` is the T-619 owner |

  `src/ui/nuklear` gets the same treatment, with `nuklear/compose/` for its 36 `compose_` files.
- **FL3 — Keep the prefixes in file names** (`sys/compose/compose_input.cpp`, not
  `sys/compose/input.cpp`). The folder adds the grouping. The prefix keeps every file name unique
  across the project, so grepping or searching by file name, editor tabs and `#include` lines stay
  unambiguous. That matches the owner's "grepability" goal. Dropping prefixes would produce several
  `input.cpp` and `actions.cpp` files.
- **FL4 — How to move without losing history.**
  - Do it **last** (after T-619), because every earlier card edits these files.
  - Use pure `git mv` commits with only include-path and CMake edits: 583 `#include "sys/…"` lines
    and about 98 CMake source entries. Keep one commit per folder.
  - Add each move commit to `.git-blame-ignore-revs`.
  - Check that the build, CTest and clang-tidy (`compile_commands.json` paths) all still pass.
- **FL5 V — A stray.** `src/common/Chart.h` is the only capitalized header name in `src/`. Lowercase
  it in the same pass.

### RS — One result shape, as the guideline asks (raised by the owner)

- **RS1 V — The guideline exists; the code doesn't share a type for it.** `decisions.md:9`: "Rust-like
  explicit error/value flow and readable monadic pipelines are welcome; expected failures remain
  values rather than control flow hidden in exceptions." The code follows the *values* half, with
  **158 `*_result_t` structs**, but each one hand-rolls its shape:

  | Shape | Count |
  |---|---|
  | `diagnostics` list (± `applied`/`ok` flag) | 63 |
  | `status` enum only | 36 |
  | `status` + `std::string message` | 32 |
  | a payload bundle only, with no failure field | 27 |

  There are **125 hand-written `explicit operator bool`**, using more than 15 different success rules:
  `diagnostics.empty()`, `!has_errors()` (which accepts warnings), `status == ready/projected/started
  && token`, `error == none`, `applied && diagnostics.empty()`, `index.has_value()`, `!path.empty()`, …
  Nothing gives the "monadic pipelines" half (`and_then`/`transform`). Every file that builds a
  failure has its own builder: the 13 `fail`/`failure`/`failed`/`rejected` helpers (AP6/AP7).
- **RS2 — Proposal: two shared types in `include/wormedit/core/result.h`.**
  - **`result_t<T, E>`**, where `E` is usually `error_t<Code> { Code code; std::string message; }`,
    for the status and status+message results (≈68). It is API-compatible with `std::expected`
    (`has_value`, `value`, `error`, `and_then`, `transform`, `or_else`), with static builders
    `result_t::ok(value)` and `result_t::fail(code, message)` that replace the per-file
    `fail`/`failure` helpers. One success rule: `has_value()`.
  - **`diagnosed_t<T>`** `{ T value; diagnostic_list_t diagnostics; }` for the diagnostic results,
    with **one** rule, `ok() = !has_errors(diagnostics)`. Warnings can travel with success, which
    `std::expected` cannot express. `document_command_result_t` becomes
    `diagnosed_t<command_effect_t>` in T-607/T-608, and `command_result_with_t<Payload>` builds on it.
  - **Payload-only "results"** (27) are not results. Rename them to what they are
    (`*_view`, `*_index`, `*_plan`) under T-612/T-617.
- **RS3 — Behaviour constraint.** Where a type's current success rule differs from its new shared
  type, keep the old rule at the call sites and record the difference on the card. The main case is
  `diagnostics.empty()`, which rejects warnings, versus `!has_errors()`, which accepts them: a type
  that rejects warnings today keeps doing so.
- **RS4 — Standard library choice (decision D14).** `std::expected` needs C++23 (GCC 12+, Clang/libc++
  16+, Xcode 15+). The project builds as C++20 (`CMakeLists.txt:16`). Local GCC is 16.2, but the
  mingw cross toolchain and the macOS adapter versions are not pinned in the repo.

### CL — A Linux clang build so the Mac isn't the only clang/libc++ check (raised by the owner)

- **CL1 V — Every preset uses GCC/libstdc++ or mingw.** `CMakePresets.json` has `headless`,
  `headless-events`, `linux-cross-headless`, `windows-cross-headless` and `windows-cross`. Clang
  (Apple clang with libc++) is exercised only on macOS, so clang-only diagnostics and libc++ gaps
  first show up there.
- **CL2 V — What is available locally.** clang 23.1.1 builds a C++23 `std::expected` program
  against libstdc++. **libc++ is not installed** (`<expected>` not found with `-stdlib=libc++`). The
  code relies on the threading features where libc++ and Apple's libc++ lagged libstdc++:
  `std::stop_token` 88 uses, `std::jthread` 5.
- **CL3 — Proposal (card T-622).**
  - Add a `linux-clang` preset (clang++, libstdc++, warnings at the same level) and a
    `linux-clang-libcxx` preset (clang++ with `-stdlib=libc++`, the closest Linux stand-in for
    Apple's toolchain).
  - `linux-clang` joins the per-card gate (build plus full CTest) from then on.
  - `linux-clang-libcxx` becomes gated once the owner installs libc++ (Arch: `libc++`). Until then,
    the card records it as configured but not run.
  - Fix clang-only warnings and errors; do not silence them.
  - It runs right before T-620, so the C++23 standard raise is checked on GCC, clang and mingw
    together.

### BK / ST — Bookmarks and BMSON stems lack a sys owner, like the metronome (raised by the owner)

- **BK1 V — Bookmarks.** `bookmark_t` is core data (`document.h:522`), and editing and navigation are
  in core (`marker_editor`, `marker_navigation`). The sys side is spread over:
  - `compose_bookmark_commands`, `bookmark_rename_commands` and `bookmark_chooser_projection`;
  - the bookmark handlers in `compose_marker_actions` and in `application_command_dispatch`;
  - state in the command state's `transport` group (`last_playing_bookmark_seek`);
  - three `std::function` services (`open_bookmark_chooser`, `transport_preserve_bookmark_revision`,
    `bookmark_clock`);
  - the RW2 row-conversion copies (`absolute_row_of`, `local_position_for_row`, `row_for_position`).

  The UI side (`bookmark_ui_t`: chooser, rename, move state) is cohesive.
- **BK2 A — Proposal.** One sys `bookmark_navigation_t` owner (a subsystem member under T-603/T-605)
  holding the seek record, the clock and the chooser/rename/seek commands. The services become member
  calls, and the sys bookmark files are named `bookmark_*`. Confirm the full inventory on the card
  before moving anything.
- **ST1 V — BMSON stems.**
  - Editing is in core (`bmson_sound_channel_editor`) and audition in audio (`bound_stem_audition`).
  - Analysis is owned by `waveform_analysis_t` (`service_stem_waveforms`, `stem_needs_analysis`,
    …; `waveform_analysis_runtime.h:56–132`).
  - Display state is scattered across `compose_scene_cache_t`: `draw_stem_sources`,
    `waveform.stem_index`, `stem_waveforms`, `packed_waveforms`, `stem_waveform_views` and
    `stem_cut_views` (`compose_view.h:133–311`).
  - The window math is in `stem_waveform_window.h`, and the projection in
    `bmson_waveform_projection.cpp` and `compose_draw_source_projection.cpp`.
- **ST2 A — Proposal.** Group the display state into one `compose_scene_cache_t::stems` sub-struct
  with `stem_for(channel)` (NV6) and `packed_for(…)` members. Keep analysis in `waveform_analysis_t`,
  editing in core, and the views as `build_stem_waveform_view` (D13).

### TI — Seconds ↔ row queries belong on `timing_index_t` (raised by the owner)

- **TI1 V — The core queries are free functions.** `timing_at(index, row[, side])`, which gives
  seconds, and `musical_position_at(index, seconds)`, which gives the exact row (`timing.h:155–204`),
  are called 81 times. `timing_index_t` (`timing.h:64`) has no members for them, so callers wrap them.
- **TI2 V — The sys wrappers repeat three steps:**
  1. convert the exact rational to `double`/`long double`;
  2. clamp at the chart end;
  3. extrapolate past the last row at the final BPM.

  The wrappers:
  - `tail_row_at_seconds` (`compose_hover_timing.cpp:36`): extrapolates the tail with 128-bit
    rational math;
  - `packed_tail_seconds_per_row` (`stem_waveform_window.h:258`): the same tail-rate idea, from
    `rows.back().effective_bpm` with a hand-written 240/BPM/divisions;
  - `row_for_time` (`compose_minimap_navigation.cpp:11`): clamps to `[0, total_rows]`;
  - `preview_playhead_row_at` (`audio_transport_frame.cpp:84`): frames → seconds, then clamps to
    `total_rows - 1`;
  - `stem_seconds_at` and `packed_seconds` (`stem_waveform_window.h:315`, `:47`): row → `double`
    seconds;
  - `bookmark_preview_frame`, `chart_visual_end_frame`, `fit_visible_source_frame` and others: 41
    declarations in all take `const timing_index_t&`, 16 of them outside `timing.cpp` and
    `timing.h`.

  **The end-of-chart rule differs between them** (clamp to `total_rows`, clamp to `total_rows - 1`,
  extrapolate, or reject), and nothing names those choices.
- **TI3 — Proposal: members on `timing_index_t`, with the end rule as a named policy.**
  - Exact: `seconds_at(row, side)` and `row_at(seconds)`, which forward to today's functions.
  - Display: `seconds_at_approx(row) -> std::optional<double>` and
    `row_at_approx(seconds, chart_end_policy_t) -> std::optional<double>`, where
    `chart_end_policy_t { reject, clamp_to_end, clamp_to_last_row, extrapolate_tail }`.
  - `tail_seconds_per_row()`: one home for the final-BPM rate.
  - Frames: `row_at_frame(frame, sample_rate, policy)`.

  Each sys wrapper becomes a call with the policy it uses today, so behaviour is unchanged and the
  choice becomes visible.
- **TI4 — Do the same sweep for the other index types.** `measure_index_t` is covered by RW. T-623
  also checks `editor_field_scene_index_t`, `editor_hit_test_index_t`, `scroll_speed_index_t` and
  `marker` indexes for free-function queries copied in sys, using the behaviour-based search from
  AP8.

### EA — Chart event edit actions: the same three structs per kind (raised by the owner)

- **EA1 V — Twelve kinds, each with an insert/update/delete trio.** The kinds are BPM, bookmark,
  section, kiai, scroll, speed, pause, warp, timing anchor, SV recipe, lane layout and semantic layer
  (measure and sound group have pairs). For every kind:
  - `insert_<kind>_action_t { chart; <value fields> }`;
  - `update_<kind>_action_t { chart; <id>; <the same value fields again> }`;
  - `delete_<kind>_action_t { chart; <id> }`;
  - a `<module>_edit_action_t` variant;
  - an `apply_<module>_edit(history, expected_revision, action, limits)`.

  Examples: `bpm_editor.h:37–56`, `scroll_speed_editor.h`, `pause_warp_editor.h` and
  `timing_anchor_editor.h:~20–47`. The id field is called `event` in some and `anchor` in others.
  There are 13 per-module `*_edit_action_t` variants.
- **EA2 V — A third vocabulary for the same values.** `timing_lane_controller.h:37–73` defines
  `timing_bpm_value_t`, `timing_scroll_value_t`, `timing_speed_value_t`, `timing_pause_value_t`,
  `timing_warp_value_t`, `timing_kiai_value_t` and `timing_anchor_value_t` in a
  `timing_lane_value_t` variant. `timing_lane_controller.cpp` translates them into the per-editor
  action structs at 21 construction sites. The same values are therefore spelled three ways: the
  document events (`bpm_event_t` and others), the editor actions, and the timing-lane values.
- **EA3 — Proposal: generic edit actions over one value type per kind** (in `command.h`, beside
  the transaction):

  ```cpp
  template <class Value> struct insert_edit_t { chart_id_t chart; Value value; };
  template <class Value> struct update_edit_t { chart_id_t chart; object_id_t id; Value value; };
  struct delete_edit_t { chart_id_t chart; object_id_t id; };
  template <class Value> using chart_edit_t =
      std::variant<insert_edit_t<Value>, update_edit_t<Value>, delete_edit_t>;
  ```

  - `Value` is the kind's value type: the existing `timing_*_value_t` plus position or span, or
    `<kind>_value_t` for the non-timing kinds. Each editor keeps
    `apply_<module>_edit(history, stamp, chart_edit_t<…>)`, run through the T-607 transaction.
  - The timing-lane controller passes its values straight through, so the 21 translations go.
  - The id field is always `id`.
  - Undo labels and per-kind validation stay in each editor.
  - Modules that edit two kinds (scroll+speed, pause+warp) keep a variant of two `chart_edit_t`s.
  - These are in-memory only: no file or ABI format changes.

### TX — ASCII case folding under eight names (raised by the owner)

- **TX1 V — One operation, eight spellings.** T-597 added `text_util::ascii_fold`
  (`src/core/text_util.h:65`), but the HL5 sweep found only copies named `ascii_fold`. The others
  survive under different names:
  - two forwarders that only wrap `text_util::ascii_fold` because it lacks a `string_view`
    overload: `ascii_fold` in `document.cpp:1526` and in `sound_group_proposal.cpp:14`;
  - independent copies:
    - `fold_ascii` (`sound_groups.cpp:28`);
    - `fold_group_name` (`sound_import.cpp:354`);
    - `ascii_lower` (`slice_spans.cpp:26`, `project_open.cpp:92`);
    - `folded` (`bga_list_projection.cpp:9`, using `std::tolower`; ASCII only because nothing calls
      `setlocale`);
    - `folded` (`audio_panel.cpp:407`, `+ 32`);
    - the per-character `folded` in `natural_order.cpp:6`;
  - three extension lowercasers: `lowercase_extension` (`project_open.cpp:32`), `lower_extension`
    (`sound_import_state.cpp:14`) and `normalized_extension` (`audio_project_import.cpp:270`).

  About 56 more inline lowercasings appear in 35 files.
- **TX2 V — The name over-promises, and one name lies.** "Fold" suggests Unicode case folding, but
  every copy lowercases A–Z only, and non-ASCII names do not fold. `folded_name`
  (`compose_sound_actions.cpp:44`) does not fold at all: it returns the file name, which is then
  compared with `natural_name_less`.
- **TX3 V — Most callers only want a case-insensitive comparison.** Sound-group and import
  duplicate checks build two lowered strings in order to compare them, and the document uniqueness
  check builds a set of lowered names. The proposal saves allocations as well as names:
  - `src/core/text_util.h` gains `ascii_lowercase(char)` and
    `ascii_lowercase(std::string_view) -> std::string`;
  - `ascii_iequal(a, b)` and `ascii_iless(a, b)`, which do not allocate;
  - `lowercase_extension(const std::filesystem::path&)`.

  Every copy and forwarder is deleted, and `folded_name` is renamed `file_name_of`. The vocabulary
  table records that `fold` is reserved for real Unicode folding.
- **TX4 — Lesson for the audit method.** Varying names hid these copies from a name-based sweep. In
  T-612, search for these helpers by what their bodies do, not by their names: the `'A'`…`'Z'`
  range, `std::tolower` and `+ ('a' - 'A')` patterns.

### SP — Staged proposals (part 2, raised by the owner)

- **SP1 V — A second command shape.** Begin (validate, then build a stamped proposal) → revise →
  apply (stale check, then commit) or cancel. Example: `quick_insert_timing_event`
  (`main.cpp:1802`) is `begin_timing_stage` followed at once by `confirm_timing_lane_pseudonote`.
  The staged path keeps the proposal in `timing_edit.stage` across frames and re-checks it in
  `validate_timing_stage_binding` (`main.cpp:1716`).
- **SP2 V — The same concept, spelled differently each time.**

  | Proposal | Stamp fields | Validity | Verbs |
  |---|---|---|---|
  | `timing_lane_pseudonote_preview_t` (`timing_lane_controller.h:85`) | `chart`, `document_revision` | `status` enum, `bool valid` and `diagnostics` | begin (sys) / confirm / cancel |
  | `sv_recipe_draft_t` (`sv_recipe.h:63`) | `chart`, `document_revision` | `status` and `diagnostics` | begin / apply |
  | `lane_migration_proposal_t` (`lane_migration.h:48`) | `song`, `chart`, `revision` | `issue` enum | plan / apply |
  | `tap_timing_apply_proposal_t` (`tap_timing_apply.h:13`) | `chart`, `document_revision` | `diagnostics` | build / apply |
  | `chart_paste_proposal_t` (`chart_clipboard.h:74`) | `chart`, `revision` | `bool valid` | propose / … |

  Also in this family:
  - `channel_map_proposal_t`;
  - `sv_recipe_range_drag_t`, `beat_interval_drag_t` and `audio_offset_drag_t`;
  - `recipe_form_preview_t`;
  - the drafts nested in sys `application_command_state_t`: BMSON marker draft, BGA and razor
    strokes, paste session, quantize editor.

  That makes about 15–20 lifecycles. They use 4 opening verbs (`begin`, `stage`, `start`,
  `propose`), 6 closing verbs (`apply`, `commit`, `confirm`, `finish`, `end`, `release`) and 3 for
  discarding (`cancel`, `discard`, `reset`).
- **SP3 — Proposal.**
  - Every proposal embeds the same stamp (`chart_stamp_t`, or `document_stamp_t` for whole-project
    proposals) and one `diagnostics` list.
  - A status enum stays only where it is a real state machine (inactive/editing/…). A `bool valid`
    alongside status and diagnostics goes.
  - The verbs are fixed: `propose_`/`begin_` → `revise_`/`update_` → `apply_` or `cancel_`.
  - `apply_X(history, proposal)` opens `document_command_transaction_t(history, label, proposal.stamp)`.
    The stale check and the commit come from the transaction, which is the "legitimate later" class
    of RV2.
  - Out of scope: the core pointer gestures (note move, duration, paint, area selection). They
    already share the gesture lifecycle from T-586 and T-598, and only adopt the stamp type.

### NV — Naming vocabulary (part 2)

- **NV1 V — Prefix counts** for value-creating functions: `make_` 178, `build_` 60, `default_` 18,
  `initial_` 13, `create_` 10, `empty_` 5, `new_` 4. `decisions.md:11` fixes the spelling (snake_case,
  `_t`) but not which verb means what.
- **NV2 A — The rename set is small.**
  - **About 14 functions** construct a project value without `make_`:
    - in `application_defaults`: `audio_project_document`, `initial_document` (two overloads),
      `empty_session_document`, `default_output_groups`;
    - `default_external_player_preferences` (core plus a sys wrapper) and
      `default_program_preferences`;
    - `default_input_profile_for`, `initial_input_profile`, `initial_profile_layout` and
      `initial_timing_value_for_stage`;
    - `new_quarantine_path`;
    - `build_proposal` (tap timing), `build_tap_timing_preview` and `build_slice_span_plan`.
  - **About 9 borderline cases**, including `open_failure` and `open_file_identity`, which open
    nothing.

  The ~20 `create_`/`open_` functions with real side effects keep their names, and so do the
  `build_*` derived-index builders (`build_measure_index`, …).

## Pattern catalog (sweep, 2026-10-12)

The owner noticed the same problems recurring and asked for them to be laid out explicitly. Six
Haiku probes swept every layer (core editors, core model, sys compose, the rest of sys, the Nuklear
UI, and formats/audio/visual) against the ten patterns below. They searched by **behaviour** (bodies,
field sets, call shapes) rather than by name. Claude checked the key claims (marked V) and rejected
one: `sdl_session_start_result_t` *is* instantiated, by both SDL outputs. Everything else is a lead
(A) that its card must confirm. This catalog is also the **review checklist for new code**.

| # | Pattern | Already in this report | New instances from the sweep | Card |
|---|---|---|---|---|
| P1 | **A helper copied under another name** | TX (ASCII fold), RW (row conversion), TI (timing wrappers) | **V** `minimum_viewport_scroll` ×3 and `normalized_position_span` ×2 in `interaction*.cpp`, although `interaction_internal.h:146` declares one (the T-598 split copied instead of sharing) · **V** `branch_scope_node`/branch-order collector identical in `channel_map_editor.cpp:217` and `lane_migration.cpp:457` · **V** base-36 digit ×5 with different case and return rules · **V** `bms.cpp:95` `utf8_error_offset` = `text_util::invalid_utf8_offset` · **V** decimal parsers ×3 in formats vs core `parse_exact_decimal` (exponent caps 18/38/int32) · **V** `rational_as_long_double` ×2 identical (`compose_waveform_overlays.cpp:17`, `timing_view.cpp:81`), plus 8+ unguarded inline divisions · A: UTF-8 continuation/truncate ×10, `trim` ×6, bounded-text validity ×6, more fold copies (`fold` ×3, `uppercase_ascii`), `account_size` forwarders ×3, gcd/128-bit math ×4, seconds↔frames ×4, rows→beat fraction ×2, round-and-clamp row ×4, ring-slot modulo ×4, `layout_at` ×3, editable-lane predicate ×5, `find_active_lane` ×2, `snapped_position` ×2, `is_switch_branch` ×3, `valid_pause_unit`/`valid_warp_encoding` ×2, `next_source_order` ×3, `is_playing` ×2, scroll→visible-row range ×5, JSON SAX limiter ×2, `parse_meter_part` vs `parse_integer` | T-625, T-627, T-628 |
| P2 | **Per-kind struct families** | EA (insert/update/delete trios) | A: visual decode results (image/video/session/job), text-codec result twins ×3, thrown `fail()` diagnostic structs ×3 | T-620, T-624 |
| P3 | **Parallel vocabularies** | EA2 (`timing_*_value_t`) | **V** `editor_selection_object_kind_t` → `timing_lane_kind_t` mapped with `default: nullopt` (`chart_clipboard.cpp:227`), so a new kind is silently dropped · A: `semantic_layer_rgba_t` (0..1) vs `color_rgba_t` (bytes); UI `nk_color`/`field::color_t`/`arrow_color_t`/`key_color_t`; overlapping `image_status_t`/`video_frame_status_t`; recipe sampling-mode enum cast by integer to the C ABI; two `visual_event_kind_t` slot tables (different by design, to be named) | T-611, T-628 |
| P4 | **Missing members on a type** | RW (`measure_index_t`), TI (`timing_index_t`), NV5/NV6 | **V** **110 inline chart lookups** (`ranges::find(charts, id, &chart_document_t::id)`): T-591 merged the `find_chart` *definitions* only · **V** `target_resource_identity_t` has no `operator==` (3 hand-written comparisons) · A: `find_note`/`find_bgm_event` ×4 + ~10 inline, `find_audio_asset` ×9 inline, lane `find`/`has_lane` ×12, lane-layout equality ×3, per-projection id→pointer maps (`packed_geometry_builder_t` and others), `has_beat/audio_marker_navigation` twins | T-626 |
| P5 | **Local outcome/result types and failure builders** | CT, AP6/AP7, RS | **V** `sound_feedback_t`/`bga_feedback_t` plus their builders are identical (`compose_sound_actions.cpp:25`, `compose_bga_actions.cpp:11`) · **V** the severity mapping hand-written 12× in sys · A: six local `reject` builders that disagree on `applied`; `sv_recipe`'s three-level reject ladder; identical proposal `fail()` ×2; `{changed, label}` outcome ×3 plus a bare `bool changed` in ~12 results; `path_result` twins ×2; SDL service results ×2; formats failure builders ×3 | T-608, T-610, T-618, T-620 |
| P6 | **Concept with no owner** | OW, AS, MT, BK, ST | A: pending import paths (`pending_audio_project_path`, `pending_chart_project_path`, `pending_bms_encoding_path`) as loose strings; UI `keysound_filter_popup_open` is a stored derived flag written at 6 sites | T-602, T-628 |
| P7 | **Duplicated fact tables** | FT | **V** **the tab names disagree, and it is visible**: the tab strip says "Project" and "Audio / Slices" (`workspace_projection.cpp:75`), while the commands say "Setup" and "Audio Slices" (`command_registry.cpp:929`) · **V** the sound import dialog labels a keysound "Sound", while the audio panel says "Keysound" · **V** the note palette is raw ints `{0,1,4,3,5,6,2}` (`compose_toolbar.cpp:485`) plus a separate core label table · **V** the external player preview set omits `.bml` (`external_player.cpp:100`) · A: step types and the 1..18 key range ×3; audio extension list ×3; `bms_line_ending_t` switch ×4; chart-object limit 1'000'000 in 8 option structs + ~12 literals | T-611 |
| P8 | **Unpack-and-rebuild contexts** | AP4 | A: `compose_frame_inputs_t` rebuilt field by field, `compose_chart_scope_t` rebuilt at 5 sites, four (frame, scope) wrapper structs, a nested `channel_ring_context_t` | T-627 |
| P9 | **Naming** | NV, NV3, TX2 | A: ~30 more yes/no checks in core editors and ~10 in compose; four `normalize` functions with different contracts; `bookmark_rename_editor_t` also edits note and section labels; the `*_owns_input` style; `PopupContextStamp` built at ~9 sites under 6 names | T-616, T-628 |
| P10 | **Hand-rolled code a dependency provides** | D10 (tags), WAV writer (noted) | A: point-in-rect ×5 and rect overlap ×4 (SDL3 has `SDL_PointInRectFloat`/`SDL_GetRectIntersectionFloat`; check the pinned SDL version) | T-628 |

**Behaviour differences the sweep exposed.** Preserve each one unless a decision below says
otherwise, and record it on its card:
- **V** `sound_slice.cpp:22` `subtract_exact` uses 64-bit checks and returns `nullopt` where
  `timing_arithmetic`'s 128-bit sum succeeds;
- the guarded rational→float conversion returns 0, while the unguarded copies give inf/NaN;
- `clamp_row` clamps both ends, while the other copies clamp only the top;
- the visible-row-range copy at `aligned_waveform.cpp:489` skips the floor/ceil, so it can be off by
  one row;
- `trim` copies use different whitespace sets;
- two bounded-text checks skip UTF-8 validation;
- UI float→byte conversion truncates in one place and rounds in another;
- **V** `dialog.cpp:199` `wrap_dialog_text`: its comment says long words are split so a line never
  overflows, but the code never splits, so a long path without spaces overflows the dialog.

## Proposed shape

```cpp
// src/sys/application.h — owned once, heap-allocated in main(), non-copyable, non-movable.
class application_t {
public:
    explicit application_t(application_startup_t&);  // ownership and dependency order only
    auto start() -> bool;                            // side effects: prefs, recovery scan, outputs, registry
    auto run_frame() -> void;                        // named phases (OW5)
    auto shutdown() noexcept -> void;                // SD1 order, idempotent; also called by ~application_t
    bool running = true;
private:
    platform_t platform;              // notifications, sink, application_runtime_t (+ RAII SDL teardown), crash marker guard
    document_session_t session;       // history, workspace, project identity (PB), recovery, autosave
    background_jobs_t jobs;           // schedulers first, then copy/metadata/export runtimes, image/video jobs
    audio_runtime_t audio;            // one #if WORMEDIT_HAS_AUDIO_DECODER unit, SD4 member order
    preferences_t preferences;        // program prefs, external player, options, shortcut editor
    application_command_state_t commands; // services / preferences / project groups (LT)
    command_registry_t command_registry;  // declared after everything its invoker reaches
    editor_views_t views;             // interaction state, controllers, scene caches, timing grid/edit
};
```

- Member order is construction order. Reverse destruction then satisfies the SD4 constraints.
  `shutdown()` covers what destructor order cannot: cancel before join, metadata cleanup after the
  join, outputs closed and all workers joined **before** platform teardown, and the crash marker
  removed last.
- Stored callbacks capture `this` or one subsystem, and call named member functions.
- `application_command_state_t` becomes `{ services; preferences; project; }`. Services become a
  member instead of a base. The project reset becomes `commands.project = {};`.
- `main()` becomes: parse arguments; `auto app = std::make_unique<application_t>(...)`; `start()`;
  `while (app->running) app->run_frame();`; `shutdown()`.
- Handlers take the narrowest owner they need: `application_t&` only for session replacement and
  top-level dispatch, a subsystem reference otherwise, or a two- or three-reference stack bundle.
  Do not recreate the 36-reference mirror.

### Part 2 shape: transaction, stamps, catalogs, vocabulary

```cpp
// include/wormedit/core/command.h — a value type, no virtuals.
struct document_stamp_t { song_id_t song; std::uint64_t revision{}; };
struct chart_stamp_t    { document_stamp_t document; chart_id_t chart; };
[[nodiscard]] auto is_current(const document_stamp_t&, const document_history_t&) -> bool;
[[nodiscard]] auto is_current(const chart_stamp_t&, const document_history_t&) -> bool;

class document_command_transaction_t {
public:
    // No stamp: a synchronous internal edit. With a stamp: an earlier-captured or delayed edit.
    document_command_transaction_t(document_history_t&, std::string label,
                                   std::optional<document_stamp_t> stamp = {},
                                   document_command_options_t = {});
    [[nodiscard]] bool ready() const noexcept;               // false: stale, or rejected
    void reject(diagnostic_code_t, std::string);
    [[nodiscard]] const chart_document_t* source_chart(chart_id_t) const;  // read before copying
    song_document_t& copy_document();                         // the one existing copy, made lazily
    chart_document_t* chart(chart_id_t);                      // lookup in the working copy
    [[nodiscard]] document_command_result_t result() const;
    [[nodiscard]] document_command_result_t commit();         // moves into commit_document_command
};

template <class Payload> struct command_result_with_t {
    document_command_result_t command; Payload payload;
    explicit operator bool() const noexcept { return static_cast<bool>(command); }
};
```

- **One boundary check per entry point.**
  - Compose view: at its entry.
  - Compose input: **per batch item** (RV3).
  - Metadata, project and workspace: their first check becomes the boundary.
  - Application commands: the existing stamped-menu check.

  Handlers below the boundary do not check again. Core commands called synchronously take no stamp.
- **Keep the legitimate later checks:** gestures from earlier frames, preview and proposal apply,
  timing-stage completion, async results, cache keys, workspace identity, and later batch items.
- **One message helper**, `stale_action_message(label)`, produces "<label> ignored because the
  document changed." A missing target stays a separate, non-stale error.
- **Catalogs.** One core table per concept with a stable id and short and long labels:
  - new-project layouts, each with its profile factory, default output groups and an "offered"
    flag;
  - playback rates;
  - BMS encodings;
  - format families, with "offered as output" and "offered as metadata" flags;
  - timing lanes, guarded by a static_assert against the enum count.

  sys passes labels and ids through the view, and actions carry the **id**, not a list position.
- **Vocabulary** (added to `decisions.md`):

  | Prefix | Meaning |
  |---|---|
  | `make_` | builds a new value with no side effects |
  | `build_` | computes a derived index or cache |
  | `create_` / `open_` | has side effects (files, sessions, OS) |
  | `plan_` / `prepare_` | computes a proposal; changes nothing |
  | `validate_` | checks and returns diagnostics; changes nothing |
  | `apply_` | an undoable command, run through the transaction |
  | `propose_` / `begin_` → `revise_` → `apply_` / `cancel_` | the lifecycle of a staged proposal (SP3) |
  | `is_` / `has_` / `can_` | yes/no checks; comparators keep `*_less` and `*_equal` (NV4) |
  | `T::from(...)` / `T::with_defaults()` | the single named factory of an aggregate type (NV5) |
  | `owner.<x>_for(key)` | a find-or-add accessor on the container's owner (NV6) |

  T-612 also adds this rule to `decisions.md`, next to the existing "methods on structs are welcome"
  line (owner-approved, 2026-10-12): **"A type's construction (`T::from`, `T::with_defaults`) and its
  basic queries (`is_`/`has_`, `row_of`, `entry_for`, …) live on the type. Structs stay mostly
  public aggregates. Free functions are for operations spanning several types, for catalogs, and
  for commands."** The reason is navigation: when the project was small, free functions spread
  across headers were fine; at its current size, a reader should find how to build and query a type
  by opening the type.

## Necessary complexity (keep)

- **Teardown order.** Cancel jobs before joining the scheduler. Metadata cleanup comes after the
  copy-scheduler join. Close audio outputs before sessions and before SDL. Destroy notifications
  last. Destroy the probe scheduler after the resident loader.
- **The project generation bump comes first** in the project reset.
- **The flat `WormUi*` ABI** (`application_runtime_t` keeps `WormWindow`/`WormAudioSystem`).
- **Per-frame projection stays on the stack.** `compose_scene_cache_t::frame` is rebuilt every frame
  by design. The tap timestamp and viewport delta stay frame-local.
- **Chart-keyed maps** (snap denominators, BGM lane mix) are project-scoped containers of per-chart
  entries. Keep them in `project`.
- **Fine-grained stamp checks** that guard actions against stale input stay where they are, even
  after grouping.
- **The `#if` decoder feature blocks.** They collapse into one subsystem each but remain.

- **Staged edits stay as orchestration around the transaction:** scratch histories, gesture undo
  merging (`merge_recent_undo_records`), and previews whose payload depends on derived ids.
- **Revision checks on state captured earlier** (RV2 "legitimate later") and per-item checks inside
  compose input batches.
- **Distinct result types** for plans, proposals, parses, queries, storage and dispatch (CT3).
- **`build_*` for derived indexes; `create_`/`open_` for functions with side effects.**

## Rejected or downgraded

| Claim | Verdict |
|---|---|
| Luna 2/3: move `y_lock`, `edit_snap_enabled`, timing focus and mask, and player preview visibility and skin into session preferences | **R** as a refactor. They reset per project today. Moving them changes behaviour, so it is decision D3. |
| Luna 3: the allowlist keeps 12 fields | **R**. It keeps 11 (counted in source). |
| Luna 4: keep `application_dispatch_context_t` as a flat borrowed view | **R** as an end state. It is acceptable only as a temporary step inside the `application_t` card, built by one factory function instead of two literals. |
| Haiku: merge the redundant explicit `cancel()` calls in shutdown into destructors | **R**. Keep them explicit inside `shutdown()`. The order is what is being documented. |
| Haiku: centralise `invalidate_playing_seek_records` and `clear_beat_fit_capture` into one boundary call | Downgraded. These are event-path invariants with different triggers. Leave them unless a card already touches the path. |

| A virtual `validate()/apply()` command interface | **R**. Commands take very different arguments, it would add vtables, and `decisions.md:12–15` rules out routine hierarchies. The transaction value type gives the same readability. |
| Collapse all 72 `*_result_t` types | **R**. Only the nine command-plus-payload shapes collapse (CT3). |
| Luna 6: treat every handler recheck after the boundary as redundant | **R** for compose input batches (RV3). |

## Decisions for the owner

**Decision log (owner, 2026-10-12).** These override the defaults and recommendations below.

| Decision | Answer | Effect on the cards |
|---|---|---|
| D1 | Fix | T-601 keeps `scratch_on_right` derived from the display preferences and outside the project reset. Add a regression test that fails today. |
| D2 | Fix | In T-602, recovery clears the external player's source path through the single project boundary. Add a regression test. |
| D3 | Keep resetting | Snap toggle, Y-lock, timing focus and mask, player preview visibility and skin, and dock width go in `project`. |
| D4 | Keep separate | The Sounds-panel preview-on-click and the editor audition-on-click stay distinct. T-601 and T-612 add a comment at both declarations saying so. |
| D5 | Join first | In T-603, `shutdown()` joins every worker (autosave scheduler, image and video jobs, waveform scheduler) before platform and SDL teardown. |
| D6 | No | The Nuklear UI state split is out of scope. |
| D7 | Everything core builds | In T-611, the New Project catalog offers every layout core can build: osu!mania 1–18K, the DP/double-scratch BMS profiles and the PMS profiles. Each entry has its label, profile factory and default output groups (StepMania groups only where a step type exists). Layouts wider than ten lanes start with no key bindings, as `make_osu_mania_profile` already does. Record this on the card. |
| D8 | Unify | In T-609, one `stale_action_message(label)` produces every message; tests that assert the old strings are updated. A missing target stays a separate error. |
| D9 | No | In T-611, MIDI and Quaver are in the format catalog but marked not offered for output. |
| D10 | (a) libavformat | T-613. |
| D11 | The ship preset requires FFmpeg | T-613 adds a release preset (Linux and Windows) that finds FFmpeg with `REQUIRED`; the dev, headless and test presets stay tolerant. Cross-building FFmpeg for Windows is a setup step for the owner: record it on the card, and do not attempt it in the sandbox. |
| D12 | Test first, then reconcile on demand | T-614 adds a test for the same-frame window. If it reproduces, `project_audio_sources_t` queries reconcile when the document revision has moved since the last reconcile (a revision compare per query; the reconcile itself only in the same-frame case), shipped with that regression test. No STOP (owner, 2026-10-13). |
| D13 | A | In T-612, projections become `build_<x>_view`. |
| D14 | (a) C++23 | In T-620, raise the standard first, in its own commit; `result_t` is an alias of `std::expected`; the macOS Xcode 15+ check is left for the owner. |
| D15 | "Project" and "Audio Slices" | In T-611, one tab table owns the label, id and enum. The labels are "Project" (not "Setup") and "Audio Slices" (no slash), used by both the tab strip and the commands. Command *ids* (`workspace.open_*`) keep their names. |
| D16 | "Keysound" | In T-611, one sound-kind label table; the import dialog shows "Keysound". |
| D17 | Allow `.bml` | In T-611, the external player preview uses the shared BMS extension table, which includes `.bml`. Add a regression test. |
| D18 | Fix the code | In T-628, `wrap_text` splits overlong words at UTF-8 character boundaries so a line never overflows; add a test with a long path that has no spaces. |
| D19 | Unify | In T-625, T-626 and T-628, each drifted copy moves to the most careful variant, with a regression test per difference: the floor/ceil row range, the guarded rational division (with one named zero-denominator policy), one float→byte rounding rule, an explicit whitespace set for `trim`, UTF-8 validation in every bounded-text check, and 128-bit `subtract_exact`. Record each changed call site on the card. |


- **D1** Fix LT2 (the scratch side reverts after project open or recovery)? Recommended: yes, keep the
  field derived from preferences and outside the project reset. Add a regression test.
- **D2** Make recovery clear `external_player_runtime.source_path` like replacement does (PB2)?
  Recommended: yes. This falls out naturally from the single project boundary.
- **D3** Should `edit_snap_enabled`, `y_lock`, compose timing focus and mask, player preview visibility
  and skin, and `compose_editor_right_pixels` survive a project open? Default: keep today's behaviour
  (reset).
- **D4** Are the Sounds panel "preview on click" and the editor "audition on click" meant to be
  separate?
- **D5** Join all workers (autosave scheduler, image and video jobs, waveform scheduler) before SDL
  teardown? Recommended: yes. It is a strictly safer order, but it is an ordering change.
- **D6** Go ahead with a Nuklear UI state lifetime split (UR6)? Recommended: later, as its own audit.

- **D7** Which layouts should the New Project wizard offer? Today it offers 4–10K and 14K. Core can
  build 1–18K osu!mania layouts, DP (BMS double scratch) and 18K PMS. Default: same list, now driven
  by the catalog.
- **D8** Unify the stale messages into one template? This changes user-visible text. Recommended: yes,
  and update tests that assert the old strings.
- **D9** Offer MIDI and Quaver as output-group families? Default: no. Conversion ignores them as
  targets (`conversion.cpp:686`), so record them as not offered in the catalog.

- **D10 — decided by the owner: (a).** "Shipped builds should have FFmpeg; we should not hand-roll
  these things." Replace the hand-written tag reader in `src/sys/audio_project_import.cpp`
  (ID3v2.2–2.4, ID3v1 and Ogg Vorbis comments with their own byte, UTF-8 and UTF-16 helpers, about
  250 lines) with `libavformat`: `avformat_open_input` + `avformat_find_stream_info` +
  `av_dict_get(... "title"/"artist")`, with the existing byte limits on the values. This also covers
  FLAC, WAV `LIST/INFO`, Opus and MP4. Audio decoding stays on miniaudio.
- **D11** Consequence of D10: both Windows presets (`windows-cross`, `windows-cross-headless`) set
  `WORMEDIT_ENABLE_VIDEO_DECODER=OFF`, so Windows builds have no FFmpeg today. Either cross-build
  FFmpeg for Windows and turn it on, or accept that Title and Artist are not prefilled there.
  Recommended: add a release/ship preset that requires FFmpeg (`REQUIRED` instead of `QUIET`), and
  keep the dev and headless presets tolerant.
- **Noted, no change: the hand-written WAV writer.** `src/audio/slice_render.cpp` builds RIFF
  headers by hand (16-bit and float variants, about 50 lines) because miniaudio is configured with
  `MA_NO_ENCODING` (`cmake/dependencies/miniaudio.cmake:51`). It is small, writes only, and is
  correct by construction. Revisit if more encoders are needed.
- **D12** Should project audio queries reconcile when the revision has moved since the frame's audio
  phase (AS3)? This closes a possible same-frame stale window, but it moves reconciliation work into
  the action phase. Default: no. Record it, and add a test that reproduces the window first.
- **D13 — decided by the owner: option A.** Rename every projection function from `project_<x>` to
  `build_<x>_view` (e.g. `project_bga_list` → `build_bga_list_view`, `project_application_frame` →
  `build_application_frame_view`), along with their `*_projection.{h,cpp}` files where a file holds
  only projections. After that, `project_` names the song project. The five noun uses keep their
  names. Add a row to the vocabulary table: "`build_<x>_view`: a read-only view of the document for
  the frontend".
- **D14 — decided by the owner: (a) C++23.** T-620 first raises the standard: `CMAKE_CXX_STANDARD 23` and
  every `cxx_std_20` → `cxx_std_23` (7 sites across `CMakeLists.txt`, `tests/CMakeLists.txt` and
  `cmake/`). `result_t<T, E>` becomes an alias of `std::expected<T, E>`. `diagnosed_t<T>` stays a
  project type, because `std::expected` cannot carry warnings alongside a success. Verified locally:
  GCC 16.2, the mingw cross GCC 16.2 and clang-tidy (LLVM 23). The macOS adapter needs Xcode 15+
  (libc++ `std::expected`); that is the owner's check, recorded on the card. Make the standard bump
  its own commit, with the full gate, before any `result_t` migration.
- **D15 — decided by the owner; see the decision log.**
- **D16 — decided by the owner; see the decision log.**
- **D17 — decided by the owner; see the decision log.**
- **D18 — decided by the owner; see the decision log.**
- **D19 — decided by the owner; see the decision log.**

## Suggested card breakdown

| Card | Scope | Findings |
|---|---|---|
| T-601 | Lifetime-group `application_command_state_t`: `services` member (no inheritance), `preferences` (exactly the 11 LT1 fields), `project` (everything that resets today). The reset becomes `project = {}`. Plus D1 if approved. | LT1, LT2, LT3, LT5, OW3a |
| T-602 | One project boundary (the pending import paths go into one `pending_import_t` owner): a `project_identity_t` with the eight PB1 fields plus the external player source path, and one `begin_project_session(...)` used by replacement and recovery. Generation bump first. Plus D2 if approved. | PB1–PB3 |
| T-603 | `application_t` owner in `src/sys/application.h/.cpp`: subsystems in dependency order, `start()`, an idempotent `shutdown()` also called from the destructor, RAII platform and crash-marker guard. Callbacks capture `this` or a subsystem. One context factory replaces the two literals. `main()` shrinks to about 30 lines. Plus D5 if approved. | OW1, OW2, OW4, SD1–SD4, CB1 |
| T-604 | Frame loop as named `run_frame` phases, and the close-confirmation callback as a member function. | OW5 |
| T-605 | Narrow handler inputs: remove `application_dispatch_context_t` and the frame-context inheritance; use subsystem references, subsystem member functions or small bundles; drop `_ref`; move `external_player_runtime_t` to its own header. Remove every `auto&` parameter and `template <typename Context>` in `main.cpp`. Record the before and after count of sys functions with seven or more parameters (76 today). | CB2, CB3, OW3b, OW6 |
| T-606 | Reset entry points: one Nuklear session-reset helper; the sound-import picker/session split, with the reset moved from a per-frame poll to the actual transition; a single scene-cache key check and invalidation. | UR1–UR3 |
| T-607 | `document_command_transaction_t` and `command_result_with_t<Payload>` in `command.h`, with a lazy single copy. Migrate the core editors for BPM, measures, markers, metadata, chart and project, and lanes. | CT1–CT4 |
| T-608 | Migrate the remaining commands: resources, BMSON, SV, timing lanes, notes and BGM. Staged edits remain orchestration around the transaction. Collapse the nine payload result types. Remove the `rejected()` builders and the inline `{.applied = false}` rejections (AP7). | CT1–CT3, AP7 |
| T-609 | Stamps and boundaries: `document_stamp_t`/`chart_stamp_t` with `is_current`, one boundary per entry point (per batch item for compose input), removal of redundant handler rechecks, no stamp for synchronous core calls, stamps kept for state captured earlier. Plus D8 if approved. Runs after T-605. | RV1–RV5 |
| T-610 | Staged proposals: a shared stamp and diagnostics in every proposal, the redundant `valid` flags removed, the verbs normalized, and each `apply_` running through the transaction with the proposal's stamp. Comes after T-607/T-608 and T-609. | SP1–SP3 |
| T-611 | Core catalogs (also the catalog's P3/P7 items: tab names with D15, sound kind labels with D16, the note-kind table keyed by enum, step types and key ranges, extension tables with D17, `bms_line_ending_t`, and an exhaustive `editor_selection_object_kind_t`↔`timing_lane_kind_t` mapping with no `default`) (layouts, playback rates, encodings, format families, timing lanes) with ids sent through the view; actions carry ids, not list positions; static_asserts against enum counts. Plus D7 and D9. | FT1–FT5 |
| T-612 | The vocabulary table in `decisions.md` plus the ~23 renames, done mechanically one module at a time. Also the ASCII text helpers (`ascii_lowercase`, `ascii_iequal`/`ascii_iless`, `lowercase_extension`): delete every copy, forwarder and inline lowercasing found by searching for what the helpers do, and rename `folded_name`. Plus D13: rename projections to `build_<x>_view`. | NV1–NV3, TX1–TX4 |
| T-613 | D10: replace the hand-written tag reader with `libavformat` metadata behind the FFmpeg build flag (rename `WORMEDIT_HAS_VIDEO_DECODER` usage for this to a `WORMEDIT_HAS_FFMPEG` capability). Without FFmpeg, there is no prefill and no error. Make the tag tests conditional on FFmpeg. Add a dependency rule to `decisions.md`. Plus D11. | D10, D11 |
| T-614 | `project_audio_sources_t` owns the registry and its sync state. Reconciliation becomes a member; consumers stop taking document and registry separately. Runs after T-603/T-605. Plus D12. | AS1–AS4 |
| T-615 | `src/audio/metronome` click synthesis plus a `metronome_t` owner in `audio_runtime_t` (bindings, registration after a reset, settings as the single source of truth). Remove the `metronome_bindings` plumbing and the three metronome `std::function` services. Runs after T-601, T-603 and T-614. | MT1–MT4 |
| T-616 | Yes/no checks: rename about 286 to `is_`/`has_`/`can_` (members too; comparators excluded); `*_matches` stale checks become `is_<x>_current`. Mechanical, one module per step. Runs after T-612. | NV4 |
| T-617 | Type-owned factories: each of the ~56 single-factory types gets `T::from(...)`/`T::with_defaults()` in place of its `make_` function, keeping aggregates aggregates; many-factory types stay in catalogs. Find-or-add accessors become `owner.<x>_for(key)` members (`stem_entry` → `cache.waveform.stem_for(channel)`). Runs after T-611 and T-612. Also RW: `measure_index_t::row_of`/`position_at`/`entry_for` members, one shared `measure_lookup_t::from(index)` replacing the five private maps (no new allocation), deletion of the sys copies; prove `absolute_row_of`'s inputs are within capacity or record the difference. | NV5, NV6, RW1–RW5 |
| T-618 | Compose action outcomes: `compose_action_outcome_t` plus `require_no_gesture`, with `finish_compose_action` doing the rebind and the feedback once. Migrate the 49 compose action handlers, the compose input handlers and the `main.cpp` rebinds. Split `compose_marker_actions.cpp` by concept. Fold `area_action_context_t` and its helpers into it (AP4). Add `notification_sink_t::post_feedback` and `notification_sink_t::muted()`, then remove the seven copies of the hint routing and the optional sinks (AP5). Replace `bookmark_command_outcome_t`, `bookmark_rename_outcome_t`, `bms_block_outcome_t` and the sound/BGA feedback twins with the shared outcome (AP6), and add one `to_notification_severity` for the 12 hand-written mappings. Find copies by what they do, not by name (AP8). Runs after T-605 and T-609. | AP1–AP8 |
| T-619 | Concept owners: `bookmark_navigation_t` (seek record, clock, chooser/rename/seek commands; the three services become member calls; files named `bookmark_*`) and `compose_scene_cache_t::stems` (display state grouped, `stem_for`/`packed_for` members). Inventory each on the card first. Runs after T-603, T-614 and T-617. | BK1–BK2, ST1–ST2 |
| T-620 | Shared results: `core/result.h` with `result_t<T, E>` (expected-compatible, `ok`/`fail` builders, `and_then`/`transform`) and `diagnosed_t<T>` (one `ok()` rule). Migrate the status and status+message results and the diagnostic ones; delete the per-file `fail`/`failure`/`failed`/`rejected` builders; keep any differing success rule at call sites and record it (RS3). Starts with the C++23 bump (D14). Runs after T-608, T-618 and T-622; before T-621. | RS1–RS4 |
| T-621 | Folders: move `src/sys` and `src/ui/nuklear` into subsystem folders (FL2), keeping file-name prefixes (FL3). Pure `git mv` plus include and CMake path edits, one commit per folder, each listed in `.git-blame-ignore-revs`. Lowercase `src/common/Chart.h`. Runs last. | FL1–FL5 |
| T-622 | Linux clang presets: `linux-clang` (libstdc++), added to the per-card gate from then on, and `linux-clang-libcxx` (gated once the owner installs libc++). Fix clang-only diagnostics rather than suppressing them. **Runs before T-620.** | CL1–CL3 |
| T-623 | `timing_index_t` query members (`seconds_at`, `row_at`, `*_approx` with `chart_end_policy_t`, `tail_seconds_per_row`, `row_at_frame`). Each sys wrapper becomes a call with its current policy. Sweep the other index types for copied queries (TI4). Runs after T-617. | TI1–TI4 |
| T-624 | Generic chart edit actions: `insert_edit_t`/`update_edit_t`/`delete_edit_t` over one value type per kind; the per-kind action trios, the `event`/`anchor` id spellings and the 21 timing-lane translations go away; per-kind validation and undo labels stay. Runs after T-607/T-608 and T-610. | EA1–EA3 |
| T-625 | Core value and text helpers (P1). In `rational.h`/`checked_math.h`/`timing_arithmetic.h`: `to_long_double`/`to_double` with one named zero-denominator policy, 128-bit gcd/multiply overloads, seconds↔frames, the rows→beat-fraction reducer, `nearest_row(row, clamp policy)`, and `normalize` names that say whether they return optional. In `text_util.h`: `is_utf8_continuation`/`truncate_utf8`, `trim` with an explicit whitespace set, `valid_bounded_text`, the remaining fold copies, `ascii_upper`. In formats: route through `invalid_utf8_offset` and `parse_exact_decimal` (with an exponent-limit option); add `bms36_value`. Constants: the chart-object limit and the default snap set. D19: unify each drifted copy on the most careful variant, with a regression test per difference. Runs before T-621. | Catalog P1 |
| T-626 | Document, chart and lane queries as members (P4): `song_document_t`/`chart_document_t` `find_chart`/`find_audio_asset`/`find_note`/`find_bgm_event`/`find_measure` members, migrating the ~110 inline chart lookups and the other inline lookups; `lane_layout_t` `find`/`contains`, `lane_definition_t::is_editable`, one `layout_at` with a row overload; defaulted `operator==` on `target_resource_identity_t` and lane types, plus a named shape comparator; control-kind predicates beside `bms_control_kind_t`. The per-projection id maps become a shared index only where an index is built today (no new allocation). Runs after T-617; before T-621. | Catalog P4 |
| T-627 | Split leftovers and rebuilt contexts (P1/P8): one definition each of `minimum_viewport_scroll`, `normalized_position_span`, `snapped_position`, `absolute_row_at_y_pixels` and `find_active_lane` in `interaction_internal.h`; shared timing-event validators and `next_source_order`; `compose_projection_context_t {frame, scope}` plus `compose_chart_scope_t::for_view`, replacing the five rebuilds and four wrapper structs. Runs before T-621. | Catalog P1, P8 |
| T-628 | Nuklear UI helpers (P1/P3/P6/P9/P10): `text_fit.h` (`fit_text_with_ellipsis`), `text_layout.h` (`wrap_text`, plus D18), a `retained_popup_t` with `owns_input()`, `PopupContextStamp::from(view)`, a shared `forward_modal_key`, rect helpers on SDL3 functions, `color.h` (`with_alpha`, `percent_to_alpha`, one float→byte rounding rule), one UI color type, `keysound_filter_popup_open` removed in favour of a method, `bookmark_rename_editor_t` → `inline_label_editor_t`. Runs before T-621. | Catalog P1, P3, P6, P9, P10 |
| (gated) | Nuklear UI state lifetime split and stamp types. Decision D6. | UR6 |
