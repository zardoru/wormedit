# Opaque dropdown surfaces and shadows

[Back to task cards](../tasks.md).

## T-529 Opaque dropdown surfaces and shadows

- **Status:** done (2026-10-11; verified; uncommitted user-feedback batch)
- **Outcome:** Fix transparent dropdown panels and ensure shared panel fills/shadows work across menus and safe combos without covering content.
- **Read first:** `src/ui/nuklear/chrome.h`, `src/ui/nuklear/components.cpp` (actual surface implementation), `tests/ui/components_tests.cpp`, relevant theme surface code only if required, focused surface/dialog/event tests.
- **Files owned:** `src/ui/nuklear/chrome.h`, `src/ui/nuklear/components.cpp` (actual surface implementation), `tests/ui/components_tests.cpp`, relevant theme surface code only if required, focused surface/dialog/event tests. This card; coordinator owns shared index/checker/status and integration. Coordinate separate frontend payload regions; append tests through incremental patches.
- **Steps:** 1. Audit implementation and mockup. 2. Implement the requested behavior through existing runtime boundaries. 3. Add meaningful interaction/persistence regressions. 4. Verify integrated build, full suite, native captures and complexity. 5. Reconcile Brain and local status.
- **Invariants:** Popup fill and shadow use the app viewport boundary independently of the host clip. Restore the original command clip and styles; paint surfaces before content.
- **Reject/failure behavior:** Stale or missing targets reject safely; interrupted gestures cancel. Missing recent files report diagnostics through the existing open path. Dropdown closure cannot click through.
- **Verify:** Normal/release builds, relevant focused tests, complete production event harness and CTest suite, affected-file clang-tidy, isolated Xvfb native inspection and documentation validation.
- **Done so far:** Popup fills and shadows now use the app viewport rather than the short host window clip. Even elevation-zero surfaces install their full surface clip and restore the original clip after painting.
- **Verification evidence:** Normal and release builds pass. Full CTest: 3,195/3,195 passed without retries. Production frontend event harness: 391 cases / 5,640 assertions. No new or increased affected-source complexity violations; existing unrelated warnings remain. Native verification uses isolated Xvfb. Evidence: `/tmp/feedback-batch-{ready-build,ready-release,all-tests,events,skins,frontend-tidy}.log`. Four new command-level checks pass 53 assertions, including first and later frames for short-host menus/combos, no viewport fallback and zero elevation. The tests reproduce the original failures. Native Compose and Appearance dropdowns are opaque, rounded, shadowed and readable: `/tmp/t529-{compose-dropdown,options-dropdown,options-dropdown-later}.png`.
- **Remaining:** None.
- **Out of scope:** Automatic commit, unrelated editor feature work, changes to authored chart content beyond explicit chart reordering.
