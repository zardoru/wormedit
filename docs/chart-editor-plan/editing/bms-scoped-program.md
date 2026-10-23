# BMS scoped program authoring

[Back to editing](../editing.md). Implementation cards:
[model and migration](../tasks/bms-scoped-model.md),
[node authoring and editor integration](../tasks/bms-scoped-authoring.md).

## Agreed product contract

Owner-approved 2026-10-20. The BMS branch panel selects **where an edit belongs**.
Its primary objects are control-flow scopes and their editable Chart content, not
retained source statements. This contract supersedes the source-browser direction
of the older [branch cards](../tasks/bms-branches.md) and
[T-399](../tasks/compose-docks.md#t-399-show-bms-branch-nodes-as-a-tree-with-node-text-limited-to-unparsed-statements).
Their completed implementation records remain historical evidence, not completion
of this feature.

```text
Root
├─ Chart A                         ← default edit target for Root
├─ Text                            ← comments or unknown commands
└─ RANDOM 4
   ├─ Chart B                      ← default edit target for RANDOM
   └─ IF 1
      ├─ Chart C                   ← default edit target for IF
      ├─ Text
      └─ Chart D                   ← explicit alternative edit target
```

- Root is the outermost scope. Every control node in the scoped model, including
  RANDOM, owns a scope with at least one Chart node and may contain several.
- A Chart node is an editable content container within one project difficulty,
  **not another difficulty**. Nodes use the difficulty's musical coordinates;
  sibling order is BMS program order, not concatenated playback time.
- Selecting a control scope targets its **first Chart child in program order**.
  That child need not be the first child of any kind. Selecting another Chart
  node explicitly targets that node. Returning to the containing scope targets
  that scope's first Chart node; it does not keep writing into the previous leaf.
- Chart nodes own modeled chart content. Recognized definitions such as `#WAV`
  need semantic representation in their owning scope, including inactive scopes;
  they are not arbitrary retained text. T-731 specifies the exact typed owners.
- Text nodes retain comments and commands for which the editor has no semantic
  meaning. Text remains in its ordered containing block and travels with that
  block. Meaningful commands must not remain visible as raw-statement scope rows.
- Users can add control nodes and edit the count on a RANDOM node directly.
  RANDOM's `n` is its outcome range, not its number of children or IF arms.
  Changing the range and choosing a preview outcome are different operations.
- Removing a control node deletes its entire subtree, including Chart and Text
  contents, **after confirmation**. It never silently promotes the contents.
- **Unwrap** is a separate explicit operation: remove the wrapper and insert its
  ordered contents into the parent at the wrapper's former position. It must not
  silently flatten conditions or repair/rebind selector references.

Scope creation creates its first empty Chart node automatically. Root cannot be
removed as a subtree; deleting a project difficulty remains the existing chart
management operation. An operation must not leave a surviving scope with no Chart
node. The last Chart node can have its content cleared, but cannot simply disappear.

## Semantic ownership and source retention

The authored program must retain **all scopes**, not only the currently evaluated
projection. A node's identity, parent and order are stable document data. Source
line numbers and byte offsets are provenance, not node identities or ownership.

| Kind | Authoritative meaning | Export responsibility |
|---|---|---|
| Control scope | Control kind, parameters, ordered children | Emit the authored control structure, including inactive arms |
| Chart content | Typed events and definitions with one scoped owner | Serialize current semantic values in that scope |
| Text | Ordered opaque text and encoding provenance | Preserve comments and unknown commands without interpreting them |

Adjacent opaque lines may form one Text block without reordering them across
semantic or control boundaries. Unknown commands remain Text until their meaning
is implemented. Original bytes may remain as provenance or an unchanged-file
shortcut; they must not become a second writable copy of modeled data.

**Do not discard recognized source merely because the active projection models
it.** Every inactive definition, event and hold endpoint must first have an
equivalent scoped semantic owner. Migration may keep a read-only legacy backing
until that equivalence is proven. It must never silently omit inactive content,
duplicate it into both owners, or relabel it as unknown Text to evade migration.

[T-731’s ownership audit](../audits/bms-scoped-ownership.md) records the command-family
owners, exact editor entries, identity/geometry rules and one-way cutover for notes/holds, BGM/BGA, timing,
measure geometry, metadata and resource definitions. Asset identity and storage
can remain shared, but a scoped WAV/BMP declaration and the asset it references
are different facts. Resource lookup must preserve the selected BMS profile's
ordered definition/override semantics, not invent a new lexical language.

## Projection and edit routing

The existing Compose/playback chart is a derived projection of the authored
program under an evaluation environment. It is not another independently editable
source of truth. Scope/Chart selection and random/switch evaluation choices need
distinct state and identities; their display interaction is [question 24](../decisions/open-questions.md).

An edit resolves a stable difficulty ID, scope ID and Chart-node ID before the
existing command transaction changes data. New authored objects go to that Chart
node. Existing objects retain their semantic owner; scope selection alone is not
a move-to-another-scope command and must not permit implicit writes into an ancestor,
sibling or another Chart node. How inherited context is displayed or explicitly
overridden is part of question 24. Selection, paste, timing, metadata and resource
operations must all use the same ownership checks.
T-731 inventories these paths; T-736 supplies the shared routing mechanism.

Changes to a scope may legitimately affect several outcomes. Evaluate affected
outcomes within existing caps, diagnose collisions, unresolved references and
unsupported syntax, and reject atomically when correctness cannot be established.
Do not validate only the visible projection or silently fall back to root.

All data edits use the existing command transaction and one Undo step. Stale
targets, invalid values, missing parents and failed serialization publish nothing.
Canceling a deletion or a destructive preview changes nothing. Undo/Redo restores
node identities, content, order, target selection validity and projection behavior.

## Structural operations

**Add:** insert a node at an explicit position in a selected scope. A control scope
starts with an empty Chart child; a new Chart or Text node is a sibling content
node. Empty scopes and newly authored projects must work without a selected raw
source line. Grammar-aware factories expose only valid placements and required
parameters. Non-body directive presentation is [question 22](../decisions/open-questions.md);
that mapping must not create an unapproved exception to the Chart-child invariant.

**Change RANDOM range:** validate a positive supported integer and preserve node
identity and child order. Do not create/delete arms as a side effect. The behavior
when existing arms or the preview value exceed the new range remains
[question 21](../decisions/open-questions.md); the UI must not choose it implicitly.

**Move:** move a whole ordered subtree, retaining its Chart and Text children.
Reject cycles, invalid parent/child grammar, unresolved definitions and invalid
affected projections. Do not silently rebase musical rows. Moving/copying individual
notes between scopes remains [T-93](../tasks/bms-branches.md#t-93-move-or-copy-selected-notes-into-another-branch-leaf).

**Delete:** preview the affected scopes and content counts, then require explicit
confirmation against the same revision and node identity. Delete only that subtree;
preserve siblings and shared assets still used elsewhere. Do not add implicit asset
garbage collection. If the current target is deleted, resolve the surviving parent's
first Chart node; Undo restores a valid target without retaining dangling references.

**Unwrap:** preserve child order and identities while moving them into the parent.
Preview the resulting scope/definition effects before confirmation. Reject an
invalid result rather than dropping colliding objects or binding IF/CASE nodes to
a different selector. Selector unwrapping needs [question 23](../decisions/open-questions.md).

## Import persistence and export

Import builds the full program from the existing BMS parser. It preserves control
structure, typed content in every scope, opaque text, order and exact musical
positions. Identical modeled source lines must not create duplicate semantic owners.
Malformed constructs remain losslessly recoverable with diagnostics; this feature
does not silently repair them or replace the opt-out repair work in T-287.

Native persistence versions the scoped program and migrates existing projects,
including their inactive branches. Do not hardcode the next schema version in this
plan: inspect the migration table when T-737 executes. Fresh node IDs must avoid
document/history collisions and survive save/reload and Undo/Redo.

Default BMS export means **do not discard control flow**. With no control flow,
edited timing exports normally. With control flow, emit the complete authored
program, not merely the active projection. Opaque text travels with its containing
block. Only explicit active-projection export may discard control flow, with the
existing loss warning and selection requirements. Unsupported or unrepresentable
semantics must fail without partial output, never be approximated silently.

## Implementation sequence and acceptance

T-731–T-737 establish the ownership contract, value model, complete import,
projection, writer, shared edit routing and native migration. T-738–T-744 add node
authoring, RANDOM editing, movement, deletion, unwrap, the scope panel and the final
cutover. Temporary legacy adapters name T-744 as their removal card. During staged
development, the old owner remains authoritative until cutover; new values are
prepared through one-way adapters, not independently published writable mirrors.
Do not enable production scoped edits against an incomplete persistence/export path.

Acceptance includes a nested RANDOM/IF fixture with root and branch notes, holds,
timing, scoped WAV definitions and comments; multiple Chart children; an empty
scope; and a malformed legacy fixture. Verify root/first-Chart targeting, explicit
alternate Chart targeting, inactive-content preservation, range edits, subtree
movement, canceled/confirmed deletion, unwrap, Undo/Redo, native reload and all
affected BMS outcomes. Verify fixed selectors and switch fallthrough under the
selected existing profile. Use the frontend event harness and isolated Xvfb for
the panel, destructive dialogs and target indication; run GATE at every implementation
card and documentation checks for the initial audit.

This plan is authorization to prepare implementation tasks, not a claim that the
model or UI has landed. T-731 is the first executable audit. Questions 21–24 block
only the cards that depend on those policies; they do not reopen the agreed contract.
