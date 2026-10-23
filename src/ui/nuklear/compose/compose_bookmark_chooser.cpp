#include "ui/nuklear/compose/compose_bookmark_chooser.h"

#include "wormedit/nuklear_config.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/popup_lifecycle.h"
#include <cstring>

#include <algorithm>
#include <cstdio>

namespace {
constexpr std::size_t page_size = 12;

void set_status(bookmark_chooser_state_t& state, const char* message) {
    std::snprintf(state.status.data(), state.status.size(), "%s", message);
}

auto current_row(const bookmark_chooser_state_t& state,
                 const wormedit::ui::bookmark_chooser_view_t& view) -> const wormedit::ui::bookmark_chooser_row_view_t* {
    if (state.selected_row < view.first_row) return nullptr;
    const auto index = state.selected_row - view.first_row;
    if (index >= view.rows.size()) return nullptr;
    return &view.rows[index];
}

auto activate_row(const wormedit::ui::bookmark_chooser_view_t& view, const wormedit::ui::bookmark_chooser_row_view_t& row,
                  wormedit::ui::workspace_action_t& action, nk_context* context,
                  bookmark_chooser_state_t& state) -> bool {
    if (!row.navigation_available || action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) {
        set_status(state, "Bookmark is unavailable at this position.");
        return false;
    }
    action.compose_action = WORM_UI_COMPOSE_ACTION_GO_TO_BOOKMARK;
    action.compose_bookmark_id.value = row.bookmark_id.value;
    action.compose_input_project_id.value = view.project_id.value;
    action.compose_input_chart_id.value = view.chart_id.value;
    action.compose_input_document_revision = view.document_revision;
    worm_ui_nuklear_bookmark_chooser_cancel(context, state);
    return true;
}

void move_selection(bookmark_chooser_state_t& state, const wormedit::ui::bookmark_chooser_view_t& view,
                    int delta) {
    if (view.total_count == 0) return;
    const auto current = std::min(state.selected_row, view.total_count - 1);
    if (delta < 0) state.selected_row = current == 0 ? 0 : current - 1;
    else state.selected_row = std::min(view.total_count - 1, current + 1);
    state.first_row = (state.selected_row / page_size) * page_size;
}

void handle_keyboard(nk_context* context, const wormedit::ui::bookmark_chooser_view_t& view,
                     wormedit::ui::workspace_action_t& action,
                     bookmark_chooser_state_t& state) {
    if (nk_input_is_key_pressed(&context->input, NK_KEY_UP)) {
        move_selection(state, view, -1);
    } else if (nk_input_is_key_pressed(&context->input, NK_KEY_DOWN)) {
        move_selection(state, view, 1);
    } else if (nk_input_is_key_pressed(&context->input, NK_KEY_ENTER)) {
        if (const auto* row = current_row(state, view))
            static_cast<void>(activate_row(view, *row, action, context, state));
    }
}

void render_page_controls(nk_context* context, const wormedit::ui::bookmark_chooser_view_t& view,
                          bookmark_chooser_state_t& state) {
    nk_layout_row_dynamic(context, 25, 3);
    if (nk_button_label(context, "Previous") && state.first_row > 0) {
        state.first_row = state.first_row > page_size ? state.first_row - page_size : 0;
        state.selected_row = state.first_row;
    }
    char page[64]{};
    std::snprintf(page, sizeof(page), "%zu-%zu of %zu",
        view.total_count == 0 ? 0 : state.first_row + 1,
        std::min(view.total_count, state.first_row + view.rows.size()), view.total_count);
    nk_label(context, page, NK_TEXT_ALIGN_CENTERED | NK_TEXT_ALIGN_MIDDLE);
    if (nk_button_label(context, "Next") && state.first_row + page_size < view.total_count) {
        state.first_row += page_size;
        state.selected_row = state.first_row;
    }
}

void render_rows(nk_context* context, const wormedit::ui::bookmark_chooser_view_t& view,
                 wormedit::ui::workspace_action_t& action, bookmark_chooser_state_t& state) {
    if (view.total_count == 0) {
        nk_layout_row_dynamic(context, 30, 1);
        nk_label(context, state.query[0] ? "No matching bookmarks." : "No bookmarks in this chart.", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE);
        return;
    }
    if (view.rows.empty() || view.rows.size() == 0 || view.first_row != state.first_row) {
        nk_layout_row_dynamic(context, 30, 1);
        nk_label(context, "Loading bookmarks…", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE);
        return;
    }
    for (std::size_t index = 0; index < view.rows.size(); ++index) {
        const auto& row = view.rows[index];
        const auto global_index = view.first_row + index;
        nk_layout_row_dynamic(context, 27, 1);
        if (!row.navigation_available) nk_widget_disable_begin(context);
        nk_bool selected = global_index == state.selected_row;
        if (nk_selectable_label(context,
                row.display_label.c_str() ? row.display_label.c_str() : "(unnamed bookmark)",
                NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, &selected)) {
            state.selected_row = global_index;
            state.selected_bookmark_id = row.bookmark_id.value;
            static_cast<void>(activate_row(view, row, action, context, state));
        }
        if (!row.navigation_available) nk_widget_disable_end(context);
    }
    render_page_controls(context, view, state);
}

auto render_search(nk_context* context, const wormedit::ui::bookmark_chooser_view_t& view,
                   bookmark_chooser_state_t& state) -> bool {
    nk_layout_row_dynamic(context, 28, 1);
    const auto previous_query = state.query;
    (void)wormedit::ui::chrome::autofocus_text_field(context, NK_EDIT_FIELD,
        state.query.data(), static_cast<int>(state.query.size()), nk_filter_default,
        state.focus_pending);
    if (state.query != previous_query) {
        state.first_row = 0;
        state.selected_row = 0;
        state.status.fill('\0');
    }
    return view.query == state.query.data();
}

} // namespace

