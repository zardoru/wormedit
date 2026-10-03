#pragma once

#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/compose_keyboard_input.h"
#include "ui/nuklear/compose_viewport_input.h"
#include "ui/nuklear/compose_timing_input.h"
#include "ui/nuklear/compose_object_input.h"

bool compose_action_has_cancel(const WormUiWorkspaceAction* action);

void queue_compose_wheel_input(double delta_x, double delta_y, double mouse_x,
                                         double mouse_y, std::uint16_t modifiers,
                                         bool coarse_steps);

void queue_compose_pinch_input(worm_ui_nuklear_pinch_phase_t phase,
    double factor, double mouse_x, double mouse_y);

void queue_compose_middle_click_input(float x, float y);

void prepare_compose_panel_input(const WormUiComposeView* view, bool compose_tab_active,
                                           bool window_focused, WormUiWorkspaceAction* action);

bool process_compose_double_clicks(const ComposeDoubleClickContext& input,
                                          bool compose_pointer_captured,
                                          bool window_focused);

auto process_compose_timing_marker_pointer(const ComposePointerEventContext& input,
                                                  bool paint_pointer_consumed) -> bool;

void process_compose_pointer_actions(const ComposePointerEventContext& input,
                                           bool paint_pointer_consumed);

void compose_handle_focus_lost_input();

void discard_compose_frame_input();
