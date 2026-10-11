#pragma once

struct ComposeToolbarContext;
struct WormUiComposeView;

void render_compose_toolbar(const ComposeToolbarContext& toolbar);
// True when the palette is enabled and at least one note kind is allowed for the chart.
bool compose_note_palette_has_buttons(const WormUiComposeView& view);
// One vertical column of note-kind buttons; call inside the palette strip group.
void render_compose_note_palette_column(const ComposeToolbarContext& toolbar);

// Shared offset and beat-fit snap controls for the inspector Timing section.
void render_compose_inspector_timing_controls(const ComposeToolbarContext& toolbar);
