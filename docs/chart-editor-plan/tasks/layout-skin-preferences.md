# Per-layout skin preferences

[Back to task cards](../tasks.md).

## T-534 Per-layout skin preferences

- **Status:** done (2026-10-11; verified; uncommitted user-feedback batch)
- **Outcome:** A persisted default skin and per-layout overrides resolve by the active selected layout with safe compatibility fallback.
- **Read first:** include/wormedit/core/preferences.h, src/core/preferences.cpp, src/sys/options_runtime.h/.cpp, src/sys/app/main.cpp, src/ui/frontend.h, src/ui/nuklear/frontend.cpp, src/ui/nuklear/options_window.cpp, preferences/Options tests and production event tests.
- **Files owned:** include/wormedit/core/preferences.h, src/core/preferences.cpp, src/sys/options_runtime.h/.cpp, src/sys/app/main.cpp, src/ui/frontend.h, src/ui/nuklear/frontend.cpp, src/ui/nuklear/options_window.cpp, preferences/Options tests and production event tests. Coordinator owns this card, task index/checker, status and integration; Codex dragon worktrees retain separate ownership.
- **Steps:** 1. Audit existing classes and mapping. 2. Implement the shared semantic model. 3. Add regression coverage. 4. Verify builds, full suite, events, complexity and native captures. 5. Reconcile Brain/status.
- **Invariants:** Authored lane IDs, channels, note assignments and document Undo remain unchanged by skin preferences. Selected layout semantics determine colors, independent of source format. Scratch movement is presentation-only.
- **Reject/failure behavior:** Unsupported or missing override falls back to compatible default then Wormedit. Invalid preferences retain safe values; stale actions reject. Numbered skins require explicit compatible classes/counts/stages.
- **Verify:** Normal/release builds; full CTest; production events; affected-file clang-tidy; importer regeneration and preview validation; isolated Xvfb; documentation validation.
- **Done so far:** Default plus up to 128 per-layout overrides persist with bounded validation and legacy migration. Appearance offers a compatible current-layout picker, Use default and resolved-preview text. Context changes invalidate stale actions; Apply/Revert/Defaults preserve draft semantics. Native restart preserves distinct default/DP5 choices. Focused preference/Options checks and production picker/bridge event checks pass.
- **Verification evidence:** Normal and release builds pass. Full CTest: **3,219/3,219 passed without retries**. Production frontend events: **394 cases / 5,668 assertions**; theme checks: **18 cases / 12,369 assertions**; core layout/preferences checks: **12 cases / 177 assertions**. All eight pinned imports reproduce; preview validates ten palettes. Affected-file clang-tidy has no new or increased complexity violations. Native isolated Xvfb verifies Forme osu10, compatible MyO2, incompatible same-count class fallback, separate default/layout choices after restart, and actual StepMania import switching to osu 5K. Evidence: `/tmp/semantic-skins-{all-tests-passing,events-final,theme-final,core-final,importer,preview,root-final-tidy,unnamed-tidy,roundtrip-tidy}.log`; `/tmp/t533-{forme-osu10,myo4-compatible,myo4-wrong-class-fallback}.png`, `/tmp/layout-skins-options-{initial,restarted}.png`, `/tmp/semantic-sm-layout-initial.png`.
- **Remaining:** None.
- **Out of scope:** Automatic commit, new chart formats and authored layout changes caused by choosing a skin.
