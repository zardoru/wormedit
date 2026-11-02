#include "ui/nuklear/storyboard/hierarchy.h"
#include "ui/nuklear/storyboard/storyboard_panel.h"
#include "ui/nuklear/chrome/text_layout.h"
#include "ui/frontend/actions.h"
#include "ui/frontend/storyboard_hierarchy.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/text_input.h"
#include "ui/nuklear/render/vector_icons.h"
#include <algorithm>
#include <array>
#include <iterator>
#include <cmath>
#include <optional>
#include <utility>

namespace wormedit::ui {
namespace {
constexpr std::string_view sprite_label = "+ Sprite";
auto has_hierarchy_pointer_input(const nk_context* context) -> bool {
    return (context->current->layout->flags & (static_cast<nk_flags>(NK_WINDOW_ROM) | NK_WINDOW_NO_INPUT)) == 0 &&
        !context->current->widgets_disabled &&
        nk_input_is_mouse_hovering_rect(&context->input, context->current->layout->clip);
}
// Transient widget capture only. Selection and editor visibility belong to the
// canonical project runtime, and pointer motion never changes document data.
class hierarchy_widget_t {
  public:
    void cancel() { rename_.reset(); drag_.reset(); target_.reset(); focused_ = false; }
    void bind(const storyboard_view_t& view, bool input_available) {
        if (!input_available || project_ != view.project || revision_ != view.revision ||
            session_ != view.session_generation || prepared_ != view.canvas.prepared_generation ||
            storyboard_ != view.state.selection.storyboard)
            cancel();
        project_ = view.project;
        revision_ = view.revision;
        session_ = view.session_generation;
        prepared_ = view.canvas.prepared_generation;
        if (rename_ && std::ranges::find(view.state.selection.nodes, rename_->hierarchy.node) ==
                view.state.selection.nodes.end()) rename_.reset();
        storyboard_ = view.state.selection.storyboard;
    }
    void begin_rename(const storyboard_view_t& view, const storyboard_hierarchy_row_t& row) {
        if (!row.editable) return;
        rename_ = storyboard_action_t::from(view, storyboard_action_kind_t::hierarchy, storyboard_);
        rename_->hierarchy = {.kind = storyboard_hierarchy_mutation_kind_t::rename, .node = row.id};
        text_.fill(0);
        std::copy_n(row.id.data(), std::min(row.id.size(), text_.size() - 1), text_.data());
        focus_pending_ = true;
    }
    auto is_renaming(std::string_view id) const -> bool { return rename_ && rename_->hierarchy.node == id; }
    void render_rename(nk_context* context, storyboard_action_t& action) {
        const auto flags = chrome::autofocus_text_field(context,
            static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT, text_.data(),
            static_cast<int>(text_.size()), nk_filter_default, focus_pending_);
        if ((flags & NK_EDIT_COMMITTED) == 0) {
            if ((flags & NK_EDIT_DEACTIVATED) != 0) rename_.reset();
            return;
        }
        action = *rename_;
        action.hierarchy.text = text_.data();
        rename_.reset();
    }
    void focus() { focused_ = true; }
    void blur() { focused_ = false; }
    auto has_focus() const -> bool { return focused_; }
    void prepare_drag(nk_context* context) {
        if (nk_input_is_key_pressed(&context->input, NK_KEY_TEXT_RESET_MODE)) {
            if (rename_) nk_edit_unfocus(context);
            cancel();
        }
        if (!drag_) return;
        target_.reset();
        const auto mouse = context->input.mouse.pos;
        moved_ = moved_ || std::hypot(mouse.x - drag_start_.x, mouse.y - drag_start_.y) >= 3.0f;
    }
    void begin_drag(nk_context* context, const storyboard_view_t& view,
                    const storyboard_hierarchy_row_t& row, struct nk_rect bounds) {
        if (!row.editable || rename_ || !has_hierarchy_pointer_input(context) ||
            !nk_input_is_mouse_hovering_rect(&context->input, bounds) ||
            !nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT)) return;
        drag_ = storyboard_action_t::from(view, storyboard_action_kind_t::hierarchy, storyboard_);
        drag_->hierarchy = {.kind = storyboard_hierarchy_mutation_kind_t::drop, .node = row.id};
        drag_start_ = context->input.mouse.pos;
        moved_ = false;
        focused_ = true;
    }
    void target(nk_context* context, const storyboard_hierarchy_row_t& row,
                const storyboard_hierarchy_layer_t& layer, struct nk_rect bounds) {
        if (!drag_ || !moved_ || !has_hierarchy_pointer_input(context) || drag_->hierarchy.node == row.id ||
            !nk_input_is_mouse_hovering_rect(&context->input, context->current->buffer.clip) ||
            !nk_input_is_mouse_hovering_rect(&context->input, bounds)) return;
        target_ = *drag_;
        target_->hierarchy.layer = layer.layer;
        const auto relative = (context->input.mouse.pos.y - bounds.y) / bounds.h;
        if (row.is_group && relative > 0.25f && relative < 0.75f) {
            target_->hierarchy.parent = row.id;
            target_->hierarchy.before.reset();
        } else {
            target_->hierarchy.parent = row.parent;
            target_->hierarchy.before = row.id;
            if (relative >= 0.5f) target_->hierarchy.before = next_sibling(layer, row);
        }
        nk_stroke_rect(nk_window_get_canvas(context), bounds, 2, 1.5f,
                       chrome::color(chrome::UiColor::accent));
    }
    void target_layer(nk_context* context, osbx_layer_t layer, struct nk_rect bounds) {
        if (!drag_ || !moved_ || !has_hierarchy_pointer_input(context) ||
            !nk_input_is_mouse_hovering_rect(&context->input, bounds) ||
            !nk_input_is_mouse_hovering_rect(&context->input, context->current->buffer.clip)) return;
        target_ = *drag_;
        target_->hierarchy.layer = layer;
        target_->hierarchy.parent.reset();
        target_->hierarchy.before.reset();
        nk_stroke_rect(nk_window_get_canvas(context), bounds, 2, 1.5f,
                       chrome::color(chrome::UiColor::accent));
    }
    void finish_drag(nk_context* context, storyboard_action_t& action) {
        if (!drag_ || context->input.mouse.buttons[NK_BUTTON_LEFT].down) return;
        if (target_ && nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT) &&
            action.kind == storyboard_action_kind_t::none) action = *target_;
        drag_.reset();
        target_.reset();
    }
    auto has_rename() const -> bool { return rename_.has_value(); }
  private:
    song_id_t project_;
    std::uint64_t revision_{}, session_{}, prepared_{};
    storyboard_id_t storyboard_;
    bool focused_{}, focus_pending_{}, moved_{};
    std::array<char, 1025> text_{};
    std::optional<storyboard_action_t> rename_, drag_, target_;
    struct nk_vec2 drag_start_{};
    static auto next_sibling(const storyboard_hierarchy_layer_t& layer,
                              const storyboard_hierarchy_row_t& row) -> std::optional<std::string> {
        auto next = std::ranges::find(layer.rows, row.id, &storyboard_hierarchy_row_t::id);
        if (next == layer.rows.end()) return {};
        for (++next; next != layer.rows.end(); ++next) {
            if (next->depth < row.depth) break;
            if (next->parent == row.parent) return next->id;
        }
        return {};
    }
};
hierarchy_widget_t widget;

