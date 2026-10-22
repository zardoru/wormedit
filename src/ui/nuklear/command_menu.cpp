#include "ui/frontend/workspace.h"
#include "core/text_util.h"
#include "ui/nuklear/command_menu.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/dialog.h"
#include "ui/nuklear/chrome/modal_input.h"
#include "ui/nuklear/sdl_backend.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

namespace {
constexpr const char* window_id = "Command menu";
constexpr std::size_t maximum_visible_matches = 20;
struct context_t {
    std::uint64_t project{}, chart{}, revision{};
    WormUiWorkspaceTab tab{};
    auto operator==(const context_t&) const -> bool = default;
};
struct menu_state_t {
    context_t current{}, opened{};
    bool available{}, open{}, release_pending{}, focus_pending{}, activate{};
    int movement{};
    std::size_t selected{};
    std::array<char, 129> query{};
    std::vector<SDL_Scancode> captured;
    wormedit::ui::chrome::Dialog dialog;
} state;

auto contains(std::string_view label, std::string_view query) -> bool {
    return std::search(label.begin(), label.end(), query.begin(), query.end(),
        [](unsigned char left, unsigned char right) {
            return wormedit::text_util::ascii_iequal(static_cast<char>(left), static_cast<char>(right));
        }) != label.end();
}
auto matching_rows(const wormedit::ui::workspace_view_t& view) -> std::vector<std::size_t> {
    std::vector<std::size_t> rows;
    const auto count = view.command_menu_rows.size();
    for (std::size_t i = 0; i < count && rows.size() < maximum_visible_matches; ++i) {
        const auto& row = view.command_menu_rows[i];
        if (!row.id.empty() && !row.label.empty() && contains(row.label, state.query.data())) rows.push_back(i);
    }
    return rows;
}
void move_selection(const wormedit::ui::workspace_view_t& view, const std::vector<std::size_t>& rows, int delta) {
    if (rows.empty()) { state.selected = 0; return; }
    auto selected = std::min(state.selected, rows.size() - 1);
    for (std::size_t tried = 0; tried < rows.size(); ++tried) {
        selected = delta < 0 ? (selected + rows.size() - 1) % rows.size()
                             : (selected + 1) % rows.size();
        if (view.command_menu_rows[rows[selected]].enabled) { state.selected = selected; return; }
    }
}
void select_enabled(const wormedit::ui::workspace_view_t& view, const std::vector<std::size_t>& rows) {
    if (rows.empty()) return;
    state.selected = std::min(state.selected, rows.size() - 1);
    if (!view.command_menu_rows[rows[state.selected]].enabled) move_selection(view, rows, 1);
    while (state.movement != 0) {
        const auto delta = state.movement > 0 ? 1 : -1;
        move_selection(view, rows, delta);
        state.movement -= delta;
    }
}
void activate_command(const wormedit::ui::workspace_view_t& view, std::size_t index,
                      wormedit::ui::workspace_action_t& action) {
    const auto& row = view.command_menu_rows[index];
    if (!state.open || state.current != state.opened || !row.enabled || row.id.empty() ||
        row.id.size() >= 129 || action.menu_command_id[0]) return;
    action.command_menu_command_id = row.id.data();
    action.command_menu_invoked = true;
    action.compose_input_project_id.value = state.opened.project;
    action.compose_input_chart_id.value = state.opened.chart;
    action.compose_input_document_revision = state.opened.revision;
    action.tab = state.opened.tab;
    cancel_command_menu();
}
void render_query(nk_context* context) {
    const auto previous = state.query;
    nk_layout_row_dynamic(context, 28, 1);
    (void)wormedit::ui::chrome::autofocus_text_field(context, NK_EDIT_FIELD,
        state.query.data(), static_cast<int>(state.query.size()), nk_filter_default, state.focus_pending);
    if (previous != state.query) state.selected = 0;
}
void render_rows(nk_context* context, const wormedit::ui::workspace_view_t& view,
                 wormedit::ui::workspace_action_t& action, int height) {
    const auto rows = matching_rows(view);
    select_enabled(view, rows);
    if (state.activate && !rows.empty()) activate_command(view, rows[state.selected], action);
    state.activate = false;
    if (rows.empty()) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "No matching commands", NK_TEXT_LEFT);
        return;
    }
    const auto page = static_cast<std::size_t>(std::clamp((height - 180) / 34, 1, 20));
    const auto first = state.selected / page * page;
    for (auto i = first; i < std::min(rows.size(), first + page); ++i) {
        const auto& row = view.command_menu_rows[rows[i]];
        nk_layout_row_dynamic(context, 26, 1);
        const auto bounds = nk_widget_bounds(context);
        if (!row.enabled) nk_widget_disable_begin(context);
        nk_bool selected = state.selected == i;
        const bool clicked = nk_selectable_label(context, row.label.data(), NK_TEXT_LEFT, &selected);
        const auto& style = context->style.selectable;
        wormedit::ui::chrome::draw_row_shortcut(context, bounds, row.shortcut.data(),
            state.selected == i ? style.text_normal_active
                                : wormedit::ui::chrome::color(wormedit::ui::chrome::UiColor::text_muted));
        if (!row.enabled) nk_widget_disable_end(context);
        if (clicked && row.enabled) activate_command(view, rows[i], action);
    }
}
auto quiet(const nk_context& context) -> bool {
    for (const auto& button : context.input.mouse.buttons) if (button.down || button.clicked) return false;
    for (const auto& key : context.input.keyboard.keys) if (key.down || key.clicked) return false;
    return true;
}
}

