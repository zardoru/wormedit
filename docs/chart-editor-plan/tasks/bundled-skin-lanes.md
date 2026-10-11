# Bundled skin lane mapping

[Back to task cards](../tasks.md).

## T-531 Bundled skin lane mapping

- **Status:** done (2026-10-11; verified; uncommitted user-feedback batch)
- **Outcome:** Forme, MDKP and every bundled field skin use semantic scratch/key bindings for DP5, DP7 and user scratch placement on both stages.
- **Read first:** scripts/import-ibmsc-theme.py, regenerated assets/themes/field JSON and provenance, src/ui/nuklear/theme.h/.cpp as needed, tests/ui/theme_tests.cpp and focused importer regressions.
- **Files owned:** scripts/import-ibmsc-theme.py, regenerated assets/themes/field JSON and provenance, src/ui/nuklear/theme.h/.cpp as needed, tests/ui/theme_tests.cpp and focused importer regressions. This card; coordinator owns index/checker/status/integration.
- **Steps:** 1. Audit current behavior. 2. Implement shared behavior with opt-in exceptions where needed. 3. Add regressions. 4. Verify build, full suite, events, native captures and complexity. 5. Update status.
- **Invariants:** Lane colors follow authored lane identity and semantic role, independent of display placement; both stages remain visible and authored chart IDs are unchanged.
- **Reject/failure behavior:** Invalid input keeps safe defaults; interrupted gestures cancel without unintended actions.
- **Verify:** Normal/release builds; focused tests; complete production event harness and CTest; affected-file clang-tidy; isolated Xvfb; documentation validation.
- **Done so far:** Unified source scratch-column mapping in the importer for BMSE, MDK, Forme and IIDX. Imported MDKP/Pomu named semantic colors for both sides. Regenerated all affected bundled skins; runtime colors continue through the shared authored-lane identity binding, independent of scratch presentation.
- **Verification evidence:** Normal and release builds pass. Full CTest: 3,195/3,195 passed without retries. Production frontend event harness: 391 cases / 5,640 assertions. No new or increased affected-source complexity violations; existing unrelated warnings remain. Native verification uses isolated Xvfb. Evidence: `/tmp/feedback-batch-{ready-build,ready-release,all-tests,events,skins,frontend-tidy}.log`. All eight imported skins pass a DP5/DP7 matrix across both stages, four authored scratch placements, reversed enumeration and note/hold lookup: 10,056 assertions. Existing Wormedit and osu!mania checks also pass. Import regeneration is reproducible. Native Forme/MDKP DP5/DP7 captures show complete P1/P2 fields and scratch colors on the scratches: `/tmp/t531-*.png`.
- **Remaining:** None.
- **Out of scope:** Automatic commit and unrelated feature changes.
