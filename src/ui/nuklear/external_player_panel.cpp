#include "ui/nuklear/external_player_panel.h"
#include "wormedit/nuklear_config.h"

#include <cstdio>
#include <cstring>

namespace {
struct player_draft_t {
    uint64_t revision{};
    size_t selected{};
    bool initialized{};
    WormUiExternalPlayerAction values{};
};
player_draft_t draft;

void copy_field(char* target, size_t capacity, const char* value) {
    std::snprintf(target, capacity, "%s", value ? value : "");
}

void refresh_draft(const WormUiExternalPlayerView& view) {
    if (draft.initialized && draft.revision == view.revision && draft.selected == view.selected) return;
    draft = {.revision = view.revision, .selected = view.selected, .initialized = true};
    if (!view.players || view.selected >= view.player_count) return;
    const auto& player = view.players[view.selected];
    copy_field(draft.values.name, sizeof(draft.values.name), player.name);
    copy_field(draft.values.path, sizeof(draft.values.path), player.path);
    copy_field(draft.values.start_arguments, sizeof(draft.values.start_arguments), player.start_arguments);
    copy_field(draft.values.here_arguments, sizeof(draft.values.here_arguments), player.here_arguments);
    copy_field(draft.values.stop_arguments, sizeof(draft.values.stop_arguments), player.stop_arguments);
}

void edit_field(nk_context* context, const char* label, char* value, int capacity) {
    nk_layout_row_dynamic(context, 22, 1);
    nk_label(context, label, NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 28, 1);
    nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, value, capacity, nk_filter_default);
}

void render_player_list(nk_context* context, const WormUiExternalPlayerView& view,
                        WormUiExternalPlayerAction& action) {
    nk_layout_row_dynamic(context, 24, 1);
    for (size_t index = 0; index < view.player_count; ++index) {
        const auto* name = view.players[index].name;
        if (nk_option_label(context, name && *name ? name : "Unnamed player", index == view.selected)) {
            if (index != view.selected) {
                action.kind = WORM_UI_EXTERNAL_PLAYER_SELECT;
                action.selected = index;
            }
        }
    }
}

void render_draft(nk_context* context, WormUiExternalPlayerAction& action) {
    edit_field(context, "Name", draft.values.name, sizeof(draft.values.name));
    edit_field(context, "Player or wrapper path", draft.values.path, sizeof(draft.values.path));
    edit_field(context, "Play from start arguments", draft.values.start_arguments, sizeof(draft.values.start_arguments));
    edit_field(context, "Play from here arguments", draft.values.here_arguments, sizeof(draft.values.here_arguments));
    edit_field(context, "Stop arguments", draft.values.stop_arguments, sizeof(draft.values.stop_arguments));
    nk_layout_row_dynamic(context, 26, 1);
    nk_label(context, "Templates: <apppath>, <filename>, <measure>", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 28, 4);
    if (nk_button_label(context, "Save")) {
        action = draft.values;
        action.kind = WORM_UI_EXTERNAL_PLAYER_SAVE;
        action.selected = draft.selected;
    }
    if (nk_button_label(context, "Add")) action.kind = WORM_UI_EXTERNAL_PLAYER_ADD;
    if (nk_button_label(context, "Remove")) action.kind = WORM_UI_EXTERNAL_PLAYER_REMOVE;
    if (nk_button_label(context, "Close")) action.kind = WORM_UI_EXTERNAL_PLAYER_CLOSE;
}
} // namespace

void worm_ui_nuklear_render_external_player_panel(nk_context* context,
    const WormUiExternalPlayerView* view, WormUiExternalPlayerAction* action) {
    if (!context || !view || !action || !view->visible) {
        draft.initialized = false;
        return;
    }
    refresh_draft(*view);
    const auto bounds = nk_rect(80, 50, 620, 560);
    const bool content = nk_begin(context, "External player preferences", bounds,
        NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_TITLE);
    if (content) {
        if (view->players) render_player_list(context, *view, *action);
        render_draft(context, *action);
    }
    nk_end(context);
}

void worm_ui_nuklear_reset_external_player_panel(void) { draft = {}; }
