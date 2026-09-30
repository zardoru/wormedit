#pragma once

#include "ui/frontend.h"
#include "wormedit/nuklear_config.h"

void draw_compose_scene_canvas(nk_context* context, const WormUiComposeView* view,
                               nk_command_buffer* canvas, struct nk_rect viewport);

auto compose_timing_marker_at(const WormUiComposeView& view, double x, double y)
    -> const WormUiTimingMarkerView*;

auto compose_editor_status_hint(const WormUiComposeView* view,
                                const WormUiTimingView* timing_view) -> const char*;

