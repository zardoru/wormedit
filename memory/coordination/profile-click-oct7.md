# T-448 profile picker boundary regression

Root approved `tests/sys/compose_action_notification_tests.cpp` and
`tests/ui/bga_lane_event_harness_tests.cpp` as the focused regression scope.
The task card also records this correction. No CMake or dispatcher-harness
extension is needed: the UI harness proves the actual dropdown emits the
profile action on press/release, and the system test feeds that action through
`dispatch_compose_workspace_action` into the document history.

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
