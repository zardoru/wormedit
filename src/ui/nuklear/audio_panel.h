#pragma once

#include "ui/frontend.h"

struct nk_context;

[[nodiscard]] int worm_ui_nuklear_audio_slices_view_valid(const WormUiAudioSlicesView* view);

void worm_ui_nuklear_render_audio_slices(struct nk_context* context,
                                         const WormUiAudioSlicesView* view,
                                         WormUiWorkspaceAction* action, bool show_asset_catalog,
                                         bool window_focused);
