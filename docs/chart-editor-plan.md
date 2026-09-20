# Chart editor plan

Status: reviewed implementation plan. This file is the stable entry point; each requirement has one canonical owner in the domain files below. Ongoing implementation state and resumption notes live in the [chart editor status index](../memory/chart-editor-status.md).

## Plan domains

- [Product goals, architecture, dependencies, and rendering direction](chart-editor-plan/overview.md)
- [Editor-owned document, timing, resource, label, and semantic-layer model](chart-editor-plan/document-model.md)
- [Editor layout, interaction, commands, selection, clipboard, and comparison](chart-editor-plan/editing.md)
- [Playback preview, deterministic player-like presentation, waveform, and streaming audio](chart-editor-plan/playback-audio.md)
- [Format adapters, conversion, native persistence, undo, validation, and diagnostics](chart-editor-plan/formats-persistence.md)
- [Test strategy and regression gates](chart-editor-plan/testing.md)
- [Implementation stages and stage exit conditions](chart-editor-plan/stages.md)
- [Technical decisions, open questions, and the current-work handoff](chart-editor-plan/decisions.md)

Load this index plus only the domain files relevant to the current task. Cross-domain requirements should link to their canonical owner rather than repeat normative text.

Keep this entry point and each domain index short. When unrelated work would
have to load the same large domain file, split it into named subdomains and
extend the documentation inventory test instead of duplicating requirements.
