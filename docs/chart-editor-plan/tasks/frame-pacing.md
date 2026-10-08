# Plan domain: high-refresh frame pacing

[Back to executable task cards](../tasks.md).

User target (2026-10-07): 200 Hz / 5 ms on the current display-capable PC,
with 144 Hz / 6.94 ms as the earlier reference. Dense charts can exceed 50,000
notes. Intended older target class is Ryzen 2000 or mobile Intel from 2015;
the user's GDI/GDAC comparison on 2008 hardware is a responsiveness reference,
not a measured claim for WormEdit. Audio has separate callback/decode threads.

## T-472 Pace frames without a fixed post-presentation sleep

- **Status:** done in code (2026-10-07); root application build and 2,574/2,574 tests pass; live display certification remains pending.
- **Outcome:** active frame pacing uses a 5 ms whole-frame budget instead of appending 1 ms after every presentation; a frame whose work or presentation already consumes the budget receives no additional sleep, while nonblocking/failed VSync and invisible windows remain bounded.
- **Read first:** `src/sys/application_frame_services.cpp`, `src/sys/application_frame_services.h`, `src/sys/main.cpp`, `src/sys/window.cpp`, `include/wormedit/window.h`.
- **Files owned:** `src/sys/application_frame_services.cpp`, `src/sys/application_frame_services.h`, `src/sys/main.cpp` (frame-start timestamp and render call only), new `src/sys/frame_pacing.cpp`, new `src/sys/frame_pacing.h`, new `tests/sys/frame_pacing_tests.cpp`, this card only. Root approved narrow manifest sections: `CMakeLists.txt` application source entry adjacent to `application_frame_services.cpp`; `tests/CMakeLists.txt` standalone two-source pacing target/include/test/labels only. No other manifest ownership transfers.
- **Steps:**
  1. Audit the production loop and record the unconditional `SDL_Delay(1)` plus existing VSync policy. Measure the legacy delay through an isolated/injected-clock schedule and keep useful CPU time distinct from presentation wait. Inspect dense-chart evidence without rewriting scene queries.
  2. Capture the whole-frame start before frame services. Compute a bounded remainder for visible frames (5 ms), hidden/minimized/occluded frames (50 ms), and apply SDL's precise delay only to a positive remainder. The configured VSync flag is not proof that present actually blocked.
  3. Preserve VSync policy. If work/present already takes the period, do not sleep or create a catch-up burst. Handle clock rollback and overflow without an unbounded delay. Visibility and monitor changes must not retain stale debt.
  4. Test injected clocks covering already-paced presentation, fast/nonblocking presentation, over-budget work, visibility transitions and repeated frames; verify the production call passes the original whole-frame start.
- **Invariants:** input and audio controls are serviced every active frame; rendering output and VSync policy remain unchanged. No pacing setting, audio extrapolation or total-chart rendering rewrite is introduced.
- **Reject/failure behavior:** absent/unavailable window state uses the bounded visible fallback; invalid elapsed time cannot produce an excessive wait.
- **Verify:** normal dependency-aware focused build and pacing tests; configured cognitive complexity at most 25; root normal application build and full CTest after integration; documentation gate. Record production wiring and simulated schedule evidence separately from live 200 Hz certification.
- **Out of scope:** stalled-present recovery/watchdog/native driver policy (T-293), throttled UI projections or extrapolation (T-277), WSOLA (T-471), changing monitor configuration, user desktop automation, per-note/measure inspector caching.

- **Baseline audit:** `application_frame_services.cpp:190` appends `SDL_Delay(1)` after present; `window.cpp:94` requests VSync interval one. Injected-clock legacy schedules were recorded before source edits in `wormedit-scratch-builds/wsola-oct7/pacing/legacy-schedule-baseline.json`: already-paced 5 ms becomes 6 ms; 1.1 ms fast present becomes 2.1 ms; 6.1 ms over-budget work becomes 7.1 ms. These are simulated schedule periods, not measured CPU/device/monitor performance. Dense-query findings remain measurement-only and out of scope.

