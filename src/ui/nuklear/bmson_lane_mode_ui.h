#pragma once
#include "ui/frontend.h"
struct nk_context;

bool bmson_lane_mode_confirmation_owns_input(bool sounds_tab = false);
void request_bmson_lane_mode(WormUiWorkspaceAction&, uint64_t project, uint64_t chart,
    uint64_t revision, uint64_t channel, bool stem, bool continues, bool sounds_tab = false, uint64_t resource = 0);
void render_bmson_lane_mode_confirmation(nk_context*, WormUiWorkspaceAction&,
    uint64_t project, uint64_t chart, uint64_t revision, bool focused, bool sounds_tab = false);
void reset_bmson_lane_mode_confirmation();
