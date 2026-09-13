# Replaceable UI frontend boundary

Status: reviewed planning contract; the checked CMake cache enum and private
`wormedit::ui_frontend` target are implemented for the initial Nuklear adapter.
Its lifecycle, event translation, responsive workspace-shell composition, and
widget types now live under `src/ui/nuklear`; the application loop translates
the real workspace controller view through a private scalar view/action
contract and dispatches revision-checked tab changes. The headless seven-tab
workspace and recovery-banner controllers provide bounded backend-neutral
views/actions; substantive pane composition, the recovery banner, and normalized
platform input remain.

Nuklear is the initial editor frontend, not a permanent dependency of the application model. A future ImGui frontend or a Clay-based layout/widget frontend should be replaceable without changing chart storage, timing, audio, commands, selection, transport, format codecs, or renderer-neutral chart drawing.

## Boundary

Use four layers:

```text
document + services
        |
application/tab controllers
        |  immutable *_view_model_t values
        |  typed ui_action_t requests
        v
replaceable UI frontend adapter
        |
render_backend_t + platform input/window services
```

Controllers own workflow state and decisions. They return plain project-defined view models containing labels, enabled/visible state, field values, validation messages, list/table rows, selection, focus requests, and stable widget/object IDs. Frontends render those values and return typed actions such as `set_bpm_t`, `select_difficulty_t`, `place_note_t`, or `open_metadata_editor_t`. Actions enter the same command/validation path used by keyboard shortcuts and automation.

No public controller, document, command, timing, or audio header may include Nuklear, ImGui, or Clay types. Do not store `nk_context*`, ImGui IDs, Clay element handles, backend colors/vectors, or backend callback objects in project state. Backend-specific state stays under its adapter directory and is destroyed when that frontend shuts down.

## Widget composition

Do not build a universal wrapper for every checkbox, table, popup, and layout primitive. Immediate-mode libraries and retained/layout libraries have different strengths, and a lowest-common-denominator widget API would become another UI framework.

Instead, stabilize the semantic boundary:

- one backend-neutral view model and action vocabulary per workspace feature;
- frontend-specific composition code for Setup, Metadata, Timing, Compose, Storyboard, Audio/Slices, and Compare/Validate;
- shared formatting, validation, filtering, sorting, and command-building functions below the frontend;
- backend-owned styling, docking, popup mechanics, text-edit buffers, and accessibility integration;
- project-owned stable IDs so a frontend does not invent document identity from pointer addresses or display strings.

Essential workspace state such as selected tab/difficulty, pane visibility, timing-lane visibility, BGM horizontal position, zoom, and active tool is backend-neutral. Optional library-specific docking/window geometry may be stored in a namespaced disposable settings block.

## Chart and waveform surfaces

The performance-critical note field, BGM lanes, timing rail, waveform, comparison overlay, and storyboard canvas do not emit thousands of ordinary UI widgets. Their controllers create compact visible-item/hit-test lists and backend-neutral draw primitives or renderer commands. A frontend supplies the containing rectangle, clipping/focus state, and normalized pointer/keyboard events; `render_backend_t` draws through SDL3 GPU.

This keeps field performance independent of the chosen panel toolkit and allows spatial indexing, batching, culling, waveform meshes, and high-lane-count horizontal scrolling to be optimized once. Hover, double click, area selection, middle-button scroll latch, trackpad scrolling, and note placement remain controller behavior rather than Nuklear behavior.

## CMake and source layout

The intended source layout is:

```text
include/wormedit/ui/       shared view models, actions, frontend contract
src/application/           workspace and tab controllers
src/ui/nuklear/            initial frontend
src/ui/imgui/              optional replacement/spike
src/ui/clay/               optional replacement/spike
src/render/                SDL3 GPU backend and draw-list implementation
```

CMake selects exactly one frontend through a cache enum such as `WORMEDIT_UI_BACKEND=nuklear|imgui|clay`. Each implementation exports the same private alias target, for example `wormedit::ui_frontend`, and only that target links the third-party UI library. The executable links the application controllers, selected frontend, renderer, and platform shell. Core/headless targets never resolve or link a UI dependency.

Keep dependency pins and licenses per frontend. Removing one frontend directory and selecting another must remove its third-party dependency from the configure graph. Unsupported values fail configuration with a useful list.

## Replacement and performance gates

Before substantial editor views are built, move the scaffold's direct `nk_*` calls and `nk_context*` ownership into `src/ui/nuklear`. The application loop should see only frontend lifecycle, normalized input dispatch, action collection, and frame rendering.

A frontend is viable when it:

- implements every required view-model/action contract or explicitly disables unfinished panels;
- passes shared scripted interaction scenarios and produces the same commands/diagnostics;
- supports text/IME input, keyboard focus, mouse/trackpad input, double click, clipboard, high DPI, multiple windows if enabled, and screen-reader/accessibility goals chosen for the release;
- renders large BGM-lane banks, dense timing lanes, waveform views, 48K fields, metadata tables, and comparison panes within measured frame-time/allocation budgets;
- survives device/window recreation without changing the document;
- has no third-party UI types in public non-frontend headers.

Performance measurements use reproducible synthetic documents and report controller preparation, draw-list generation, UI composition, GPU submission, allocations, and worst-frame latency separately. A slow frontend can be replaced without rewriting the core; a slow chart draw/controller path remains visible as a shared problem.

## Tests

- compile/link tests proving headless core and controllers have no UI dependency;
- include-boundary checks rejecting Nuklear/ImGui/Clay headers outside their adapter directories;
- view-model unit tests for formatting, enabled states, validation, focus requests, and stable IDs;
- action tests proving different input paths create identical validated commands;
- scripted frontend conformance scenarios for tabs, metadata, timing edits, note placement, hover/double click, selection, scrolling, dialogs, and recovery banners;
- lifecycle tests for init failure, shutdown, renderer/device loss, window recreation, and frontend restart;
- performance fixtures for dense notes/timing, hundreds of BGM lanes, long waveform caches, 24K/48K fields, and side-by-side comparison;
- per-frontend screenshot tests only for presentation; semantic behavior remains in shared controller tests.
