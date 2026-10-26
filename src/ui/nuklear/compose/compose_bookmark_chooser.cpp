#include "ui/nuklear/compose/compose_bookmark_chooser.h"
#include "ui/nuklear/compose/compose_markers.h"

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

void emit_reorder(const wormedit::ui::bookmark_chooser_view_t& view,
                  wormedit::object_id_t bookmark, wormedit::object_id_t target, bool after,
                  wormedit::ui::workspace_action_t& action, bookmark_chooser_state_t& state) {
    if (!bookmark || !target || bookmark == target || action.compose_action != WORM_UI_COMPOSE_ACTION_NONE)
        return;
    action.compose_action = WORM_UI_COMPOSE_ACTION_REORDER_BOOKMARK;
    action.compose_bookmark_reorder = {.bookmark = bookmark, .target = target,
        .place_after = after, .sort = view.sort, .descending = view.descending};
    action.compose_input_project_id = view.project_id;
    action.compose_input_chart_id = view.chart_id;
    action.compose_input_document_revision = view.document_revision;
    state.sort = wormedit::bookmark_sort_t::custom;
    state.descending = false;
    state.dragged_bookmark = {};
}

void render_reorder_button(nk_context* context,
                          const wormedit::ui::bookmark_chooser_view_t& view,
                          const wormedit::ui::bookmark_chooser_row_view_t& row,
                          bool after,
                          wormedit::ui::workspace_action_t& action, bookmark_chooser_state_t& state) {
    const auto target = after ? row.next_bookmark : row.previous_bookmark;
    char tooltip[96]{};
    std::snprintf(tooltip, sizeof(tooltip), "Move bookmark #%llu %s",
        static_cast<unsigned long long>(row.bookmark_id.value), after ? "down" : "up");
    const wormedit::ui::chrome::IconButton button{
        .glyph = after ? wormedit::ui::lucide::Glyph::chevron_down : wormedit::ui::lucide::Glyph::chevron_up,
        .tooltip = tooltip, .enabled = static_cast<bool>(target)};
    if (button.render(context)) emit_reorder(view, row.bookmark_id, target, after, action, state);
}

void update_bookmark_drop(nk_context* context, const wormedit::ui::bookmark_chooser_view_t& view,
                          const wormedit::ui::bookmark_chooser_row_view_t& row, struct nk_rect bounds,
                          wormedit::ui::workspace_action_t& action, bookmark_chooser_state_t& state) {
    if (!state.dragged_bookmark || state.dragged_bookmark == row.bookmark_id ||
        !nk_input_is_mouse_hovering_rect(&context->input, bounds) ||
        !nk_input_is_mouse_hovering_rect(&context->input, context->current->buffer.clip)) return;
    const bool after = context->input.mouse.pos.y >= bounds.y + bounds.h / 2;
    const auto y = after ? bounds.y + bounds.h : bounds.y;
    nk_stroke_line(nk_window_get_canvas(context), bounds.x, y, bounds.x + bounds.w, y,
        2, wormedit::ui::chrome::color(wormedit::ui::chrome::UiColor::accent));
    if (nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT))
        emit_reorder(view, state.dragged_bookmark, row.bookmark_id, after, action, state);
}

auto render_bookmark_label(nk_context* context, const wormedit::ui::bookmark_chooser_view_t& view,
                           std::size_t index, wormedit::ui::workspace_action_t& action,
                           bookmark_chooser_state_t& state) -> struct nk_rect {
    const auto& row = view.rows[index];
    const auto bounds = nk_widget_bounds(context);
    const bool compact = nk_window_get_content_region_size(context).x < 420;
    const char* label = compact ? " " : row.display_label.c_str();
    if (!row.navigation_available) nk_widget_disable_begin(context);
    nk_bool selected = state.open ? view.first_row + index == state.selected_row
                                 : row.bookmark_id.value == state.selected_bookmark_id;
    if (nk_selectable_label(context, label,
            NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, &selected) && !state.dragged_bookmark) {
        state.selected_row = view.first_row + index;
        state.selected_bookmark_id = row.bookmark_id.value;
        static_cast<void>(activate_row(view, row, action, context, state));
    }
    if (!row.navigation_available) nk_widget_disable_end(context);
    if (compact) worm_ui_nuklear_draw_marker_row_text(context, bounds,
        row.name.empty() ? row.display_label.c_str() : row.name.c_str(), row.position_label.c_str());
    if (wormedit::ui::nuklear::tooltip::hovered(context, bounds))
        wormedit::ui::nuklear::tooltip::text(context, row.display_label.c_str());
    return bounds;
}

