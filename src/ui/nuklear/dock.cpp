#include "ui/nuklear/dock.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/tooltip.h"
#include "ui/nuklear/render/vector_icons.h"
#include <nuklear.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace wormedit::ui {
namespace {
constexpr float dock_button_height = 30.0f;
auto rect(const compose_rect_t& r) -> struct nk_rect {
    return nk_rect(static_cast<float>(r.left), static_cast<float>(r.top),
                   static_cast<float>(r.right - r.left), static_cast<float>(r.bottom - r.top));
}
auto glyph(dock_panel_t panel) -> lucide::Glyph {
    using lucide::Glyph;
    switch (panel) {
    case dock_panel_t::inspector: return Glyph::panel_right_open;
    case dock_panel_t::sounds: return Glyph::audio_lines;
    case dock_panel_t::images: return Glyph::eye;
    case dock_panel_t::measures: return Glyph::grip_vertical;
    case dock_panel_t::layers: return Glyph::layers;
    case dock_panel_t::markers: return Glyph::anchor;
    case dock_panel_t::mixer: return Glyph::audio_lines;
    case dock_panel_t::generator: return Glyph::flame;
    }
    return Glyph::panel_right_open;
}
void begin_capture(nk_context* context, dock_capture_t kind, const dock_geometry_t& geometry,
                    dock_pointer_t& pointer) {
    pointer.capture = kind;
    pointer.x = context->input.mouse.pos.x;
    pointer.y = context->input.mouse.pos.y;
    if (!geometry.panes[0]) return;
    const auto& first = geometry.panes[0]->bounds;
    pointer.width = static_cast<float>(geometry.strip.left - 4 - first.left);
    if (geometry.panes[1]) {
        const auto extent = geometry.is_side_by_side() ? first.right - first.left : first.bottom - first.top;
        pointer.split = static_cast<float>(extent / geometry.split_extent());
    }
}
void render_handle(nk_context* context, const compose_rect_t& bounds, const char* tip,
                    dock_capture_t capture, const dock_geometry_t& geometry,
                    dock_pointer_t& pointer) {
    nk_layout_space_push(context, rect(bounds));
    const chrome::NativeIconCell cell{context, pointer.is_active() && pointer.capture == capture,
                                      capture != dock_capture_t::none, tip};
    const auto actual = cell.bounds();
    const auto highlighted = (pointer.is_active() && pointer.capture == capture) ||
        nk_input_is_mouse_hovering_rect(&context->input, actual);
    nk_fill_rect(nk_window_get_canvas(context), actual, 2,
                 highlighted ? cell.tint() : context->style.window.border_color);
    if (capture == dock_capture_t::none || !nk_input_is_mouse_hovering_rect(&context->input, actual)) return;
    if (!pointer.is_active() && nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT))
        begin_capture(context, capture, geometry, pointer);
}
void update_resize(nk_context* context, const dock_geometry_t& geometry,
                     ui_layout_preferences_t& layout, dock_pointer_t& pointer) {
    const auto& mouse = context->input.mouse.pos;
    if (pointer.capture == dock_capture_t::width) {
        const auto width = pointer.width + pointer.x - mouse.x;
        layout.set_width(geometry.is_side_by_side() ? (width - 6) / 2 : width);
    }
    if (pointer.capture == dock_capture_t::split) {
        const auto delta = geometry.is_side_by_side() ? mouse.x - pointer.x : mouse.y - pointer.y;
        layout.set_split(pointer.split + static_cast<float>(delta / geometry.split_extent()));
    }
    if (pointer.capture != dock_capture_t::panel &&
        nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT)) pointer = {};
}
void activate_panel(ui_layout_preferences_t& layout, dock_panel_t panel, bool stacked) {
    const auto panes = layout.visible_panels();
    if (!stacked && layout.is_visible(panel) && !panes.back()) layout.show(panel, false);
    else layout.activate(panel, stacked);
}
void finish_panel_drag(nk_context* context, std::optional<dock_panel_t> hovered,
                        ui_layout_preferences_t& layout, dock_pointer_t& pointer) {
    if (pointer.capture != dock_capture_t::panel || !pointer.panel ||
        !nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT)) return;
    const auto moved = std::hypot(context->input.mouse.pos.x - pointer.x,
                                  context->input.mouse.pos.y - pointer.y) >= 4;
    if (moved && hovered) {
        const auto found = std::ranges::find(layout.order(), *hovered);
        layout.reorder(*pointer.panel, static_cast<std::size_t>(found - layout.order().begin()));
    } else if (!moved && hovered == pointer.panel) {
        activate_panel(layout, *pointer.panel, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
    }
    pointer = {};
}
void draw_side_by_side(nk_command_buffer* canvas, struct nk_rect bounds, nk_color tint) {
    const auto frame = vector_icons::Frame::fit(bounds);
    nk_stroke_rect(canvas, nk_rect(frame.px(2), frame.py(3), 20 * frame.scale, 18 * frame.scale),
                   2 * frame.scale, frame.stroke(), tint);
    nk_stroke_line(canvas, frame.px(12), frame.py(3), frame.px(12), frame.py(21), frame.stroke(), tint);
}
void render_panel_button(nk_context* context, dock_panel_t panel, const char* tip, bool selected) {
    if (panel == dock_panel_t::images)
        (void)chrome::DrawnIconButton{vector_icons::image, tip, selected}.render(context);
    else if (panel == dock_panel_t::mixer)
        (void)chrome::DrawnIconButton{vector_icons::sliders_vertical, tip, selected}.render(context);
    else
        (void)chrome::IconButton{glyph(panel), tip, selected}.render(context);
}
auto dock_strip_flags(nk_context* context, const dock_geometry_t& geometry) -> nk_flags {
    const auto rows = static_cast<float>(dock_panel_catalog.size() + 1);
    const auto required = rows * (dock_button_height + context->style.window.spacing.y) + 2.0f;
    return geometry.strip.bottom - geometry.strip.top < required ? 0 : NK_WINDOW_NO_SCROLLBAR;
}
void render_strip(nk_context* context, const dock_geometry_t& geometry,
                    ui_layout_preferences_t& layout, dock_pointer_t& pointer,
                    const std::array<bool, dock_panel_count>& available, bool input_available) {
    static const auto tips = [] {
        std::array<std::string, dock_panel_count> result;
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = std::string{dock_panel_catalog[i].title} + " panel (Shift-click to add; drag to reorder)";
        return result;
    }();
    nk_layout_space_push(context, rect(geometry.strip));
    nk_style_push_vec2(context, &context->style.window.group_padding, nk_vec2(1, 1));
    const auto visible = nk_group_begin(context, "Compose dock strip", dock_strip_flags(context, geometry));
    nk_style_pop_vec2(context);
    if (!visible) return;
    std::optional<dock_panel_t> hovered;
    for (const auto panel : layout.order()) {
        const auto index = static_cast<std::size_t>(panel);
        const auto& tip = tips[index];
        nk_layout_row_dynamic(context, dock_button_height, 1);
        const auto bounds = nk_widget_bounds(context);
        if (!available[index]) nk_widget_disable_begin(context);
        render_panel_button(context, panel, tip.c_str(), geometry.is_visible(panel));
        if (!available[index]) nk_widget_disable_end(context);
        if (available[index] && nk_input_is_mouse_hovering_rect(&context->input, bounds)) hovered = panel;
    }
    if (hovered && !pointer.is_active() && nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT)) {
        begin_capture(context, dock_capture_t::panel, geometry, pointer);
        pointer.panel = hovered;
    }
    finish_panel_drag(context, hovered, layout, pointer);
    nk_layout_row_dynamic(context, dock_button_height, 1);
    if (chrome::DrawnIconButton{.draw = draw_side_by_side, .tooltip = "Side-by-side panels",
            .selected = layout.is_side_by_side(), .enabled = input_available}.render(context))
        layout.set_side_by_side(!layout.is_side_by_side());
    nk_group_end(context);
}
}
auto dock_geometry_t::from(float width, float height, const ui_layout_preferences_t& layout,
                           const std::array<bool, dock_panel_count>& available, float reserved_left_width) -> dock_geometry_t {
    dock_geometry_t result{.strip = {width - 40, 8, width - 8, height - 8}, .reserved_width = 44};
    std::array<std::optional<dock_panel_t>, 2> visible;
    std::size_t count{};
    for (const auto panel : layout.order())
        if (layout.is_visible(panel) && available[static_cast<std::size_t>(panel)]) visible[count++] = panel;
    // Preserve a useful field at larger UI scales without changing the saved layout.
    const auto canvas_width = width < 700.0f && count != 1 ? std::max(250.0f, width * 0.55f) : 250.0f;
    const auto column_space = width - canvas_width - reserved_left_width - 50.0f;
    const auto columns = count == 2 && layout.is_side_by_side() && column_space >= 2 * dock_minimum_width + 6;
    const auto desired_width = columns ? 2 * layout.width() + 6 : layout.width();
    const auto panel_width = std::min(desired_width, column_space);
    if (!count || panel_width < dock_minimum_width || height < (count == 1 ? 96.0f : 140.0f)) return result;
    const auto right = width - 44;
    const auto left = right - panel_width;
    result.reserved_width = width - left + 6;
    result.width_handle = compose_rect_t{left - 6, 8, left - 1, height - 8};
    result.panes[0] = dock_pane_t{*visible[0], {left, 8, right, height - 8}};
    if (count == 2) {
        if (columns) {
            const auto first_width = std::clamp((panel_width - 6) * layout.split(),
                dock_minimum_width, panel_width - 6 - dock_minimum_width);
            const auto split = left + first_width;
            result.panes[0]->bounds.right = split;
            result.split_handle = compose_rect_t{split, 8, split + 6, height - 8};
            result.panes[1] = dock_pane_t{*visible[1], {split + 6, 8, right, height - 8}};
        } else {
            const auto split = 8 + (height - 22) * layout.split();
            result.panes[0]->bounds.bottom = split;
            result.split_handle = compose_rect_t{left, split, right, split + 6};
            result.panes[1] = dock_pane_t{*visible[1], {left, split + 6, right, height - 8}};
        }
    }
    return result;
}
auto dock_geometry_t::is_visible(dock_panel_t panel) const -> bool {
    return std::ranges::any_of(panes, [panel](const auto& pane) { return pane && pane->panel == panel; });
}
auto dock_geometry_t::is_side_by_side() const -> bool {
    return panes[0] && panes[1] && panes[0]->bounds.right < panes[1]->bounds.left;
}
auto dock_geometry_t::split_extent() const -> double {
    if (!panes[0] || !panes[1]) return 1;
    const auto& first = panes[0]->bounds;
    const auto& last = panes[1]->bounds;
    return std::max(1.0, is_side_by_side() ? last.right - first.left - 6 : last.bottom - first.top - 6);
}
void render_dock_controls(nk_context* context, const dock_geometry_t& geometry,
                          ui_layout_preferences_t& layout, dock_pointer_t& pointer,
                          const std::array<bool, dock_panel_count>& available, bool input_available) {
    if (pointer.is_active() && !nk_input_is_mouse_down(&context->input, NK_BUTTON_LEFT) &&
        !nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT)) pointer = {};
    if (!input_available) { pointer = {}; nk_widget_disable_begin(context); }
    update_resize(context, geometry, layout, pointer);
    if (geometry.width_handle)
        render_handle(context, *geometry.width_handle, "Resize dock width", input_available ? dock_capture_t::width : dock_capture_t::none, geometry, pointer);
    if (geometry.split_handle)
        render_handle(context, *geometry.split_handle, "Resize dock split", input_available ? dock_capture_t::split : dock_capture_t::none, geometry, pointer);
    render_strip(context, geometry, layout, pointer, input_available ? available : std::array<bool, dock_panel_count>{}, input_available);
    if (!input_available) { pointer = {}; nk_widget_disable_end(context); }
}
} // namespace wormedit::ui
