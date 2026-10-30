# Navigation and modifiability: independent review follow-ups

Scope: PR81 after `4eb80bc`, [T-785–T-786](../chart-editor-plan/tasks/navigation-review-followups.md).
This supplements the [integration audit](navigation-modifiability-2026-10-29.md);
its earlier measurements and acceptance logs describe their recorded source revisions.

## Review and bounded response

The user-requested Claude Opus review found no blocking defect in the source it traced.
It did not rerun builds/tests, validate measurement JSON, deeply review build reuse,
or deeply review the clipboard/quantize moves. Its source assessment does not clear CI
or constitute a GitHub approval.

Two nonblocking maintenance concerns are addressed here:

- The input context used an unchecked optional dereference and `std::get` for admitted
  payloads. All reviewed current routes matched correctly, but a future misroute could
  terminate the application. Queries now return `get_if` pointers and every consumer
  rejects missing/mismatched payloads through existing unsupported-input feedback.
  The BGA family checks before resetting or consuming the pending stroke. Its direct
  regression supplies missing/wrong payloads for begin, update and release.
- Object-only prepared mutations unnecessarily acquired a resolved catalog before
  entering the native object adapter. The only current object-only caller, lane
  assignment, ignores that catalog argument. It now uses the native adapter directly,
  borrowing the transaction's current document for the callback. That adapter retains
  authoritative target resolution and publication. Combined settings/object edits still
  retain the exact resolved catalog across deterministic replay. No current chart where
  the extra read alone caused a behavior failure was established.

Remaining findings are explicit: solo targets remain chart-local IDs without a chart
stamp, preserving existing behavior; combined replay still relies on its documented
callback contract; typed payloads are checked at runtime rather than coupled to route
types. Invalid BGA stroke values are rejected before dispatch and preserve a pending
stroke, including on begin; current frontend producers do not emit those invalid values.
The direct-include guard retains its documented macro/alias/transitive limitations.

## CI and compiler requirements

The reviewed PR's Linux failures also occurred on its main base `a18cf453`:
Clang 18 lacked the separate designated-initializer warning and the required
`std::expected` support with its selected library; GCC 13 lacked `std::bind_back`.
The workflow, warning policy and result owner were unchanged by the earlier refactor.
This is baseline evidence, not a waiver or a passing check.

