#pragma once

#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/compose_keyboard_input.h"
#include "ui/nuklear/compose_viewport_input.h"
#include "ui/nuklear/compose_timing_input.h"
#include "ui/nuklear/compose_object_input.h"

bool compose_action_has_cancel(const wormedit::ui::workspace_action_t* action);

void queue_compose_pinch_input(worm_ui_nuklear_pinch_phase_t phase,
    double factor, double mouse_x, double mouse_y);

bool process_compose_double_clicks(const ComposeDoubleClickContext& input,
                                          bool compose_pointer_captured,
                                          bool window_focused);

auto process_compose_timing_marker_pointer(const ComposePointerEventContext& input,
                                                  bool paint_pointer_consumed) -> bool;

void process_compose_pointer_actions(const ComposePointerEventContext& input,
                                           bool paint_pointer_consumed);

uint64_t compose_sound_object_at(const WormUiComposeView& view, double x, double y);

auto handle_same_sound_pointer(const ComposePointerEventContext& input) -> bool;

void update_compose_sound_hover(const ComposePointerEventContext& input);
