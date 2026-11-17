#pragma once

#include "ui/frontend/metadata_audio.h"
#include "ui/nuklear/audio/audio_ui_window.h"

struct nk_context;

void worm_ui_nuklear_render_metadata_panel(struct nk_context* context,
                                           const wormedit::ui::metadata_view_t* view,
                                           wormedit::ui::audio_ui_window_t* action, bool window_focused);
