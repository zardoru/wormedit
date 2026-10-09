# October 9 BGM drag and displacement batch

User authorized T-425 followed by T-421 after commit f471ab1. Work stays on main;
other worktrees are preserved.

T-425 replaces the old cancel-and-convert drop with live gesture updates and a
release transaction. Moving into BGM preserves the vertical drag delta for every
selected note and hold tail. Moving back returns to ordinary playable-lane
movement. The preview uses the actual conversion allocator, including new lanes.

T-421 adds Options → Editing → “When a move lands on a note”: Reject (default)
or Move it to a BGM lane. The gesture captures the option at begin. Displaced
occupants keep their rows, sounds, semantic layers, identities and hold tails.
Allocation failure leaves the live document and Undo history unchanged.

## Structures and procedure

- `pending_note_move_gesture_t` retains the conversion target, captured collision
  policy, BGM event/lane previews and rejection diagnostics alongside the existing
  playable-note previews. These fields belong to interaction state, not the song.
- `stage_bgm_note_move` builds a scratch document. A direct BGM drop relocates the
  notes before conversion. Displacement converts occupants first, then relocates
  the selected notes. Both use `move_notes_to_bgm`, including its linked BMSON
  route; final validation precedes the single live `commit_document_command`.
- Linked BMSON movement translates source-event ends along with heads, keeping
  retained holds valid for slice derivation and conversion.
- `editor_preferences_t::move_collision_policy` persists as `reject` or
  `displace_to_bgm`; invalid values retain Reject. Options drafts validate the
  field and save it through the existing atomic preferences path.
- Core scene geometry projects transient BGM heads/tails and newly allocated
  lane headers. Scalar ghost flags fit the existing 96-byte BGM record budget.
  Ghosts bypass retained paint caching and pointer picking; accepted destinations
  are amber, rejected destinations red, with an affected-note status caption.

Exact supporting-file extensions are recorded on the two cards, including the
production dispatcher/projection tests and the legacy drop-routing regression.
Paste/draw collision policy and reverse BGM dragging remain outside this batch.

## Verification

Normal Debug build (Lua OFF, video ON) passes with ccache under `/tmp`.
Focused core BGM movement: 14 cases / 177 assertions. Production input/projection:
3 cases / 37 assertions. The existing conflict-outline and drop-routing
regressions pass independently (8 and 7 assertions respectively).

Final full suite: **2,939/2,939**, 56.13 seconds, including **265 event-harness cases**.
Dedicated `ctest -L event-harness -j4`: **265/265**, 1.87 seconds.
Final `documentation.chart_editor_state` and `git diff --check` pass.
Configured affected-source/test complexity checks introduce no new/increased
findings. The full suite first caught an unnecessary summary-count gate on
occupant outlines and the legacy expectation of cancel-and-convert routing;
the gate is removed and the regression now verifies live update/release input.

Three reviewed ordinary-main-object SDL dummy/software captures show shifted
conversion, displacement into a newly allocated lane, and rejected displacement.
Their probe checks preview non-mutation, atomic release, hold tails and Undo.
No ordinary application object files were replaced. Isolated Xvfb cannot create
listening sockets in this sandbox; native display/cadence remains unmeasured.

Logs: `/tmp/wormedit-move-build-final-pass.log`,
`/tmp/wormedit-move-ctest-pass.log`, `/tmp/wormedit-move-tidy-final-sources.log`,
`/tmp/wormedit-move-tidy-final-tests.log`, `/tmp/wormedit-move-tidy-regression.log`,
`/tmp/wormedit-move-{core,sys}-final.log`, `/tmp/wormedit-move-xvfb.log`,
`/tmp/wormedit-move-event-label.log`, `/tmp/wormedit-move-docs-final.log`.
Captures and reproducible probe:
`/home/zardoru/devel/wormedit-scratch-builds/oct9-move/captures/{convert,displace,reject}.png`.
