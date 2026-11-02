#pragma once
#include "ui/frontend/storyboard.h"
struct nk_context;
namespace wormedit::ui {
void render_storyboard_video_dialog(nk_context*, const storyboard_view_t&, storyboard_action_t&);
// The picker uses the existing generation-protected native dialog slot.
void reset_storyboard_video_dialog();
}
