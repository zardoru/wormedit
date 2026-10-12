# Remaining scratchpad feedback

[Back to the task card index](../tasks.md).

Synced feedback, 2026-10-11. Root may record supporting ownership needed for the stated outcome.

## T-573 Keep source assets available after session recovery

- **Status:** done
- **Outcome:** BMSON and other imported audio/visual asset paths remain usable after autosave recovery.
- **Read first:** src/core/autosave_loop.cpp; src/sys/application_frame_services.cpp; src/sys/main.cpp; tests/core/autosave_loop_tests.cpp.
- **Files owned:** include/wormedit/core/autosave_loop.h; src/sys/application_frame_services.h; src/core/autosave_loop.cpp; src/sys/application_frame_services.cpp; src/sys/main.cpp; tests/core/autosave_loop_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-completion.md; memory/chart-editor-status/resume.md.
- **Steps:** 1. Reproduce the reported failure and audit existing shared mechanisms. 2. Implement the outcome using the existing source identities, runtime and layout services. 3. Add focused regressions covering success, invalid/stale inputs and reversibility. 4. Build, run the full suite and applicable native/frontend verification, then record evidence and archive the completed feedback.
- **Invariants:** Source identities, authored timing and source files remain unchanged except deliberate layout authoring. Runtime controls are session state; failed edits and recovery never partially replace the document.
- **Reject/failure behavior:** Missing or ambiguous sources and stale UI actions reject atomically with an actionable message.
- **Verify:** Normal/Release builds and full CTest; production event harness for UI routing; affected default clang-tidy; isolated authenticated Xvfb native behavior; documentation and whitespace gates.
- **Out of scope:** Unrelated roadmap cards and publishing an unfinished manual.

## T-574 Arm a stem from a double-clicked playable note

- **Status:** done
- **Outcome:** Double-clicking a linked stem note arms its sound without opening the note label editor.
- **Read first:** src/sys/compose_view.cpp; src/ui/frontend.h; src/ui/nuklear/compose_input_panel.cpp; tests/ui/nuklear_event_harness_tests.cpp.
- **Files owned:** tests/sys/frame_timing_tests.cpp; src/ui/nuklear/compose_state.cpp; src/sys/compose_view.cpp; src/ui/frontend.h; src/ui/nuklear/compose_input_panel.cpp; tests/ui/nuklear_event_harness_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-completion.md; memory/chart-editor-status/resume.md.
- **Steps:** 1. Reproduce the reported failure and audit existing shared mechanisms. 2. Implement the outcome using the existing source identities, runtime and layout services. 3. Add focused regressions covering success, invalid/stale inputs and reversibility. 4. Build, run the full suite and applicable native/frontend verification, then record evidence and archive the completed feedback.
- **Invariants:** Source identities, authored timing and source files remain unchanged except deliberate layout authoring. Runtime controls are session state; failed edits and recovery never partially replace the document.
- **Reject/failure behavior:** Missing or ambiguous sources and stale UI actions reject atomically with an actionable message.
- **Verify:** Normal/Release builds and full CTest; production event harness for UI routing; affected default clang-tidy; isolated authenticated Xvfb native behavior; documentation and whitespace gates.
- **Out of scope:** Unrelated roadmap cards and publishing an unfinished manual.

## T-575 Return toggled stem notes to their owned source lane

- **Status:** done
- **Outcome:** Toggling a linked stem note back to BGM uses the lane explicitly owned by its channel, regardless of source order.
- **Read first:** src/core/bmson_sound_channel_editor.cpp; tests/core/bmson_sound_channel_editor_tests.cpp.
- **Files owned:** src/core/bmson_sound_channel_editor.cpp; tests/core/bmson_sound_channel_editor_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-completion.md; memory/chart-editor-status/resume.md.
- **Steps:** 1. Reproduce the reported failure and audit existing shared mechanisms. 2. Implement the outcome using the existing source identities, runtime and layout services. 3. Add focused regressions covering success, invalid/stale inputs and reversibility. 4. Build, run the full suite and applicable native/frontend verification, then record evidence and archive the completed feedback.
- **Invariants:** Source identities, authored timing and source files remain unchanged except deliberate layout authoring. Runtime controls are session state; failed edits and recovery never partially replace the document.
- **Reject/failure behavior:** Missing or ambiguous sources and stale UI actions reject atomically with an actionable message.
- **Verify:** Normal/Release builds and full CTest; production event harness for UI routing; affected default clang-tidy; isolated authenticated Xvfb native behavior; documentation and whitespace gates.
- **Out of scope:** Unrelated roadmap cards and publishing an unfinished manual.

