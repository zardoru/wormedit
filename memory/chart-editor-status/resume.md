# Status: resume checklist

[Back to the chart editor status index](../chart-editor-status.md).

## Immediate work

1. Add concrete visual-asset resolution, decoding, and renderer-owned composition over the player preview's deterministic background/poor/layer/video/storyboard snapshot. Preserve the scalar scene boundary and keep gameplay rules out of this surface.
2. Continue the behavior-preserving frontend split around backend-neutral view/action contracts; the current Nuklear backend must not own document, transport, or scene policy, and a later wxWidgets adapter should not require moving those responsibilities again.
3. Add richer profile-specific discontinuity ordering and later GPU composition to the preview. Much later, add the area-selection menu's deterministic player-preview video exporter over the exact selected interval.
4. Capture BMSE/iBMSC clipboard samples with tool/version provenance, then broaden BMS dialect/profile and exact branch-editing fixtures.
5. Expand SM/SSC and osu!mania implementation-profile/version fixtures while retaining explicit format limits and loss diagnostics.

## Handoff rule

Load this file plus the one relevant [implemented-capability ledger](implemented.md), [constraint slice](code-constraints.md), and plan domain. Historical implementation detail does not belong in this checklist.
