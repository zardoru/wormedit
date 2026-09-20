# Status: resume checklist

[Back to the chart editor status index](../chart-editor-status.md).

## Immediate work

1. Feed the bounded static-image decoder through a generation-checked asynchronous cache into renderer-owned texture composition for the player preview's deterministic visual snapshot. Preserve the scalar scene boundary; keep video/storyboard evaluation and gameplay rules separate.
2. Continue the behavior-preserving frontend split around backend-neutral view/action contracts; the current Nuklear backend must not own document, transport, or scene policy, and a later wxWidgets adapter should not require moving those responsibilities again.
3. Add richer profile-specific discontinuity ordering and later GPU composition to the preview. Much later, add the area-selection menu's deterministic player-preview video exporter over the exact selected interval.
4. Capture BMSE/iBMSC clipboard samples with tool/version provenance, then broaden BMS dialect/profile and exact branch-editing fixtures.
5. Expand SM/SSC and osu!mania implementation-profile/version fixtures while retaining explicit format limits and loss diagnostics.

## Handoff rule

Load this file plus the one relevant [implemented-capability ledger](implemented.md), [constraint slice](code-constraints.md), and plan domain. Historical implementation detail does not belong in this checklist.
