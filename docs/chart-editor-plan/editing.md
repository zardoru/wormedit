# Plan domain: editing and interaction

[Back to the chart editor plan index](../chart-editor-plan.md).

This domain is split by the context needed for implementation. Load this index and only the relevant slice:

- [Workspace layout, panes, frontend boundary, and player-preview placement](editing/layout.md)
- [Selection, transforms, quantization, lane input, and command routing](editing/interactions.md)
- [Tags, markers, channel maps, branch projection, clipboard, and comparison](editing/metadata-interop.md)
- [BMS scoped program authoring and Chart Text control ownership](editing/bms-scoped-program.md)

The frontend contract stays scalar and backend-neutral across all three slices so the current Nuklear implementation does not become the application architecture. A possible wxWidgets frontend is an anticipated adapter, not a completion criterion.
