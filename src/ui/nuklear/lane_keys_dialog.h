#pragma once
#include "ui/frontend/lane_keys.h"
struct nk_context;
namespace wormedit::ui::nuklear {
void render_lane_keys_dialog(nk_context*, const lane_keys_view_t&, lane_keys_action_t&, int width, int height);
}