void worm_ui_nuklear_bookmark_chooser_open(bookmark_chooser_state_t& state,
                                           std::uint64_t project_id,
                                           std::uint64_t chart_id,
                                           std::uint64_t document_revision) {
    state.open = true;
    state.release_pending = false;
    state.close_pending = false;
    state.project_id = project_id;
    state.chart_id = chart_id;
    state.document_revision = document_revision;
    state.first_row = 0;
    state.row_limit = page_size;
    state.selected_row = 0;
    state.selected_bookmark_id = 0;
    state.status.fill('\0');
    state.query.fill('\0');
    state.focus_pending = true;
}

void worm_ui_nuklear_bookmark_chooser_cancel(nk_context* context,
                                            bookmark_chooser_state_t& state) {
    (void)context;
    state.cancel();
}

auto worm_ui_nuklear_bookmark_chooser_owns_input(
    const bookmark_chooser_state_t& state) -> bool {
    return state.owns_input();
}

auto worm_ui_nuklear_bookmark_chooser_release_pending(
    const bookmark_chooser_state_t& state) -> bool {
    return state.release_pending;
}

void worm_ui_nuklear_bookmark_chooser_release_delivered(nk_context* context,
                                                       bookmark_chooser_state_t& state) {
    (void)wormedit::ui::chrome::release_retained_popup(
        context, state, wormedit::ui::chrome::PopupReleaseKeys::all);
}

auto worm_ui_nuklear_render_compose_bookmark_chooser(
    nk_context* context, const wormedit::ui::bookmark_chooser_view_t& view,
    wormedit::ui::workspace_action_t* action, bool window_focused,
    bookmark_chooser_state_t* state) -> bool {
    if (!context || !action || !state) return false;
    if (!state->open && !state->close_pending) {
        return wormedit::ui::chrome::release_retained_popup(
            context, *state, wormedit::ui::chrome::PopupReleaseKeys::all);
    }
    if (state->open && (!window_focused || state->project_id != view.project_id.value ||
        state->chart_id != view.chart_id.value)) {
        worm_ui_nuklear_bookmark_chooser_cancel(context, *state);
    }
    const auto parent = nk_window_get_bounds(context);
    const auto region = nk_window_get_content_region(context);
    const auto width = std::min(680.0f, std::max(360.0f, parent.w - 24.0f));
    const auto height = std::min(480.0f, std::max(260.0f, parent.h - 24.0f));
    const auto bounds = nk_rect((parent.w - width) * 0.5f - region.x,
                                (parent.h - height) * 0.5f - region.y, width, height);
    auto popup = wormedit::ui::chrome::begin_retained_popup(context, *state,
        "Go to bookmark", NK_WINDOW_BORDER, bounds,
        wormedit::ui::chrome::retained_popup_begin_failure_t::retain);
    if (!popup.has_contents()) return popup.owns_input();
    if (!state->open) {
        wormedit::ui::chrome::end_retained_popup(context, *state, popup,
            wormedit::ui::chrome::retained_popup_edit_focus_t::preserve,
            wormedit::ui::chrome::retained_popup_close_trigger_t::closed_or_pending);
        return true;
    }
    if (state->document_revision != view.document_revision) {
        state->document_revision = view.document_revision;
        state->first_row = 0;
        state->selected_row = 0;
        state->selected_bookmark_id = 0;
        set_status(*state, "Bookmarks changed. Choose an entry again.");
    }
    const bool results_current = render_search(context, view, *state);
    if (results_current) handle_keyboard(context, view, *action, *state);
    if (!state->open || state->close_pending) {
        wormedit::ui::chrome::end_retained_popup(context, *state, popup,
            wormedit::ui::chrome::retained_popup_edit_focus_t::preserve,
            wormedit::ui::chrome::retained_popup_close_trigger_t::closed_or_pending);
        return true;
    }
    nk_layout_row_dynamic(context, 26, 1);
    nk_label(context, "Select a bookmark by position and name · ↑/↓ choose · Enter jumps", NK_TEXT_ALIGN_LEFT |
             NK_TEXT_ALIGN_MIDDLE);
    if (state->status[0]) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, state->status.data(), NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE);
    }
    if (results_current) render_rows(context, view, *action, *state);
    else {
        nk_layout_row_dynamic(context, 30, 1);
        nk_label(context, "Searching…", NK_TEXT_LEFT);
    }
    wormedit::ui::chrome::end_retained_popup(context, *state, popup,
        wormedit::ui::chrome::retained_popup_edit_focus_t::preserve,
        wormedit::ui::chrome::retained_popup_close_trigger_t::closed_or_pending);
    return true;
}

void worm_ui_nuklear_render_bookmark_list(nk_context* context,
    const wormedit::ui::bookmark_chooser_view_t& view, wormedit::ui::workspace_action_t* action,
    bookmark_chooser_state_t& state) {
    if (state.project_id != view.project_id.value || state.chart_id != view.chart_id.value) state = {};
    state.project_id = view.project_id.value;
    state.chart_id = view.chart_id.value;
    state.document_revision = view.document_revision;
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, wormedit::dock_panel_catalog[static_cast<std::size_t>(wormedit::dock_panel_t::markers)].title.data(), NK_TEXT_LEFT);
    if (render_search(context, view, state)) render_rows(context, view, *action, state);
    else {
        nk_layout_row_dynamic(context, 24, 1);
        nk_label(context, "Updating markers…", NK_TEXT_LEFT);
    }
}