- **Implementation:** the monotonic whole-frame start is the first statement in the active loop, before notifications and frame services, and is passed unchanged to the renderer. A pure stateless policy subtracts elapsed time from 5 ms (active) or 50 ms (current hidden/minimized/occluded state). It requests zero additional delay at or beyond the period, and uses `SDL_DelayPrecise` only for a positive remainder. Missing window state uses active fallback. Subtraction avoids deadline-addition overflow; rollback requests zero delay and the next coherent frame starts fresh. There is no accumulated scheduling debt across visibility or display changes. VSync interval one and rendered output remain unchanged; configured VSync is not used as proof of a blocking present.
- **Schedule evidence:** the compiled injected-clock cases keep work and presentation wait separate: 1 ms work + 4 ms present requests zero sleep (legacy total 6 ms); 1 ms work + 6 ms present requests zero sleep (legacy 8 ms); 1 ms work + 0.1 ms nonblocking present requests 3.9 ms remainder (legacy total 2.1 ms); 6 ms work + 0.1 ms present requests zero sleep (legacy 7.1 ms). A 120-frame active→inactive→active sequence uses the current period, followed by overrun/rollback/new-epoch cases without catch-up. This establishes deterministic pacing policy, not CPU duration or actual monitor cadence.
- **Worker verification:** fresh optimized normal CMake/Ninja standalone target builds in three dependency-tracked steps without SDL/core/Catch builds or network fetches. All **257 active checks** pass under `NDEBUG`, including precise boundaries, large elapsed values, near-uint64-limit starts, rollback, nonblocking/failed-VSync-equivalent timing, slower-display wait and visibility transitions. Pacing plus documentation CTest passes **2/2**. Production `main.cpp` and `application_frame_services.cpp` pass actual main compiler flags with `-fsyntax-only`, including pinned SDL visibility flags and `SDL_DelayPrecise`; static wiring check confirms original start reaches the render call after all services. Configured clang-tidy has no new violation: legacy `main`/`refresh_application_command_states` scores 27 and included `build_speed_graph` 29 remain unchanged; all new policy/tests/wrapper code stays at or below 25. Root reviewed the exact code/manifests diff. `third_party/libotoworm` remains an empty directory.
- **Evidence:** `/home/zardoru/devel/wormedit-scratch-builds/wsola-oct7/pacing/`: `legacy-schedule-baseline.json`, `configure.log`, `build.log`, `policy-schedule.log`, `tests.log`, `production-wiring-check.log`, `production-syntax.log`, `baseline-tidy.log`, `production-tidy.log`, `policy-tidy.log`, and `complexity-comparison.json`. The audio worker's exclusive performance window was respected; no concurrent pacing builds or probes ran during its capture.
- **Pending integration/performance:** root integrated worker `f27b524` as `9ed59b8`; normal application build and all **2,574/2,574 tests pass**, including the standalone pacing policy. Both clean worker trees and about 1.2 GB of generated builds were removed; branches/logs retained. No user desktop, application process, display configuration or original chart was changed. Live 200 Hz / 5 ms cadence, dense-chart whole-frame budget and intended older-device performance remain unmeasured; prior dense-query audit findings remain measurement-only. The change removes a fixed extra wait and supplies a bounded fallback, without claiming a stalled-present fix or new projection performance.

## T-475 Bound measured dense-chart interaction projection costs

