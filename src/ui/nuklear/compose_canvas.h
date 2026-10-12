#pragma once

#include "ui/frontend.h"
#include "wormedit/nuklear_config.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

struct WormUiTestingComposeGhostDrawState {
    std::size_t emitted_segments{};
    std::uint8_t red{}, green{}, blue{}, alpha{};
};

void draw_compose_scene_canvas(nk_context* context, const WormUiComposeView* view,
                               nk_command_buffer* canvas, struct nk_rect viewport);
void reset_compose_canvas_viewport_for_testing();
void draw_compose_quantize_ghosts_after_docks(nk_context* context,
                                              const WormUiComposeView* view,
                                              bool section_visible);
auto compose_canvas_viewport_for_testing() -> std::optional<WormUiTestingRect>;
auto compose_quantize_ghosts_drawn_for_testing() -> bool;
auto compose_quantize_ghost_draw_state_for_testing() -> WormUiTestingComposeGhostDrawState;

auto compose_timing_marker_at(const WormUiComposeView& view, double x, double y)
    -> const WormUiTimingMarkerView*;

// Horizontal extent (document pixels) a visible timing lane's content may use; empty when hidden.
auto compose_timing_lane_inner_extent(const WormUiComposeView& view, WormUiTimingLaneKind lane)
    -> std::optional<std::pair<double, double>>;

auto compose_editor_status_hint(const WormUiComposeView* view,
                                const WormUiTimingView* timing_view) -> const char*;

void draw_compose_tool_chip(nk_context*, const WormUiComposeView&, nk_command_buffer*, struct nk_rect);
auto process_compose_tool_chip(nk_context*, const WormUiComposeView&, struct nk_rect, bool focused) -> bool;

// Shared screen-space header button geometry for drawing and pointer capture.
auto compose_bgm_mix_button_rect(const WormUiComposeBgmLaneView& lane,
    const WormUiComposeView& view, struct nk_rect viewport, bool solo) -> struct nk_rect;