auto node_action(const storyboard_view_t& view, storyboard_hierarchy_mutation_kind_t kind,
                  std::string id) -> storyboard_action_t {
    auto action = storyboard_action_t::from(view, storyboard_action_kind_t::hierarchy,
                                           view.state.selection.storyboard);
    action.hierarchy = {.kind = kind, .node = std::move(id)};
    return action;
}
auto node_selection(const storyboard_view_t& view, std::string id) -> storyboard_action_t {
    auto action = storyboard_action_t::from(view, storyboard_action_kind_t::select_nodes,
                                           view.state.selection.storyboard);
    action.nodes.push_back(std::move(id));
    return action;
}
auto selected_row(const storyboard_view_t& view) -> const storyboard_hierarchy_row_t* {
    if (view.state.selection.nodes.empty()) return nullptr;
    for (const auto& layer : view.hierarchy.layers) {
        const auto found = std::ranges::find(layer.rows, view.state.selection.nodes.front(),
                                             &storyboard_hierarchy_row_t::id);
        if (found != layer.rows.end()) return &*found;
    }
    return nullptr;
}
void set_add_parent(const storyboard_view_t& view, storyboard_action_t& action) {
    const auto* row = selected_row(view);
    if (row && row->layer == action.hierarchy.layer) action.hierarchy.parent = row->id;
}
void render_layer_move(nk_context* context, const storyboard_view_t& view, storyboard_action_t& action) {
    const auto* row = selected_row(view);
    nk_layout_row_dynamic(context, 24, 1);
    if (!row || !row->editable) return;
    if (!chrome::safe_combo_begin_label(context, "Move to layer", nk_vec2(220, 150))) return;
    nk_layout_row_dynamic(context, 24, 1);
    for (const auto& layer : view.hierarchy.layers) {
        if (nk_combo_item_label(context, layer.title.empty() ? "" : layer.title.data(), NK_TEXT_LEFT)) {
            action = node_action(view, storyboard_hierarchy_mutation_kind_t::set_layer, row->id);
            action.hierarchy.layer = layer.layer;
        }
    }
    chrome::safe_combo_end(context);
}
void render_sprite_choices(nk_context* context, const storyboard_view_t& view,
                           const storyboard_hierarchy_layer_t& layer, storyboard_action_t& action,
                           bool editable) {
    const auto bounds = nk_widget_bounds(context);
    if (!editable || view.inspector.images.empty()) {
        nk_widget_disable_begin(context);
        (void)nk_button_label(context, sprite_label.data());
        nk_widget_disable_end(context);
        if (view.inspector.images.empty() && nk_input_is_mouse_hovering_rect(&context->input, bounds))
            nuklear::tooltip::text(context, "Add an image asset before creating a sprite.");
        return;
    }
    if (!chrome::safe_combo_begin_label(context, sprite_label.data(), nk_vec2(260, 220))) return;
    nk_layout_row_dynamic(context, 24, 1);
    for (const auto& image : view.inspector.images) {
        if (!nk_combo_item_label(context, image.label.c_str(), NK_TEXT_LEFT)) continue;
        action = node_action(view, storyboard_hierarchy_mutation_kind_t::add_sprite, {});
        action.hierarchy.layer = layer.layer;
        action.hierarchy.image = image.id;
        set_add_parent(view, action);
    }
    chrome::safe_combo_end(context);
}
void render_layer_heading(nk_context* context, const storyboard_view_t& view,
                          const storyboard_hierarchy_layer_t& layer, storyboard_action_t& action,
                          bool input_available) {
    const auto* selected = view.selected();
    const bool editable = input_available && selected && selected->is_editable();
    const auto sprite_width = chrome::text_width(context->style.font, sprite_label) +
        24 + 2 * context->style.combo.content_padding.x;
    const auto required = chrome::text_width(context->style.font, layer.title) +
        2 * context->style.text.padding.x + chrome::IconButton::width + sprite_width +
        2 * context->style.window.spacing.x;
    const bool wrapped = nk_window_get_content_region(context).w < required;
    nk_layout_row_template_begin(context, 24);
    nk_layout_row_template_push_dynamic(context);
    if (!wrapped) {
        chrome::IconButton::layout_columns(context, 1);
        nk_layout_row_template_push_static(context, sprite_width);
    }
    nk_layout_row_template_end(context);
    auto layer_bounds = nk_widget_bounds(context);
    layer_bounds.w = nk_window_get_content_region(context).w;
    nk_label(context, layer.title.empty() ? "" : layer.title.data(), NK_TEXT_LEFT);
    if (input_available) widget.target_layer(context, layer.layer, layer_bounds);
    if (wrapped) {
        nk_layout_row_template_begin(context, 24);
        chrome::IconButton::layout_columns(context, 1);
        nk_layout_row_template_push_dynamic(context);
        nk_layout_row_template_end(context);
    }
    const auto add_tip = "Add group in " + std::string{layer.title};
    if (chrome::DrawnIconButton{.draw = vector_icons::plus, .tooltip = add_tip.c_str(),
        .enabled = editable}.render(context)) {
        action = node_action(view, storyboard_hierarchy_mutation_kind_t::add_group, {});
        action.hierarchy.layer = layer.layer;
        set_add_parent(view, action);
    }
    render_sprite_choices(context, view, layer, action, editable);
    if (!layer.shown_in_state) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label_colored(context, "Not shown in this state", NK_TEXT_LEFT,
            chrome::color(chrome::UiColor::text_muted));
    }
    if (layer.rows.empty()) {
        nk_layout_row_dynamic(context, 22, 1);
        const auto* empty = layer.layer == osbx_layer_t::fail ? "Empty — shown when failing"
            : layer.layer == osbx_layer_t::pass ? "Empty — shown when passing" : "Empty layer";
        nk_label_colored(context, empty, NK_TEXT_LEFT, chrome::color(chrome::UiColor::text_muted));
    }
}
void render_node_label(nk_context* context, const storyboard_view_t& view,
                       const storyboard_hierarchy_row_t& row, storyboard_action_t& action, bool input_available) {
    if (widget.is_renaming(row.id)) {
        widget.render_rename(context, action);
        return;
    }
    const auto bounds = nk_widget_bounds(context);
    int selected = row.selected;
    const auto label = std::string(std::min<std::size_t>(row.depth, 8) * 2, ' ') + (row.lua_generated ? "ƒ " : "") + row.id;
    if (!input_available) nk_widget_disable_begin(context);
    const bool clicked = nk_selectable_label(context, label.c_str(), NK_TEXT_LEFT, &selected);
    if (!input_available) nk_widget_disable_end(context);
    if (clicked && input_available) {
        action = node_selection(view, row.id);
        widget.focus();
    }
    if (!row.warning.empty() && nk_input_is_mouse_hovering_rect(&context->input, bounds))
        nuklear::tooltip::text(context, row.warning);
}
void render_node(nk_context* context, const storyboard_view_t& view,
                 const storyboard_hierarchy_layer_t& layer, const storyboard_hierarchy_row_t& row,
                 storyboard_action_t& action, bool input_available) {
    nk_layout_row_template_begin(context, 24);
    nk_layout_row_template_push_static(context, 14);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_static(context, 14);
    chrome::IconButton::layout_columns(context, 1);
    nk_layout_row_template_end(context);
    const auto bounds = nk_widget_bounds(context);
    const auto grip = nk_widget_bounds(context);
    const auto drag_tip = "Drag node " + row.id;
    (void)chrome::IconButton{.glyph = lucide::Glyph::grip_vertical, .tooltip = drag_tip.c_str(),
        .enabled = input_available && row.editable}.render(context);
    if (input_available) widget.begin_drag(context, view, row, grip);
    render_node_label(context, view, row, action, input_available);
    nk_label_colored(context, row.warning.empty() ? "" : "!", NK_TEXT_LEFT,
                     chrome::color(chrome::UiColor::conflict));
    const auto eye_tip = std::string(row.editor_hidden ? "Show " : "Hide ") + row.id + " in editor";
    if (chrome::IconButton{.glyph = row.editor_hidden ? lucide::Glyph::eye_off : lucide::Glyph::eye,
        .tooltip = eye_tip.c_str(),
        .enabled = input_available}.render(context))
        action = node_action(view, storyboard_hierarchy_mutation_kind_t::toggle_editor_visibility, row.id);
    auto target_bounds = bounds;
    target_bounds.w = nk_window_get_content_region(context).w;
    if (input_available) widget.target(context, row, layer, target_bounds);
}
} // namespace

