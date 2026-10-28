#include "ui/nuklear/project/bms_branch_panel.h"

#include "wormedit/nuklear_config.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/text_block.h"
#include "ui/nuklear/chrome/text_fit.h"
#include "ui/nuklear/chrome/tooltip.h"
#include "ui/nuklear/render/lucide_icons.h"
#include "ui/nuklear/render/theme.h"
#include "ui/nuklear/render/vector_icons.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <optional>
#include <set>
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
namespace chrome = wormedit::ui::chrome;
namespace metrics = wormedit::ui::metrics;
using wormedit::ui::theme::UiColor;
using wormedit::ui::vector_icons::Frame;

constexpr float row_height = metrics::control_compact;
constexpr float indent_step = 14.0f;
constexpr float chevron_width = 16.0f;
constexpr float icon_cell = 24.0f;

enum class popup_t { none, add, move };
// UI-only state: folded nodes, and which inline chooser is open for which node.
struct tree_state_t {
    std::set<wormedit::branch_id_t> collapsed;
    popup_t popup{popup_t::none};
    wormedit::branch_id_t popup_node{};
} tree_state;

// Glyphs on Lucide's 24x24 grid, drawn with primitives like vector_icons.h.
void polyline(struct nk_command_buffer* canvas, const Frame& f, std::initializer_list<float> grid,
    nk_color tint) {
    std::vector<float> points;
    for (std::size_t index = 0; index < grid.size(); index += 2)
        points.insert(points.end(), {f.px(*(grid.begin() + index)), f.py(*(grid.begin() + index + 1))});
    nk_stroke_polyline(canvas, points.data(), static_cast<int>(points.size() / 2), f.stroke(), tint);
}
void line(struct nk_command_buffer* canvas, const Frame& f, float x0, float y0, float x1, float y1, nk_color tint) {
    nk_stroke_line(canvas, f.px(x0), f.py(y0), f.px(x1), f.py(y1), f.stroke(), tint);
}
void icon_plus(struct nk_command_buffer* canvas, struct nk_rect bounds, nk_color tint) {
    const auto f = Frame::fit(bounds);
    line(canvas, f, 12, 5, 12, 19, tint);
    line(canvas, f, 5, 12, 19, 12, tint);
}
void icon_move(struct nk_command_buffer* canvas, struct nk_rect bounds, nk_color tint) {
    const auto f = Frame::fit(bounds);
    line(canvas, f, 8, 20, 8, 4, tint);
    polyline(canvas, f, {4, 8, 8, 4, 12, 8}, tint);
    line(canvas, f, 16, 4, 16, 20, tint);
    polyline(canvas, f, {12, 16, 16, 20, 20, 16}, tint);
}
void icon_trash(struct nk_command_buffer* canvas, struct nk_rect bounds, nk_color tint) {
    const auto f = Frame::fit(bounds);
    line(canvas, f, 3, 6, 21, 6, tint);
    polyline(canvas, f, {6, 6, 7, 21, 17, 21, 18, 6}, tint);
    polyline(canvas, f, {9, 6, 9, 3, 15, 3, 15, 6}, tint);
    line(canvas, f, 10, 10, 10, 17, tint);
    line(canvas, f, 14, 10, 14, 17, tint);
}
void icon_unwrap(struct nk_command_buffer* canvas, struct nk_rect bounds, nk_color tint) {
    const auto f = Frame::fit(bounds);
    polyline(canvas, f, {8, 4, 4, 4, 4, 20, 8, 20}, tint);
    polyline(canvas, f, {16, 4, 20, 4, 20, 20, 16, 20}, tint);
    line(canvas, f, 12, 17, 12, 8, tint);
    polyline(canvas, f, {9, 11, 12, 8, 15, 11}, tint);
}
void icon_pencil(struct nk_command_buffer* canvas, struct nk_rect bounds, nk_color tint) {
    const auto f = Frame::fit(bounds);
    polyline(canvas, f, {4, 20, 5, 15, 16, 4, 20, 8, 9, 19, 4, 20}, tint);
}
void icon_chevron(struct nk_command_buffer* canvas, struct nk_rect bounds, bool open, nk_color tint) {
    const auto f = Frame::fit(bounds);
    if (open) polyline(canvas, f, {6, 9, 12, 15, 18, 9}, tint);
    else polyline(canvas, f, {9, 6, 15, 12, 9, 18}, tint);
}
void icon_diamond(struct nk_command_buffer* canvas, const Frame& f, bool filled, nk_color tint) {
    float points[]{f.px(12), f.py(2), f.px(22), f.py(12), f.px(12), f.py(22), f.px(2), f.py(12)};
    if (filled) nk_fill_polygon(canvas, points, 4, tint);
    else nk_stroke_polygon(canvas, points, 4, f.stroke(), tint);
}
void icon_fork(struct nk_command_buffer* canvas, const Frame& f, nk_color tint) {
    line(canvas, f, 6, 3, 6, 15, tint);
    nk_stroke_circle(canvas, nk_rect(f.px(15), f.py(3), 6 * f.scale, 6 * f.scale), f.stroke(), tint);
    nk_stroke_circle(canvas, nk_rect(f.px(3), f.py(15), 6 * f.scale, 6 * f.scale), f.stroke(), tint);
    polyline(canvas, f, {18, 9, 18, 12, 15, 15, 9, 15}, tint);
}
void icon_page(struct nk_command_buffer* canvas, const Frame& f, nk_color tint) {
    nk_stroke_rect(canvas, nk_rect(f.px(5), f.py(3), 14 * f.scale, 18 * f.scale), 2 * f.scale, f.stroke(), tint);
    line(canvas, f, 9, 9, 15, 9, tint);
    line(canvas, f, 9, 13, 15, 13, tint);
    line(canvas, f, 9, 17, 13, 17, tint);
}
void icon_root(struct nk_command_buffer* canvas, const Frame& f, nk_color tint) {
    nk_stroke_circle(canvas, nk_rect(f.px(3), f.py(3), 18 * f.scale, 18 * f.scale), f.stroke(), tint);
    nk_fill_circle(canvas, nk_rect(f.px(8), f.py(8), 8 * f.scale, 8 * f.scale), tint);
}
void draw_kind_icon(nk_context* context, struct nk_rect cell, scope_kind_t kind, nk_color tint) {
    auto* canvas = nk_window_get_canvas(context);
    const auto box = nk_rect(cell.x + (cell.w - icon_cell) * 0.5f, cell.y + (cell.h - icon_cell) * 0.5f,
        icon_cell, icon_cell);
    const auto f = Frame::fit(box);
    switch (kind) {
    case scope_kind_t::root: icon_root(canvas, f, tint); break;
    case scope_kind_t::random: case scope_kind_t::switch_scope: icon_diamond(canvas, f, false, tint); break;
    case scope_kind_t::set_random: case scope_kind_t::set_switch: icon_diamond(canvas, f, true, tint); break;
    case scope_kind_t::if_arm: case scope_kind_t::else_if: case scope_kind_t::else_arm:
    case scope_kind_t::case_arm: case scope_kind_t::default_arm: icon_fork(canvas, f, tint); break;
    case scope_kind_t::chart: wormedit::ui::lucide::draw(context, box, wormedit::ui::lucide::Glyph::music_2, tint); break;
    case scope_kind_t::text: icon_page(canvas, f, tint); break;
    case scope_kind_t::bare: nk_fill_circle(canvas, nk_rect(f.px(9), f.py(9), 6 * f.scale, 6 * f.scale), tint); break;
    }
}

