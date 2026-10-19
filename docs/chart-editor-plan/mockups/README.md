# UI reference mockups

[Back to the task card index](../tasks.md).

Reference mockups for the 2026-10-05 feedback cards. They show the intended layout,
hierarchy and states. Implementers match their structure and spacing rules. They are
not pixel specifications, and the real theme roles take precedence. The colors come
from `assets/themes/ui-dark.json` roles.

To regenerate them, run `python3 docs/chart-editor-plan/mockups/generate.py`. Edit the
generator, not the SVG files.

| Mockup | Cards |
| --- | --- |
| [Component gallery](components.svg) | [T-436](../tasks/dialogs.md#t-436-regularize-the-ui-components-one-set-of-sizes-radii-gaps-and-state-colors) (tokens and states used by every card below) |
| [Unsaved changes](unsaved-changes.svg) | [T-407, T-408](../tasks/dialogs.md) (shared dialog rules) |
| [Tap tempo](tap-timing.svg) | [T-409](../tasks/dialogs.md#t-409-redesign-the-tap-timing-tool) |
| [Chart table](chart-table.svg) | [T-415–T-418](../tasks/project-charts.md) |
| [Quantize controls](quantize.svg) | [T-419](../tasks/editing-feedback.md#t-419-replace-the-quantize-settings-form-with-one-click-controls), [T-437](../tasks/editing-feedback.md#t-437-preview-quantization-as-before-and-after-ghosts-on-the-field) |
| [Move collisions, Draw tool and lane numbers](field-move-collision.svg) | [T-420, T-421](../tasks/editing-feedback.md), [T-412, T-414](../tasks/draw-tool.md), [T-387](../tasks/compose-cleanup.md#t-387-outline-the-lane-header-numbers-in-black) |
| [Bookmarks and sections](bookmarks-sections.svg) | [T-394](../tasks/compose-cleanup.md#t-394-split-the-markers-controls-into-bookmarks-and-sections-with-icon-buttons) (approved by the user on 2026-10-05, with the title aligned to the highlight edge) |
| [Hitsound palette](hitsound-palette.svg) | [T-216](../tasks/compose-ux.md#t-216-add-osu-style-hitsound-controls-to-the-left-note-palette) |
| [New project layout picker](new-project-layout.svg) | Proposal, no card yet: game list, layout cards grouped by lane shape, variant toggles and a lane preview instead of the 60-entry combo |
| [Timing links](timing-links.svg) | [T-720](../tasks/editing-depth.md#t-720-plan-granular-timing-links-with-copy-on-write-overrides) (proposal for its design pass; assumes named shared timing sets) |
| [Exports (output groups)](output-groups.svg) | Proposal, no card yet; see [output-groups-ux-2026-10-19.md](../../audits/output-groups-ux-2026-10-19.md) |
