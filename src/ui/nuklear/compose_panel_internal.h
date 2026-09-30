#pragma once

#include "ui/nuklear/compose_state.h"
#include "wormedit/nuklear_config.h"

static auto& state = worm_ui_nuklear_compose_state();
static constexpr double compose_measure_rail_width = 42.0;

struct ComposePaintContext {
    struct nk_context* context;
    const WormUiComposeView* view;
    struct nk_command_buffer* canvas;
    struct nk_rect viewport;
    struct nk_rect rail;
};

