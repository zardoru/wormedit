#include "ui/nuklear/storyboard/timeline.h"
#include "ui/nuklear/storyboard/storyboard_panel.h"
#include "ui/frontend/storyboard_timeline.h"
#include "ui/frontend/compose.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/text_layout.h"
#include <SDL3/SDL_keyboard.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace wormedit::ui {
namespace {
using kind_t = storyboard_timeline_action_kind_t;
using phase_t = storyboard_inspector_phase_t;
constexpr float label_width = 160, row_height = 25, ruler_height = 25;
struct timeline_geometry_t {
    struct nk_rect bounds;
    const storyboard_viewport_t& viewport;
    [[nodiscard]] auto x_at(double time) const -> float {
        return bounds.x + label_width + static_cast<float>((time - viewport.timeline_begin_ms) * viewport.timeline_pixels_per_ms);
    }
    [[nodiscard]] auto time_at(float x) const -> double {
        return viewport.timeline_begin_ms + (x - bounds.x - label_width) / viewport.timeline_pixels_per_ms;
    }
    [[nodiscard]] auto row_y(std::size_t row) const -> float {
        return bounds.y + ruler_height + (static_cast<float>(row) + 0.5f) * row_height;
    }
};
class timeline_pointer_t {
  public:
    storyboard_action_t captured;
    std::optional<storyboard_action_t> cancellation;
    osbx_track_target_t focused_track;
    struct nk_vec2 anchor{}, current{};
    bool active{}, dragging{}, box{}, focused{}, available{}, release_pending{};
    void cancel() {
        if (dragging) {
            cancellation = captured;
            cancellation->timeline.phase = phase_t::cancel;
        }
        active = false; dragging = false; focused = false; available = false; release_pending = false;
        focused_track = {};
    }
    void bind(const storyboard_view_t& view, bool input_available) {
        if (!input_available || project_ != view.project || session_ != view.session_generation ||
            storyboard_ != view.state.selection.storyboard || (active && (captured.project != view.project ||
            captured.revision != view.revision || captured.session_generation != view.session_generation ||
            captured.storyboard != view.state.selection.storyboard ||
            captured.prepared_generation != view.canvas.prepared_generation))) cancel();
        project_ = view.project; session_ = view.session_generation; storyboard_ = view.state.selection.storyboard;
        if (!focused_track.node.empty() && std::ranges::find(view.state.selection.nodes, focused_track.node) ==
            view.state.selection.nodes.end()) {
            focused_track = {}; focused = false;
        }
        available = input_available;
    }
  private:
    song_id_t project_;
    storyboard_id_t storyboard_;
    std::uint64_t session_{};
};
timeline_pointer_t pointer;

auto capture(const storyboard_view_t& view, kind_t kind) -> storyboard_action_t {
    auto result = storyboard_action_t::from(view, storyboard_action_kind_t::timeline, view.state.selection.storyboard);
    result.timeline.kind = kind;
    result.timeline.time = view.timeline.time;
    result.timeline.grid_enabled = view.timeline.grid_enabled;
    return result;
}
auto selected_keys(const storyboard_timeline_view_t& timeline) -> std::vector<osbx_key_target_t> {
    std::vector<osbx_key_target_t> keys;
    for (const auto& row : timeline.rows)
        for (const auto& key : row.keys)
            if (key.selected && key.editable) keys.push_back({.track = row.track, .key = key.id});
    return keys;
}
void begin_key(const storyboard_view_t& view, const storyboard_timeline_row_t& row,
    const storyboard_timeline_key_t& key, storyboard_action_t& output) {
    pointer.focused_track = row.track;
    pointer.captured = capture(view, kind_t::move_keys);
    auto& payload = pointer.captured.timeline;
    payload.track = row.track;
    if (key.selected) payload.keys = selected_keys(view.timeline);
    else payload.keys = {{.track = row.track, .key = key.id}};
    payload.anchor_time_ms = key.time_ms;
    output = capture(view, kind_t::select_keys);
    output.timeline.track = row.track;
    output.timeline.keys = payload.keys;
    if ((SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0) {
        auto previous = selected_keys(view.timeline);
        const osbx_key_target_t target{.track = row.track, .key = key.id};
        const auto found = std::ranges::find(previous, target);
        if (found == previous.end()) previous.push_back(target); else previous.erase(found);
        output.timeline.keys = std::move(previous);
    }
}
auto press_marker(const storyboard_view_t& view, const timeline_geometry_t& geometry,
    storyboard_action_t& output) -> bool {
    for (std::size_t i = 0; i < view.timeline.rows.size(); ++i) {
        const auto& row = view.timeline.rows[i];
        if (std::abs(pointer.anchor.y - geometry.row_y(i)) > row_height * 0.5f) continue;
        pointer.focused_track = row.track;
        for (const auto& key : row.keys) {
            const auto x = geometry.bounds.x + label_width + static_cast<float>(key.position);
            if (!key.editable || std::abs(pointer.anchor.x - x) > 7) continue;
            begin_key(view, row, key, output);
            return true;
        }
    }
    return false;
}
void finish_box(const storyboard_view_t& view, const timeline_geometry_t& geometry, storyboard_action_t& output) {
    output = capture(view, kind_t::select_keys);
    const auto left = std::min(pointer.anchor.x, pointer.current.x), right = std::max(pointer.anchor.x, pointer.current.x);
    const auto top = std::min(pointer.anchor.y, pointer.current.y), bottom = std::max(pointer.anchor.y, pointer.current.y);
    for (std::size_t i = 0; i < view.timeline.rows.size(); ++i) {
        const auto& row = view.timeline.rows[i];
        if (geometry.row_y(i) < top || geometry.row_y(i) > bottom) continue;
        for (const auto& key : row.keys) {
            const auto x = geometry.bounds.x + label_width + static_cast<float>(key.position);
            if (key.editable && x >= left && x <= right) output.timeline.keys.push_back({.track = row.track, .key = key.id});
        }
    }
}
void revise_pointer(nk_context* context, const storyboard_view_t& view,
    const timeline_geometry_t& geometry, storyboard_action_t& output) {
    pointer.current = context->input.mouse.pos;
    const auto released = pointer.release_pending || nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT) != 0;
    if (pointer.box) {
        if (released) { finish_box(view, geometry, output); pointer.active = false; }
        return;
    }
    const auto delta = pointer.current.x - pointer.anchor.x;
    if (!pointer.dragging && std::abs(delta) < 3) {
        if (released) pointer.active = false;
        return;
    }
    output = pointer.captured;
    output.timeline.pointer_delta_ms = delta / geometry.viewport.timeline_pixels_per_ms;
    output.timeline.shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    output.timeline.phase = !pointer.dragging ? phase_t::begin : released ? phase_t::apply : phase_t::revise;
    // Capture the valid originals first, even when the first motion crosses an
    // occupied time. Later revisions may be refused and then recover in place.
    if (output.timeline.phase == phase_t::begin) output.timeline.pointer_delta_ms = 0;
    pointer.dragging = true;
    if (released) {
        // A press->release jump still begins first; apply on the following frame.
        if (output.timeline.phase == phase_t::begin) { pointer.release_pending = true; return; }
        pointer.active = false; pointer.dragging = false;
        pointer.release_pending = false;
    }
}
void zoom_scroll(nk_context* context, const storyboard_view_t& view,
    const timeline_geometry_t& geometry, storyboard_action_t& output) {
    const auto wheel = context->input.mouse.scroll_delta;
    if (wheel.x == 0 && wheel.y == 0) return;
    output = storyboard_action_t::from(view, storyboard_action_kind_t::set_viewport, view.state.selection.storyboard);
    if ((SDL_GetModState() & SDL_KMOD_CTRL) != 0) {
        const auto x = context->input.mouse.pos.x;
        const auto time = geometry.time_at(x);
        output.viewport.timeline_pixels_per_ms = std::clamp(view.state.viewport.timeline_pixels_per_ms *
            std::pow(1.2, wheel.y), 0.0001, 100.0);
        output.viewport.timeline_begin_ms = time - (x - geometry.bounds.x - label_width) /
            output.viewport.timeline_pixels_per_ms;
    } else {
        output.viewport.timeline_begin_ms -= (wheel.x != 0 ? wheel.x : wheel.y) * 70 /
            view.state.viewport.timeline_pixels_per_ms;
    }
    context->input.mouse.scroll_delta = nk_vec2(0, 0);
}
auto lua_band_at(const storyboard_view_t&, const timeline_geometry_t&, struct nk_vec2)
    -> const storyboard_timeline_row_t*;
void handle_pointer(nk_context* context, const storyboard_view_t& view,
    const timeline_geometry_t& geometry, storyboard_action_t& output, bool available) {
    pointer.bind(view, available);
    if (drain_storyboard_timeline_cancel(output) || !available) return;
    if (output.kind != storyboard_action_kind_t::none) {
        if (pointer.active) pointer.cancel();
        return;
    }
    const auto& input = context->input;
    const bool hovered = nk_input_is_mouse_hovering_rect(&input, geometry.bounds) &&
        nk_input_is_mouse_hovering_rect(&input, context->current->buffer.clip);
    if (pointer.active) { revise_pointer(context, view, geometry, output); return; }
    if (nk_input_is_mouse_pressed(&input, NK_BUTTON_LEFT)) pointer.focused = hovered;
    if (!hovered) return;
    zoom_scroll(context, view, geometry, output);
    if (output.kind != storyboard_action_kind_t::none) return;
    if (const auto* row = lua_band_at(view, geometry, input.mouse.pos)) {
        if (nk_input_is_mouse_pressed(&input, NK_BUTTON_LEFT)) pointer.focused_track = row->track;
        nuklear::tooltip::text(context, "Linked to Lua: bake or unlink to edit.");
        return;
    }
    if (output.kind != storyboard_action_kind_t::none || !nk_input_is_mouse_pressed(&input, NK_BUTTON_LEFT)) return;
    pointer.anchor = input.mouse.pos; pointer.current = pointer.anchor;
    if (pointer.anchor.x < geometry.bounds.x + label_width) {
        const auto index = static_cast<int>((pointer.anchor.y - geometry.bounds.y - ruler_height) / row_height);
        if (index >= 0 && static_cast<std::size_t>(index) < view.timeline.rows.size())
            pointer.focused_track = view.timeline.rows[static_cast<std::size_t>(index)].track;
        return;
    }
    if (pointer.anchor.y < geometry.bounds.y + ruler_height) {
        output = capture(view, kind_t::seek); output.timeline.pointer_time_ms = geometry.time_at(pointer.anchor.x); return;
    }
    pointer.active = true;
    pointer.box = !press_marker(view, geometry, output);
    if (pointer.box) pointer.captured = capture(view, kind_t::select_keys);
}
void text(nk_context* context, nk_command_buffer* canvas, struct nk_rect rect, std::string_view value, nk_color tint) {
    nk_draw_text(canvas, rect, value.data(), static_cast<int>(value.size()),
        context->style.font, nk_rgba(0, 0, 0, 0), tint);
}
void draw_marker(nk_command_buffer* canvas, float x, float y, nk_color color, bool ghost = false) {
    std::array<float, 8> points{x, y - 5, x + 5, y, x, y + 5, x - 5, y};
    if (ghost) nk_stroke_polygon(canvas, points.data(), 4, 1, color);
    else nk_fill_polygon(canvas, points.data(), 4, color);
}
void draw_lua_band(nk_context* context, const storyboard_timeline_row_t& row, float y,
    const timeline_geometry_t& geometry) {
    if (!row.lua) return;
    auto* canvas = nk_window_get_canvas(context);
    const auto origin = geometry.bounds.x + label_width;
    const auto left = std::max(origin, origin + static_cast<float>(row.lua->begin));
    const auto right = std::min(geometry.bounds.x + geometry.bounds.w, origin + static_cast<float>(row.lua->end));
    if (right <= left) return;
    const auto tint = chrome::color(chrome::UiColor::accent);
    nk_fill_rect(canvas, nk_rect(left, y - 8, right - left, 16), 0, nk_rgba(tint.r, tint.g, tint.b, 35));
    // Clip both hatch and dot iteration to the visible lane, never the source span.
    for (auto x = left; x < right; x += 8) nk_stroke_line(canvas, x, y + 7, std::min(x + 7, right), y - 7, 1, tint);
    for (const auto sample : row.lua->samples) {
        const auto x = origin + static_cast<float>(sample);
        if (x >= left && x <= right) nk_fill_circle(canvas, nk_rect(x - 2, y - 2, 4, 4), tint);
    }
}
auto lua_band_at(const storyboard_view_t& view, const timeline_geometry_t& geometry, struct nk_vec2 point) -> const storyboard_timeline_row_t* {
    const auto lane_x = point.x - geometry.bounds.x - label_width;
    for (std::size_t i = 0; i < view.timeline.rows.size(); ++i) {
        const auto& row = view.timeline.rows[i];
        if (std::abs(point.y - geometry.row_y(i)) > row_height * 0.5f) continue;
        for (const auto& key : row.keys)
            if (key.editable && std::abs(lane_x - key.position) <= 7) return nullptr;
        if (row.lua && std::abs(point.y - geometry.row_y(i)) <= row_height * 0.5f &&
            lane_x >= row.lua->begin && lane_x <= row.lua->end) return &row;
    }
    return nullptr;
}
void draw_row(nk_context* context, const storyboard_timeline_row_t& row, std::size_t index,
    const timeline_geometry_t& geometry) {
    auto* canvas = nk_window_get_canvas(context);
    const auto y = geometry.row_y(index);
    const auto muted = chrome::color(chrome::UiColor::text_muted);
    text(context, canvas, nk_rect(geometry.bounds.x + 6 + static_cast<float>(row.depth) * 12, y - 10,
        label_width - 12, 20), (row.lua ? "ƒ " + row.label : row.label).c_str(), muted);
    nk_stroke_line(canvas, geometry.bounds.x, y + row_height * 0.5f,
        geometry.bounds.x + geometry.bounds.w, y + row_height * 0.5f, 1, nk_rgba(128, 128, 128, 30));
    const auto clip = canvas->clip;
    nk_push_scissor(canvas, nk_rect(std::max(clip.x, geometry.bounds.x + label_width), clip.y,
        std::max(0.0f, std::min(clip.x + clip.w, geometry.bounds.x + geometry.bounds.w) -
            std::max(clip.x, geometry.bounds.x + label_width)), clip.h));
    draw_lua_band(context, row, y, geometry);
    for (std::size_t k = 0; k < row.keys.size(); ++k) {
        const auto& key = row.keys[k];
        const auto x = geometry.bounds.x + label_width + static_cast<float>(key.position);
        if (k + 1 < row.keys.size()) {
            const auto end = geometry.bounds.x + label_width + static_cast<float>(row.keys[k + 1].position);
            nk_stroke_line(canvas, x, y, end, y, 1, muted);
            if (end - x > 70) text(context, canvas, nk_rect(x + 8, y - 11, end - x - 16, 20), key.easing_label.c_str(), muted);
        }
        if (x >= geometry.bounds.x + label_width - 6 && x <= geometry.bounds.x + geometry.bounds.w + 6)
            draw_marker(canvas, x, y, key.selected ? nk_rgb(255, 199, 80) : muted);
    }
    nk_push_scissor(canvas, clip);
}
void draw_grid(nk_context* context, const storyboard_view_t& view, const timeline_geometry_t& geometry) {
    auto* canvas = nk_window_get_canvas(context);
    for (const auto& line : view.timeline.lines) {
        const auto x = geometry.x_at(line.time_ms);
        if (x < geometry.bounds.x + label_width || x > geometry.bounds.x + geometry.bounds.w) continue;
        nk_stroke_line(canvas, x, geometry.bounds.y, x, geometry.bounds.y + geometry.bounds.h, 1,
            nk_rgba(160, 160, 160, line.measure ? 95 : 40));
    }
    const auto time = view.timeline.time.is_valid() ?
        static_cast<double>(view.timeline.time.milliseconds) + view.timeline.time.fraction() : 0;
    const auto playhead = geometry.bounds.x + label_width + static_cast<float>(view.timeline.playhead_position);
    if (view.timeline.time.is_valid() && playhead >= geometry.bounds.x + label_width && playhead <= geometry.bounds.x + geometry.bounds.w)
        nk_stroke_line(canvas, playhead, geometry.bounds.y, playhead, geometry.bounds.y + geometry.bounds.h, 1.5f, nk_rgb(255, 112, 100));
    std::array<char, 64> label{};
    std::snprintf(label.data(), label.size(), "%.3f ms", time);
    text(context, canvas, nk_rect(geometry.bounds.x + 5, geometry.bounds.y, label_width - 10, ruler_height),
        view.timeline.time.is_valid() ? std::string_view(label.data()) : "Clock unavailable",
        chrome::color(chrome::UiColor::text_muted));
    if (!view.timeline.status.empty())
        text(context, canvas, nk_rect(geometry.bounds.x + label_width, geometry.bounds.y, geometry.bounds.w - label_width, 22),
            view.timeline.status, nk_rgb(255, 112, 100));
}
void draw_pointer(nk_context* context, const storyboard_view_t& view, const timeline_geometry_t& geometry) {
    if (!pointer.active) return;
    auto* canvas = nk_window_get_canvas(context);
    if (pointer.box) {
        nk_stroke_rect(canvas, nk_rect(std::min(pointer.anchor.x, pointer.current.x), std::min(pointer.anchor.y, pointer.current.y),
            std::abs(pointer.current.x - pointer.anchor.x), std::abs(pointer.current.y - pointer.anchor.y)), 0, 1, nk_rgb(255, 199, 80));
        return;
    }
    if (!pointer.dragging) return;
    const auto delta = view.timeline.preview_delta_ms ? static_cast<double>(*view.timeline.preview_delta_ms) :
        (pointer.current.x - pointer.anchor.x) / geometry.viewport.timeline_pixels_per_ms;
    const auto time = static_cast<double>(pointer.captured.timeline.anchor_time_ms) + delta;
    draw_marker(canvas, geometry.x_at(static_cast<double>(pointer.captured.timeline.anchor_time_ms)),
        pointer.anchor.y, nk_rgb(170, 170, 170), true);
    draw_marker(canvas, geometry.x_at(time), pointer.anchor.y,
        view.timeline.preview_reason.empty() ? nk_rgb(255, 199, 80) : nk_rgb(255, 112, 100));
    std::array<char, 64> label{};
    std::snprintf(label.data(), label.size(), "%.3f ms", time);
    text(context, canvas, nk_rect(pointer.current.x + 8, pointer.anchor.y - 24, 160, 20), label.data(), nk_rgb(255, 199, 80));
    if (!view.timeline.preview_reason.empty())
        text(context, canvas, nk_rect(geometry.bounds.x + label_width, geometry.bounds.y, geometry.bounds.w - label_width, 22),
            view.timeline.preview_reason.c_str(), nk_rgb(255, 112, 100));
}
void draw_curve(nk_context* context, const storyboard_easing_choice_t& choice) {
    const auto rect = nk_widget_bounds(context); nk_spacing(context, 1);
    std::array<float, 66> points{};
    for (std::size_t i = 0; i < choice.preview.size(); ++i) {
        points[i * 2] = rect.x + static_cast<float>(i) / 32 * rect.w;
        points[i * 2 + 1] = rect.y + rect.h * (1 - std::clamp(choice.preview[i], -0.1f, 1.1f));
    }
    nk_stroke_polyline(nk_window_get_canvas(context), points.data(),
        static_cast<int>(choice.preview.size()), 1, nk_rgb(255, 199, 80));
}
void custom_bezier(nk_context* context, const storyboard_view_t& view, const storyboard_timeline_row_t& row,
    const storyboard_timeline_key_t& key, storyboard_action_t& output) {
    const auto* authored = std::get_if<osbx_cubic_bezier_t>(&key.easing);
    if (!authored) return;
    auto curve = *authored;
    std::array<float, 4> values{static_cast<float>(curve.x1), static_cast<float>(curve.y1),
        static_cast<float>(curve.x2), static_cast<float>(curve.y2)};
    const auto before = values;
    constexpr std::array<const char*, 4> labels{"x1", "y1", "x2", "y2"};
    nk_layout_row_dynamic(context, 25, 2);
    for (std::size_t i = 0; i < values.size(); ++i) {
        const bool x = i % 2 == 0;
        nk_property_float(context, labels[i], x ? 0.0f : -10.0f, &values[i], x ? 1.0f : 10.0f, 0.05f, 0.005f);
    }
    if (values == before) return;
    curve = {.x1 = values[0], .y1 = values[1], .x2 = values[2], .y2 = values[3]};
    output = capture(view, kind_t::easing);
    output.timeline.track = row.track;
    output.timeline.keys = {{.track = row.track, .key = key.id}};
    output.timeline.easing = curve;
}
void easing_picker(nk_context* context, const storyboard_view_t& view, storyboard_action_t& output, bool available) {
    const storyboard_timeline_row_t* selected_row{};
    const storyboard_timeline_key_t* selected_key{};
    for (const auto& row : view.timeline.rows)
        for (std::size_t i = 0; i + 1 < row.keys.size(); ++i)
            if (row.keys[i].selected && row.keys[i].editable) { selected_row = &row; selected_key = &row.keys[i]; }
    const bool enabled = available && selected_row && selected_row->editable;
    if (!enabled) nk_widget_disable_begin(context);
    if (chrome::safe_combo_begin_label(context, selected_key ? selected_key->easing_label.c_str() : "Easing", nk_vec2(260, 270))) {
        if (enabled) custom_bezier(context, view, *selected_row, *selected_key, output);
        for (const auto& choice : view.timeline.easings) {
            nk_layout_row_begin(context, NK_STATIC, 32, 2);
            nk_layout_row_push(context, 135);
            const bool picked = nk_button_label(context, choice.label.c_str()) != 0;
            nk_layout_row_push(context, 70); draw_curve(context, choice); nk_layout_row_end(context);
            if (picked && enabled) {
                output = capture(view, kind_t::easing);
                output.timeline.track = selected_row->track;
                output.timeline.keys = {{.track = selected_row->track, .key = selected_key->id}};
                output.timeline.easing = choice.easing;
                nk_combo_close(context);
            }
        }
        chrome::safe_combo_end(context);
    }
    if (!enabled) nk_widget_disable_end(context);
}
void lua_track_button(nk_context* context, const storyboard_view_t& view, bool available) {
    if (pointer.focused_track.node.empty()) return;
    const auto* link = view.inspector.lua_link_for({.track = pointer.focused_track});
    const bool enabled = available && link && link->editable;
    nk_layout_row_dynamic(context, 24, 1);
    const auto bounds = nk_widget_bounds(context);
    if (!enabled) nk_widget_disable_begin(context);
    const auto* title = link && !link->link_id.empty() ? "Edit Lua link…" : "Link to Lua…";
    if (nk_button_label(context, title) && enabled) open_storyboard_lua_editor(view, {.track = pointer.focused_track});
    if (!enabled) nk_widget_disable_end(context);
    if (!enabled && nk_input_is_mouse_hovering_rect(&context->input, bounds))
        nuklear::tooltip::text(context, link ? link->reason : "Select an authored numeric track to link.");
}
void toolbar(nk_context* context, const storyboard_view_t& view, storyboard_action_t& output, bool available) {
    constexpr std::string_view add_label = "Add key (K)", delete_label = "Delete keys";
    const auto& button = context->style.button;
    const auto padding = 2 * (button.padding.x + button.border + button.rounding);
    nk_layout_row_begin(context, NK_STATIC, 25, 4);
    nk_layout_row_push(context, chrome::text_width(context->style.font, add_label) + padding);
    if (nk_button_label(context, add_label.data()) && available && !pointer.focused_track.node.empty()) {
        output = capture(view, kind_t::add_key); output.timeline.track = pointer.focused_track;
    }
    nk_layout_row_push(context, chrome::text_width(context->style.font, delete_label) + padding);
    if (nk_button_label(context, delete_label.data()) && available) {
        output = capture(view, kind_t::erase_keys); output.timeline.keys = selected_keys(view.timeline);
    }
    nk_layout_row_push(context, 85);
    const auto grid_bounds = nk_widget_bounds(context);
    if (!view.timeline.grid_available) nk_widget_disable_begin(context);
    auto grid = static_cast<nk_bool>(view.timeline.grid_enabled);
    if (nk_checkbox_label(context, "Beat/grid", &grid) && available) {
        output = capture(view, kind_t::set_grid); output.timeline.grid_enabled = grid != 0;
    }
    if (!view.timeline.grid_available) {
        nk_widget_disable_end(context);
        if (nk_input_is_mouse_hovering_rect(&context->input, grid_bounds))
            nk_tooltip(context, "Beat/grid snapping needs compatible selected-chart timing and an active grid. Millisecond snapping remains available.");
    }
    nk_layout_row_push(context, 150); easing_picker(context, view, output, available);
    nk_layout_row_end(context);
    lua_track_button(context, view, available);
}
} // namespace

