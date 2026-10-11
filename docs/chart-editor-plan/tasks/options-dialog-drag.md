# Options dialog dragging

[Back to task cards](../tasks.md).

## T-532 Options dialog dragging

- **Status:** done (2026-10-11; verified; uncommitted user-feedback batch)
- **Outcome:** Options has a draggable header while shared alerts remain immovable by default.
- **Read first:** src/ui/nuklear/dialog.h/.cpp, src/ui/nuklear/options_window.h/.cpp, tests/ui/dialog_tests.cpp and tests/ui/nuklear_event_harness_tests.cpp.
- **Files owned:** src/ui/nuklear/dialog.h/.cpp, src/ui/nuklear/options_window.h/.cpp, tests/ui/dialog_tests.cpp and tests/ui/nuklear_event_harness_tests.cpp. Coordinator adds focus-loss and modal-block cancellation hooks in src/ui/nuklear/frontend.cpp. This card; coordinator owns index/checker/status/integration.
- **Steps:** 1. Audit current behavior. 2. Implement shared behavior with opt-in exceptions where needed. 3. Add regressions. 4. Verify build, full suite, events, native captures and complexity. 5. Update status.
- **Invariants:** Dragging is opt-in. Existing resize, viewport clamp, close, preference draft and alert semantics remain intact.
- **Reject/failure behavior:** Invalid input keeps safe defaults; interrupted gestures cancel without unintended actions.
- **Verify:** Normal/release builds; focused tests; complete production event harness and CTest; affected-file clang-tidy; isolated Xvfb; documentation validation.
- **Done so far:** Added opt-in movable shared-dialog headers and a visible Lucide grip; Options enables movement and retained resizing, while alerts remain centered and immovable. Moving retains position and clamps to the viewport. Close, hidden views, focus loss and modal blocking cancel movement.
- **Verification evidence:** Normal and release builds pass. Full CTest: 3,195/3,195 passed without retries. Production frontend event harness: 391 cases / 5,640 assertions. No new or increased affected-source complexity violations; existing unrelated warnings remain. Native verification uses isolated Xvfb. Evidence: `/tmp/feedback-batch-{ready-build,ready-release,all-tests,events,skins,frontend-tidy}.log`. Production chart/Options focused checks pass 45 assertions / 5 cases, including actual header drag, Close and focus-loss/hide cancellation. Existing immovable-alert and resize regressions pass in the full suite. Native movement by 140×100 retains the footer and bounds: `/tmp/options-drag-{before,during,after}.png`.
- **Remaining:** None.
- **Out of scope:** Automatic commit and unrelated feature changes.
