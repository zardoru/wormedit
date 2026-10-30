# Navigation and modifiability integration — 2026-10-29

The [independent review follow-up audit](navigation-review-followups-2026-10-29.md)
tracks post-`4eb80bc` source safeguards and CI compiler fixes. Its current verification
state supersedes any readiness interpretation of the earlier local results below.

The implemented batch consolidates [PR77](https://github.com/zardoru/wormedit/pull/77),
[PR78](https://github.com/zardoru/wormedit/pull/78),
[PR79](https://github.com/zardoru/wormedit/pull/79) and
[PR80](https://github.com/zardoru/wormedit/pull/80) in
[PR81](https://github.com/zardoru/wormedit/pull/81). The benchmark base is `d154284`;
integration also includes main's waveform-detail fix `a18cf45`. Pending feature PRs were
examined as change-scope examples, without merging their implementations.

Round one is accepted at `23e113b`. Round two continues the same PR and resolves the solo
policy, selected input-family admission and boundary-guard findings below. Its current
acceptance and measured tradeoffs are recorded at the end of this report; the supplemental
findings retain their original review context.

## Implemented owners and observed results

The lane reports retain their own fixed configurations, source revisions and measurement
protocols. Counts from different rows/configurations must not be combined into a speedup.

| Concern | Implemented change | Evidence and limits |
| --- | --- | --- |
| Repeated compilation | Shared frontend/support static targets, with smaller dependency owners beneath them | A's native configuration: 1,214 → 1,075 first-party compile actions; 852 → 713 production actions; 17 exact duplicate production actions → zero. All five measured narrow target closures are unchanged. No elapsed-time claim. |
| Heavy public contracts | `document_command_types.h` and `position_types.h` own shared values | B's normalized document-header reach: 663 → 656 sources; command-header reach: 376 → 376. Real notifications dependencies: 18 → 7 files; sound groups: 26 → 29; interaction: 55 → 58. |
| Workflow/projection ownership | Application state has an application header; clipboard lifecycle and quantize have named owners | C's same-configuration scene-header fanout: 190 objects/83 sources → 159/72. Paste implementation include closure: 211 → 43 files. Quantize settings edit: three → four objects, a negative result. |
| Repeated structural membership | `chart_collections.h` supplies typed flat membership and point participation | D migrates seven consumers; root closes three retained lists in diagnostics, redivision and semantic-layer caps. Ten reviewed lists now use one membership owner. Literal `chart.volumes`/`chart.sample_sets` search in core: 14 → seven files. Nested, optional, reference, endpoint, codec and domain-validation policies remain explicit. |
| Forwarding ceremony | Callers reach clipboard `revise`/`apply` directly | Root removes two pure wrappers. The begin adapter remains because it reads the clipboard service and clears paint modes on admission. |
| Storage assumptions | Typed operations declare storage support; shared staging selects existing prepared adapters | Unsupported storage refuses atomically. Root removes two unused composite modes. The exercised combined callback retains its documented deterministic replay requirement; it is not an arbitrary side-effect callback. |

The lane audits contain the repeatable protocols and negative controls:

- [Build target reuse](build-target-reuse-2026-10-29.md).
- [Core edit contracts](core-edit-contracts-2026-10-29.md).
- [Compose workflow ownership](compose-workflow-ownership-2026-10-29.md).
- [Typed collection traversal](chart-collection-traversal-2026-10-29.md).

Navigation evidence is weaker than compile-action evidence. C's fresh HIGH navigation pilot
inspected seven files before and after and did not complete both requested routes; it also
deviated from the prescribed tool protocol. D's membership lookup adds one header hop. B's
domain → runner → prepared-adapter route still has three implementation owners. There is no
supported general navigation-speed improvement. The concrete gains are named owners, two
removed forwarding hops, a smaller structural search result and fewer repeated extension sites.

## Supplemental design findings

These findings distinguish observed obligations from hypothetical failures. The independent
supplemental audit read root `6aeb05c`, C `80f4cbe` and D's then-uncommitted diff. The final
source review read `d154284..822cc78`. Root rechecked the retained sites while integrating.

### High: solo-selection policy has an implicit synchronization obligation

`src/sys/compose/input/compose_input.cpp` installs the application's effective solo layer in
interaction state before dispatch. `src/sys/compose/views/compose_view.cpp` independently
installs the same value while projecting. The solo action in `compose_actions.cpp` changes
the application flag; core selection and hit testing read interaction state. The recent
`4c5fade` fix crossed application input, projection, interaction, hit testing and selection.
This establishes a maintenance obligation, without establishing a current failing interaction.

Next bounded change: let the semantic-layer session owner supply one effective selection
policy and install it at selection dispatch. Projection should read that policy. Benchmark
the same target/solo change and event sequence with and without a preceding projection;
require identical selection and fewer independent synchronization sites. Keep core selection
independent of frontend/application state. This batch relocates the state declaration but
does not complete that behavioral ownership change.

### High: individual Compose inputs have an untyped field protocol

`src/ui/frontend/actions.h` gives `compose_input_action_t` a kind plus shared fields whose
meaning varies by kind. `span_edge` carries either a recipe command or a span handle;
`paint_intent` carries different policies. Semantic-layer paint in `compose_input.cpp`
interprets any value other than 2 as addition, despite the documented 1/2 contract.
Producers and consumers independently know these rules; incompatible field combinations
remain constructible. No invalid value emitted by the current UI was demonstrated.

Next bounded change: type the paint and recipe/span payload families and convert once at
admission, rejecting unsupported raw values there. Retain the legitimate frame action
accumulator, which can contain simultaneous events. Benchmark one added paint policy by
the necessary producer, admission, domain and test files; require exhaustive handling and
a boundary regression for unsupported values. Do not move current-document checks out of
the transaction or duplicate the shared stale/publication safeguards.

### Medium: combined storage mutation still relies on replayable callbacks

`src/core/command.cpp` runs the combined settings/object mutation through two authoritative
adapters. Its current caller is deterministic and respects the captured catalog lifetime;
the final review found no current failure. Root narrowed the API to exercised modes and
made the replay restriction explicit in `chart_edit_storage_support.h` and the architecture.

If another caller needs counters, allocation or external effects, produce a settings/object
plan once and apply its portions through the existing adapters, or introduce a narrowly
named replayable operation. Do not silently expand the general callback contract. Preserve
one lazy transaction copy, exact owner identities, atomic rejection and one publication.

### Medium: durability orchestration depends on concrete format storage

`include/wormedit/core/command_journal.h`, `autosave_loop.h`, `recovery_controller.h` and
`startup_session.h` import native persistence contracts. Journal APIs expose native snapshot,
limit and diagnostic types, including the storage service's publication concerns. This is a
concrete core → formats dependency, rather than evidence of an incorrect recovery path.

A future ownership change should put durability/session orchestration with application
services while preserving codec/storage implementation in formats. Extract shared value
contracts only when necessary; there is no second storage consumer justifying a generic
interface. Benchmark a snapshot/publication contract change by its consumer and rebuild scope.

### Medium: the frontend boundary guard covers only part of the seam

`cmake/check_ui_boundary.cmake` scans public core/audio/formats headers and their `.cpp`
files but omits private headers in those directories. It detects toolkit dependencies, not
the reverse frontend → document/history/editor-state restriction. A green result does not
establish both directions. No current forbidden dependency was demonstrated by this gap.

Extend the existing guard to private headers and add a precise reverse include check with
shared vocabulary explicitly allowed. Validate it using representative forbidden fixtures
or temporary mutations, rather than a new broad architecture framework.

## Coverage of the requested smells

| Requested concern | Verdict |
| --- | --- |
| Implicit coupling | Solo-policy synchronization remains; storage replay is explicit and narrowed but still a contract. |
| Layer-first folders | Directory shape alone is not evidence of a defect. Keep layer seams; group coherent workflows within them. The clipboard/quantize owners are the bounded application of this strategy. |
| Duplication | Repeated compilation, ten reviewed structural lists and two paste forwarders are addressed. Similar domain policies are not presumed interchangeable. |
| Representable illegal states | Typed storage declarations improve one boundary. Individual input payload combinations remain a high-priority follow-up. The whole frame accumulator is valid. |
| God files | Workflow/state extraction reduces unrelated responsibilities in projection/actions. Large action contracts and remaining synchronization deserve behavioral ownership work; line count alone is insufficient. |
| Shotgun surgery | Structural collection registration has one owner in the reviewed scope. Sample controls' full 68-file feature change also contains legitimate persistence, import and UI work. |
| Layering violations | Core durability services import concrete format services. Frontend guard coverage is incomplete; broader frontend leakage was not proven. |
| Pass-through wrappers | Two pure paste wrappers are removed. Typed route runners, begin admission and timing access adapters retain real dispatch/policy responsibilities. |
| Errors/validation away from boundaries | Raw action admission can improve. Domain/current-state, prepared-owner and publication checks remain authoritative where they execute. |

## Combined acceptance

The initial Lua-enabled integration build and independent review exposed a missing direct
`song_document_t` declaration in `lua_document_api.h`. Root added the declaration and the
Lua fixture's own document-model include. Both Lua targets build and 109/109 Lua tests pass.
The first integration gate did not run CTest after its build failure; it is not a passing gate.

The next full run found only a documentation failure: the architecture map used `.h/.cpp`
as a path shorthand. Root expanded both entries to existing paths; the focused check and
the entire GATE were rerun. No failure was waived and no checker allowance was raised.

Final combined configuration: existing root Make/Debug build, GCC `/usr/bin/c++`,
application, Lua, audio/image/video decoders and FFmpeg enabled. With the matching
Clang/clang-tidy/libclang 23.1.3 environment, root ran
`scripts/gate.sh build --jobs 2 --complexity-base d154284`:

```text
gate: 2 job(s), set explicitly; logs in /tmp/wormedit-gate.qNoKkO
  pass  build
  pass  ctest
  pass  whitespace
  pass  complexity
  100% tests passed out of 5596
gate: passed
```

Summary: `/tmp/wormedit-integration-gate-accepted.log`; changed-TU complexity: 45/45.
The latest-main waveform implementation was rebuilt in both app and instrumented tests
before this run. An earlier in-flight build was stopped when main merged, so stale objects
were not accepted as evidence.

Additional matching-toolchain complexity passes 44/44 unchanged header-consuming units,
including scripting, prepared storage, application and collection consumers. Together with
the gate this covers 89 distinct translation units. Reproduction list:
`/tmp/wormedit-integration-header-consumers.txt`; summary:
`/tmp/wormedit-integration-header-complexity.log`; details:
`/tmp/wormedit-complexity.5uj0b3c6`.

The external PR58 property source at `e15c2cb457221cf2d316efde56ac8b4ee5b45f29`
matches the pinned Git object byte-for-byte (SHA256
`3c5ff607abf41bc9239c70f0c14b28e94f291ac3b315968f627785a166cdde34`). Compiling it with
the current measure-editor test command and linking the current core libraries passes
193,484 assertions in two fast/extended cases. The pending PR itself is not merged.
Reproduction helper: `/tmp/wormedit-integration-property-check.py`; result:
`/tmp/wormedit-integration-property.log`.

`scripts/smoke-compose-virtual-x.sh /tmp/wormedit-integration-compose.png compose` passes.
The PNG was opened and inspected: Compose field/timing lanes, toolbar, quantize inspector
and Sounds dock render normally. This is an isolated startup/render check; event workflows
are covered by the full gate. No test window opened on the user's desktop.

Standalone headless/event-only results remain scoped to A's configuration. This combined
acceptance is local Linux/GCC native evidence; it is not a macOS, Windows or Wasm claim.
No complexity baseline, test limit or allowance was increased. The generated tracked
walkthrough fixture was restored after all runtime consumers finished and excluded from the
change. Final documentation passes 6/6 (`/tmp/wormedit-integration-docs-final.log`), including
chart-editor state, architecture names and ceremony ratchets; staged whitespace passes.

## Round two: policy, admission and dependency boundaries

T-782/T-783 and T-784 are complete in the same PR. Solo eligibility has one session query,
implemented out of line; selection entry surfaces install it and projection reads it without
repairing retained state. Existing application refresh retains invalid-target/brush cleanup.
Paint, Draw-source, span and recipe selectors have one admission owner; unsupported values
reject before handlers, with existing family diagnostics preserved. The frame accumulator
remains transport. Its event-scoped typed variant still relies on an internal route pairing,
explicitly reviewed and covered by actual selected workflows.

The guard includes private headers and a reviewed frontend vocabulary import list. BGA usage
contracts reuse round one's light values; the sys adapter declares/includes its own model
dependency. The guard remains a direct source check with documented alias/macro/transitive
limitations. See the [input contracts audit](compose-input-contracts-2026-10-29.md) and
[boundary audit](frontend-boundary-guard-2026-10-29.md) for full protocols and source scope.

Measured results:

- Raw paint/span selector interpretations: four source files → one admission file. Selected
  handlers have zero wire-selector reads; five legitimate reads remain in admission.
- Solo eligibility: four derivations → one query body. There are three dispatch installation
  surfaces instead of two retained writes; projection's retained repair is removed.
- Matched idle Make graph: a session-header touch plans 181 compiler output objects; a policy
  implementation touch plans four. This is a timestamp-only edit-location simulation, not a
  historical wall-time comparison. The definition adds one hop and four cold compiler actions.
- Matched 1,076 existing objects: model-header dependencies 972/656 objects/sources → 884/580.
  Four new policy objects are outside both sides. BGA/action headers' own reach is unchanged.
- Current native database: 1,082 entries/757 unique sources, 720 production entries and zero
  exact duplicate production actions. Round one's fixed-configuration denominator is unchanged.
- Negative controls: input-panel/recipe-form includes remain 120/76; input dispatch grows
  223→224 files. Components shrink 58→51 and lose the model dependency. No general navigation
  speed or elapsed build-time improvement is claimed.

Final combined Debug/GCC Lua ON GATE passes build, 5,602/5,602 tests, whitespace and 10
changed-TU complexity checks against `23e113b` (`/tmp/wormedit-gate.LyyJ9B`). Eight additional
consumers pass pinned 23.1.3 complexity. Focused selected workflows pass 847 assertions/58 cases;
event harness 721/721; isolated Compose smoke is inspected. A fresh Sol-high source review
found no blockers; runtime caught two fixture assumptions and two diagnostic regressions,
which were corrected without weakened assertions or allowances before this final green gate.

Remaining priorities are durability/session service ownership and the combined storage callback's
explicit replay requirement. The code does not yet express replayability in its callback type;
the current caller remains deterministic. No generic persistence or edit-plan framework was
introduced in either round. Broader god-file, directory and full action-variant changes require
their own measured scope rather than a claim that this batch closes every design smell.

Final round-two documentation checks pass 6/6 (`/tmp/wormedit-round2-docs-final.log`),
including state, architecture names and count ratchets. Staged whitespace is checked before
commit; generated walkthrough content is restored and excluded.
