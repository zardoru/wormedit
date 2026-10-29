# Frontend dependency boundary

## T-784 Cover private headers and frontend domain dependencies

- **Status:** partial (2026-10-29; ownership allocated, implementation in progress)
- **Done so far:** Reused light BGA value owners, expanded private-header coverage, enforced reviewed frontend direct imports and added guard rejection regressions. Shared frontend target builds; guard/state checks pass 3/3 and four explicit consuming complexity checks pass. Same-command compiler controls and negative results are recorded in the audit.
- **Remaining:** Combined verification with T-782/T-783, matched post-build dependency counts and final reporting.
- **Outcome:** The existing guard rejects toolkit dependencies in private core/audio/formats headers and frontend imports of model/history/state services; allowed shared vocabulary remains usable.
- **Read first:** `docs/architecture.md`, `docs/chart-editor-plan/decisions/code-style.md`, `cmake/check_ui_boundary.cmake`, `src/ui/frontend/bga_usage.h`.
- **Files owned:** `cmake/check_ui_boundary.cmake`, `cmake/check_ui_boundary_tests.cmake`, `CMakeLists.txt`, `src/ui/frontend/bga_usage.h`, `docs/chart-editor-plan/tasks/frontend-boundary-guard.md`, `docs/audits/frontend-boundary-guard-2026-10-29.md`. Root coordinates direct-include fallout extensions before edits; T-782/T-783 own their already-listed consuming paths.
- **Steps:** 1. Inventory frontend core imports and freeze baseline direct/transitive reach. 2. Replace the BGA document import with its actual shared value owners. 3. Expand the existing guard's source inventory and add a precise reviewed shared-vocabulary allowlist for frontend direct imports, including relative path escape checks. 4. Exercise positive vocabulary and forbidden private-header/domain-service cases. 5. Verify builds, changed-header consumers, GATE and docs; record measured scope and limits.
- **Invariants:** Existing runtime APIs, schemas and frontend view/action values are unchanged. This is a source dependency guard, not a C++ parser or a proof against macro-generated/aliased includes.
- **Reject/failure behavior:** Forbidden imports fail the existing architecture check with file/include attribution. No new allowance waives a domain dependency.
- **Verify:** `cmake -DWORMEDIT_SOURCE_ROOT=$PWD -P cmake/check_ui_boundary.cmake`; guard regression script; GATE and explicit header-consuming complexity using matching 23.1.3; documentation checks; whitespace.
- **Out of scope:** Toolkit replacement, moving persistence services, new generic architecture framework, T-782/T-783 semantic selection/input implementation.
