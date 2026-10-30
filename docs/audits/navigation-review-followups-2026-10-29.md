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

## Verification in progress

- Initial matching-toolchain complexity: four changed production units pass,
  `/tmp/wormedit-review-followups-complexity-initial.log`.
- Before the two CI test-support fixes, native Lua ON GATE passes 5,603/5,603,
  whitespace and five changed-unit complexity checks against `4eb80bc`:
  `/tmp/wormedit-review-final-gate.log`, details `/tmp/wormedit-gate.zkquKu`.
  Four additional consuming units pass `/tmp/wormedit-review-final-header-complexity.log`.
  The new admission test initially scored 49; named stroke setup and the existing fatal
  `expect_compose_test` helper retain every assertion and satisfy the limit. Focused
  GCC and Clang runs retain 931 assertions/59 cases. Both CI supporting files separately
  pass matching complexity, `/tmp/wormedit-review-ci-tests-complexity.log`.
- Real GCC and Clang compiler-policy fixture projects pass C++23 requirements,
  omitted designated members and complete positional members. Clang rejects incomplete
  positional members; GCC retains its existing allowance. An intentionally unavailable
  standard-library fixture fails the requirement probe before the dependency boundary.
  Driver: `/tmp/wormedit-review-compiler-probes.py`; report/logs:
  `/tmp/wormedit-compiler-probes.edsbawfg` (updated diagnostic implementation).
- Exact selected Ubuntu pairs pass the feature probe and actual warning-policy fixtures:
  `/tmp/wormedit-ci-supported-policy-probes.log`,
  `/tmp/wormedit-ci-clangpp-21-policy.log`, `/tmp/wormedit-ci-gpp-14-policy.log`.
  Reproducer: `/tmp/wormedit-ci-toolchain-08d/run-supported-probes.py` with
  task-owned `wormedit-ci-toolchain-probe` Ubuntu container images.
  Clang rejects incomplete positional members; GCC retains its existing allowance.
  `/tmp/wormedit-ci-clangpp-19-probe.log` and `/tmp/wormedit-ci-clangpp-20-probe.log`
  retain the rejected combinations and direct compiler errors. Containers install binary
  packages without changing the host toolchain; they do not build the full project.
- Workflow YAML parses; Linux pins and Clang library-selection flags are explicit.
- Clang 23.1.4 preset application and frame-timing targets build successfully:
  `/tmp/wormedit-review-clang-targets-verified.log`. Focused workflows pass 931 assertions
  in 59 cases, `/tmp/wormedit-review-clang-workflows.log`.
  The new direct-family fixture initially attempted to default-construct a measure index;
  it now uses the existing measure-index and proposal factories. No assertions or allowances
  were removed to fix that compile error.
- Final native GCC/Lua GATE, event harness, additional consuming-header complexity,
  final documentation and remote CI remain pending. No pass is claimed for those yet.
  The initial default-target Clang build was intentionally interrupted without a
  compiler failure to finish these relevant targets first; its log is
  `/tmp/wormedit-review-clang-build.log`. Targeted completion is recorded separately
  in the verified log above; no complete Clang all-target build is claimed.
