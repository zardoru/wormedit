# Priority user feedback worktrees — 2026-10-11

Every open Brain scratchpad feedback item is user feedback and high priority. User authorized parallel worktrees and agents. Existing Claude-owned worktrees are separate and must not be repurposed.

| Track | Cards | Current worktree | Future stable Codex path |
| --- | --- | --- | --- |
| CJK text display | T-515 | `wormedit-feedback-cjk` | `wormedit-codex-ruth` |
| BMSE scratch, milder default colors, osu!mania 10K | T-516–T-518 | `wormedit-feedback-skins` | `wormedit-codex-smaug` |
| Scroll direction, wheel inversion, waveform opacity | T-519–T-521 | `wormedit-feedback-navigation` | `wormedit-codex-saphira` |
| Bookmark search, song listing, BMS song-bar defaults | T-522–T-524 | `wormedit-feedback-library` | `wormedit-codex-glaurung` |

**Naming convention:** specific dragon names above identify **Codex**, with explicit `codex-` prefixes to avoid confusing Claude collaborators. Claude retains dragon classes such as `wyrm`, `hydra`, `wyvern`, and `drake`. Reuse stable absolute Codex worktree/build paths across future task branches to improve ccache reuse. Keep current batch paths stable until active verification finishes; transition the slots afterward.

Coordinator owns shared task index, status, integration, conflict resolution and final build/full-suite verification. Workers own their dedicated card files and approved implementation files. Shared frontend/theme/toolbar edits are limited to agreed separate regions and integrated by the coordinator. The user authorized committing the completed, verified batch. The manual link remains dependent on finishing the manual.

Batch verification is complete: normal build, 3,162/3,162 CTest checks and 375/375 frontend event cases pass. All ten cards are done and committed together. The muted default palette also incorporates the user’s follow-up that saturated backgrounds were too harsh. Stable Codex slot names above remain reserved for subsequent batches.

Follow-up user feedback: stable slot `wormedit-codex-glaurung` is now active on branch `fix/menu-input-capture` for T-525. T-516 stage-local lane-number correction is integrated by the coordinator in main. The earlier completion paragraph records the initial batch checkpoint; these newly reported regressions are being repaired separately.

T-526 uses stable `wormedit-codex-ruth` on `fix/skin-scratch-geometry` and `wormedit-codex-smaug` on `fix/skin-scratch-preferences`. Only presentation changes: authored layouts and channels stay intact. Workers submit frozen patches; coordinator performs the combined build and verification in the main checkout.

Follow-up T-516/T-525/T-526 is complete and uncommitted. Combined normal/release builds, all 3,175 checks (one SDL timeout passed on isolated retry), 379 frontend event cases and native scratch placement/Options checks are verified. Workers submitted frozen patches; no worker commits were made.

Verified uncommitted follow-up T-528–T-532: Codex Glaurung handled recents/startup, Smaug handled dropdown surfaces and bundled skin lane mappings, Ruth handled chart grips/reorder and opt-in Options header dragging. All patches integrated into main; these Codex names remain distinct from Claude dragon-class slots. Full CTest 3,195/3,195 and 391 production event cases pass.

Verified uncommitted semantic-skin follow-up T-533–T-535: Codex Smaug owned semantic palettes and imported schemes; Glaurung owned bounded per-layout preferences and Options runtime; Ruth owned canonical layout identity/classes, saved generic-class migration and BMSON stage geometry. Coordinator integrated current-layout UI/runtime, blank imported chart-name safety, cards, Brain and final verification. Stable Codex slots remain distinct from Claude dragon classes. Full CTest 3,219/3,219 and 394 production event cases pass.

T-536 measure-local grid/navigation feedback is verified and uncommitted. Codex Ruth handled shared core snapping/cursor and BMSON razor paths; Smaug handled grid/projection phase; Glaurung handled playback/follow/bookmark/minimap; coordinator handled edit/resource/paint/SV/timing integration and real frontend/native checks. Full CTest 3,240/3,240 and 395 event cases pass. Stable Codex slots remain distinct from Claude worktrees.

Commit checkpoint (2026-10-11): the verified T-516/T-525–T-536 follow-ups recorded above are committed together in this revision. Prior uncommitted labels describe their earlier verification checkpoints.
