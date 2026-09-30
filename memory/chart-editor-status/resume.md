# Status: resume checklist

[Back to the chart editor status index](../chart-editor-status.md).

## Immediate work

Work is executed as [task cards](../../docs/chart-editor-plan/tasks.md). Take the first card whose status is `ready`; do not start a blocked card.

1. [Compose panel split](../../docs/chart-editor-plan/tasks/compose-split.md): T-01 canvas, then T-02 toolbar, T-03 inspector, T-04 input. Behavior-neutral moves out of `src/ui/nuklear/compose_panel.cpp`.
2. [Compose features](../../docs/chart-editor-plan/tasks/compose-features.md): T-10 narrow inspector widgets, T-11 independent BGM scroll, T-12 BMSON slice reset, T-13 virtual `#000`–`#999` canvas, T-14 waveform overview (needs a user decision first). Preserve profile-specific BMS-tile/dance-arrow glyphs, the shared mobile-head viewport contract, existing controller actions, and resize/high-DPI behavior.
3. [Interop evidence](../../docs/chart-editor-plan/tasks/interop-evidence.md): T-30 BMSE/iBMSC clipboard samples (needs the user to supply captures; do not infer from the source audit).
4. Much later: the area-selection menu's deterministic player-preview video exporter over the exact selected interval. No card yet.

## Handoff rule

Load this file plus the one relevant [implemented-capability ledger](implemented.md), [constraint slice](code-constraints.md), and plan domain. Historical implementation detail does not belong in this checklist.
