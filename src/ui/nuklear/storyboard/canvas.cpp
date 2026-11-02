#include "ui/nuklear/storyboard/assets.h"
#include "ui/nuklear/storyboard/canvas.h"
#include "ui/nuklear/chrome/content_layout.h"
#include "ui/nuklear/audio/player_preview_texture.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/text_layout.h"
#include <wormedit/core/storyboard_canvas.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#if defined(WORMEDIT_TEST_ICON_WIDGETS)
#include "ui/nuklear/chrome/icon_widget_recording.h"
#endif

namespace wormedit::ui {
namespace {
constexpr std::string_view fit_label = "Fit";
constexpr std::string_view safe_area_label = "16:9 safe";
constexpr const char* backgrounds[]{"Theme", "Black", "White"};

class canvas_pointer_t {
  public:
    void cancel() { captured_ = {}; button_ = NK_BUTTON_MAX; }
    void bind(const storyboard_view_t& view, bool available) {
        if (!available || captured_.project != view.project || captured_.revision != view.revision ||
            captured_.session_generation != view.session_generation || captured_.storyboard != view.state.selection.storyboard ||
            captured_.prepared_generation != view.canvas.prepared_generation ||
            (active() && captured_.viewport != view.state.viewport)) cancel();
    }
    void capture(const storyboard_view_t& view, nk_buttons button, visual_point_t position) {
        captured_ = storyboard_action_t::from(view, button == NK_BUTTON_MIDDLE ? storyboard_action_kind_t::set_viewport :
            storyboard_action_kind_t::select_nodes, view.state.selection.storyboard);
        button_ = button; position_ = position;
    }
    [[nodiscard]] auto active() const -> bool { return button_ != NK_BUTTON_MAX; }
    [[nodiscard]] auto button() const -> nk_buttons { return button_; }
    [[nodiscard]] auto position() const -> visual_point_t { return position_; }
    auto action() -> storyboard_action_t& { return captured_; }
  private:
    storyboard_action_t captured_;
    nk_buttons button_{NK_BUTTON_MAX};
    visual_point_t position_;
};
canvas_pointer_t pointer;

class gizmo_pointer_t {
  public:
    void cancel() {
        if (started_) {
            pending_cancel_ = captured_;
            pending_cancel_->inspector.phase = storyboard_inspector_phase_t::cancel;
        }
        drag_.reset(); started_ = false;
    }
    void bind(const storyboard_view_t& view, bool available) {
        if (!drag_) return;
        const bool target_exists = std::ranges::any_of(view.canvas.gizmo.targets(),
            [&](const auto& target) { return target.op == captured_.inspector.op; });
        if ((started_ && !view.canvas.gizmo.is_staged) || (!started_ && !view.canvas.gizmo.can_begin) ||
            !available || !target_exists || captured_.project != view.project || captured_.revision != view.revision ||
            captured_.session_generation != view.session_generation ||
            captured_.storyboard != view.state.selection.storyboard ||
            captured_.prepared_generation != view.canvas.prepared_generation ||
            captured_.viewport != view.state.viewport ||
            view.canvas.gizmo.node != captured_.inspector.node) cancel();
    }
    auto capture(const storyboard_view_t& view, const storyboard_gizmo_target_t& target,
        storyboard_gizmo_axis_t axis, visual_point_t logical, visual_point_t screen, storyboard_action_t& output) -> bool {
        auto drag = storyboard_gizmo_drag_t::from(target.kind, target.world_side, target.value, logical, axis);
        if (!drag) return false;
        drag_ = std::move(*drag); start_ = screen; started_ = false;
        captured_ = storyboard_action_t::from(view, storyboard_action_kind_t::inspector, view.state.selection.storyboard);
        captured_.inspector = {.field = storyboard_inspector_field_t::transform,
            .node = view.canvas.gizmo.node, .op = target.op, .key = target.key,
            .value = target.value, .key_time_ms = target.key_time_ms};
        output = captured_;
        output.inspector.phase = storyboard_inspector_phase_t::focus;
        return true;
    }
    [[nodiscard]] auto active() const -> bool { return drag_.has_value(); }
    auto drain(storyboard_action_t& action) -> bool {
        if (!pending_cancel_ || action.kind != storyboard_action_kind_t::none) return false;
        action = std::move(*pending_cancel_); pending_cancel_.reset(); return true;
    }
    auto capture_key(const SDL_KeyboardEvent& event) -> bool {
        if (escape_captured_ && event.key == SDLK_ESCAPE) {
            if (!event.down) escape_captured_ = false;
            return true;
        }
        if (!active()) return false;
        if (event.key == SDLK_ESCAPE && event.down && !event.repeat) {
            escape_captured_ = true; cancel();
        }
        return true;
    }
    void update(const nk_input& input, const storyboard_canvas_geometry_t& geometry, storyboard_action_t& action) {
        if (!drag_ || action.kind != storyboard_action_kind_t::none) return;
        const visual_point_t mouse{input.mouse.pos.x, input.mouse.pos.y};
        const bool released = nk_input_is_mouse_released(&input, NK_BUTTON_LEFT);
        if (released && !started_) { cancel(); return; }
        if (!released && !nk_input_is_mouse_down(&input, NK_BUTTON_LEFT)) { cancel(); return; }
        if (!started_ && std::hypot(mouse.x - start_.x, mouse.y - start_.y) < 3) return;
        auto value = drag_->revise(geometry.to_canvas(mouse), (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
        if (!value) { cancel(); return; }
        if (!released && *value == captured_.inspector.value) return;
        captured_.inspector.value = std::move(*value);
        action = captured_;
        action.inspector.phase = !started_ ? storyboard_inspector_phase_t::begin : released ?
            storyboard_inspector_phase_t::apply : storyboard_inspector_phase_t::revise;
        started_ = true;
        if (released) { drag_.reset(); started_ = false; }
    }
  private:
    std::optional<storyboard_gizmo_drag_t> drag_;
    storyboard_action_t captured_;
    std::optional<storyboard_action_t> pending_cancel_;
    visual_point_t start_;
    bool started_{}, escape_captured_{};
};
gizmo_pointer_t gizmo_pointer;

struct gizmo_handle_t {
    std::size_t target_index{};
    visual_point_t position, stem;
    storyboard_gizmo_axis_t axis{storyboard_gizmo_axis_t::both};
    std::string tooltip;
    void set_label(std::string_view prefix, std::string_view op, std::optional<std::size_t> corner = {}) {
        tooltip.assign(prefix); tooltip.append(op);
        if (corner) { tooltip.append(" corner "); tooltip.append(std::to_string(*corner)); }
    }
};
class gizmo_handle_storage_t {
  public:
    void reset() { count_ = 0; }
    auto append(std::size_t target, visual_point_t position, visual_point_t stem,
        storyboard_gizmo_axis_t axis = storyboard_gizmo_axis_t::both) -> gizmo_handle_t& {
        if (count_ == storage_.size()) storage_.emplace_back();
        auto& handle = storage_[count_++];
        handle.target_index = target; handle.position = position; handle.stem = stem; handle.axis = axis;
        return handle;
    }
    [[nodiscard]] auto items() const -> std::span<const gizmo_handle_t> { return std::span{storage_}.first(count_); }
  private:
    // Retain geometry/label capacity, never a pointer into a frame view.
    std::vector<gizmo_handle_t> storage_;
    std::size_t count_{};
};
gizmo_handle_storage_t gizmo_handles;
auto offset_axis(const storyboard_gizmo_target_t& target, visual_point_t origin,
    storyboard_gizmo_axis_t axis) -> visual_point_t {
    const auto local = axis == storyboard_gizmo_axis_t::x ? visual_point_t{1, 0} : visual_point_t{0, 1};
    const auto zero = target.world_side.apply({0, 0}), end = target.world_side.apply(local);
    const auto length = std::hypot(end.x - zero.x, end.y - zero.y);
    return {origin.x + 32 * (end.x - zero.x) / length, origin.y + 32 * (end.y - zero.y) / length};
}
auto gizmo_center(const storyboard_canvas_outline_t* outline, const storyboard_gizmo_target_t& target)
    -> visual_point_t {
    if (!outline) return target.world_side.apply({0, 0});
    if (!outline->group) return outline->origin;
    return {(outline->points[0].x + outline->points[2].x) / 2,
        (outline->points[0].y + outline->points[2].y) / 2};
}
void append_move_handles(const storyboard_gizmo_target_t& target, std::size_t index, visual_point_t origin,
    gizmo_handle_storage_t& handles) {
    handles.append(index, origin, origin).set_label("Move ", target.op);
    for (const auto axis : {storyboard_gizmo_axis_t::x, storyboard_gizmo_axis_t::y})
        handles.append(index, offset_axis(target, origin, axis), origin, axis).set_label(
            axis == storyboard_gizmo_axis_t::x ? "Move X " : "Move Y ", target.op);
}
void build_gizmo_handles(const storyboard_view_t& view, const storyboard_canvas_geometry_t& geometry,
    gizmo_handle_storage_t& handles) {
    handles.reset();
    const auto outline = std::ranges::find(view.canvas.outlines, view.canvas.gizmo.node, &storyboard_canvas_outline_t::node);
    const auto targets = view.canvas.gizmo.targets();
    for (std::size_t index = 0; index < targets.size(); ++index) {
        const auto& target = targets[index];
        const auto center = gizmo_center(outline == view.canvas.outlines.end() ? nullptr : &*outline, target);
        const auto origin = geometry.to_screen(center);
        if (target.kind == osbx_transform_kind_t::translate) {
            append_move_handles(target, index, origin, handles);
        } else if (target.kind == osbx_transform_kind_t::pivot) {
            const auto pivot = geometry.to_screen(target.world_side.apply({target.value[0], target.value[1]}));
            handles.append(index, pivot, pivot).set_label("Pivot ", target.op);
        } else if (outline != view.canvas.outlines.end() && target.kind == osbx_transform_kind_t::scale) {
            for (std::size_t i = 0; i < outline->points.size(); ++i)
                handles.append(index, geometry.to_screen(outline->points[i]), origin).set_label("Scale ", target.op, i);
        } else if (outline != view.canvas.outlines.end() && target.kind == osbx_transform_kind_t::rotate) {
            const auto a = geometry.to_screen(outline->points[0]), b = geometry.to_screen(outline->points[1]);
            const visual_point_t top{(a.x + b.x) / 2, (a.y + b.y) / 2};
            const auto length = std::hypot(top.x - origin.x, top.y - origin.y);
            if (length > 0) handles.append(index,
                {top.x + 24 * (top.x - origin.x) / length, top.y + 24 * (top.y - origin.y) / length}, top).set_label("Rotate ", target.op);
        }
    }
}
auto handle_gizmo_input(nk_context* context, const storyboard_view_t& view,
    const storyboard_canvas_geometry_t& geometry, struct nk_rect bounds,
    storyboard_action_t& action, bool available) -> bool {
    gizmo_pointer.bind(view, available);
    if (gizmo_pointer.drain(action)) return true;
    if (gizmo_pointer.active()) { gizmo_pointer.update(context->input, geometry, action); return true; }
    if (!available || !view.canvas.gizmo.can_begin || action.kind != storyboard_action_kind_t::none || pointer.active()) return false;
    const auto& input = context->input;
    if (!nk_input_is_mouse_pressed(&input, NK_BUTTON_LEFT) ||
        !nk_input_is_mouse_hovering_rect(&input, bounds) ||
        !nk_input_is_mouse_hovering_rect(&input, context->current->buffer.clip)) return false;
    const visual_point_t mouse{input.mouse.pos.x, input.mouse.pos.y};
    const auto handles = gizmo_handles.items();
    for (auto it = handles.rbegin(); it != handles.rend(); ++it)
        if (std::hypot(mouse.x - it->position.x, mouse.y - it->position.y) <= 7)
            return gizmo_pointer.capture(view, view.canvas.gizmo.targets()[it->target_index], it->axis,
                geometry.to_canvas(mouse), mouse, action);
    return false;
}

auto selected_hit(const storyboard_view_t& view, std::span<const std::string_view> hits, bool cycle) -> std::string_view {
    if (hits.empty()) return {};
    if (!cycle || view.state.selection.nodes.empty()) return hits.front();
    const auto current = std::ranges::find(hits, view.state.selection.nodes.front());
    return current == hits.end() || current + 1 == hits.end() ? hits.front() : *(current + 1);
}
void capture_selection(const storyboard_view_t& view, const storyboard_canvas_geometry_t& geometry,
    visual_point_t mouse) {
    std::vector<storyboard_canvas_target_t> targets;
    for (const auto& sprite : view.canvas.sprites) {
        if (!sprite.image_ready || sprite.opacity <= 0) continue;
        targets.push_back({.node = sprite.node_id, .world = {.values = sprite.transform},
            .width = sprite.image_width, .height = sprite.image_height, .origin_x = sprite.origin_x, .origin_y = sprite.origin_y});
    }
    std::vector<std::string_view> hits;
    build_storyboard_canvas_hits(targets, geometry.to_canvas(mouse), hits);
    const auto modifiers = SDL_GetModState();
    const auto chosen = selected_hit(view, hits, (modifiers & SDL_KMOD_ALT) != 0);
    pointer.capture(view, NK_BUTTON_LEFT, mouse);
    auto& nodes = pointer.action().nodes;
    if ((modifiers & SDL_KMOD_SHIFT) != 0) nodes = view.state.selection.nodes;
    if (chosen.empty()) return;
    const auto existing = std::ranges::find(nodes, chosen);
    if (existing != nodes.end()) nodes.erase(existing);
    else nodes.emplace_back(chosen); // Own the ID through release; never retain a borrowed sprite.
}
void handle_canvas_input(nk_context* context, const storyboard_view_t& view,
    const storyboard_canvas_geometry_t& geometry, struct nk_rect bounds, storyboard_action_t& action, bool available) {
    pointer.bind(view, available);
    if (!available || action.kind != storyboard_action_kind_t::none) return;
    const auto& input = context->input;
    const visual_point_t mouse{input.mouse.pos.x, input.mouse.pos.y};
    const bool hovered = nk_input_is_mouse_hovering_rect(&input, bounds) &&
        nk_input_is_mouse_hovering_rect(&input, context->current->buffer.clip);
    if (hovered && !pointer.active()) {
        if (nk_input_is_mouse_pressed(&input, NK_BUTTON_LEFT)) capture_selection(view, geometry, mouse);
        else if (nk_input_is_mouse_pressed(&input, NK_BUTTON_MIDDLE)) pointer.capture(view, NK_BUTTON_MIDDLE, mouse);
    }
    if (pointer.active()) {
        if (nk_input_is_mouse_released(&input, pointer.button())) {
            if (pointer.button() == NK_BUTTON_MIDDLE || hovered) {
                action = pointer.action();
                if (pointer.button() == NK_BUTTON_MIDDLE) {
                    action.viewport.pan_x += (mouse.x - pointer.position().x) / geometry.scale;
                    action.viewport.pan_y += (mouse.y - pointer.position().y) / geometry.scale;
                }
            }
            pointer.cancel();
        } else if (!nk_input_is_mouse_down(&input, pointer.button())) pointer.cancel();
        return;
    }
    const auto wheel = input.mouse.scroll_delta.y;
    if (!hovered || wheel == 0) return;
    action = storyboard_action_t::from(view, storyboard_action_kind_t::set_viewport, view.state.selection.storyboard);
    action.viewport.zoom = std::clamp(view.state.viewport.zoom * std::pow(1.15, wheel),
        storyboard_canvas_geometry_t::minimum_zoom, storyboard_canvas_geometry_t::maximum_zoom);
    const auto after = storyboard_canvas_geometry_t::from(geometry.viewport, view.canvas.width, view.canvas.height,
        action.viewport.zoom, {action.viewport.pan_x, action.viewport.pan_y});
    if (!after) { action = {}; return; }
    const auto before_point = geometry.to_canvas(mouse), after_point = after->to_canvas(mouse);
    action.viewport.pan_x += after_point.x - before_point.x;
    action.viewport.pan_y += after_point.y - before_point.y;
}
void render_toolbar_title(nk_context* context, std::string_view title) {
    const auto* font = context->style.font;
    const auto fit_width = std::max(chrome::IconButton::width,
        chrome::text_width(font, fit_label) + 2 * context->style.button.padding.x);
    const auto safe_width = chrome::text_width(font, safe_area_label) + 24 +
        2 * context->style.checkbox.padding.x;
    float background_width{};
    for (const auto* label : backgrounds)
        background_width = std::max(background_width, chrome::text_width(font, label));
    background_width += 24 + 2 * context->style.combo.content_padding.x;
    const auto required = chrome::text_width(font, title) + 2 * context->style.text.padding.x +
        fit_width + safe_width + background_width + 3 * context->style.window.spacing.x;
    const bool wrapped = nk_window_get_content_region(context).w < required;
    nk_layout_row_template_begin(context, 24);
    nk_layout_row_template_push_dynamic(context);
    if (!wrapped) {
        nk_layout_row_template_push_static(context, fit_width);
        nk_layout_row_template_push_static(context, safe_width);
        nk_layout_row_template_push_static(context, background_width);
    }
    nk_layout_row_template_end(context);
    nk_label(context, title.data(), NK_TEXT_LEFT);
    if (wrapped) {
        nk_layout_row_template_begin(context, 24);
        nk_layout_row_template_push_static(context, fit_width);
        nk_layout_row_template_push_static(context, safe_width);
        nk_layout_row_template_push_dynamic(context);
        nk_layout_row_template_end(context);
    }
}
void render_toolbar(nk_context* context, const storyboard_view_t& view, storyboard_action_t& action, bool available) {
    char title[80];
    std::snprintf(title, sizeof(title), "Canvas  %.0f%% · %zu sprites", view.state.viewport.zoom * 100, view.canvas.sprites.size());
    render_toolbar_title(context, title);
    auto viewport = view.state.viewport;
    if (nk_button_label(context, fit_label.data()) && available) { viewport.zoom = 1; viewport.pan_x = viewport.pan_y = 0; }
    nk_bool safe = viewport.safe_area;
    if (nk_checkbox_label(context, safe_area_label.data(), &safe) && available) viewport.safe_area = safe;
    const int background = chrome::safe_combo(context, backgrounds, 3, static_cast<int>(viewport.background), 24, nk_vec2(160, 130));
    if (available) viewport.background = static_cast<storyboard_canvas_background_t>(background);
    if (viewport != view.state.viewport && available) {
        action = storyboard_action_t::from(view, storyboard_action_kind_t::set_viewport, view.state.selection.storyboard);
        action.viewport = viewport;
    }
}
void draw_outlines(nk_command_buffer* commands, const storyboard_view_t& view, const storyboard_canvas_geometry_t& geometry) {
    const auto color = chrome::color(chrome::UiColor::accent);
    for (const auto& outline : view.canvas.outlines) {
        for (std::size_t i = 0; i < outline.points.size(); ++i) {
            const auto a = geometry.to_screen(outline.points[i]), b = geometry.to_screen(outline.points[(i + 1) % outline.points.size()]);
            nk_stroke_line(commands, a.x, a.y, b.x, b.y, 1.5f, color);
        }
        if (outline.group) continue;
        const auto origin = geometry.to_screen(outline.origin);
        nk_stroke_line(commands, origin.x - 4, origin.y, origin.x + 4, origin.y, 1, color);
        nk_stroke_line(commands, origin.x, origin.y - 4, origin.x, origin.y + 4, 1, color);
    }
}
void draw_gizmo_handle(nk_context* context, const storyboard_view_t& view, const gizmo_handle_t& handle) {
    auto* commands = nk_window_get_canvas(context);
    const auto point = handle.position;
    const auto color = handle.axis == storyboard_gizmo_axis_t::x ? nk_rgb(230, 78, 78) :
        handle.axis == storyboard_gizmo_axis_t::y ? nk_rgb(75, 210, 126) : chrome::color(chrome::UiColor::accent);
    const auto kind = view.canvas.gizmo.targets()[handle.target_index].kind;
    if (kind == osbx_transform_kind_t::rotate || handle.axis != storyboard_gizmo_axis_t::both)
        nk_stroke_line(commands, handle.stem.x, handle.stem.y, point.x, point.y, 2, color);
    const auto rect = nk_rect(point.x - 5, point.y - 5, 10, 10);
    if (kind == osbx_transform_kind_t::pivot) nk_stroke_circle(commands, rect, 2, color);
    else if (kind == osbx_transform_kind_t::rotate) nk_fill_circle(commands, rect, color);
    else nk_fill_rect(commands, rect, 0, color);
    if (nuklear::tooltip::hovered(context, rect)) nuklear::tooltip::text(context, handle.tooltip);
#if defined(WORMEDIT_TEST_ICON_WIDGETS)
    if (testing::icon_widget_target) {
        const auto clip = commands->clip;
        testing::icon_widget_target->append(handle.tooltip, {.x = rect.x, .y = rect.y, .w = rect.w, .h = rect.h},
            {.x = clip.x, .y = clip.y, .w = clip.w, .h = clip.h});
    }
#endif
}
void draw_gizmos(nk_context* context, const storyboard_view_t& view,
    const storyboard_canvas_geometry_t& geometry) {
    build_gizmo_handles(view, geometry, gizmo_handles);
    for (const auto& handle : gizmo_handles.items()) draw_gizmo_handle(context, view, handle);
}
void render_missing_gizmo_ops(nk_context* context, const storyboard_view_t& view,
    storyboard_action_t& action, bool available) {
    for (const auto kind : view.canvas.gizmo.missing) {
        const auto choice = std::ranges::find(view.inspector.transform_choices, kind, &storyboard_transform_choice_t::kind);
        if (choice == view.inspector.transform_choices.end()) continue;
        const auto label = "Append " + choice->label + " op";
        nk_layout_row_dynamic(context, 24, 1);
        if (nk_button_label(context, label.c_str()) && available && !gizmo_pointer.active()) {
            action = storyboard_action_t::from(view, storyboard_action_kind_t::inspector, view.state.selection.storyboard);
            action.inspector = {.field = storyboard_inspector_field_t::add_op,
                .node = view.canvas.gizmo.node, .op_kind = kind};
        }
    }
}
auto canvas_status_message(const storyboard_view_t& view) -> std::string_view {
    if (!view.canvas.status.empty()) return view.canvas.status;
    if (!view.canvas.gizmo.reason.empty()) return view.canvas.gizmo.reason;
    if (!view.canvas.gizmo.missing.empty()) return "Missing transform op: append explicitly above to enable its gizmo.";
    if (!view.canvas.gizmo.targets().empty()) return "Drag handles · Shift constrains · Escape cancels · Matrix/skew: inspector only";
    return view.canvas.available ? std::string_view{"Click to select · Alt-click cycles · Wheel zooms · Middle-drag pans"} : view.canvas.message;
}
void draw_canvas(nk_context* context, SDL_Renderer* renderer, const storyboard_view_t& view,
    struct nk_rect bounds, const storyboard_canvas_geometry_t& geometry) {
    auto* commands = nk_window_get_canvas(context);
    const auto clip = commands->clip;
    nk_push_scissor(commands, nk_rect(std::max(bounds.x, clip.x), std::max(bounds.y, clip.y),
        std::max(0.0f, std::min(bounds.x + bounds.w, clip.x + clip.w) - std::max(bounds.x, clip.x)),
        std::max(0.0f, std::min(bounds.y + bounds.h, clip.y + clip.h) - std::max(bounds.y, clip.y))));
    const auto background = view.state.viewport.background == storyboard_canvas_background_t::black ? nk_rgb(0, 0, 0) :
        view.state.viewport.background == storyboard_canvas_background_t::white ? nk_rgb(255, 255, 255) : chrome::color(chrome::UiColor::background);
    nk_fill_rect(commands, bounds, 0, background);
    const auto& c = geometry.canvas;
    const auto destination = nk_rect(c.left, c.top, c.width, c.height);
    worm_ui_nuklear_preview_visual_result_t failure;
    if (renderer) {
        for (const bool foreground : {false, true}) {
            const auto result = worm_ui_nuklear_draw_player_preview_storyboard_sprites(commands, destination, renderer,
                view.project, view.revision, view.canvas.sprites, foreground);
            if (result.failure_count) failure = result;
        }
    }
    nk_stroke_rect(commands, destination, 0, 1, chrome::color(chrome::UiColor::border));
    if (view.state.viewport.safe_area) {
        const double safe_height = std::min(view.canvas.height, view.canvas.width * 9 / 16);
        const auto top = geometry.to_screen({0, (view.canvas.height - safe_height) / 2});
        nk_stroke_rect(commands, nk_rect(top.x, top.y, c.width, safe_height * geometry.scale), 0, 1,
            chrome::color(chrome::UiColor::text_muted));
    }
    draw_outlines(commands, view, geometry);
    draw_gizmos(context, view, geometry);
    if (failure.failure_count) nk_draw_text(commands, nk_rect(bounds.x + 8, bounds.y + 8, bounds.w - 16, 24),
        worm_ui_nuklear_preview_visual_failure_message(failure.first_failure),
        static_cast<int>(std::char_traits<char>::length(worm_ui_nuklear_preview_visual_failure_message(failure.first_failure))),
        context->style.font, background, chrome::color(chrome::UiColor::conflict));
    nk_push_scissor(commands, clip);
}
} // namespace
void cancel_storyboard_canvas_pointer() { pointer.cancel(); gizmo_pointer.cancel(); gizmo_handles.reset(); }
auto drain_storyboard_canvas_cancel(storyboard_action_t& action) -> bool { return gizmo_pointer.drain(action); }
auto capture_storyboard_canvas_key(nk_context*, const SDL_KeyboardEvent& event) -> bool {
    return gizmo_pointer.capture_key(event);
}
void render_storyboard_canvas(nk_context* context, SDL_Renderer* renderer, const storyboard_view_t& view,
    storyboard_action_t& action, bool input_available) {
    if (!input_available || !view.canvas.available) nk_widget_disable_begin(context);
    render_toolbar(context, view, action, input_available && view.canvas.available);
    render_missing_gizmo_ops(context, view, action, input_available && view.canvas.available);
    if (!input_available || !view.canvas.available) nk_widget_disable_end(context);
    const auto message = canvas_status_message(view);
    nk_layout_row_dynamic(context, 44, 1);
    nk_label_wrap(context, message.empty() ? "" : message.data());
    const auto remaining = layout::remaining_content_height(context,
        {.minimum = 24, .include_spacing = true});
    nk_layout_row_dynamic(context, remaining, 1);
    struct nk_rect bounds{};
    const auto widget = nk_widget(&bounds, context);
    pointer.bind(view, input_available);
    gizmo_pointer.bind(view, input_available && view.canvas.available);
    auto pan = visual_point_t{view.state.viewport.pan_x, view.state.viewport.pan_y};
    const rect_t rectangle{bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h};
    const auto original = storyboard_canvas_geometry_t::from(rectangle, view.canvas.width, view.canvas.height,
        view.state.viewport.zoom, pan);
    if (original && pointer.active() && pointer.button() == NK_BUTTON_MIDDLE) {
        pan.x = pointer.action().viewport.pan_x + (context->input.mouse.pos.x - pointer.position().x) / original->scale;
        pan.y = pointer.action().viewport.pan_y + (context->input.mouse.pos.y - pointer.position().y) / original->scale;
    }
    const auto geometry = storyboard_canvas_geometry_t::from(rectangle, view.canvas.width, view.canvas.height,
        view.state.viewport.zoom, pan);
    if (geometry && widget != NK_WIDGET_INVALID) {
        draw_canvas(context, renderer, view, bounds, *geometry);
        const bool available = input_available && widget == NK_WIDGET_VALID && view.canvas.available;
        if (!handle_storyboard_asset_drop(context, view, *geometry, action, input_available && view.canvas.available) &&
            !handle_gizmo_input(context, view, *geometry, bounds, action, available))
            handle_canvas_input(context, view, *geometry, bounds, action, available);
    } else { pointer.cancel(); gizmo_pointer.cancel(); }
}
} // namespace wormedit::ui
