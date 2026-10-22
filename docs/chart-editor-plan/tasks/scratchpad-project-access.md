# Scratchpad project access and Metadata scrolling

[Back to the task card index](../tasks.md).

Synced user feedback, 2026-10-11. Priority bugs selected after T-564–T-566.

## T-567 Hide the song minimap by default for BMSON

- **Status:** done
- **Outcome:** BMSON follows the existing BMS hidden song-bar default; the toolbar remains available and explicit user toggles override it.
- **Read first:** src/sys/compose/views/compose_view.cpp; tests/sys/frame_timing_tests.cpp.
- **Files owned:** src/sys/compose/views/compose_view.cpp; tests/sys/frame_timing_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-project-access.md; docs/chart-editor-plan/tasks.md; memory/chart-editor-status/resume.md. Root records supporting scope.
- **Steps:** Extend the source-family default; verify BMSON/BMS and other chart families, manual show/hide and chart switches.
- **Invariants:** No authored document change or removal of the explicit toolbar override.
- **Reject/failure behavior:** Stale or unavailable contexts reject without partial edits or accidental publication.
- **Verify:** Focused production event/core/sys checks; normal/Release builds and full CTest; affected default clang-tidy; authenticated isolated native Xvfb; documentation and whitespace checks.
- **Out of scope:** Do not change unrelated waveform rendering or saved chart data.

## T-568 Keep Metadata scrollable around its embedded waveform

- **Status:** done
- **Outcome:** Metadata plain-wheel input scrolls the form and its embedded waveform keeps a bounded height, so lower sections stay reachable.
- **Read first:** src/ui/nuklear/audio/audio_panel.cpp; src/ui/nuklear/metadata_audio_section.cpp; tests/ui/nuklear_event_harness_tests.cpp.
- **Files owned:** src/ui/nuklear/audio/audio_panel.cpp; src/ui/nuklear/metadata_audio_section.cpp; tests/ui/nuklear_event_harness_tests.cpp; tests/ui/menu_bar_event_harness_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-project-access.md; docs/chart-editor-plan/tasks.md; memory/chart-editor-status/resume.md. Root records supporting scope.
- **Steps:** Reproduce repeated scroll growth; give embedded preview fixed geometry; reserve plain wheel for Metadata scrolling and retain explicit zoom buttons; verify actual scroll, clipped input and unchanged audio offset.
- **Invariants:** Standalone Audio/Slices wheel zoom and deliberate offset drags keep their existing behavior.
- **Reject/failure behavior:** Stale or unavailable contexts reject without partial edits or accidental publication.
- **Verify:** Focused production event/core/sys checks; normal/Release builds and full CTest; affected default clang-tidy; authenticated isolated native Xvfb; documentation and whitespace checks.
- **Out of scope:** Do not change authored audio timing or redesign Metadata fields.

## T-569 Open Export from any project tab without a Compose dock dependency

