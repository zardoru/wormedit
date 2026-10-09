#include "ui/nuklear/bmson_lane_mode_ui.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/context_menu.h"
#include "wormedit/nuklear_config.h"
#include <algorithm>

namespace {
struct Confirmation {
    bool open{}, release_pending{}, sounds_tab{}, visible{};
    wormedit::ui::chrome::PopupContextStamp stamp{};
    uint64_t channel{};
} confirmation;

void emit(WormUiWorkspaceAction& action, uint64_t project, uint64_t chart,
          uint64_t revision, uint64_t channel, WormUiComposeActionKind kind) {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action.compose_action = kind;
    action.compose_input_project_id = project;
    action.compose_input_chart_id = chart;
    action.compose_input_document_revision = revision;
    action.compose_bmson_channel_id = channel;
}
}

bool bmson_lane_mode_confirmation_owns_input(bool sounds_tab) {
    return confirmation.sounds_tab == sounds_tab && (confirmation.open || confirmation.release_pending);
}
void reset_bmson_lane_mode_confirmation() { confirmation = {}; }

void request_bmson_lane_mode(WormUiWorkspaceAction& action, uint64_t project, uint64_t chart,
    uint64_t revision, uint64_t channel, bool stem, bool continues, bool sounds_tab, uint64_t resource) {
    if ((!channel && !resource) || action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    if (!stem && continues) {
        confirmation = {.open = true, .sounds_tab = sounds_tab,
                        .stamp = {project, chart, revision, 0}, .channel = channel};
        return;
    }
    emit(action, project, chart, revision, channel, stem ? WORM_UI_COMPOSE_ACTION_MAKE_BMSON_STEM
                                                       : WORM_UI_COMPOSE_ACTION_PACK_BMSON_CHANNEL);
    action.compose_resource_id = resource;
}

void render_bmson_lane_mode_confirmation(nk_context* context, WormUiWorkspaceAction& action,
    uint64_t project, uint64_t chart, uint64_t revision, bool focused, bool sounds_tab) {
    (void)wormedit::ui::chrome::popup_update_release_pending(context, confirmation.release_pending);
    if (!confirmation.open) return;
    const auto region = nk_window_get_content_region(context);
    const auto bounds = nk_rect(0, 60, std::min(440.0f, region.w), 210);
    if (wormedit::ui::chrome::popup_dismiss_requested(context,
            wormedit::ui::chrome::popup_screen_bounds(context, bounds), confirmation.visible) ||
        !focused || sounds_tab != confirmation.sounds_tab ||
        !wormedit::ui::chrome::popup_context_matches(confirmation.stamp, {project, chart, revision, 0})) {
        confirmation.open = false;
        confirmation.release_pending = true;
        wormedit::ui::chrome::dismiss_popup(context, "Pack BMSON channel", bounds);
        return;
    }
    if (!nk_popup_begin(context, NK_POPUP_STATIC, "Pack BMSON channel",
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, bounds)) return;
    confirmation.visible = true;
    nk_layout_row_dynamic(context, 28, 1);
    nk_label(context, "continuation points need a stem lane", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 66, 1);
    nk_label_wrap(context, "Packing requires converting every continuation point in this channel to a restart. This changes how the audio plays. Undo restores the channel and its stem lane.");
    nk_layout_row_dynamic(context, 30, 1);
    if (nk_button_label(context, "Convert continue points to restarts and pack") &&
        action.compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        emit(action, project, chart, revision, confirmation.channel,
             WORM_UI_COMPOSE_ACTION_RESTART_AND_PACK_BMSON_CHANNEL);
        confirmation.open = false;
    }
    if (nk_button_label(context, "Cancel")) confirmation.open = false;
    if (!confirmation.open) {
        confirmation.release_pending = true;
        nk_popup_close(context);
    }
    nk_popup_end(context);
}
