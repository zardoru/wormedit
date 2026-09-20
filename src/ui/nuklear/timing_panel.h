#pragma once

#include "ui/frontend.h"
#include "ui/nuklear/panel_input.h"

#include <cstddef>

struct nk_context;

[[nodiscard]] int worm_ui_nuklear_timing_view_valid(const WormUiTimingView* view);

void worm_ui_nuklear_timing_handle_focus_lost();

void worm_ui_nuklear_reconcile_timing_gestures(const WormUiTimingView* view, bool timing_tab_active,
                                               bool window_focused);

void worm_ui_nuklear_emit_timing_cancellations(const WormUiTimingView* view, bool timing_tab_active,
                                               WormUiWorkspaceAction* action);

void worm_ui_nuklear_render_timing_panel(struct nk_context* context, const WormUiTimingView* view,
                                         WormUiWorkspaceAction* action, bool window_focused,
                                         const worm_ui_nuklear_pointer_click_t* double_clicks,
                                         std::size_t double_click_count);

void worm_ui_nuklear_render_timing_stage_editor(struct nk_context* context,
                                                const WormUiTimingView* view,
                                                WormUiWorkspaceAction* action);
