#pragma once

#include "ui/frontend.h"
#include <span>

struct nk_context;

[[nodiscard]] int worm_ui_nuklear_audio_slices_view_valid(const wormedit::ui::audio_slices_view_t* view);

void worm_ui_nuklear_render_audio_slices(struct nk_context* context,
                                         const wormedit::ui::audio_slices_view_t* view,
                                         wormedit::ui::workspace_action_t* action, bool show_asset_catalog,
                                         bool window_focused,
                                         std::span<const wormedit::ui::audio_waveform_column_view_t> columns = {});
