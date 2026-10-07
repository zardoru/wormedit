# Plan domain: high-refresh frame pacing

[Back to executable task cards](../tasks.md).

User target (2026-10-07): 200 Hz / 5 ms on the current display-capable PC,
with 144 Hz / 6.94 ms as the earlier reference. Dense charts can exceed 50,000
notes. Intended older target class is Ryzen 2000 or mobile Intel from 2015;
the user's GDI/GDAC comparison on 2008 hardware is a responsiveness reference,
not a measured claim for WormEdit. Audio has separate callback/decode threads.

## T-472 Pace frames without a fixed post-presentation sleep

- **Status:** ready
- **Outcome:** active frame pacing uses a 5 ms whole-frame budget instead of appending 1 ms after every presentation; a frame whose work or presentation already consumes the budget receives no additional sleep, while nonblocking/failed VSync and invisible windows remain bounded.
- **Read first:** `src/sys/application_frame_services.cpp`, `src/sys/application_frame_services.h`, `src/sys/main.cpp`, `src/sys/window.cpp`, `include/wormedit/window.h`.
- **Files owned:** `src/sys/application_frame_services.cpp`, `src/sys/application_frame_services.h`, `src/sys/main.cpp` (frame-start timestamp and render call only), new `src/sys/frame_pacing.cpp`, new `src/sys/frame_pacing.h`, new `tests/sys/frame_pacing_tests.cpp`, this card only. Root owns manifests until an explicit section-level handoff.
- **Steps:**
  1. Audit the production loop and record the unconditional `SDL_Delay(1)` plus existing VSync policy. Measure the legacy delay through an isolated/injected-clock schedule and keep useful CPU time distinct from presentation wait. Inspect dense-chart evidence without rewriting scene queries.
  2. Capture the whole-frame start before frame services. Compute a bounded remainder for visible frames (5 ms), hidden/minimized/occluded frames (50 ms), and apply SDL's precise delay only to a positive remainder. The configured VSync flag is not proof that present actually blocked.
  3. Preserve VSync policy. If work/present already takes the period, do not sleep or create a catch-up burst. Handle clock rollback and overflow without an unbounded delay. Visibility and monitor changes must not retain stale debt.
  4. Test injected clocks covering already-paced presentation, fast/nonblocking presentation, over-budget work, visibility transitions and repeated frames; verify the production call passes the original whole-frame start.
- **Invariants:** input and audio controls are serviced every active frame; rendering output and VSync policy remain unchanged. No pacing setting, audio extrapolation or total-chart rendering rewrite is introduced.
- **Reject/failure behavior:** absent/unavailable window state uses the bounded visible fallback; invalid elapsed time cannot produce an excessive wait.
- **Verify:** normal dependency-aware focused build and pacing tests; configured cognitive complexity at most 25; root normal application build and full CTest after integration; documentation gate. Record production wiring and simulated schedule evidence separately from live 200 Hz certification.
- **Out of scope:** stalled-present recovery/watchdog/native driver policy (T-293), throttled UI projections or extrapolation (T-277), WSOLA (T-471), changing monitor configuration, user desktop automation, per-note/measure inspector caching.
