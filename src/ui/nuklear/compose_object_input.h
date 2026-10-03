#pragma once

#include "ui/nuklear/compose_panel_internal.h"

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

bool selected_bmson_lane_at(const ComposePointerEventContext& input, double x, double y);
auto bmson_razor_input_kind() -> WormUiComposeInputKind;
auto handle_compose_measure_click(const ComposePointerEventContext& input,
                                  bool paint_pointer_consumed) -> bool;
auto handle_compose_alt_pointer(const ComposePointerEventContext& input,
                                bool paint_pointer_consumed) -> bool;