void cancel_storyboard_timeline_input() { pointer.cancel(); }
auto drain_storyboard_timeline_cancel(storyboard_action_t& output) -> bool {
    if (!pointer.cancellation || output.kind != storyboard_action_kind_t::none) return false;
    output = std::move(*pointer.cancellation); pointer.cancellation.reset(); return true;
}
auto capture_storyboard_timeline_key(nk_context*, const SDL_KeyboardEvent& event) -> bool {
    if (!pointer.active || event.key != SDLK_ESCAPE || !event.down || event.repeat) return false;
    pointer.cancel(); return true;
}
auto storyboard_timeline_shortcut(const storyboard_view_t& view, const shortcut_event_t& key,
    storyboard_action_t& output) -> bool {
    if (!pointer.focused || !pointer.available || key.repeat || key.modifiers != 0 || pointer.active) return false;
    if (key.hid_usage == shortcut_key_t::k && !pointer.focused_track.node.empty()) {
        output = capture(view, kind_t::add_key); output.timeline.track = pointer.focused_track; return true;
    }
    if (key.hid_usage == shortcut_key_t::delete_key && !view.state.selection.keys.empty()) {
        output = capture(view, kind_t::erase_keys); output.timeline.keys = selected_keys(view.timeline); return true;
    }
    return false;
}
void render_storyboard_timeline(nk_context* context, const storyboard_view_t& view,
    storyboard_action_t& output, bool available) {
    pointer.bind(view, available);
    if (drain_storyboard_timeline_cancel(output)) return;
    render_storyboard_generator_ownership(context, view, output, available);
    toolbar(context, view, output, available);
    if (view.timeline.rows.empty()) {
        nk_layout_row_dynamic(context, 25, 1); nk_label(context, view.timeline.message.c_str(), NK_TEXT_LEFT); return;
    }
    nk_layout_row_dynamic(context, ruler_height + row_height * static_cast<float>(view.timeline.rows.size()), 1);
    const auto bounds = nk_widget_bounds(context); nk_spacing(context, 1);
    const timeline_geometry_t geometry{.bounds = bounds, .viewport = view.state.viewport};
    draw_grid(context, view, geometry);
    for (std::size_t i = 0; i < view.timeline.rows.size(); ++i) draw_row(context, view.timeline.rows[i], i, geometry);
    handle_pointer(context, view, geometry, output, available);
    draw_pointer(context, view, geometry);
}
} // namespace wormedit::ui
