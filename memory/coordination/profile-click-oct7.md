# T-448 profile picker boundary regression

Root approved `tests/sys/compose_action_notification_tests.cpp` and
`tests/ui/bga_lane_event_harness_tests.cpp` as the focused regression scope.
The task card also records this correction. No CMake or dispatcher-harness
extension is needed: the UI harness proves the actual dropdown emits the
profile action on press/release, and the system test sends the matching action type and payload through
`dispatch_compose_workspace_action` into the document history. These are paired
UI and handler checks; they do not share one harness action across binaries.

When `populate_bgm_only_compose_projection` creates a projection without
retained interaction state, the visible profile picker still emits a normal
workspace action. The boundary previously handled only BGM column count when
state was absent, so it consumed profile actions without a document change.
The correction permits the first profile and initial layout insertion through
the production boundary using a temporary interaction state only when the
active Compose chart and document revision match the action payload and the
chart has no lane layouts. Charts with existing layouts remain rejected on the
missing-state path. The BGM column count path remains supported.

Focused verification should cover BMS 10K selection, an initial layout insert,
wrong-chart and stale-revision refusal, refusal when a layout already exists,
unchanged BGM event data, one undo entry, plus the harness's real dropdown
press/release action. The shared main build is read-only for this work; root
will run the integrated build and suite after review.

## Reviewed integration

Sol independently reviewed and reran the focused checks: 55 production-handler
assertions and 11 scrolled dropdown assertions pass. Root integrated the change
as `2749c6d`. Application build and all **2,437/2,437** tests pass, including
the three new workspace-boundary cases and real 10K press/release regression.
Configured complexity checks show no new violations. Root logs:
`/tmp/wormedit-oct7-root/profile-boundary-integrated-build.log` and
`profile-boundary-integrated-tests.log`.

Root's actual CP932 dragback sabun probe reproduced the old failure:
`profile-boundary-baseline.log` reports `layouts=0 undo=0`. After integration,
`profile-boundary-integrated.log` reports **1 layout, 10 bindings, 1 undo**.
All serialized BGM events and source columns are identical; undo restores
the original no-layout state. The source chart was read-only. No user desktop
was opened; the running application must restart to use the rebuilt executable.
