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
    // From the selected area; a selection overrides the authored preview for one export.
    void begin(const wormedit::ui::compose_view_t& view);
    // From the authored song preview, or its audition default length when point-only.
    void begin_from_preview(const wormedit::ui::compose_view_t& view);
    void start(const wormedit::ui::compose_view_t& view, preview_audio_range_view_t source,
               std::uint64_t begin_row, std::uint64_t end_row);
    void render(nk_context* context, const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action);
};
}
