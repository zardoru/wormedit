#include "ui/nuklear/project/bms_branch_panel.h"

#include "wormedit/nuklear_config.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/text_block.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {
using scope_operation_t = wormedit::ui::bms_program_operation_t;
using scope_kind_t = wormedit::ui::bms_program_node_kind_t;
using scope_view_t = wormedit::ui::bms_program_view_t;
// Warnings use the theme's conflict role so they stay legible on light and dark panels.
auto scope_warning_color() -> nk_color {
    return wormedit::ui::chrome::color(wormedit::ui::chrome::UiColor::conflict);
}

void emit_scope_action(wormedit::ui::workspace_action_t& action, const scope_view_t& view,
    scope_operation_t operation, wormedit::branch_id_t node = {}, std::string text = {}) {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action.compose_action = WORM_UI_COMPOSE_ACTION_BMS_PROGRAM;
    action.bms_program = {.project = view.project, .difficulty = view.difficulty, .revision = view.revision,
        .selected = view.selected, .operation = operation, .node = node, .text = std::move(text)};
}
void scope_warning_icon(nk_context* context, nk_color color) {
    struct nk_rect bounds{};
    if (!nk_widget(&bounds, context)) return;
    const auto x = bounds.x + bounds.w * 0.5f;
    const auto y = bounds.y + 3.0f;
    auto* canvas = nk_window_get_canvas(context);
    nk_stroke_triangle(canvas, x, y, x - 7, y + 14, x + 7, y + 14, 1.5f, color);
    nk_stroke_line(canvas, x, y + 4, x, y + 8, 1.5f, color);
    nk_fill_circle(canvas, nk_rect(x - 1, y + 10, 2, 2), color);
}
void scope_message(nk_context* context, const char* text, bool warning, bool error = false) {
    const auto color = error ? nk_rgb(255, 130, 130) : warning ? scope_warning_color() : context->style.text.color;
    nk_style_push_color(context, &context->style.text.color, color);
    if (warning) {
        nk_layout_row_begin(context, NK_DYNAMIC, 20.0f, 2);
        nk_layout_row_push(context, 0.09f); scope_warning_icon(context, color);
        nk_layout_row_push(context, 0.91f); nk_label(context, "Warning", NK_TEXT_LEFT);
        nk_layout_row_end(context);
    }
    wormedit::ui::chrome::text_block_t{.text = text}.render(context);
    nk_style_pop_color(context);
}
void scope_button(nk_context* context, const char* label, const scope_view_t& view,
    wormedit::ui::workspace_action_t& action, scope_operation_t operation, bool enabled = true) {
    const auto clicked = wormedit::ui::chrome::Button{.label = label, .enabled = enabled}.render(context);
    if (clicked) { nk_edit_unfocus(context); emit_scope_action(action, view, operation); }
}
void scope_text_input(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action,
    const std::string& value, scope_operation_t operation, bool multiline = false) {
    constexpr std::size_t maximum = 1024U * 1024U;
    if (value.size() >= maximum) {
        scope_message(context, "Text exceeds the panel editor capacity; the original remains intact.", true);
        return;
    }
    std::vector<char> buffer(std::min(maximum, std::max(std::size_t{8192}, value.size() + 4096)), '\0');
    std::copy(value.begin(), value.end(), buffer.begin());
    nk_layout_row_dynamic(context, multiline ? 96.0f : 25.0f, 1);
    nk_edit_string_zero_terminated(context, multiline ? NK_EDIT_BOX : NK_EDIT_FIELD, buffer.data(),
        static_cast<int>(buffer.size()), nk_filter_default);
    if (value != buffer.data()) emit_scope_action(action, view, operation, {}, std::string{buffer.data()});
}
void render_scope_row(nk_context* context, const scope_view_t& view,
    const wormedit::ui::bms_program_row_t& row, wormedit::ui::workspace_action_t& action) {
    std::string text(std::min(row.depth, std::size_t{24}) * 2, ' ');
    if (row.kind != scope_kind_t::text && row.kind != scope_kind_t::set_random && row.kind != scope_kind_t::set_switch)
        text += row.unreachable ? "Warning: " : row.playback_active ? "Playing: " : "Inactive: ";
    text += row.label;
    const auto color = row.unreachable ? scope_warning_color() : context->style.text.color;
    const std::array text_colors{&context->style.selectable.text_normal, &context->style.selectable.text_hover,
        &context->style.selectable.text_pressed, &context->style.selectable.text_normal_active,
        &context->style.selectable.text_hover_active, &context->style.selectable.text_pressed_active};
    // A selected row sits on the theme's selection fill and takes its text color.
    const auto selected_color = wormedit::ui::chrome::color(wormedit::ui::chrome::UiColor::selected_text);
    for (std::size_t index = 0; index < text_colors.size(); ++index)
        nk_style_push_color(context, text_colors[index], index < 3 ? color : selected_color);
    nk_layout_row_begin(context, NK_DYNAMIC, 23.0f, row.unreachable ? 2 : 1);
    if (row.unreachable) { nk_layout_row_push(context, 0.09f); scope_warning_icon(context, color); }
    nk_layout_row_push(context, row.unreachable ? 0.91f : 1.0f);
    nk_bool selected = row.selected;
    if (nk_selectable_label(context, text.c_str(), NK_TEXT_LEFT, &selected)) {
        nk_edit_unfocus(context);
        emit_scope_action(action, view, scope_operation_t::select, row.node);
    }
    nk_layout_row_end(context);
    for (std::size_t index = 0; index < text_colors.size(); ++index) nk_style_pop_color(context);
}
void render_scope_tree(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    nk_layout_row_dynamic(context, 230.0f, 1);
    if (!nk_group_begin(context, "Scope tree", NK_WINDOW_BORDER)) return;
    for (const auto& row : view.rows) render_scope_row(context, view, row, action);
    nk_group_end(context);
}
void render_scope_draft(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    if (view.stale_draft) scope_message(context, "The document changed. Reload before saving this draft.", true);
    if (view.has_parameter) {
        const auto selected = std::ranges::find(view.rows, view.selected, &wormedit::ui::bms_program_row_t::node);
        nk_layout_row_dynamic(context, 20.0f, 1);
        nk_label(context, selected != view.rows.end() && selected->kind == scope_kind_t::random ?
            "RANDOM range (outcome count)" : "Node value (DEF for default case)", NK_TEXT_LEFT);
        scope_text_input(context, view, action, view.parameter, scope_operation_t::draft_parameter);
    }
    if (view.has_text) scope_text_input(context, view, action, view.text, scope_operation_t::draft_text, true);
    if (view.has_parameter || view.has_text) {
        nk_layout_row_dynamic(context, 25.0f, 2);
        scope_button(context, "Save node", view, action, scope_operation_t::save, !view.stale_draft);
        scope_button(context, "Reload / discard", view, action, scope_operation_t::reload);
    }
    if (view.can_preview) {
        nk_layout_row_dynamic(context, 20.0f, 1);
        if (view.playback_choice) nk_labelf(context, NK_TEXT_LEFT, "Playback preview choice: %lld", static_cast<long long>(*view.playback_choice));
        else nk_label(context, "Playback preview choice: unresolved", NK_TEXT_LEFT);
        scope_text_input(context, view, action, view.preview, scope_operation_t::draft_preview);
        nk_layout_row_dynamic(context, 25.0f, 2);
        if (nk_button_label(context, "Set preview")) emit_scope_action(action, view, scope_operation_t::set_preview, {}, view.preview);
        scope_button(context, "Clear preview", view, action, scope_operation_t::clear_preview);
    }
}
void render_scope_add(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    nk_layout_row_dynamic(context, 25.0f, 1);
    if (!wormedit::ui::chrome::safe_combo_begin_label(context, "Add node…", nk_vec2(240, 260))) return;
    for (const auto& kind : wormedit::ui::bms_program_kinds) {
        if (!kind.can_add) continue;
        nk_layout_row_dynamic(context, 23, 1);
        if (!nk_button_label(context, kind.label.data())) continue;
        emit_scope_action(action, view, scope_operation_t::add);
        action.bms_program.kind = kind.kind;
        action.bms_program.parent = view.selected;
        // Runtime resolves a scope's last child, or the selected leaf's parent/anchor.
        nk_combo_close(context);
    }
    wormedit::ui::chrome::safe_combo_end(context);
}
void render_scope_move(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    nk_layout_row_dynamic(context, 25.0f, 1);
    if (!wormedit::ui::chrome::safe_combo_begin_label(context, "Move node…", nk_vec2(260, 250))) return;
    for (const auto& row : view.rows) {
        if (row.node == view.selected) continue;
        const auto inside = row.kind != scope_kind_t::chart && row.kind != scope_kind_t::text &&
            row.kind != scope_kind_t::set_random && row.kind != scope_kind_t::set_switch;
        const auto title = std::string{inside ? "Inside " : "After "} + row.path;
        nk_layout_row_dynamic(context, 23, 1);
        if (!nk_button_label(context, title.c_str())) continue;
        emit_scope_action(action, view, scope_operation_t::move, row.node);
        action.bms_program.parent = inside ? row.node : wormedit::branch_id_t{};
        action.bms_program.after = inside ? std::nullopt : std::optional{row.node};
        nk_combo_close(context);
    }
    wormedit::ui::chrome::safe_combo_end(context);
}
void render_scope_panel(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    render_scope_tree(context, view, action);
    scope_message(context, ("Selected node: " + view.target_path).c_str(), false);
    if (view.content_chart) {
        const auto row = std::ranges::find(view.rows, *view.content_chart, &wormedit::ui::bms_program_row_t::node);
        if (row != view.rows.end()) scope_message(context, ("Content target: " + row->path).c_str(), false);
    }
    scope_message(context, view.content_chart ? "Common and ancestor content is read-only (gray). Select its owning node to edit it." :
        "This node has no Chart content. Select a scope or Chart for content editing.", false);
    render_scope_draft(context, view, action);
    render_scope_add(context, view, action);
    render_scope_move(context, view, action);
    nk_layout_row_dynamic(context, 25.0f, 2);
    scope_button(context, "Delete…", view, action, scope_operation_t::prepare_delete, view.can_delete);
    scope_button(context, "Unwrap…", view, action, scope_operation_t::prepare_unwrap, view.can_unwrap);
    if (!view.confirmation.empty()) {
        scope_message(context, view.confirmation.c_str(), true);
        nk_layout_row_dynamic(context, view.unconditional_conversion ? 42.0f : 25.0f, 1);
        scope_button(context, view.unconditional_conversion ? "Make branches always apply" : "Confirm", view, action, view.confirmation_operation.value_or(scope_operation_t::cancel));
        nk_layout_row_dynamic(context, 25.0f, 1);
        scope_button(context, "Cancel", view, action, scope_operation_t::cancel);
    }
    for (const auto& message : view.messages) scope_message(context, message.text.c_str(), message.warning, message.error);
}
} // namespace

bool worm_ui_nuklear_bms_branch_panel_visible(const wormedit::ui::compose_view_t* view) {
    return view && view->bms_program.available;
}

void worm_ui_nuklear_render_bms_branch_panel(struct nk_context* context,
    const wormedit::ui::compose_view_t* view, wormedit::ui::workspace_action_t* action) {
    if (!context || !view || !action || !worm_ui_nuklear_bms_branch_panel_visible(view)) return;
    render_scope_panel(context, view->bms_program, *action);
}