void render_bookmark_row(nk_context* context, const wormedit::ui::bookmark_chooser_view_t& view,
                         std::size_t index, wormedit::ui::workspace_action_t& action,
                         bookmark_chooser_state_t& state) {
    const auto& row = view.rows[index];
    nk_layout_row_template_begin(context, 27);
    nk_layout_row_template_push_static(context, 22);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_static(context, 25);
    nk_layout_row_template_push_static(context, 25);
    nk_layout_row_template_end(context);
    const auto handle = nk_widget_bounds(context);
    const bool handle_hovered = nk_widget_is_hovered(context);
    char tooltip[96]{};
    std::snprintf(tooltip, sizeof(tooltip), "Drag bookmark #%llu to rearrange",
        static_cast<unsigned long long>(row.bookmark_id.value));
    {
        const wormedit::ui::chrome::NativeIconCell cell{context,
            state.dragged_bookmark == row.bookmark_id, true, tooltip};
        wormedit::ui::lucide::draw(context, cell.bounds(),
            wormedit::ui::lucide::Glyph::grip_vertical, cell.tint());
    }
    if (handle_hovered && !context->current->widgets_disabled &&
        nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT)) {
        state.dragged_bookmark = row.bookmark_id;
        state.drag_revision = view.document_revision;
    }
    const auto label_bounds = render_bookmark_label(context, view, index, action, state);
    render_reorder_button(context, view, row, false, action, state);
    render_reorder_button(context, view, row, true, action, state);
    const auto drop_bounds = nk_rect(handle.x, handle.y,
        label_bounds.x + label_bounds.w - handle.x, handle.h);
    update_bookmark_drop(context, view, row, drop_bounds, action, state);
}

void render_rows(nk_context* context, const wormedit::ui::bookmark_chooser_view_t& view,
                 wormedit::ui::workspace_action_t& action, bookmark_chooser_state_t& state) {
    if (state.drag_revision != view.document_revision || context->current->widgets_disabled)
        state.dragged_bookmark = {};
    if (view.total_count == 0) {
        nk_layout_row_dynamic(context, 30, 1);
        nk_label(context, state.query[0] ? "No matching bookmarks." : "No bookmarks in this chart.", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE);
        return;
    }
    if (view.rows.empty() || view.first_row != state.first_row) {
        nk_layout_row_dynamic(context, 30, 1);
        nk_label(context, "Loading bookmarks…", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE);
        return;
    }
    for (std::size_t index = 0; index < view.rows.size(); ++index)
        render_bookmark_row(context, view, index, action, state);
    if (!nk_input_is_mouse_down(&context->input, NK_BUTTON_LEFT)) state.dragged_bookmark = {};
    render_page_controls(context, view, state);
}

auto render_sort_controls(nk_context* context, const wormedit::ui::bookmark_chooser_view_t& view,
                          bookmark_chooser_state_t& state) -> bool {
    static constexpr std::array sorts{
        std::pair{wormedit::bookmark_sort_t::custom, "Custom"},
        std::pair{wormedit::bookmark_sort_t::position, "Position"},
        std::pair{wormedit::bookmark_sort_t::name, "Name"}};
    const auto previous_sort = state.sort;
    const auto previous_descending = state.descending;
    nk_layout_row_dynamic(context, 25,
        nk_window_get_content_region_size(context).x < 420 ? 2 : 4);
    for (const auto& [sort, label] : sorts)
        if (nk_option_label(context, label, state.sort == sort)) state.sort = sort;
    if (nk_button_label(context, state.descending ? "Descending" : "Ascending"))
        state.descending = !state.descending;
    if (state.sort != previous_sort || state.descending != previous_descending) {
        state.first_row = 0;
        state.selected_row = 0;
        state.dragged_bookmark = {};
    }
    return state.sort == view.sort && state.descending == view.descending;
}

auto render_search(nk_context* context, const wormedit::ui::bookmark_chooser_view_t& view,
                   bookmark_chooser_state_t& state) -> bool {
    nk_layout_row_dynamic(context, 28, 1);
    const auto previous_query = state.query;
    (void)wormedit::ui::chrome::autofocus_text_field(context, NK_EDIT_FIELD,
        state.query.data(), static_cast<int>(state.query.size()), nk_filter_default,
        state.focus_pending);
    if (state.query != previous_query) {
        state.dragged_bookmark = {};
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
    state.dragged_bookmark = {};
    state.focus_pending = true;
}

void worm_ui_nuklear_bookmark_chooser_cancel(nk_context* context,
                                            bookmark_chooser_state_t& state) {
    (void)context;
    state.dragged_bookmark = {};
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
    const auto height = std::min(560.0f, std::max(260.0f, parent.h - 24.0f));
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
    const bool search_current = render_search(context, view, *state);
    const bool sort_current = render_sort_controls(context, view, *state);
    const bool results_current = search_current && sort_current;
    if (results_current && !state->dragged_bookmark) handle_keyboard(context, view, *action, *state);
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
    const bool search_current = render_search(context, view, state);
    const bool sort_current = render_sort_controls(context, view, state);
    if (search_current && sort_current) render_rows(context, view, *action, state);
    else {
        nk_layout_row_dynamic(context, 24, 1);
        nk_label(context, "Updating markers…", NK_TEXT_LEFT);
    }
}
