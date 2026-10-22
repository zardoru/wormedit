#pragma once
#include "ui/frontend.h"
#include "ui/nuklear/chrome/dialog.h"
#include <array>

namespace wormedit::ui {
struct preview_audio_dialog_t {
    bool open{};
    preview_audio_export_action_t draft;
    preview_audio_range_view_t range;
    std::array<char, 1024> filename{};
    double duration{};
    int fade_out_ms{};
    nk_bool replace_existing{};
    chrome::Dialog dialog;
    void begin(const wormedit::ui::compose_view_t& view);
    void render(nk_context* context, const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action);
};
}
