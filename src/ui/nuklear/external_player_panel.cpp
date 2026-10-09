#include "ui/nuklear/external_player_panel.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/dialog.h"
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
wormedit::ui::chrome::Dialog external_player_dialog;

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

void edit_field(nk_context* context, const char* label, char* value, int capacity,
                const char* tooltip = nullptr) {
    nk_layout_row_dynamic(context, 15, 1);
    const auto label_bounds = nk_widget_bounds(context);
    nk_label_colored(context, label, NK_TEXT_LEFT,
                     wormedit::ui::chrome::color(wormedit::ui::chrome::UiColor::text_muted));
    if (tooltip && wormedit::ui::nuklear::tooltip::hovered(context, label_bounds))
        wormedit::ui::nuklear::tooltip::text(context, tooltip);
    nk_layout_row_dynamic(context, wormedit::ui::metrics::control_default, 1);
    wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, value, capacity, nk_filter_default);
}

void render_player_list(nk_context* context, const WormUiExternalPlayerView& view,
                        WormUiExternalPlayerAction& action) {
    nk_layout_row_dynamic(context, wormedit::ui::metrics::control_compact, 1);
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
    namespace chrome = wormedit::ui::chrome;
    constexpr auto templates = "Arguments may use <apppath>, <filename> and <measure>.";
    edit_field(context, "Name", draft.values.name, sizeof(draft.values.name));
    edit_field(context, "Player or wrapper path", draft.values.path, sizeof(draft.values.path));
    edit_field(context, "Play from start arguments", draft.values.start_arguments,
               sizeof(draft.values.start_arguments), templates);
    edit_field(context, "Play from here arguments", draft.values.here_arguments,
               sizeof(draft.values.here_arguments), templates);
    edit_field(context, "Stop arguments", draft.values.stop_arguments,
               sizeof(draft.values.stop_arguments), templates);
    chrome::dialog_footer_begin(external_player_dialog, context, 4, true);
    if (chrome::dialog_button(external_player_dialog, context, "Remove", chrome::DialogButtonRole::destructive))
        action.kind = WORM_UI_EXTERNAL_PLAYER_REMOVE;
    if (chrome::dialog_button(external_player_dialog, context, "Add", chrome::DialogButtonRole::secondary))
        action.kind = WORM_UI_EXTERNAL_PLAYER_ADD;
    if (chrome::dialog_button(external_player_dialog, context, "Close", chrome::DialogButtonRole::secondary))
        action.kind = WORM_UI_EXTERNAL_PLAYER_CLOSE;
    if (chrome::dialog_button(external_player_dialog, context, "Save", chrome::DialogButtonRole::primary)) {
        action = draft.values;
        action.kind = WORM_UI_EXTERNAL_PLAYER_SAVE;
        action.selected = draft.selected;
    }
}
} // namespace

void worm_ui_nuklear_render_external_player_panel(nk_context* context,
    const WormUiExternalPlayerView* view, WormUiExternalPlayerAction* action) {
    if (!context || !view || !action || !view->visible) {
        draft.initialized = false;
        return;
    }
    refresh_draft(*view);
    namespace chrome = wormedit::ui::chrome;
    if (chrome::dialog_begin(external_player_dialog, context, "External player preferences",
                             "External player preferences",
                             {.modal = false, .closable = true, .width = 560.0f})) {
        if (view->players) render_player_list(context, *view, *action);
        render_draft(context, *action);
    }
    if (chrome::dialog_end(external_player_dialog, context) == chrome::DialogResult::cancel)
        action->kind = WORM_UI_EXTERNAL_PLAYER_CLOSE;
}

void worm_ui_nuklear_reset_external_player_panel(void) { draft = {}; }
