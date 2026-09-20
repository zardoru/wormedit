# Status: current milestone

[Back to the chart editor status index](../chart-editor-status.md).

Last updated: 2026-09-20

## Active fronts

- Formats: finish externally sourced compatibility evidence. The osu!mania v128 encoder inventory and bounded StepMania 5.1/Etterna 0.75.1 SSC profiles now have pinned fixture coverage, and Setup plus the shared conversion request expose their three independent policy axes. The request now drives an atomic in-memory writer/reimport preparation service shared by future preview, CLI, and publication layers. Runtime BMSE/iBMSC clipboard capture remains. See the [format ledger](implemented/formats-persistence.md) and [resume checklist](resume.md).
- Editing/frontend: continue through the backend-neutral C view/action boundary. Nuklear is the first adapter; a later wxWidgets adapter stays possible without entering current completion criteria. See the [editing/frontend ledger](implemented/editing-frontend.md).
- Preview: retain the modeless DDReam/osu!mania/StepMania-style player surface over the shared transport, without gameplay rules or a workspace-mode change. The selected-area video exporter remains a very-late consumer. See the [visual-preview ledger](implemented/visual-preview.md) and [player-preview plan](../../docs/chart-editor-plan/playback/player-preview.md).

Completed implementation detail belongs only in the [implemented-capability index](implemented.md). Task order belongs only in `resume.md`; this file stays a short routing summary.