## T-576 Mute and solo BGM lanes during preview

- **Status:** done
- **Outcome:** Every BGM lane exposes mute/solo controls that affect chart preview without modifying authored data.
- **Read first:** src/ui/nuklear/compose_canvas.cpp; src/ui/nuklear/compose_input_panel.cpp; src/sys/compose_view.h; src/sys/compose_view.cpp; src/sys/compose_input.cpp.
- **Files owned:** tests/ui/compose_stem_lane_render_tests.cpp; tests/sys/sdl_preview_output_tests.cpp; src/sys/compose_input.h; include/wormedit/audio/transport.h; include/wormedit/audio/preview_bridge.h; include/wormedit/audio/preview_session.h; src/audio/transport.cpp; src/audio/preview_bridge.cpp; src/audio/preview_session.cpp; src/sys/chart_preview_state.h; src/sys/chart_preview_state.cpp; src/sys/main.cpp; src/ui/frontend.h; src/ui/nuklear/compose_canvas.h; src/ui/nuklear/compose_object_input.h; src/ui/nuklear/compose_object_input.cpp; src/ui/nuklear/compose_panel.cpp; src/ui/nuklear/compose_state.h; tests/core/audio_transport_tests.cpp; tests/core/audio_preview_bridge_tests.cpp; tests/sys/audio_transport_notification_tests.cpp; tests/sys/frame_timing_tests.cpp; tests/ui/nuklear_event_harness_tests.cpp; src/ui/nuklear/compose_canvas.cpp; src/ui/nuklear/compose_input_panel.cpp; src/sys/compose_view.h; src/sys/compose_view.cpp; src/sys/compose_input.cpp; docs/chart-editor-plan/tasks/scratchpad-completion.md; memory/chart-editor-status/resume.md.
- **Steps:** 1. Reproduce the reported failure and audit existing shared mechanisms. 2. Implement the outcome using the existing source identities, runtime and layout services. 3. Add focused regressions covering success, invalid/stale inputs and reversibility. 4. Build, run the full suite and applicable native/frontend verification, then record evidence and archive the completed feedback.
- **Invariants:** Source identities, authored timing and source files remain unchanged except deliberate layout authoring. Runtime controls are session state; failed edits and recovery never partially replace the document.
- **Reject/failure behavior:** Missing or ambiguous sources and stale UI actions reject atomically with an actionable message.
- **Verify:** Normal/Release builds and full CTest; production event harness for UI routing; affected default clang-tidy; isolated authenticated Xvfb native behavior; documentation and whitespace gates.
- **Out of scope:** Unrelated roadmap cards and publishing an unfinished manual.

## T-577 Support SpecialStyle scratch lanes and compatible layout swapping