- **Status:** done
- **Outcome:** File Export opens the shared output-group dialog from Project, Metadata, Compose and other project tabs; publication uses the existing conversion service. Export lives in the Project chart header and File menu, removed from the Compose inspector as requested.
- **Read first:** src/core/command_registry.cpp; src/sys/commands/application_command_dispatch.cpp; src/sys/compose/actions/compose_actions.cpp; src/sys/compose/views/compose_view.h; src/sys/app/main.cpp; src/sys/workspace_projection.cpp; src/ui/frontend.h; src/ui/nuklear/frontend.cpp; src/ui/nuklear/compose_resources.cpp; src/ui/nuklear/setup_panel.cpp; src/ui/nuklear/menu_bar.cpp; tests/sys/application_commands_tests.cpp; tests/sys/compose_action_notification_tests.cpp; tests/ui/nuklear_event_harness_tests.cpp.
- **Files owned:** src/core/command_registry.cpp; src/sys/commands/application_command_dispatch.cpp; src/sys/compose/actions/compose_actions.cpp; src/sys/compose/views/compose_view.h; src/sys/app/main.cpp; src/sys/workspace_projection.cpp; src/ui/frontend.h; src/ui/nuklear/frontend.cpp; src/ui/nuklear/compose_resources.cpp; src/ui/nuklear/setup_panel.cpp; src/ui/nuklear/menu_bar.cpp; tests/sys/application_commands_tests.cpp; tests/sys/compose_action_notification_tests.cpp; tests/ui/nuklear_event_harness_tests.cpp; tests/ui/menu_bar_event_harness_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-project-access.md; docs/chart-editor-plan/tasks.md; memory/chart-editor-status/resume.md. Root records supporting scope.
- **Steps:** Remove Export from dock-only command routing; project-stamp the shared dialog and project export dispatch; project output groups without a Compose scene; verify stale/foreign context, modal capture, missing groups, cancellation and successful disposable export.
- **Invariants:** Export requires a current project/chart/revision, guards active gestures/jobs and preserves existing overwrite and background publication rules.
- **Reject/failure behavior:** Stale or unavailable contexts reject without partial edits or accidental publication.
- **Verify:** Focused production event/core/sys checks; normal/Release builds and full CTest; affected default clang-tidy; authenticated isolated native Xvfb; documentation and whitespace checks.
- **Out of scope:** Output-group CRUD remains T-261; do not broaden conversion policy or publish user files.

## T-570 Use whitespace between Quantize/Snap setting groups

- **Status:** done
- **Outcome:** Space, Halfway objects, Hold tails and Presets retain their existing separation without horizontal divider strokes.
- **Read first:** src/ui/nuklear/compose_inspector.cpp.
- **Files owned:** src/ui/nuklear/compose_inspector.cpp; docs/chart-editor-plan/tasks/scratchpad-project-access.md; docs/chart-editor-plan/tasks.md; memory/chart-editor-status/resume.md.
- **Steps:** Replace those four separators with identical-height blank rows; inspect the native Compose inspector.
- **Invariants:** Setting controls, layout spacing and quantization behavior remain unchanged.
- **Reject/failure behavior:** Existing input guards remain unchanged.
- **Verify:** Normal/Release builds and full CTest; affected default clang-tidy; isolated native Xvfb; documentation and whitespace checks.
- **Out of scope:** Other section and toolbar separators remain unchanged.

## Completion evidence — 2026-10-11

Normal and Release builds pass. Normal full CTest passes **3,398/3,398**. Release full CTest passes **3,397/3,398**, with the sole SDL audition timeout passing on isolated retry. The stale-project and missing-path rejection checks use independent fixtures and pass 10 assertions in each build. The production event harness verifies the shared Export dialog across five tabs, exact project/chart/revision/group stamps, one Project-header command and no inspector Export button, plus wheel scrolling without waveform zoom or audio-offset edits. System tests cover hidden defaults and explicit overrides, project-level command guards and successful BMSON publication from Compose, Project and Metadata without retained Compose state. Affected default clang-tidy reports no new/increased complexity violations; documentation and whitespace gates pass.

Authenticated isolated native Xvfb verifies Project-header Export and File Export from Metadata, bounded 180 px waveform with reachable lower sections, hidden BMSON minimap and unchanged blank spacing between quantize settings. A disposable relative-path fixture successfully publishes a BMSON with two channels and six measure lines; Save preserves the complete authored document. The first absolute-path fixture correctly rejects unsafe BMSON audio paths before publication. The SDL audition rejection test passes alone but still times out in the Release full run, including a serial run; this remains an unresolved audio-test issue outside this UI batch.

Evidence: `/tmp/project-access-{normal,release}-complete.log`, `/tmp/project-access-{timeout-retry,release-final-retry,rejections-tidy}.log`, `/tmp/project-access-placement-events.log`, `/tmp/project-access-{tidy,tidy-last,tidy-followup,placement-tidy,inspector-tidy}.log`, `/tmp/project-access-native-{compose,project-final,metadata-menu-final,metadata-clean,published-relative}.png`. Output-group CRUD remains **T-261**, and BGM mute/solo and special-style 16K remain pending.
