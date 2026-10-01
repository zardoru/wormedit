#pragma once

#include "ui/nuklear/compose_panel_internal.h"

bool compose_action_has_cancel(const WormUiWorkspaceAction* action);

void queue_compose_key_input(std::uint32_t keycode, std::uint32_t scancode,
                                       std::uint16_t modifiers, bool down, bool repeat);

void queue_compose_wheel_input(double delta_x, double delta_y, double mouse_x,
                                         double mouse_y, std::uint16_t modifiers,
                                         bool coarse_steps);

void queue_compose_pinch_input(worm_ui_nuklear_pinch_phase_t phase,
    double factor, double mouse_x, double mouse_y);

void queue_compose_middle_click_input(float x, float y);

void process_compose_key_events(struct nk_context* context, const WormUiComposeView* view,
                                       WormUiWorkspaceAction* action, bool window_focused,
                                       bool ime_composition_active);

void prepare_compose_panel_input(const WormUiComposeView* view, bool compose_tab_active,
                                           bool window_focused, WormUiWorkspaceAction* action);

ComposePointerFocusState update_compose_pointer_focus(
    const ComposePointerFocusContext& focus);

ComposeViewportCursor collect_compose_viewport_inputs(
    const ComposeViewportInputContext& input);

bool process_compose_beat_fit_pointer(const ComposeBeatFitInput& input);

void process_compose_overview_input(const ComposeOverviewInput& input);

bool process_compose_double_clicks(const ComposeDoubleClickContext& input,
                                          bool compose_pointer_captured,
                                          bool window_focused);

void update_timing_stage_drag(const ComposePointerEventContext& input);

auto begin_compose_resource_pointer(const ComposePointerEventContext& input,
                                           bool paint_pointer_consumed) -> bool;

auto process_bmson_razor_pointer(const ComposePointerEventContext& input,
                                        bool paint_pointer_consumed) -> bool;

auto process_bmson_marker_move_pointer(const ComposePointerEventContext& input,
                                              bool paint_pointer_consumed) -> bool;

auto process_semantic_layer_paint_pointer(const ComposePointerEventContext& input,
                                                 bool& paint_pointer_consumed) -> bool;

void process_note_paint_pointer(const ComposePointerEventContext& input,
                                       bool& paint_pointer_consumed);

void process_compose_drag_gestures(const ComposePointerEventContext& input,
                                          bool paint_pointer_consumed);

auto process_compose_timing_marker_pointer(const ComposePointerEventContext& input,
                                                  bool paint_pointer_consumed) -> bool;

void process_compose_pointer_actions(const ComposePointerEventContext& input,
                                           bool paint_pointer_consumed);

void compose_handle_focus_lost_input();

void discard_compose_frame_input();