struct row_geometry_t {
    struct nk_rect bounds;
    struct nk_rect chevron;
    struct nk_rect icon;
    struct nk_rect text;
    struct nk_rect dot;
};
auto row_geometry(struct nk_rect bounds, std::size_t depth) -> row_geometry_t {
    const auto left = bounds.x + metrics::gap_small + static_cast<float>(depth) * indent_step;
    const auto dot = nk_rect(bounds.x + bounds.w - 20.0f, bounds.y, 16.0f, bounds.h);
    const auto icon = nk_rect(left + chevron_width, bounds.y, icon_cell - 4.0f, bounds.h);
    const auto text_left = icon.x + icon.w + metrics::gap_small;
    return {bounds, nk_rect(left, bounds.y, chevron_width, bounds.h), icon,
        nk_rect(text_left, bounds.y, std::max(1.0f, dot.x - text_left), bounds.h), dot};
}
void draw_guides(nk_context* context, const row_geometry_t& geometry, std::size_t depth) {
    auto* canvas = nk_window_get_canvas(context);
    const auto tint = chrome::mix_color(chrome::color(UiColor::background), chrome::color(UiColor::text), 22);
    for (std::size_t level = 0; level < depth; ++level) {
        const auto x = geometry.bounds.x + metrics::gap_small + static_cast<float>(level) * indent_step +
            chevron_width * 0.5f;
        nk_stroke_line(canvas, x, geometry.bounds.y - 2.0f, x, geometry.bounds.y + geometry.bounds.h + 2.0f, 1.0f, tint);
    }
}
void draw_label(nk_context* context, struct nk_rect area, const std::string& label, nk_color tint) {
    const auto* font = context->style.font;
    const auto fitted = chrome::fit_text_prefix(label, chrome::font_text_width_t{font}, area.w);
    const auto cut = chrome::text_width(font, label) <= area.w ? label.size() : fitted.value_or(0);
    std::string shown = label.substr(0, cut);
    if (cut < label.size()) shown += std::string{chrome::text_ellipsis};
    const auto box = nk_rect(area.x, area.y + (area.h - font->height) * 0.5f, area.w, font->height);
    nk_draw_text(nk_window_get_canvas(context), box, shown.c_str(), static_cast<int>(shown.size()), font,
        nk_rgba(0, 0, 0, 0), tint);
}
auto row_tooltip(const scope_view_t& view, const wormedit::ui::bms_program_row_t& row, bool target) -> std::string {
    std::string text = row.path;
    text += row.unreachable ? "\nUnreachable: a previous arm always matches." :
        row.playback_active ? "\nPlaying in the current preview." : "\nInactive in the current preview.";
    if (target) text += "\nContent target: notes edit this Chart. Other content stays read-only (gray).";
    else if (row.selected && !view.content_chart) text += "\nNo Chart content here. Select a scope or Chart.";
    return text;
}
void draw_row_state(nk_context* context, const row_geometry_t& geometry, const wormedit::ui::bms_program_row_t& row,
    nk_color tint) {
    auto* canvas = nk_window_get_canvas(context);
    const auto cx = geometry.dot.x + geometry.dot.w * 0.5f;
    const auto cy = geometry.dot.y + geometry.dot.h * 0.5f;
    if (row.unreachable) {
        nk_stroke_triangle(canvas, cx, cy - 6, cx - 6, cy + 5, cx + 6, cy + 5, 1.5f, tint);
        nk_fill_circle(canvas, nk_rect(cx - 1, cy + 1, 2, 2), tint);
    } else if (row.playback_active) {
        nk_fill_circle(canvas, nk_rect(cx - 4, cy - 4, 8, 8), tint);
    } else {
        nk_stroke_circle(canvas, nk_rect(cx - 4, cy - 4, 8, 8), 1.5f, tint);
    }
}
auto row_tint(const wormedit::ui::bms_program_row_t& row) -> nk_color {
    if (row.unreachable) return scope_warning_color;
    return chrome::color(row.selected ? UiColor::selected_text : UiColor::text);
}
void paint_row_background(nk_context* context, const row_geometry_t& geometry,
    const wormedit::ui::bms_program_row_t& row, bool hover, bool target) {
    auto* canvas = nk_window_get_canvas(context);
    if (row.selected) nk_fill_rect(canvas, geometry.bounds, metrics::radius_control, chrome::color(UiColor::selected));
    else if (hover) nk_fill_rect(canvas, geometry.bounds, metrics::radius_control, chrome::color(UiColor::surface_hover));
    if (target)
        nk_fill_rect(canvas, nk_rect(geometry.bounds.x, geometry.bounds.y + 3.0f, 3.0f, geometry.bounds.h - 6.0f), 1.5f,
            chrome::color(UiColor::accent));
}
void render_scope_row(nk_context* context, const scope_view_t& view,
    const wormedit::ui::bms_program_row_t& row, bool has_children, bool target,
    wormedit::ui::workspace_action_t& action) {
    nk_layout_row_dynamic(context, row_height, 1);
    struct nk_rect bounds{};
    const auto cell = nk_widget(&bounds, context);
    if (cell == NK_WIDGET_INVALID) return;
    const auto geometry = row_geometry(bounds, row.depth);
    const auto live = cell == NK_WIDGET_VALID;
    const auto hover = live && nk_input_is_mouse_hovering_rect(&context->input, bounds);
    paint_row_background(context, geometry, row, hover, target);
    draw_guides(context, geometry, row.depth);
    const auto tint = row_tint(row);
    const auto folded = tree_state.collapsed.contains(row.node);
    if (has_children) icon_chevron(nk_window_get_canvas(context), geometry.chevron, !folded, tint);
    draw_kind_icon(context, geometry.icon, row.kind, tint);
    draw_label(context, geometry.text, row.label, tint);
    if (target) icon_pencil(nk_window_get_canvas(context), nk_rect(geometry.dot.x - 18.0f, bounds.y, 16.0f, bounds.h), tint);
    draw_row_state(context, geometry, row, tint);
    if (hover && wormedit::ui::nuklear::tooltip::hovered(context, bounds))
        wormedit::ui::nuklear::tooltip::text(context, row_tooltip(view, row, target));
    if (!live || !nk_input_mouse_clicked(&context->input, NK_BUTTON_LEFT, bounds)) return;
    nk_edit_unfocus(context);
    if (has_children && nk_input_is_mouse_hovering_rect(&context->input, geometry.chevron)) {
        if (folded) tree_state.collapsed.erase(row.node);
        else tree_state.collapsed.insert(row.node);
    } else {
        emit_scope_action(action, view, scope_operation_t::select, row.node);
    }
}

