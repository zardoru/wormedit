#pragma once

struct ComposeToolbarContext;
namespace wormedit::ui { struct compose_view_t; }

void render_compose_toolbar(const ComposeToolbarContext& toolbar);
// True when the palette is enabled and at least one note kind is allowed for the chart.
bool compose_note_palette_has_buttons(const wormedit::ui::compose_view_t& view);
// One vertical column of note-kind buttons; call inside the palette strip group.
void render_compose_note_palette_column(const ComposeToolbarContext& toolbar);
void render_compose_hitsound_menu(const ComposeToolbarContext& toolbar);

// Shared offset and beat-fit snap controls for the inspector Timing section.
void render_compose_inspector_offset_controls(const ComposeToolbarContext& toolbar);
void render_compose_inspector_snap_control(const ComposeToolbarContext& toolbar);