The first pin in `08d014a` was insufficient: its Clang 19/GNU 14 capability check
[failed before the project build](https://github.com/zardoru/wormedit/actions/runs/40986507526/job/123838680151).
This is a failure of our selected pair, separate from the earlier main baseline.
An exact Ubuntu 24.04 container reproduces missing `std::bind_back`: GNU 14 gates
it on the explicit-this feature macro, absent from that Clang. Clang 20 with the
same library exposes the function but fails its invocation through `forward_like`.
Adding `<utility>` does not fix that observed combination. Neither pair is accepted.

Linux CI now selects GCC 14 on Ubuntu 24.04 and Clang 21/GNU 15 on the official
[Ubuntu 26.04 runner](https://github.com/actions/runner-images/blob/main/images/ubuntu/Ubuntu2604-Readme.md).
Exact Ubuntu containers compile, link and execute the feature probe for both selected
pairs. Clang's GNU 15 installation is explicit instead of relying on the newest installed GCC.
The [GNU 14 feature definition](https://github.com/gcc-mirror/gcc/blob/releases/gcc-14.2.0/libstdc%2B%2B-v3/include/bits/version.def)
and [Clang 19 warning split](https://releases.llvm.org/19.1.0/tools/clang/docs/ReleaseNotes.html)
explain the separate library-feature and diagnostic requirements.

Configure probes `std::expected` and `std::bind_back` before dependency fetching;
Clang also probes its separate designated-initializer warning. Unsupported pairs fail
with the compiler's failed-probe output directly in the diagnostic. Existing warning flags, warnings-as-errors, and the matching
23.1.3 complexity baseline are unchanged.

The corrected-pair run at `3e28191` builds Clang successfully and passes macOS.
Its remaining failures expose two test-support defects: GNU 14 returns `dangling`
from the temporary filesystem range in the BMS export walkthrough, and the journal
overflow helper destroys stack latches before its capturing worker necessarily returns.
The walkthrough now owns the range in a local variable; the overflow helper releases
and waits for its graph before returning. The latter is a concrete lifetime defect found
while tracing the Clang timeout; remote confirmation of that timeout fix remains pending.
The actual walkthrough translation unit passes GNU 14 syntax checking with the root
compile arguments in the isolated existing image, `/tmp/wormedit-ci-gcc14-walkthrough-check.log`.
The overflow case passes 100 consecutive native runs with `--repeat until-fail:100`
and a five-second diagnostic bound, `/tmp/wormedit-review-journal-stress.log`.
CI's existing 120-second timeout and all original assertions remain unchanged.

The `71f96be` run passes Clang and macOS, confirming the preceding journal fix remotely.
GCC builds successfully and reports four failed bar-line clipboard tests. Each failure
is a row comparison produced by the shared `bar_line_rows` oracle. It borrows
`measure_index_t::from(chart)->entries` directly in the range initializer.
[GCC's support table](https://gcc.gnu.org/projects/cxx-status.html#cxx23) places the
C++23 temporary-lifetime extension in GCC 15; GCC 14 destroys that expected owner too
soon. The helper now retains the result and checks it before iterating. A matching
expected/vector pattern under GCC 14 AddressSanitizer reports stack-use-after-scope
before the fix and passes with the named owner:
`/tmp/wormedit-range-owner-probe/result.log`, `temporary.log`, and `named.log`.
The four actual clipboard tests pass natively, `/tmp/wormedit-review-clipboard-lifetime-tests.log`,
and the changed unit passes matching complexity, `/tmp/wormedit-review-bar-lines-complexity.log`.
The updated full gate and remote GCC confirmation remain pending at this checkpoint.

## Local acceptance and remaining CI verification

C++ source: `e313cc0beb50a15d5fd40b94e525201dbd1cc6ca`, compared with `4eb80bc`.
The final native configuration retains Debug/GCC 16, app, Lua, audio/image/video and
FFmpeg enabled. No feature, timeout, warning or complexity allowance was relaxed.

- Final GATE passes build, **5,603/5,603 tests**, whitespace and **seven changed-unit
  complexity checks**, `/tmp/wormedit-review-ci-accepted-gate.log`; details
  `/tmp/wormedit-gate.5i1FO8`. Matching Clang/clang-tidy/libclang is 23.1.3.
- **Four additional unchanged consuming units pass**, for eleven distinct units in
  this follow-up: `src/core/interaction.cpp`, `src/core/semantic_layer_editor.cpp`,
  `src/sys/compose/input/compose_resource_input.cpp`, and
  `src/sys/compose/input/compose_waveform_fit_input.cpp`.
  `/tmp/wormedit-review-final-header-complexity.log` records their checks. The subsequent
  CI fixes change tests only, so these header-consumer results remain applicable.
- GCC and Clang focused policy/admission/workflow runs each pass **931 assertions in
  59 cases**. Logs: `/tmp/wormedit-review-ci-fixed-gcc-workflows.log` and
  `/tmp/wormedit-review-clang-complete-workflows.log`. Prepared edit workflows pass
  **1,355 assertions in 69 cases**, `/tmp/wormedit-review-prepared-workflows.log`.
- The event harness passes **721/721**, `/tmp/wormedit-review-ci-fixed-events.log`.
  The journal overflow case passes **100 consecutive runs** with
  `ctest --repeat until-fail:100 --timeout 5`; this is a stricter diagnostic bound,
  with the workflow's 120-second timeout preserved. Log:
  `/tmp/wormedit-review-journal-stress.log`.
- Clang 23.1.4 application and frame-timing targets build. The initial default-target
  build was intentionally interrupted to finish these relevant targets; a later fixture
  construction error was corrected with the existing measure-index/proposal factories.
  Logs: `/tmp/wormedit-review-clang-final-build.log` and
  `/tmp/wormedit-review-clang-complete-build.log`. No complete Clang all-target build is claimed.
- The new admission test initially scored 49. Named stroke setup and the existing fatal
  `expect_compose_test` helper retain every assertion and satisfy the limit; the focused
  assertion/case totals remain identical. No baseline entry or suppression was added.
- Isolated Xvfb Compose smoke passes and its normal empty seven-key Compose view was
  inspected: `/tmp/wormedit-review-compose.png`, log
  `/tmp/wormedit-review-compose-smoke.log`. Generated tracked walkthrough content is
  restored after runtime consumers finish and excluded from commits.
- Real local GCC/Clang compiler-policy fixtures and unsupported-library failure pass:
  `/tmp/wormedit-review-compiler-probes-final.log`, details
  `/tmp/wormedit-compiler-probes.edsbawfg`. Exact selected Ubuntu pairs pass API
  compile/link/execution and actual warning-policy fixtures:
  `/tmp/wormedit-ci-supported-policy-probes.log`,
  `/tmp/wormedit-ci-clangpp-21-policy.log`, `/tmp/wormedit-ci-gpp-14-policy.log`.
  Clang rejects incomplete positional members; GCC retains its existing allowance.
  The rejected 19/20 combinations and direct compiler errors remain in
  `/tmp/wormedit-ci-clangpp-19-probe.log` and `/tmp/wormedit-ci-clangpp-20-probe.log`.
  Reproducers live in `/tmp/wormedit-ci-toolchain-08d`; containers install binary
  packages without changing the host toolchain and do not build the full project.

Remote CI remains a separate acceptance step. `3e28191` passes macOS and the full
Clang build, but its GCC walkthrough compile and Clang journal-timeout failures require
confirmation after the `e313cc0` test-support fixes. T-785 is locally complete; T-786
remains partial pending those jobs. The PR remains a draft and has no claimed approval.