// Both choosers and the tree sit in the same inset: no border and no group padding of their own,
// so every control in the panel shares the dock's left and right edge.
auto inset_group_begin(nk_context* context, const char* name, float height) -> bool {
    nk_layout_row_dynamic(context, height, 1);
    const auto area = nk_widget_bounds(context);
    nk_fill_rect(nk_window_get_canvas(context), area, metrics::radius_bar, chrome::section_card_color());
    nk_style_push_vec2(context, &context->style.window.group_padding, nk_vec2(0, metrics::gap_small * 0.5f));
    nk_style_push_style_item(context, &context->style.window.fixed_background, nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    const auto open = nk_group_begin(context, name, 0) != 0;
    nk_style_pop_style_item(context);
    nk_style_pop_vec2(context);
    return open;
}
auto inset_height(nk_context* context, std::size_t rows, float row_extent, float limit) -> float {
    const auto content = static_cast<float>(rows) * (row_extent + context->style.window.spacing.y) + metrics::gap_medium;
    return std::clamp(content, std::min(96.0f, limit), std::max(96.0f, limit));
}
auto has_visible_children(const scope_view_t& view, std::size_t index) -> bool {
    return index + 1 < view.rows.size() && view.rows[index + 1].depth > view.rows[index].depth;
}
void render_scope_tree(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    std::vector<std::size_t> visible;
    std::optional<std::size_t> folded_depth;
    for (std::size_t index = 0; index < view.rows.size(); ++index) {
        const auto& row = view.rows[index];
        if (folded_depth && row.depth > *folded_depth) continue;
        folded_depth = tree_state.collapsed.contains(row.node) && has_visible_children(view, index) ?
            std::optional{row.depth} : std::nullopt;
        visible.push_back(index);
    }
    const auto limit = nk_window_get_content_region(context).h * 0.5f;
    if (!inset_group_begin(context, "Scope tree", inset_height(context, visible.size(), row_height, limit))) return;
    for (const auto index : visible) {
        const auto& row = view.rows[index];
        const auto target = view.content_chart && *view.content_chart == row.node;
        render_scope_row(context, view, row, has_visible_children(view, index), target, action);
    }
    nk_group_end(context);
}
void open_popup(const scope_view_t& view, popup_t which) {
    tree_state.popup = tree_state.popup == which ? popup_t::none : which;
    tree_state.popup_node = view.selected;
}
void render_scope_bar(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    nk_layout_row_template_begin(context, metrics::control_default);
    for (int index = 0; index < 4; ++index) nk_layout_row_template_push_static(context, chrome::IconButton::width);
    nk_layout_row_template_end(context);
    if (chrome::DrawnIconButton{icon_plus, "Add node…", tree_state.popup == popup_t::add}.render(context))
        open_popup(view, popup_t::add);
    if (chrome::DrawnIconButton{icon_move, "Move node…", tree_state.popup == popup_t::move}.render(context))
        open_popup(view, popup_t::move);
    if (chrome::DrawnIconButton{icon_trash, view.can_delete ? "Delete…" : "Delete (not available for this node)",
            false, view.can_delete}.render(context))
        emit_scope_action(action, view, scope_operation_t::prepare_delete);
    if (chrome::DrawnIconButton{icon_unwrap, view.can_unwrap ? "Unwrap… (remove this scope, keep its content)" :
            "Unwrap (not available for this node)", false, view.can_unwrap}.render(context))
        emit_scope_action(action, view, scope_operation_t::prepare_unwrap);
}
void render_scope_add(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    if (tree_state.popup != popup_t::add) return;
    std::vector<const scope_kind_t*> kinds;
    std::vector<const char*> labels;
    for (const auto& kind : wormedit::ui::bms_program_kinds) {
        if (!kind.can_add) continue;
        kinds.push_back(&kind.kind);
        labels.push_back(kind.label.data());
    }
    const auto result = chrome::ChipGrid{.labels = labels.data(), .count = static_cast<int>(labels.size()),
        .chosen = -1, .columns = 3}.render(context);
    chrome::group_gap(context);
    if (result.chosen < 0) return;
    emit_scope_action(action, view, scope_operation_t::add);
    action.bms_program.kind = *kinds[static_cast<std::size_t>(result.chosen)];
    action.bms_program.parent = view.selected;
    // Runtime resolves a scope's last child, or the selected leaf's parent/anchor.
    tree_state.popup = popup_t::none;
}
void render_scope_move(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    if (tree_state.popup != popup_t::move) return;
    const auto candidates = view.rows.size() - 1;
    if (!inset_group_begin(context, "Move targets", inset_height(context, candidates, row_height,
            nk_window_get_content_region(context).h * 0.4f))) return;
    for (const auto& row : view.rows) {
        if (row.node == view.selected) continue;
        const auto inside = row.kind != scope_kind_t::chart && row.kind != scope_kind_t::text &&
            row.kind != scope_kind_t::set_random && row.kind != scope_kind_t::set_switch;
        const auto title = std::string{inside ? "Inside " : "After "} + row.path;
        nk_layout_row_dynamic(context, row_height, 1);
        if (!chrome::Button{title.c_str(), chrome::ButtonKind::ghost, title.c_str()}.render(context)) continue;
        emit_scope_action(action, view, scope_operation_t::move, row.node);
        action.bms_program.parent = inside ? row.node : wormedit::branch_id_t{};
        action.bms_program.after = inside ? std::nullopt : std::optional{row.node};
        tree_state.popup = popup_t::none;
    }
    nk_group_end(context);
}
void render_scope_pair(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action,
    chrome::Button first, scope_operation_t first_operation, chrome::Button second, scope_operation_t second_operation) {
    nk_layout_row_dynamic(context, metrics::control_default, 2);
    if (first.render(context)) { nk_edit_unfocus(context); emit_scope_action(action, view, first_operation); }
    if (second.render(context)) { nk_edit_unfocus(context); emit_scope_action(action, view, second_operation); }
}
void render_scope_detail(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    const auto selected = std::ranges::find(view.rows, view.selected, &wormedit::ui::bms_program_row_t::node);
    const std::string title = selected != view.rows.end() ? selected->label : "Node";
    constexpr char hash[] = "scope-node-detail";
    if (!chrome::section_push(context, NK_TREE_NODE, title.c_str(), NK_MAXIMIZED, hash, sizeof(hash) - 1, 0)) return;
    if (view.has_parameter) {
        nk_layout_row_dynamic(context, 20.0f, 1);
        nk_label(context, selected != view.rows.end() && selected->kind == scope_kind_t::random ?
            "Outcome count" : "Value (DEF = default case)", NK_TEXT_LEFT);
        scope_text_input(context, view, action, view.parameter, scope_operation_t::draft_parameter);
    }
    if (view.has_text) scope_text_input(context, view, action, view.text, scope_operation_t::draft_text, true);
    render_scope_pair(context, view, action, {"Save", chrome::ButtonKind::primary, "Save this node", !view.stale_draft},
        scope_operation_t::save, {"Reload", chrome::ButtonKind::secondary, "Discard the draft and reload from the document"},
        scope_operation_t::reload);
    chrome::section_pop(context);
}
void render_scope_preview(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    constexpr char hash[] = "scope-preview-detail";
    if (!chrome::section_push(context, NK_TREE_NODE, "Playback preview", NK_MAXIMIZED, hash, sizeof(hash) - 1, 0)) return;
    nk_layout_row_dynamic(context, 20.0f, 1);
    if (view.playback_choice) nk_labelf(context, NK_TEXT_LEFT, "Choice: %lld", static_cast<long long>(*view.playback_choice));
    else nk_label(context, "Choice: unresolved", NK_TEXT_LEFT);
    scope_text_input(context, view, action, view.preview, scope_operation_t::draft_preview);
    nk_layout_row_dynamic(context, metrics::control_default, 2);
    if (chrome::Button{"Set", chrome::ButtonKind::primary, "Pin this outcome for the preview"}.render(context))
        emit_scope_action(action, view, scope_operation_t::set_preview, {}, view.preview);
    if (chrome::Button{"Clear", chrome::ButtonKind::secondary, "Go back to the resolved outcome"}.render(context))
        emit_scope_action(action, view, scope_operation_t::clear_preview);
    chrome::section_pop(context);
}
void render_scope_confirmation(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    if (view.confirmation.empty()) return;
    scope_message(context, view.confirmation.c_str(), true);
    const auto operation = view.confirmation_operation.value_or(scope_operation_t::cancel);
    const auto destructive = operation == scope_operation_t::confirm_delete;
    nk_layout_row_dynamic(context, metrics::control_default, 1);
    if (chrome::Button{view.unconditional_conversion ? "Make branches always apply" : "Confirm",
            destructive ? chrome::ButtonKind::destructive : chrome::ButtonKind::primary}.render(context))
        emit_scope_action(action, view, operation);
    nk_layout_row_dynamic(context, metrics::control_default, 1);
    if (chrome::Button{"Cancel", chrome::ButtonKind::secondary}.render(context))
        emit_scope_action(action, view, scope_operation_t::cancel);
}
void render_scope_panel(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    if (tree_state.popup != popup_t::none && tree_state.popup_node != view.selected) tree_state.popup = popup_t::none;
    render_scope_bar(context, view, action);
    render_scope_add(context, view, action);
    render_scope_move(context, view, action);
    chrome::group_gap(context);
    render_scope_tree(context, view, action);
    chrome::group_gap(context);
    if (!view.content_chart) {
        nk_style_push_color(context, &context->style.text.color, chrome::text_faint());
        chrome::text_block_t{.text = "No Chart content here. Select a scope or Chart."}.render(context);
        nk_style_pop_color(context);
    }
    if (view.stale_draft) scope_message(context, "The document changed. Reload before saving this draft.", true);
    if (view.has_parameter || view.has_text) render_scope_detail(context, view, action);
    if (view.can_preview) render_scope_preview(context, view, action);
    render_scope_confirmation(context, view, action);
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
