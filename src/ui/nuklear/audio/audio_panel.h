#pragma once

#include "ui/nuklear/audio/audio_ui_window.h"
#include "ui/frontend/audio_slicer.h"
#include "ui/frontend/common.h"
#include "ui/frontend/project.h"
#include <span>

struct nk_context;

[[nodiscard]] int worm_ui_nuklear_audio_slices_view_valid(const wormedit::ui::audio_slices_view_t* view);

void worm_ui_nuklear_render_audio_slices(struct nk_context* context,
                                         const wormedit::ui::audio_slices_view_t* view,
                                         wormedit::ui::audio_ui_window_t* action, bool show_asset_catalog,
                                         bool window_focused,
                                         std::span<const wormedit::ui::audio_waveform_column_view_t> columns = {});
