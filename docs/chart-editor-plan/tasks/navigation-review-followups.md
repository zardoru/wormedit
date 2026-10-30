# Navigation and modifiability review follow-ups

## T-785 Reject missing admitted selectors and avoid unrelated prepared reads

- **Status:** done (2026-10-30; final local Lua ON GATE and explicit consuming checks pass)
- **Completion:** All admitted-payload readers return existing failure values for absent/wrong alternatives. Object-only prepared edits use the native adapter directly; combined catalog lifetime and replay remain explicit. Code `f70c747` passes 5,603/5,603 native tests, eight changed-unit plus four additional consuming complexity checks, 931 focused assertions/59 cases on GCC and Clang, 1,355 prepared assertions/69 cases, 721 event tests and inspected isolated Compose smoke. See the audit for source pins, logs and remaining limitations.
- **Outcome:** Missing or mismatched admitted payloads return existing input feedback; object-only prepared edits do not require a separate catalog projection.
- **Read first:** `docs/architecture.md`, `docs/chart-editor-plan/decisions/code-style.md`, `docs/audits/compose-input-contracts-2026-10-29.md`, `src/core/command.cpp`.
- **Files owned:** `src/sys/compose/input/compose_input_internal.h`, `src/sys/compose/input/compose_input.cpp`, `src/sys/compose/input/compose_timing_input.cpp`, `src/sys/compose/input/compose_export_cut_input.cpp`, `src/core/command.cpp`, `include/wormedit/core/chart_edit_storage_support.h`, `tests/sys/frame_timing_tests.cpp`, `tests/core/bms_program_edit_tests.cpp`, `docs/chart-editor-plan/tasks/navigation-review-followups.md`, `docs/audits/navigation-review-followups-2026-10-29.md`.
- **Steps:** 1. Check every admitted-payload consumer and object-only adapter caller. 2. Reject absent or mismatched payloads before mutation using existing failure values. 3. Keep object-only edits within their authoritative native adapter and retain catalog lifetime for combined edits. 4. Run meaningful workflow regressions, explicit header consumers and GATE against `4eb80bc`.
- **Invariants:** Valid input values, transaction publication, prepared owners and deterministic combined replay retain their existing behavior. An empty object list remains a no-op.
- **Reject/failure behavior:** Selector mismatch produces existing unsupported-input feedback rather than optional dereference or variant exception. Native adapter errors remain authoritative.
- **Verify:** Lua-enabled native GATE with matching Clang 23.1.3, `--jobs 2 --complexity-base 4eb80bc`; focused admission/paint/span/recipe and prepared edit tests; event harness; explicit consuming complexity; docs and whitespace.
- **Out of scope:** Chart-stamping solo targets, redesigning combined replay, persistence ownership, whole-frame variants or allowing invalid frontend values.

## T-786 Use capable CI compilers and diagnose unsupported local toolchains

- **Status:** done (2026-10-30; local verification and corrected remote CI pass)
- **Completion:** Selected Ubuntu/compiler/library pairs pass actual API execution and initializer-policy fixtures. Final local GATE passes 5,603/5,603 and all matching complexity checks. The journal overflow case passes 100 consecutive native runs; the named-owner clipboard fix passes a GCC 14 sanitizer reproducer and all four native cases. Remote [run 41001583485](https://github.com/zardoru/wormedit/actions/runs/41001583485) at `e4616f7` passes Linux GCC, Linux Clang and macOS, confirming the corrected toolchains and test-support fixes. Windows is skipped by the existing workflow condition. No failed job is waived; PR81 remains a draft for owner review.
- **Outcome:** Linux CI explicitly selects compilers and standard libraries supporting existing C++23 features and the designated-initializer warning policy; configure diagnoses missing capabilities before dependency builds.
- **Read first:** `docs/chart-editor-plan/decisions/code-style.md`, `.github/workflows/ci.yml`, `cmake/warnings.cmake`, `CMakePresets.json`, `docs/building.md`.
- **Files owned:** `.github/workflows/ci.yml`, `CMakeLists.txt`, `cmake/check_compiler_requirements.cmake`, `tests/ui/walkthrough_osu-stepmania_event_harness_tests.cpp`, `tests/core/command_journal_tests.cpp`, `tests/core/chart_clipboard_tests.cpp`, `docs/building.md`, `docs/chart-editor-plan/tasks/navigation-review-followups.md`, `docs/audits/navigation-review-followups-2026-10-29.md`.
- **Steps:** 1. Verify supported Ubuntu compiler packages and the Clang warning split. 2. Pin Linux GCC/Clang and install their required C++ library. 3. Probe actual C++23 and warning capabilities during configure. 4. Verify GCC and Clang configuration/builds, preserve diagnostics, and report actual CI outcomes.
- **Invariants:** Warnings remain errors; omitted designated members remain allowed while Clang rejects omitted positional members. The complexity toolchain and baseline are unchanged.
- **Reject/failure behavior:** Unsupported compiler/library combinations fail configure with an actionable diagnostic; no silent downgrade or warning suppression is added.
- **Verify:** GCC native GATE, Linux Clang preset build, compiler capability and warning-policy probes, workflow YAML parsing, documentation and whitespace; remote checks after pushing PR81.
- **Out of scope:** Unrelated compiler warnings, platform rewrites, complexity allowances, dependency upgrades or merging PR81.

Root owns supporting registration/index/status/architecture/audit updates in
`cmake/check_chart_editor_docs.cmake`, `docs/chart-editor-plan/tasks.md`,
`memory/chart-editor-status/resume.md`, `docs/architecture.md`, and
`docs/audits/navigation-modifiability-2026-10-29.md`. No implementation lane is active;
these bounded supporting paths are approved before edits.

T-786 coordinator scope decision before the CI matrix update: use the official
Ubuntu 26.04 runner for Clang 21 with GNU 15, retaining the Ubuntu 24.04 GCC 14 job.
The exact Ubuntu container probe passes for that Clang/library pair; Ubuntu 24.04
Clang 19 and 20 with GNU 14 fail the real `bind_back` probe. The already-owned
workflow, compiler-requirement module, building guide and audit record this selection
and expose compiler output directly on configure failure.

T-786 exact supporting extension approved before edits after the `3e28191` CI run:
`tests/ui/walkthrough_osu-stepmania_event_harness_tests.cpp` gives the exported-directory
range a named lifetime for GNU 14; `tests/core/command_journal_tests.cpp` waits for the
blocking graph to finish before its captured stack latches are destroyed. These address
the observed GCC build failure and a concrete lifetime defect found while tracing the
Clang timeout. Existing timeouts and assertions are preserved; production journal policy
remains outside this test-support extension.

T-786 exact supporting extension approved before edits after the `71f96be` GCC run:
`tests/core/chart_clipboard_tests.cpp` retains the measure-index result in the shared
bar-line row assertion helper. Its four failures all read through a temporary expected
owner in a range-for; GCC 14 lacks the C++23 lifetime extension implemented in GCC 15.
The fix makes the owner explicit and retains all row assertions. Clang and macOS pass
that remote run, including the preceding journal-lifetime fix.
