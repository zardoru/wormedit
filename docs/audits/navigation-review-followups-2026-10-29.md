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

Linux CI now explicitly selects GCC 14 or Clang 19 and installs GCC 14's library.
The Clang job selects that installation instead of relying on the runner's newest GCC.
Ubuntu publishes [GCC 14](https://packages.ubuntu.com/noble/amd64/g%2B%2B-14)
and [Clang 19](https://packages.ubuntu.com/noble-updates/clang-19) for these runners.
[Clang 19's release notes](https://releases.llvm.org/19.1.0/tools/clang/docs/ReleaseNotes.html)
document the warning split and concepts support needed by libstdc++'s `expected`.

Configure probes `std::expected` and `std::bind_back` before dependency fetching;
Clang also probes its separate designated-initializer warning. Unsupported pairs fail
with an actionable message. Existing warning flags, warnings-as-errors, and the matching
23.1.3 complexity baseline are unchanged.

## Verification in progress

- Initial matching-toolchain complexity: four changed production units pass,
  `/tmp/wormedit-review-followups-complexity-initial.log`.
- Real GCC and Clang compiler-policy fixture projects pass C++23 requirements,
  omitted designated members and complete positional members. Clang rejects incomplete
  positional members; GCC retains its existing allowance. An intentionally unavailable
  standard-library fixture fails the requirement probe before the dependency boundary.
  Driver: `/tmp/wormedit-review-compiler-probes.py`; report/logs:
  `/tmp/wormedit-compiler-probes.a71985ru`.
- Workflow YAML parses; Linux pins and Clang library-selection flags are explicit.
- Clang 23.1.4 preset application and frame-timing target build (including the new
  direct-family regression), final native GATE, focused workflows, event harness,
  final consuming-header complexity and remote CI remain pending. No pass is claimed yet.
  The initial default-target Clang build was intentionally interrupted without a
  compiler failure to finish these relevant targets first; its log is
  `/tmp/wormedit-review-clang-build.log`. Targeted completion is recorded separately
  in `/tmp/wormedit-review-clang-targets.log`; no complete Clang all-target build is claimed.