- **Status:** done
- **Outcome:** SpecialStyle osu!mania 16K renders both scratch lanes and ordinary 16K can switch to its compatible SpecialStyle layout.
- **Read first:** src/core/lane_profiles.cpp; src/formats/osu_mania.cpp; src/sys/compose_view.cpp; tests/core/lane_profile_tests.cpp.
- **Files owned:** tests/core/interaction_tests.cpp; include/wormedit/core/interaction.h; src/core/editor_scene.cpp; src/core/interaction_hit_test.cpp; src/ui/frontend.h; src/ui/nuklear/compose_panel.cpp; src/sys/compose_view.h; src/sys/compose_input.cpp; tests/ui/nuklear_event_harness_tests.cpp; include/wormedit/core/lane_presentation.h; include/wormedit/core/lane_profile_switch.h; src/core/lane_presentation.cpp; src/core/lane_profile_switch.cpp; src/ui/nuklear/theme.cpp; tests/core/lane_profile_switch_tests.cpp; tests/formats/osu_mania_tests.cpp; tests/sys/frame_timing_tests.cpp; tests/ui/theme_tests.cpp; src/core/lane_profiles.cpp; src/formats/osu_mania.cpp; src/sys/compose_view.cpp; tests/core/lane_profile_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-completion.md; memory/chart-editor-status/resume.md.
- **Steps:** 1. Reproduce the reported failure and audit existing shared mechanisms. 2. Implement the outcome using the existing source identities, runtime and layout services. 3. Add focused regressions covering success, invalid/stale inputs and reversibility. 4. Build, run the full suite and applicable native/frontend verification, then record evidence and archive the completed feedback.
- **Invariants:** Source identities, authored timing and source files remain unchanged except deliberate layout authoring. Runtime controls are session state; failed edits and recovery never partially replace the document.
- **Reject/failure behavior:** Missing or ambiguous sources and stale UI actions reject atomically with an actionable message.
- **Verify:** Normal/Release builds and full CTest; production event harness for UI routing; affected default clang-tidy; isolated authenticated Xvfb native behavior; documentation and whitespace gates.
- **Out of scope:** Unrelated roadmap cards and publishing an unfinished manual.

## Completion evidence: T-573–T-575

Verified 2026-10-11. Recovery snapshots anchor relative audio, visual, MIDI,
generator and waveform-cache paths against the live session asset directory;
the live authored paths stay unchanged. This preserves references in newly
written recovery snapshots; it cannot reconstruct missing directory metadata in
older imported-session backups.

Stem note heads carry their resource and stem classification after geometry
projection, including cached scene reuse. Double-click arms a valid stem and
keeps ordinary/missing-note label behavior. Arming survives a filtered Sounds
list. The scalar note record remains within its existing 96-byte limit.

Returning a linked stem note to BGM resolves explicit channel ownership before
source order. Native verification double-clicks note 17/event 16, then uses its
lane key to toggle it into owned lane 15 (source order 2, channel index 0).
Save/Undo restores the complete authored document except revision.

Normal/Release builds and full CTest pass **3,414/3,414 each**. Focused recovery
and assignment tests pass 19 assertions; projection/cache reuse and production
event tests pass 15 assertions each. Affected default clang-tidy reports no new
or increased complexity violations. Documentation and whitespace checks pass.
Native tests use an authenticated isolated Xvfb.

Evidence: `/tmp/scratchpad-commit-final-{build,release-build,ctest,release-ctest,tidy}.log`,
`/tmp/scratchpad-commit-{core,events,release-focused}.log`,
`/tmp/scratchpad-commit-armed-fixed.png`, `/tmp/scratchpad-commit-toggled.png`,
`/tmp/scratchpad-commit-{before,toggled}.wormedit`.
At that checkpoint T-576/T-577 remained ready. The About/manual link still awaits the finished manual.


## Implementation checkpoint: T-577

Implemented 2026-10-11: ordinary osu!mania 12K/16K can select SpecialStyle
and switch back while retaining stable lane identities, authored notes and input
bindings. SpecialStyle adopts its two-stage geometry; playable scratch-class
columns follow the shared scratch presentation preference. Export writes the
selected SpecialStyle flag. Both layout selectors name the variant explicitly.
The uncovered canvas width now feeds the shared layout and input geometry,
keeping wide fields visible alongside open docks.

Normal and Release builds pass, with **3,420/3,420 full CTest checks each**.
Focused core/format, projection and frontend tests pass 64, 16 and 5 assertions,
respectively. Affected default clang-tidy reports no new or increased complexity
violations. Isolated authenticated Xvfb confirms all 16 columns, both outside
scratch colors, profile selection, Save and authored-document restoration with
Undo. The initial native pointer-picking attempt missed the rightmost note.
That investigation is resolved by the completion evidence below.
T-576 mute/solo remains pending.