void cancel_storyboard_hierarchy_input() { widget.cancel(); }

void render_storyboard_hierarchy(nk_context* context, const storyboard_view_t& view,
                                 storyboard_action_t& action, bool input_available) {
    widget.bind(view, input_available);
    widget.prepare_drag(context);
    render_storyboard_generator_ownership(context, view, action, input_available);
    if (nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT) &&
        !nk_input_is_mouse_hovering_rect(&context->input, nk_window_get_content_region(context)))
        widget.blur();
    if (!view.selected()) {
        nk_layout_row_dynamic(context, 24, 1);
        nk_label(context, view.hierarchy.message.empty() ? "" : view.hierarchy.message.data(), NK_TEXT_LEFT);
    }
    for (const auto& layer : view.hierarchy.layers) {
        render_layer_heading(context, view, layer, action, input_available);
        for (const auto& row : layer.rows) render_node(context, view, layer, row, action, input_available);
    }
    widget.finish_drag(context, action);
    if (input_available) render_layer_move(context, view, action);
    nk_layout_row_dynamic(context, 22, 1);
    nk_label_colored(context, "↑↓ select · F2 rename · Del delete · Ctrl+D duplicate", NK_TEXT_LEFT,
                     chrome::color(chrome::UiColor::text_muted));
}
namespace {
auto arrow_target(const storyboard_view_t& view, shortcut_key_t key) -> const storyboard_hierarchy_row_t* {
    std::vector<const storyboard_hierarchy_row_t*> rows;
    for (const auto& layer : view.hierarchy.layers)
        for (const auto& row : layer.rows) rows.push_back(&row);
    if (rows.empty()) return nullptr;
    const auto* row = selected_row(view);
    if (!row) return rows.front();
    const auto found = std::ranges::find(rows, row);
    if (key == shortcut_key_t::up) return found == rows.begin() ? row : *std::prev(found);
    if (key == shortcut_key_t::down) return std::next(found) == rows.end() ? row : *std::next(found);
    for (const auto* candidate : rows) {
        if (key == shortcut_key_t::left && row->parent == candidate->id) return candidate;
        if (key == shortcut_key_t::right && candidate->parent == row->id) return candidate;
    }
    return row;
}
} // namespace

