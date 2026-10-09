# October 9 BGM conversion completion batch

Authorized by the user after the recommended T-486, T-423, T-424 batch.
Main started at 9040d75. Other worktrees are preserved.

Coordinator scope extensions: T-423 includes the approved October 8 linked-hold
handoff, codec regression files, indexed scene geometry, scalar projection and
Nuklear BGM drawing. T-486 shares packed channel creation across single-click and
stroke insertion. T-424 additionally owns compose_note_to_bgm.{h,cpp},
include/wormedit/core/command_registry.h, src/ui/workspace_menu.h and
src/ui/nuklear/frontend.cpp for the shared action and Edit menu.

Implementation adjustment: lane bindings belong to the chart input profile,
whereas remappable editor commands belong to the command registry. Register the
BGM pseudo-lane operations there (Backquote / Shift+Backquote), using the same
conversion helper as Compose input. This gives shortcut persistence, conflict
checks, focus gates, and menu dispatch without duplicating keyboard execution.
No T-421 or T-425 gesture work is included.

Verification completed on normal main Debug configuration (Lua OFF, video ON):
- `cmake --build build -j4` passed with ccache stored under `/tmp`.
- Final full `ctest --test-dir build --output-on-failure -j4`: **2,926/2,926**, 42.07 s.
- Final `-L event-harness`: **263/263**; focused core tags: 9 cases/97 assertions;
  focused BGM tail/key/menu harness: 3 cases/39 assertions; real application command: 1 case/12 assertions.
- Packed export regression failed before the shared creator fix; focused packed green run: 8 cases/63 assertions.
- Configured affected-source/test tidy completed. New functions are <=25; existing unrelated source and Catch macro findings are retained. Menu/helper extraction avoids increasing source complexity.
- Actual editor publication tests now require the hold-tail warning detail for both BMS and osu! exports.
- Isolated Xvfb cannot create sockets in this sandbox. Two reviewed ordinary-main-object SDL dummy/software captures show a selected layered hold with its head visible and offscreen. The capture probe verifies native end preservation and Undo. No object files in the ordinary build were replaced.

Evidence logs: `/tmp/wormedit-oct9-final-ctest.log`,
`/tmp/wormedit-batch-event-final.log`, `/tmp/wormedit-batch-focused-core.log`,
`/tmp/wormedit-batch-focused-events.log`, `/tmp/wormedit-batch-final-tidy.log`,
`/tmp/wormedit-batch-menu-final-tidy.log`. Captures and reproducible probe:
`/home/zardoru/devel/wormedit-scratch-builds/oct9-bgm/captures/{visible,offscreen}.png`.
The full-suite menu regression caught a nested disabled tooltip popup; tooltips are now deferred until the parent popup ends. The final full suite passes.

Additional supporting scope: compose_input_panel.cpp keeps duration bodies from
becoming head hit targets; application_commands_tests.cpp checks real application
command format/rejection gates. BGM scalar head coordinates are shifted in both
retained scene windows and painter tile extraction, maintaining the 96-byte
record budget. Context-menu shortcut text is projected from the live registry.

Renderer verification scope extends compose_stem_lane_render_tests.cpp to verify
selected and unselected BGM duration bodies when the head is offscreen.

Final documentation check passes after the card/resume updates. An isolated
HEAD-main tidy comparison confirms `main` and
`refresh_application_command_states` remain at their baseline score of 27
(`/tmp/wormedit-baseline-main-tidy.log`); the new shortcut helper is below 25.
