# BMS control-flow research and `libotoworm` audit

Last reviewed: 2026-09-13.

References:

- [BMS command memo, English mirror](https://saxxonpike.github.io/bms-command-memo/index.html)
- [`libotoworm` BMS tree declarations](../../../libotoworm/include/bms.h)
- [`libotoworm` BMS tree parser/evaluator](../../../libotoworm/src/bms.cpp)
- [`libotoworm` BMS loader call site](../../../libotoworm/src/note_loader_bms.cpp)

## Command families in scope

- Random selection: `#RANDOM n`, fixed `#SETRANDOM n`, and `#ENDRANDOM`.
- Conditional groups: `#IF n`, `#ELSEIF n`/`#ELIF n`, `#ELSE`, and `#ENDIF`.
- Switch selection: `#SWITCH n`, fixed `#SETSWITCH n`, `#CASE n`, `#DEF`, `#SKIP`, and `#ENDSW`.

The command memo documents broad implementation disagreement. In particular, content directly inside a random block, orphan/malformed conditionals, `#ELSE`, nesting, and switch fallthrough do not behave uniformly. The editor therefore needs named target profiles and fixture-backed evaluation/export rules.

For the switch form described by the memo, evaluation begins at the matching `#CASE` or `#DEF`. Later cases fall through until `#SKIP` or `#ENDSW`; choosing one case subtree alone is not equivalent. The memo also describes random and switch blocks nested within one another, though player limits vary.

## Current `libotoworm` behavior

The local library provides useful `scope`, `child`, `command`, `parse_error`, and `tree` types, including random, IF/ELSEIF/ELSE, switch case/default, and a switch-break marker. It retains raw command lines inside this transient tree.

It is incomplete for wormedit's editing contract:

- `#SETRANDOM` is appended as a raw line and does not set an evaluator value.
- `#SETSWITCH` and `#ENDRANDOM` have no semantic dispatch entries.
- starting a `#RANDOM` while already directly inside one implicitly exits the prior scope; random inside switch scopes is rejected, so the documented mixed nesting cannot be represented generally.
- `tree::evaluate()` owns a default-constructed RNG and offers no caller-provided branch-selection environment or seed.
- switch evaluation chooses only one matching/default scope. It does not execute later cases through the stored `switch_break`/`#SKIP` boundary.
- the chart loader immediately calls `ParsedTree->evaluate()` and converts only that result. The tree and all inactive branches are discarded before wormedit receives the chart.
- parser failure returns one error and no recoverable partial tree, which is insufficient for a source editor that must show and repair malformed structures without losing raw spans.

Stage 0 should lock these observations down with direct library tests before deciding whether to extend the library API or build a wormedit-owned byte-preserving BMS program layer.

## Required editor contract

- Keep stable IDs and ordered raw spans for every structural node and command.
- Evaluate from an explicit map of random/switch node IDs to selected values; seeded rolling populates that map rather than hiding choices inside the evaluator.
- Produce an active projection containing root/common commands and all leaves reached by the selection environment.
- Let the tree UI edit structure while the chart surface edits objects in an explicit selected-leaf or common scope.
- Preserve malformed or unsupported structures as raw nodes with diagnostics whenever safe recovery is possible.
- Bound nesting, node counts, and outcome enumeration without limiting ordinary file size or silently flattening branches.
- Test every supported target profile with positive and negative fixtures and compare both the control tree and each selected projection after export/reload.

## Current Wormedit editing slice

The retained control tree supports exact-provenance replacement of one raw
statement in an inactive branch. A separate command can insert a valid raw
statement immediately after a source-verified statement in an exact inactive
leaf. Replacement requires the statement ID, source order, byte span, retained
text, and command token to match; insertion additionally requires exact leaf
and anchor identities plus their source order, spans, and retained text. Active,
stale, ambiguous, structural, malformed, or over-limit requests reject
atomically. Active branch content must still be edited through materialized
semantic objects. Inactive insertion is
currently limited to a one-chart BMS source document; broader native projects
must wait for an explicit source-identity contract for selecting among charts.
Branch creation/removal/reordering, common-scope or replicated insertion, and
moving content between branches remain unimplemented.
