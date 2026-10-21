# Recent projects and startup layout

[Back to task cards](../tasks.md).

## T-528 Recent projects and startup layout

- **Status:** done (2026-10-11; verified; uncommitted user-feedback batch)
- **Outcome:** Persist successful project/chart opens and expose them in File and startup, honoring the existing recent-files limit; center startup controls with deliberate margins.
- **Read first:** `src/sys/preferences/preferences_runtime.cpp`, `src/sys/main.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/menu_bar.cpp`, `tests/ui/menu_bar_event_harness_tests.cpp`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/menu_bar.cpp/.h`, `src/ui/workspace_menu.h`, `src/sys/macos_menu.mm`, `src/ui/frontend.h` (recent views/actions only), `src/sys/main.cpp`, `src/sys/preferences_runtime.*`, `tests/ui/nuklear_options_state_tests.cpp`, `tests/ui/menu_bar_event_harness_tests.cpp`, new recent-project runtime helpers, relevant tests and build registration. `tests/CMakeLists.txt` may add the existing nlohmann_json dependency for the runtime history test target. This card; coordinator owns shared index/checker/status and integration. Coordinate separate frontend payload regions; append tests through incremental patches.
- **Steps:** 1. Audit implementation and mockup. 2. Implement the requested behavior through existing runtime boundaries. 3. Add meaningful interaction/persistence regressions. 4. Verify integrated build, full suite, native captures and complexity. 5. Reconcile Brain and local status.
- **Invariants:** Only successful opens/imports/saves update history. Existing recent-files limit controls visibility; history is bounded, normalized and deduplicated. Open actions retain unsaved-change guards and menu pointer capture.
- **Reject/failure behavior:** Stale or missing targets reject safely; interrupted gestures cancel. Missing recent files report diagnostics through the existing open path. Dropdown closure cannot click through.
- **Verify:** Normal/release builds, relevant focused tests, complete production event harness and CTest suite, affected-file clang-tidy, isolated Xvfb native inspection and documentation validation.
- **Done so far:** Successful startup/open/import/save paths now update bounded atomically persisted, normalized newest-first history. File and startup expose entries with full-path tooltips and honor the recent-files preference. File commands precede a scrollable history; startup uses a centered 560-pixel column with deliberate margins and an independently scrolling list.
- **Verification evidence:** Normal and release builds pass. Full CTest: 3,195/3,195 passed without retries. Production frontend event harness: 391 cases / 5,640 assertions. No new or increased affected-source complexity violations; existing unrelated warnings remain. Native verification uses isolated Xvfb. Evidence: `/tmp/feedback-batch-{ready-build,ready-release,all-tests,events,skins,frontend-tidy}.log`. History checks: 3 cases / 125 assertions; recent-event checks: 2 cases / 26 assertions including 100 entries in a small window. Native startup/File scrolling, successful recent open and persistence after restart are verified. Captures: `/tmp/recent-{startup,file,file-scrolled,opened,restarted}-native.png`.
- **Remaining:** None.
- **Out of scope:** Automatic commit, unrelated editor feature work, changes to authored chart content beyond explicit chart reordering.
