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
