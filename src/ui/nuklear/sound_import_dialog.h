#pragma once

#include "ui/nuklear/sound_import_state.h"
#include "ui/nuklear/keysound_list_state.h"
#include "ui/frontend.h"

struct nk_context;

enum class worm_ui_sound_import_dialog_action_t : std::uint8_t {
    none,
    confirm,
    cancel,
};

// Draws the retained import review overlay. Confirm returns intent only; the
// caller serializes the state and reports success/failure explicitly.
[[nodiscard]] auto worm_ui_nuklear_render_sound_import_dialog(
    nk_context* context, worm_ui_sound_import_state_t& state, int window_width,
    int window_height, const WormUiKindCatalogView& catalog)
    -> worm_ui_sound_import_dialog_action_t;

void worm_ui_nuklear_render_keysound_group_review(
    nk_context* context, const WormUiComposeView& view, WormUiWorkspaceAction& action,
    worm_ui_keysound_group_review_t& state, bool& release_pending,
    int window_width, int window_height);