Evidence: `/tmp/specialstyle-final-{build,release-build,core,projection,events,ctest,tidy}.log`,
`/tmp/specialstyle-release-{focused,ctest}.log`,
`/tmp/specialstyle-native-{choice,switched,pick}.png` and
`/tmp/specialstyle-native-{before,switched}.wormedit`.


## Completion evidence: T-577

Verified 2026-10-11. The initial profile/dock implementation is committed as
`1d39348`. Follow-up picking fixes are uncommitted. Rendered note-head bounds
and split-stage lane bounds are shared with pointer picking; clicks cover the
complete filled head and exclude the stage gap. Hover and label editing receive
the same geometry. Invalid sizes reject safely; legacy core callers retain their
point/radius defaults.

Normal/Release builds and full CTest pass **3,423/3,423 each**. Focused
core/format/picking and frontend regressions pass **133 and 22 assertions**.
Affected default clang-tidy reports no new or increased complexity violations.
Native authenticated isolated Xvfb confirms all 16 columns with both scratch
colors, profile selection and persistence. Clicking the rightmost head selects
note 17 in lane 16; Delete removes exactly that note, Save persists it, and Undo
restores the complete authored document except revision. Earlier profile Save
and Undo checks also preserve the document and note/input identities.

Evidence: `/tmp/specialstyle-picking-{final-build,normal-final-build,release-build,ctest,release-ctest,tidy,core-tidy-final}.log`,
`/tmp/specialstyle-picking-{core-final,events-final}.log`,
`/tmp/specialstyle-picking-selected.png`,
`/tmp/specialstyle-picking-{deleted,restored}.wormedit`.
T-576 remains ready. Manual/About remains conditional on the finished manual.


T-576 implementation decision (2026-10-11): controls are chart-scoped session
state keyed by stable BGM lane ID. Solo is additive and limits the BGM mix;
ordinary playable notes and music remain audible. A stem lane controls its
channel's linked playable portions as well as BGM portions. Muting silences
active and future voices without retiring them; unmuting resumes an active stem
at its current source position. Shared sound resources remain independent.
Ownership above extends to the existing preview runtime and transport to make
these controls affect real audio, rather than only canvas presentation.

T-576 header layout: controls share the existing 18-pixel header, leaving BGM
marker drags unobstructed. Ordinary lanes and wide stems show M/S continuously;
compact stem headers retain a useful UTF-8 filename until header hover, with
active mute/solo controls always visible. Button tooltips retain the full stem
filename. The wide-header golden reflects the space reserved for controls; the
compact filename golden remains unchanged.


## Completion evidence: T-576 (2026-10-11)

BGM headers now expose live M/S controls, keyed by chart and stable lane ID.
Additive solo and explicit mute affect BGM and owned stem-linked playable
portions without muting unrelated voices that share the same sound. Active
muted stems keep their source clock and resume at the current position.
Controls consume press/release before painting or dragging; unavailable and
stale actions reject without changing state. BGM-only charts receive the same
canvas-width feedback as playable charts. Compact stem captions retain useful
UTF-8 filenames, exposing controls on hover and keeping active controls visible.

Normal and Release builds pass; full CTest passes **3,431/3,431 in each**.
Focused checks cover PCM mixing and seek group retention (22 assertions), real
SDL postmix silence/audibility without restarting playback, source ownership,
projection/session/stale-state behavior (66 assertions across the combined
SpecialStyle checks), production event routing (323 assertions / 27 cases),
and filename rendering (351 assertions). Affected default clang-tidy has no
new or increased complexity violations. Native authenticated isolated Xvfb
verifies ordinary and compact UTF-8 stem M/S, BGM-only dock visibility
and unchanged authored data after Save.

Evidence: `/tmp/bgm-mix-commit-{normal,release}-ctest.log`,
`/tmp/bgm-mix-{caption-build,release-caption-build,caption-events,caption-render,final-projection,audio,device,tidy,final-tidy,caption-tidy}.log`,
`/tmp/bgm-mix-native-{controls,stem-caption,stem-hover,stem-muted}.png`.
T-577 shared geometry/picking fixes are included in these complete suites.
Manual/About remains conditional on the finished manual.