void prepare_command_menu(const wormedit::ui::workspace_view_t* view, bool available) {
    state.available = available && view;
    if (view) state.current = {view->project_id.value, view->selected_chart_id.value,
                              view->document_revision, view->active_tab};
    if (state.open && (!state.available || state.current != state.opened)) cancel_command_menu();
}
auto open_command_menu() -> bool {
    if (!state.available || state.open || state.release_pending) return false;
    state.opened = state.current;
    state.query = {};
    state.selected = 0;
    state.movement = 0;
    state.activate = false;
    state.open = state.focus_pending = true;
    state.dialog = {};
    return true;
}
void cancel_command_menu() {
    if (!state.open) return;
    state.open = state.focus_pending = state.activate = false;
    state.release_pending = true;
}
void reset_command_menu() { state = {}; }
auto command_menu_owns_input() -> bool { return state.open || state.release_pending; }
auto capture_command_menu_key(nk_context* context, const SDL_KeyboardEvent& event, bool ime) -> bool {
    const auto held = std::ranges::find(state.captured, event.scancode);
    if (!command_menu_owns_input() && held == state.captured.end()) return false;
    if (!event.down) {
        if (held != state.captured.end()) state.captured.erase(held);
        if (state.open) wormedit::ui::chrome::forward_modal_key(context, event);
        return true;
    }
    if (held == state.captured.end()) state.captured.push_back(event.scancode);
    if (!state.open) return true;
    if (event.key == SDLK_ESCAPE) cancel_command_menu();
    else if (event.key == SDLK_UP || event.key == SDLK_DOWN)
        state.movement = std::clamp(state.movement + (event.key == SDLK_UP ? -1 : 1), -20, 20);
    else if (event.key == SDLK_RETURN || event.key == SDLK_KP_ENTER) {
        if (!ime && !event.repeat) state.activate = true;
    } else wormedit::ui::chrome::forward_modal_key(context, event);
    return true;
}
auto capture_command_menu_text(nk_context* context, const SDL_Event& event) -> bool {
    if (!command_menu_owns_input()) return false;
    if (state.open) { auto copy = event; nk_sdl_handle_event(context, &copy); }
    return true;
}
void render_command_menu(nk_context* context, const wormedit::ui::workspace_view_t& view,
                         wormedit::ui::workspace_action_t& action, int width, int height) {
    if (!state.open) {
        nk_window_close(context, window_id);
        if (quiet(*context)) state.release_pending = false;
        return;
    }
    using namespace wormedit::ui::chrome;
    static constexpr const char* title_keys[]{"↑", "↓", "Enter", "Esc"};
    if (dialog_begin(state.dialog, context, window_id, "Command menu",
        {.width = 680, .area = nk_vec2(static_cast<float>(width), static_cast<float>(height)),
         .enter_confirms = false, .title_font = wormedit::ui::theme::display_font,
         .title_keys = title_keys})) {
        render_query(context);
        render_rows(context, view, action, height);
        if (!state.open) nk_edit_unfocus(context);
    }
    if (dialog_end(state.dialog, context) == DialogResult::cancel) cancel_command_menu();
}
