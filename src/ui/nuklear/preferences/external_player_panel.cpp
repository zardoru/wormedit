#include "ui/nuklear/preferences/external_player_panel.h"
#include "ui/nuklear/chrome.h"
#include <array>
#include <cstdio>

namespace {
void emit(wormedit::ui::options_action_t& action, const wormedit::ui::options_view_t& view,
          WormUiOptionsField field, std::uint32_t index) {
    action = {.kind = WORM_UI_OPTIONS_EDIT_FIELD, .revision = view.revision,
        .category = WORM_UI_OPTIONS_PLAYERS, .field = field, .player_index = index};
}
void edit_player_field(nk_context* context, const wormedit::ui::options_view_t& view,
                       wormedit::ui::options_action_t& action, const char* label,
                       std::string_view value, WormUiOptionsField field) {
    std::array<char, wormedit::maximum_preference_string_bytes + 1> text{};
    std::snprintf(text.data(), text.size(), "%.*s", static_cast<int>(value.size()), value.empty() ? "" : value.data());
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, label, NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 28, 1);
    wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
        text.data(), text.size(), nk_filter_default);
    if (value == text.data() || action.kind != WORM_UI_OPTIONS_NONE) return;
    emit(action, view, field, static_cast<std::uint32_t>(view.selected_player));
    action.text = text.data();
}
}

void worm_ui_nuklear_render_external_players_page(nk_context* context,
    const wormedit::ui::options_view_t* view, wormedit::ui::options_action_t* action) {
    nk_layout_row_dynamic(context, 28, 1);
    for (std::size_t index = 0; index < view->players.size(); ++index) {
        const auto name = view->players[index].name;
        if (nk_option_label(context, name.empty() ? "Unnamed player" : name.data(), index == view->selected_player) &&
            index != view->selected_player && action->kind == WORM_UI_OPTIONS_NONE)
            emit(*action, *view, WORM_UI_OPTIONS_FIELD_PLAYER_SELECTED, static_cast<std::uint32_t>(index));
    }
    nk_layout_row_dynamic(context, 28, 2);
    if (nk_button_label(context, "Add") && action->kind == WORM_UI_OPTIONS_NONE)
        emit(*action, *view, WORM_UI_OPTIONS_FIELD_PLAYER_ADD, 0);
    const bool removable = view->players.size() > 1;
    if (!removable) nk_widget_disable_begin(context);
    if (nk_button_label(context, "Remove") && removable && action->kind == WORM_UI_OPTIONS_NONE)
        emit(*action, *view, WORM_UI_OPTIONS_FIELD_PLAYER_REMOVE, static_cast<std::uint32_t>(view->selected_player));
    if (!removable) nk_widget_disable_end(context);
    if (view->selected_player >= view->players.size()) return;
    const auto& player = view->players[view->selected_player];
    edit_player_field(context, *view, *action, "Name", player.name, WORM_UI_OPTIONS_FIELD_PLAYER_NAME);
    edit_player_field(context, *view, *action, "Player or wrapper path", player.path, WORM_UI_OPTIONS_FIELD_PLAYER_PATH);
    edit_player_field(context, *view, *action, "Play from start arguments", player.start_arguments, WORM_UI_OPTIONS_FIELD_PLAYER_START);
    edit_player_field(context, *view, *action, "Play from here arguments", player.here_arguments, WORM_UI_OPTIONS_FIELD_PLAYER_HERE);
    edit_player_field(context, *view, *action, "Stop arguments", player.stop_arguments, WORM_UI_OPTIONS_FIELD_PLAYER_STOP);
    nk_layout_row_dynamic(context, 40, 1);
    nk_label_wrap(context, "Arguments may use <apppath>, <filename> and <measure>. Changes are saved with Apply.");
}
