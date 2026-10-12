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

- **Status:** ready
- **Outcome:** Every BGM lane exposes mute/solo controls that affect chart preview without modifying authored data.
- **Read first:** src/ui/nuklear/compose_canvas.cpp; src/ui/nuklear/compose_input_panel.cpp; src/sys/application_command_state.h; src/sys/compose_view.cpp; src/sys/compose_input.cpp.
- **Files owned:** src/ui/nuklear/compose_canvas.cpp; src/ui/nuklear/compose_input_panel.cpp; src/sys/application_command_state.h; src/sys/compose_view.cpp; src/sys/compose_input.cpp; docs/chart-editor-plan/tasks/scratchpad-completion.md; memory/chart-editor-status/resume.md.
- **Steps:** 1. Reproduce the reported failure and audit existing shared mechanisms. 2. Implement the outcome using the existing source identities, runtime and layout services. 3. Add focused regressions covering success, invalid/stale inputs and reversibility. 4. Build, run the full suite and applicable native/frontend verification, then record evidence and archive the completed feedback.
- **Invariants:** Source identities, authored timing and source files remain unchanged except deliberate layout authoring. Runtime controls are session state; failed edits and recovery never partially replace the document.
- **Reject/failure behavior:** Missing or ambiguous sources and stale UI actions reject atomically with an actionable message.
- **Verify:** Normal/Release builds and full CTest; production event harness for UI routing; affected default clang-tidy; isolated authenticated Xvfb native behavior; documentation and whitespace gates.
- **Out of scope:** Unrelated roadmap cards and publishing an unfinished manual.

## T-577 Support SpecialStyle scratch lanes and compatible layout swapping

- **Status:** ready
- **Outcome:** SpecialStyle osu!mania 16K renders both scratch lanes and ordinary 16K can switch to its compatible SpecialStyle layout.
- **Read first:** src/core/lane_profiles.cpp; src/formats/osu_mania.cpp; src/sys/compose_view.cpp; tests/core/lane_profile_tests.cpp.
- **Files owned:** src/ui/frontend.h; src/ui/nuklear/compose_panel.cpp; src/sys/compose_view.h; src/sys/compose_input.cpp; tests/ui/nuklear_event_harness_tests.cpp; include/wormedit/core/lane_presentation.h; include/wormedit/core/lane_profile_switch.h; src/core/lane_presentation.cpp; src/core/lane_profile_switch.cpp; src/ui/nuklear/theme.cpp; tests/core/lane_profile_switch_tests.cpp; tests/formats/osu_mania_tests.cpp; tests/sys/frame_timing_tests.cpp; tests/ui/theme_tests.cpp; src/core/lane_profiles.cpp; src/formats/osu_mania.cpp; src/sys/compose_view.cpp; tests/core/lane_profile_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-completion.md; memory/chart-editor-status/resume.md.
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
T-576/T-577 remain ready. The About/manual link still awaits the finished manual.


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
Undo. The final native pointer-picking attempt did not select the rightmost
note; this remains unresolved, so T-577 stays ready rather than being archived.
T-576 mute/solo remains pending.

Evidence: `/tmp/specialstyle-final-{build,release-build,core,projection,events,ctest,tidy}.log`,
`/tmp/specialstyle-release-{focused,ctest}.log`,
`/tmp/specialstyle-native-{choice,switched,pick}.png` and
`/tmp/specialstyle-native-{before,switched}.wormedit`.
