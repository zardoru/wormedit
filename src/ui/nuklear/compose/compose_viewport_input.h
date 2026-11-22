#pragma once

#include "ui/nuklear/compose/compose_panel_internal.h"

ComposePointerFocusState update_compose_pointer_focus(
    const ComposePointerFocusContext& focus);
ComposeViewportCursor collect_compose_viewport_inputs(
    const ComposeViewportInputContext& input);
bool process_compose_beat_fit_pointer(const ComposeBeatFitInput& input);
void process_compose_overview_input(const ComposeOverviewInput& input);
void append_compose_preview_wheel_inputs(const wormedit::ui::compose_view_t& view,
                                         wormedit::ui::compose_ui_window_t* action, bool window_focused);
