#pragma once

#include "ui/frontend.h"
#include "wormedit/nuklear_config.h"

#include <optional>
#include <utility>

void draw_compose_scene_canvas(nk_context* context, const WormUiComposeView* view,
                               nk_command_buffer* canvas, struct nk_rect viewport);
void reset_compose_canvas_viewport_for_testing();
auto compose_canvas_viewport_for_testing() -> std::optional<WormUiTestingRect>;

auto compose_timing_marker_at(const WormUiComposeView& view, double x, double y)
    -> const WormUiTimingMarkerView*;

// Horizontal extent (document pixels) a visible timing lane's content may use; empty when hidden.
auto compose_timing_lane_inner_extent(const WormUiComposeView& view, WormUiTimingLaneKind lane)
    -> std::optional<std::pair<double, double>>;

auto compose_editor_status_hint(const WormUiComposeView* view,
                                const WormUiTimingView* timing_view) -> const char*;
