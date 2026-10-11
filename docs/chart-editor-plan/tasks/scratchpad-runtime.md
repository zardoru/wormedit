# Scratchpad runtime and timing feedback

[Back to the task card index](../tasks.md).

## T-558 Reduce and verify Release frame CPU costs

- **Status:** partial
- **Outcome:** HeadBONKache Release frame CPU averages stay within 5 ms; BMSON playback projection and Nuklear costs are measured and reduced where application work is responsible.
- **Read first:** src/sys/frame_timing.cpp; src/core/workspace.cpp; src/sys/compare_view.cpp; src/sys/bmson_waveform_projection.cpp; src/ui/nuklear/compose_sounds_panel.cpp.
- **Files owned:** include/wormedit/core/workspace.h; src/core/workspace.cpp; src/sys/compare_view.h; src/sys/compare_view.cpp; src/sys/main.cpp; src/sys/frame_timing.cpp; tests/core/workspace_tests.cpp; tests/sys/frame_timing_tests.cpp; src/audio/channel_waveform.cpp; tests/core/audio_aligned_waveform_tests.cpp; src/sys/bmson_waveform_projection.cpp; docs/chart-editor-plan/tasks/scratchpad-runtime.md; docs/chart-editor-plan/tasks.md; memory/chart-editor-status/resume.md. Root records supporting scope if required.
- **Steps:** 1. Capture native Release CPU and wall baselines with no agent builds running. 2. Remove measured repeated work without weakening validation or rendering. 3. Verify cache invalidation and projection equivalence. 4. Repeat native playback measurements and report remaining uncertainty.
- **Invariants:** authored content and validation stay unchanged; revision, project identity and limits invalidate caches; dynamic focus/tab/selection guards stay live; CPU costs are distinguished from scheduling and VSync waits.
- **Reject/failure behavior:** invalid and stale documents retain diagnostics; caches never turn a rejected document into an accepted view.
- **Verify:** normal/Release builds and full CTest; affected default clang-tidy; native isolated authenticated Xvfb with timing reports; documentation and whitespace checks.
- **Out of scope:** user desktop, external game processes and audio loop behavior.

## T-559 Remove audible gaps at preview loop boundaries

- **Status:** ready
- **Outcome:** Preview loops continue without a frontend-service drain/reprime gap at supported playback rates and pitch modes.
- **Read first:** src/sys/sdl_preview_output.cpp; src/audio/preview_session.cpp; tests/sys/sdl_preview_output_tests.cpp.
- **Files owned:** src/sys/sdl_preview_output.cpp; src/sys/sdl_preview_output.h; src/audio/preview_session.cpp; include/wormedit/audio/preview_session.h; tests/sys/sdl_preview_output_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-runtime.md; memory/chart-editor-status/resume.md. Root records necessary supporting scope.
- **Steps:** 1. Measure queued audio at boundaries. 2. Prepare wrap playback without waiting for the control frame. 3. Preserve source clock, voice tails, generation and cancellation. 4. Verify continuous samples across rates and pitch modes.
- **Invariants:** callback safety; Stop/seek/edit cancellation; source positions and loop bounds remain authoritative.
- **Reject/failure behavior:** unavailable or invalid loops fail clearly; no callback blocking or stale audio publication.
- **Verify:** audio and SDL integration tests, normal/Release full CTest, default clang-tidy and isolated native playback.
- **Out of scope:** authored loop export and document edits.

## T-560 Convert an empty selected row range to Stop or Delay

- **Status:** ready
- **Outcome:** A selection menu command replaces an empty row interval with a Stop/Delay, shifts later objects earlier while preserving song time, and offers an explicit measure-resize policy in one Undo.
- **Read first:** src/sys/compose_area_actions.cpp; include/wormedit/core/measure_editor.h; src/core/measure_editor.cpp; include/wormedit/core/timing.h; src/ui/nuklear/compose_area_menu.cpp.
- **Files owned:** include/wormedit/core/measure_editor.h; src/core/measure_editor.cpp; src/sys/compose_area_actions.cpp; src/sys/compose_area_actions.h; src/ui/frontend.h; src/ui/nuklear/compose_area_menu.cpp; tests/core/measure_editor_tests.cpp; tests/sys/frame_timing_tests.cpp; tests/ui/nuklear_event_harness_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-runtime.md; memory/chart-editor-status/resume.md. Root records supporting scope after the timing audit.
- **Steps:** 1. Audit all positioned object families and exact pause semantics. 2. Build atomic conversion with row-domain duration and explicit measure policy. 3. Route guarded selection commands. 4. Verify absolute song times, all remapped endpoints and full Undo restoration.
- **Invariants:** shared row mapping; exact timing; no deleted notes or hold tails; source encodings and BMSON retained aliases stay coherent.
- **Reject/failure behavior:** stale, note-occupied or unsafe timing intervals reject atomically with an actionable diagnostic.
- **Verify:** core timing equivalence and Undo tests; production event harness; normal/Release full CTest; default clang-tidy and isolated native Save/Undo.
- **Out of scope:** manual publication and WASM work.

T-558 supporting scope: main passes the existing project-session generation into the workspace frame cache, so reopening the same project ID/revision clears binding, outline and Compare caches. The cache keys alone cannot distinguish in-place project replacement.

T-558 progress (2026-10-11): cached immutable binding ID/bounds validation removes per-frame asset/resource hash-set construction. The cache includes document address, song ID, revision and workspace limits; project-session generation clears the whole frame/Compare cache on replacement, including same-ID/same-revision reopen. Default uncached core callers retain their behavior. Dynamic tab/focus/chart/comparison guards still run every frame. Diagnostic file reports now include total main-thread frame CPU averages. Native Release HeadBONKache at the same 0:17 playback viewport reduces preparation CPU average 0.601→0.331 ms; frame CPU average 3.331 ms and p99 4.858 ms. BMSON three-stem/200-keysound playback reports frame CPU 2.387 ms average, BMSON projection CPU 0.091 ms average / 0.149 ms p99, and Nuklear CPU 0.433 ms average / 0.694 ms p99. These are isolated authenticated Xvfb measurements on a host with other user workloads, and do not reproduce the original BMSON spike. The native capture does not establish a full analyzed-stem waveform stress case; that remains part of the investigation. Requested the affected local chart path/settings while keeping the scratchpad feedback pending. No user workload was stopped.

T-558 validation: normal/Release builds and all 3,367 CTest checks in each pass. Default clang-tidy on affected sources/tests reports no new/increased complexity violations; legacy main, speed-graph and Catch findings remain. Documentation and whitespace checks pass. Evidence: `/tmp/runtime-perf-{final-build,release-final-build,final-ctest,release-ctest,tidy,session-tidy}.log`, `/tmp/headbonk-perf-{baseline,after}-report.log`, `/tmp/bmson-perf-after-report.log`, `/tmp/{headbonk,bmson}-perf-after-playing.png`. T-558 stays partial until the original performance observations are resolved; T-559/T-560 are queued and unimplemented.
