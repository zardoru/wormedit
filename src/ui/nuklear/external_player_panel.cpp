#include "ui/nuklear/external_player_panel.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/dialog.h"
#include "wormedit/nuklear_config.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace {
// A real editing bound retained from the preferences input fields.
inline constexpr std::size_t external_player_field_bytes = 4097;
struct player_draft_values_t {
    std::array<char, external_player_field_bytes> name{};
    std::array<char, external_player_field_bytes> path{};
    std::array<char, external_player_field_bytes> start_arguments{};
    std::array<char, external_player_field_bytes> here_arguments{};
    std::array<char, external_player_field_bytes> stop_arguments{};
};
struct player_draft_t {
    uint64_t revision{};
    size_t selected{};
    bool initialized{};
    player_draft_values_t values{};
};
player_draft_t draft;
wormedit::ui::chrome::Dialog external_player_dialog;

void copy_field(char* target, size_t capacity, const char* value) {
    std::snprintf(target, capacity, "%s", value ? value : "");
}

void refresh_draft(const wormedit::ui::external_player_view_t& view) {
    if (draft.initialized && draft.revision == view.revision && draft.selected == view.selected) return;
    draft = {.revision = view.revision, .selected = view.selected, .initialized = true};
    if (view.selected >= view.players.size()) return;
    const auto& player = view.players[view.selected];
    copy_field(draft.values.name.data(), draft.values.name.size(), player.name.data());
    copy_field(draft.values.path.data(), draft.values.path.size(), player.path.data());
    copy_field(draft.values.start_arguments.data(), draft.values.start_arguments.size(), player.start_arguments.data());
    copy_field(draft.values.here_arguments.data(), draft.values.here_arguments.size(), player.here_arguments.data());
    copy_field(draft.values.stop_arguments.data(), draft.values.stop_arguments.size(), player.stop_arguments.data());
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

void render_player_list(nk_context* context, const wormedit::ui::external_player_view_t& view,
                        wormedit::ui::external_player_action_t& action) {
    nk_layout_row_dynamic(context, wormedit::ui::metrics::control_compact, 1);
    for (size_t index = 0; index < view.players.size(); ++index) {
        const auto name = view.players[index].name;
        if (nk_option_label(context, !name.empty() ? name.data() : "Unnamed player", index == view.selected)) {
            if (index != view.selected) {
                action.kind = wormedit::ui::external_player_action_kind_t::select;
                action.selected = index;
            }
        }
    }
}

void render_draft(nk_context* context, wormedit::ui::external_player_action_t& action) {
    namespace chrome = wormedit::ui::chrome;
    constexpr auto templates = "Arguments may use <apppath>, <filename> and <measure>.";
    edit_field(context, "Name", draft.values.name.data(), draft.values.name.size());
    edit_field(context, "Player or wrapper path", draft.values.path.data(), draft.values.path.size());
    edit_field(context, "Play from start arguments", draft.values.start_arguments.data(),
               draft.values.start_arguments.size(), templates);
    edit_field(context, "Play from here arguments", draft.values.here_arguments.data(),
               draft.values.here_arguments.size(), templates);
    edit_field(context, "Stop arguments", draft.values.stop_arguments.data(),
               draft.values.stop_arguments.size(), templates);
    chrome::dialog_footer_begin(external_player_dialog, context, 4, true);
    if (chrome::dialog_button(external_player_dialog, context, "Remove", chrome::DialogButtonRole::destructive))
        action.kind = wormedit::ui::external_player_action_kind_t::remove;
    if (chrome::dialog_button(external_player_dialog, context, "Add", chrome::DialogButtonRole::secondary))
        action.kind = wormedit::ui::external_player_action_kind_t::add;
    if (chrome::dialog_button(external_player_dialog, context, "Close", chrome::DialogButtonRole::secondary))
        action.kind = wormedit::ui::external_player_action_kind_t::close;
    if (chrome::dialog_button(external_player_dialog, context, "Save", chrome::DialogButtonRole::primary)) {
        action = {.name = draft.values.name.data(), .path = draft.values.path.data(),
                  .start_arguments = draft.values.start_arguments.data(),
                  .here_arguments = draft.values.here_arguments.data(),
                  .stop_arguments = draft.values.stop_arguments.data()};
        action.kind = wormedit::ui::external_player_action_kind_t::save;
        action.selected = draft.selected;
    }
}
} // namespace

void worm_ui_nuklear_render_external_player_panel(nk_context* context,
    const wormedit::ui::external_player_view_t* view, wormedit::ui::external_player_action_t* action) {
    if (!context || !view || !action || !view->visible) {
        draft.initialized = false;
        return;
    }
    refresh_draft(*view);
    namespace chrome = wormedit::ui::chrome;
    if (chrome::dialog_begin(external_player_dialog, context, "External player preferences",
                             "External player preferences",
                             {.modal = false, .closable = true, .width = 560.0f})) {
        if (!view->players.empty()) render_player_list(context, *view, *action);
        render_draft(context, *action);
    }
    if (chrome::dialog_end(external_player_dialog, context) == chrome::DialogResult::cancel)
        action->kind = wormedit::ui::external_player_action_kind_t::close;
}

void worm_ui_nuklear_reset_external_player_panel(void) { draft = {}; }