bool storyboard_hierarchy_shortcut(const storyboard_view_t& view, const shortcut_event_t& key,
                                   storyboard_action_t& action) {
    if (!view.visible || !widget.has_focus()) return false;
    if (key.hid_usage == shortcut_key_t::escape) {
        widget.cancel();
        return true;
    }
    if (widget.has_rename()) return false;
    if (key.modifiers == 0 && key.hid_usage >= shortcut_key_t::right && key.hid_usage <= shortcut_key_t::up) {
        if (const auto* row = arrow_target(view, key.hid_usage))
            action = node_selection(view, row->id);
        return true;
    }
    const auto* row = selected_row(view);
    if (!row) return false;
    if (key.modifiers == 0 && key.hid_usage == shortcut_key_t::f2) {
        widget.begin_rename(view, *row);
        return true;
    }
    if (key.modifiers == 0 && key.hid_usage == shortcut_key_t::delete_key) {
        if (row->editable) action = node_action(view, storyboard_hierarchy_mutation_kind_t::erase, row->id);
        return true;
    }
    if (key.modifiers == WORM_UI_SHORTCUT_MODIFIER_CONTROL && key.hid_usage == shortcut_key_t::d) {
        if (row->editable) action = node_action(view, storyboard_hierarchy_mutation_kind_t::duplicate, row->id);
        return true;
    }
    return false;
}
} // namespace wormedit::ui
