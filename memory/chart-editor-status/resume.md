# Status: resume checklist

[Back to the chart editor status index](../chart-editor-status.md).

## Immediate work

1. Establish the planned SDL3 GPU path without moving decode, layout, schedule, or document policy into the frontend; preserve the typed per-resource composition diagnostics when replacing the temporary OpenGL uploader.
2. Continue the behavior-preserving frontend split by extracting Compose composition behind the backend-neutral view/action contract; Metadata, Timing, and Audio/Slices composition plus retained pane state are already separated, while the current Nuklear backend must not acquire document, transport, or scene policy and a later wxWidgets adapter should not require moving those responsibilities again.
3. Add richer profile-specific discontinuity ordering plus video/storyboard evaluation to the preview. Much later, add the area-selection menu's deterministic player-preview video exporter over the exact selected interval.
4. Capture BMSE/iBMSC clipboard samples with tool/version provenance, then broaden BMS dialect/profile and exact branch-editing fixtures.
5. Expand SM/SSC and osu!mania implementation-profile/version fixtures while retaining explicit format limits and loss diagnostics.

## Handoff rule

Load this file plus the one relevant [implemented-capability ledger](implemented.md), [constraint slice](code-constraints.md), and plan domain. Historical implementation detail does not belong in this checklist.