- **Status:** ready
- **Outcome:** dense-chart single-note/BGM inspector, armed-paint occupancy and interaction geometry projection avoid measured unnecessary chart-sized warm work without stale editing state. Optimize only hotspots demonstrated by an optimized production projection probe; retain cold/edit costs separately.
- **Read first:** `src/sys/compose_view.cpp`, `src/sys/compose_view.h`, `src/core/interaction.cpp`, `include/wormedit/core/interaction.h`, `tests/sys/frame_timing_tests.cpp`, `tests/core/interaction_tests.cpp`, T-472 above.
- **Files owned:** `src/sys/compose_view.cpp`, `src/sys/compose_view.h`, `src/core/interaction.cpp`, `include/wormedit/core/interaction.h`, `tests/sys/frame_timing_tests.cpp`, `tests/core/interaction_tests.cpp`, this card only. Root owns manifests and whole-frame instrumentation.
- **Steps:**
  1. Check current source against the prior read-only audit; build normal RelWithDebInfo production objects. Preserve a baseline probe with 50k/100k total notes and matched visible density, no selection, late/offscreen single selection, empty late armed-paint hover and cached production follow. Report cold startup, revision changes, window crossings and zoom separately, retaining boundary maxima.
  2. Rank measured warm costs and implement the smallest revision/selection/geometry-bound lookup or reuse improvements that materially reduce them. Existing scene interval/window caches are preserved; this is not a renderer rewrite or a generic arena migration.
  3. Verify stale-key rejection for edit/delete/undo, chart/document replacement, selection, layout, zoom and cursor changes as applicable. Compare projected values and scene geometry against fresh production projections.
  4. Repeat the identical optimized probe in a root-granted quiet measurement window; record wall p50/p99/max and thread CPU, fixture/visible counts and operation/cache counters where available. No live monitor or old-device claim from headless data.
- **Invariants:** exact editing, selection, ghost occupancy, source ownership and visible scene semantics remain unchanged. Cache lifetime follows document/chart/revision and all consumed dependencies.
- **Reject/failure behavior:** stale/missing cached state falls back to the existing exact path; bounded cache exhaustion must not hide edits.
- **Verify:** normal application-compatible focused build; `ctest --test-dir build --output-on-failure -R 'interaction|frame-timing'`; affected configured clang-tidy; documentation gate; root full build/CTest and merged probe.
- **Out of scope:** T-277 projection throttling/extrapolation, T-272 broad arena conversion, T-293 presentation watchdog, monitor settings, audio changes, total-scene rewrite.

## T-476 Measure the integrated whole-frame dense-chart budget

- **Status:** ready
- **Outcome:** an optimized integrated production workspace/projection/frontend/software-render probe reports useful frame work against 5 ms, distinct from presentation and pacing wait. Actual 200 Hz display certification remains pending unless a permitted real-display measurement exists.
- **Read first:** `src/sys/application_frame_projection.cpp`, `src/sys/application_frame_services.cpp`, `src/sys/frame_timing.cpp`, `tests/sys/frame_timing_tests.cpp`, `tests/ui/compose_canvas_render_tests.cpp`, T-475 above.
- **Files owned:** this card only; root scratch probes outside the repository. Supporting production instrumentation requires exact recorded scope extension before edits.
- **Steps:**
  1. Build normal optimized production targets and identify which frame stages the probe truly executes. Include frontend/software submission, matched-visible 50k/100k fixtures, warm follow/idle, selection, paint hover and boundary transitions where supported.
  2. Capture wall p50/p99/max, thread CPU, phase costs, scene/visible counters and 5 ms budget misses in an exclusive measurement window. Explicitly identify omitted event/audio/device/present stages; never label a projection-only result whole-frame.
  3. Repeat after integrated T-475, retaining startup/rebuild/settlement spikes separately and all sequence boundary samples. Record old-device and real-monitor validation as pending.
- **Invariants:** original charts remain read-only; no user desktop automation or app restart. Timings from Debug or concurrent build activity are not optimized acceptance measurements.
- **Reject/failure behavior:** unsupported backend/display measurements are reported as unavailable, with narrower production evidence identified precisely.
- **Verify:** probe executes successfully; actual output and workload identity saved; documentation gate; root normal full suite after implementation integration.
- **Out of scope:** frame-pacing policy changes, renderer rewrites, display configuration and hardware-equivalence extrapolation.
