# October 7 editing batch review

Coordinator: Sol6.1 medium. Executor: Luna medium. Base: `14f8c25`.

## Scope approvals

T-454 owns the targeted mixed-selection command in `include/wormedit/core/note_to_bgm.h` and `src/core/note_to_bgm.cpp`, plus `tests/core/note_to_bgm_tests.cpp`, in addition to its original files. These support atomic plain mixed assignment and successive selection behavior; the general verb refactor remains out of scope.

T-448 additionally owns `src/ui/frontend.h` (profile capacity), `src/ui/nuklear/compose_inspector.cpp` (initial unselected choice), and `tests/sys/compose_action_notification_tests.cpp` (production dispatcher tests). Root approved these exact extensions before edits. Root additionally approved `tests/sys/frame_timing_tests.cpp` for production BGM-only projection evidence before edits. Root also approved final review fixes to initial SpecialStyle choices and their distinct production labels. No existing same-count switching or import detection rule changed.

## Review and evidence

Source commits: `2d3a2ae` (T-454) and `d6cde6b` (T-448), followed by the coordinator review correction commit containing this note.

T-454: note-only assignment retains its previous dispatcher path. Mixed plain assignment proposes both conversions on scratch history and commits once. Successive assignment chooses the first selected object by measure/row with deterministic ties, then removes only that successfully moved object from selection. Rejection leaves history and selection unchanged.

T-448: initial layouts start at the first materialized measure, row zero; empty BMS charts materialize one measure in the same undo command. Stale revisions reject. Binding remapping uses an immutable mapping, preserving scratch/key identity. Initial choices include all 41 built-ins, including distinct 12K/16K SpecialStyle labels; the 64-entry frontend capacity also holds the placeholder.

Independent final focused runs after the review fixes: core `[note-to-bgm],[lane-profile-switch]` passed 182 assertions in 24 cases; frontend `[bgm-to-lane],[lane-profile]` passed 19 assertions in 3 cases; production dispatcher `[compose-action-notifications][lane-profile]` passed 20 assertions in 4 cases. The production BGM-only projection regression passed 10 assertions in 1 case, proving all 41 choices, current=-1, available placeholder capacity and distinct ordinary/SpecialStyle labels. The documentation test and `git diff --check` passed.

Configured clang-tidy threshold is 25 in `.clang-tidy`. Full affected-file review log: `/tmp/editing-oct7-final-review-tidy.log`; final corrected-file log: `/tmp/editing-oct7-final-review-tidy-fix.log`. Additional production projection-test tidy log: `/tmp/editing-oct7-final-review-projection-tidy.log`. New implementation and test functions have no complexity violations; remaining reported violations belong to unchanged pre-existing functions/tests. The review extracted assertion helpers with caller locations to remove Catch macro inflation in new tests.

Full isolated event harness remains 187/192: only the five known stale bookmark rename cases fail; root separately corrected those on main. Log: `/tmp/editing-oct7-final-review-harness.log`. Root must rerun the complete integrated build/tests and advance card statuses after integration. Real-window visual verification remains pending; no desktop was opened.

Root scratch probe `/tmp/wormedit-oct7-root/sabun-profile-probe.log` passed: CP932 scratch import, initial 7K+scratch layout/bindings, native save/reopen exact preservation of 1,146 BGM events and 16 source columns, note placement/undo and profile undo. The original chart was read-only and its SHA-256 stayed unchanged. The original card's 559 count describes raw `#mmm01` lines, not parsed events. That probe used the pre-review 39-profile list; added SpecialStyle creation/binding/undo is covered by the final core tests.

Final source review approves this batch for root serial integration, subject to the integrated checks above.
