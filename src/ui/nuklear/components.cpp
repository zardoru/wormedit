#include "ui/nuklear/chrome.h"

#include <algorithm>
#include <cstring>

namespace wormedit::ui::chrome {
namespace {

using theme::UiColor;

constexpr nk_color transparent{0, 0, 0, 0};

struct hit_t {
    bool hovered{};
    // Hovered and not covered by another window; only tooltips use this.
    bool tooltip_hovered{};
    bool pressed{};
    bool clicked{};
};

// `state` is what nk_widget() returned for the cell that contains `area`. Hover is reported
// even for disabled components so their tooltips can explain themselves; pressed and clicked
// never are.
auto hit_test(const nk_context* context, nk_widget_layout_states state, struct nk_rect area,
              bool enabled) -> hit_t {
    hit_t hit;
    if (state == NK_WIDGET_INVALID || !context->current || !context->current->layout) return hit;
    if (context->active != context->current && !popup_panel(context)) return hit;
    const auto& input = context->input;
    hit.hovered = nk_input_is_mouse_hovering_rect(&input, area) &&
                  nk_input_is_mouse_hovering_rect(&input, context->current->layout->clip);
    hit.tooltip_hovered = hit.hovered && nuklear::tooltip::hovered(context, area);
    if (!hit.hovered || !enabled || state == NK_WIDGET_DISABLED) return hit;
    hit.pressed = nk_input_is_mouse_down(&input, NK_BUTTON_LEFT) != 0;
    hit.clicked = nk_input_has_mouse_click_in_button_rect(&input, NK_BUTTON_LEFT, area) != 0 &&
                  nk_input_is_mouse_pressed(&input, NK_BUTTON_LEFT) != 0;
    return hit;
}

auto state_of(const hit_t& hit, bool enabled) -> ControlState {
    if (!enabled) return ControlState::disabled;
    return hit.pressed ? ControlState::active : hit.hovered ? ControlState::hover
                                                            : ControlState::normal;
}

auto text_width(const nk_context* context, const char* text, int length) -> float {
    const auto* font = context->style.font;
    return font && font->width ? font->width(font->userdata, font->height, text, length) : 0.0f;
}

void draw_centered_text(nk_context* context, struct nk_rect area, const char* text,
                        nk_color color, nk_text_alignment alignment = NK_TEXT_CENTERED) {
    if (!text || !text[0]) return;
    const auto* font = context->style.font;
    if (!font) return;
    auto* canvas = nk_window_get_canvas(context);
    const auto length = static_cast<int>(std::strlen(text));
    auto box = area;
    if (alignment == NK_TEXT_LEFT) {
        box.x += metrics::gap_medium;
        box.w = std::max(0.0f, box.w - metrics::gap_medium);
    } else {
        box.x += std::max(0.0f, (area.w - text_width(context, text, length)) * 0.5f);
        box.w = std::min(area.w, text_width(context, text, length) + 1.0f);
    }
    box.y += std::max(0.0f, (area.h - font->height) * 0.5f);
    box.h = font->height;
    nk_draw_text(canvas, box, text, length, font, transparent, color);
}

void paint_look(nk_context* context, struct nk_rect area, float radius, const ControlLook& look) {
    auto* canvas = nk_window_get_canvas(context);
    if (look.fill.a != 0) nk_fill_rect(canvas, area, radius, look.fill);
    if (look.stroke.a != 0) nk_stroke_rect(canvas, area, radius, 1.0f, look.stroke);
}

void tooltip_if_hovered(nk_context* context, const hit_t& hit, const char* tooltip) {
    if (hit.tooltip_hovered && tooltip && tooltip[0]) nuklear::tooltip::text(context, tooltip);
}

auto with_alpha(nk_color color, nk_byte alpha) -> nk_color {
    color.a = alpha;
    return color;
}

auto disabled_look() -> ControlLook {
    return {color(UiColor::surface), color(UiColor::border), metrics::text_faint};
}

} // namespace

auto surface_shadow_clip(struct nk_rect bounds, int elevation, struct nk_rect limit)
    -> struct nk_rect {
    const auto reach = metrics::shadow_reach(elevation);
    const auto left = std::max(bounds.x - reach.left, limit.x);
    const auto top = std::max(bounds.y - reach.top, limit.y);
    const auto right = std::min(bounds.x + bounds.w + reach.right, limit.x + limit.w);
    const auto bottom = std::min(bounds.y + bounds.h + reach.bottom, limit.y + limit.h);
    return nk_rect(left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top));
}

void paint_surface(struct nk_command_buffer* canvas, struct nk_rect bounds, int elevation,
                   struct nk_rect limit) {
    if (!canvas) return;
    const auto clip = canvas->clip;
    const auto shadow = metrics::elevation(elevation);
    if (shadow.steps > 0) {
        nk_push_scissor(canvas, surface_shadow_clip(bounds, elevation, limit));
        for (int step = 1; step <= shadow.steps; ++step) {
            const auto grow = static_cast<float>(step);
            nk_fill_rect(canvas,
                         nk_rect(bounds.x - grow, bounds.y - grow + shadow.offset_y,
                                 bounds.w + 2 * grow, bounds.h + 2 * grow),
                         metrics::radius_surface + grow, nk_rgba(0, 0, 0, shadow.alpha));
        }
    }
    nk_fill_rect(canvas, bounds, metrics::radius_surface, color(UiColor::surface));
    nk_stroke_rect(canvas, bounds, metrics::radius_surface, 1.0f, color(UiColor::border_hover));
    nk_push_scissor(canvas, clip);
}

auto button_look(ButtonKind kind, ControlState state) -> ControlLook {
    if (state == ControlState::disabled) return disabled_look();
    const bool hover = state == ControlState::hover;
    const bool active = state == ControlState::active;
    switch (kind) {
    case ButtonKind::primary:
        return {active ? nk_rgb(16, 72, 178) : color(UiColor::selected),
                hover || active ? color(UiColor::border_hover) : color(UiColor::selected),
                color(UiColor::selected_text)};
    case ButtonKind::ghost:
        return {active  ? color(UiColor::surface_active)
                : hover ? color(UiColor::surface_hover)
                        : transparent,
                transparent, hover || active ? color(UiColor::text_bright) : color(UiColor::text)};
    case ButtonKind::destructive:
        return {active  ? with_alpha(metrics::destructive, 90)
                : hover ? with_alpha(metrics::destructive, 48)
                        : transparent,
                metrics::destructive, metrics::destructive_text};
    case ButtonKind::secondary:
        break;
    }
    return {active ? color(UiColor::surface_active) : color(UiColor::surface_hover),
            hover || active ? color(UiColor::border_hover) : color(UiColor::border),
            hover || active ? color(UiColor::text_bright) : color(UiColor::text)};
}

auto chip_look(bool chosen, ControlState state) -> ControlLook {
    if (state == ControlState::disabled) return disabled_look();
    const bool raised = state == ControlState::hover || state == ControlState::active;
    if (chosen)
        return {color(UiColor::selected),
                color(UiColor::border_hover), color(UiColor::selected_text)};
    return {state == ControlState::active ? color(UiColor::surface_active)
                                          : color(UiColor::surface_hover),
            raised ? color(UiColor::border_hover) : color(UiColor::border),
            raised ? color(UiColor::text_bright) : color(UiColor::text)};
}

auto Button::render(nk_context* context) const -> bool {
    struct nk_rect bounds{};
    const auto cell = nk_widget(&bounds, context);
    if (cell == NK_WIDGET_INVALID) return false;
    const auto hit = hit_test(context, cell, bounds, enabled);
    paint_look(context, bounds, metrics::radius_control, button_look(kind, state_of(hit, enabled)));
    draw_centered_text(context, bounds, label,
                       button_look(kind, state_of(hit, enabled)).text);
    tooltip_if_hovered(context, hit, tooltip);
    return enabled && hit.clicked;
}

auto Checkbox::render(nk_context* context) const -> bool {
    struct nk_rect bounds{};
    const auto cell = nk_widget(&bounds, context);
    if (cell == NK_WIDGET_INVALID) return false;
    const auto hit = hit_test(context, cell, bounds, enabled);
    auto* canvas = nk_window_get_canvas(context);
    const auto box = nk_rect(bounds.x + metrics::gap_small,
                             bounds.y + (bounds.h - metrics::checkbox_box) * 0.5f,
                             metrics::checkbox_box, metrics::checkbox_box);
    const bool lit = hit.hovered && enabled;
    if (!enabled) {
        nk_stroke_rect(canvas, box, 3.0f, 1.5f, color(UiColor::border));
    } else if (state == CheckState::off) {
        nk_stroke_rect(canvas, box, 3.0f, 1.5f,
                       lit ? color(UiColor::text_bright) : color(UiColor::border_hover));
    } else {
        nk_fill_rect(canvas, box, 3.0f, color(UiColor::selected));
        nk_stroke_rect(canvas, box, 3.0f, 1.5f,
                       lit ? color(UiColor::border_hover) : color(UiColor::selected));
        const auto mark = color(UiColor::selected_text);
        if (state == CheckState::on) {
            nk_stroke_line(canvas, box.x + 3.5f, box.y + 8.5f, box.x + 6.8f, box.y + 11.8f, 2.0f, mark);
            nk_stroke_line(canvas, box.x + 6.8f, box.y + 11.8f, box.x + 12.5f, box.y + 4.8f, 2.0f, mark);
        } else {
            nk_stroke_line(canvas, box.x + 4.0f, box.y + 8.0f, box.x + 12.0f, box.y + 8.0f, 2.0f, mark);
        }
    }
    const auto label_area = nk_rect(box.x + box.w + metrics::gap_medium, bounds.y,
                                    std::max(0.0f, bounds.x + bounds.w - box.x - box.w - metrics::gap_medium),
                                    bounds.h);
    draw_centered_text(context, nk_rect(label_area.x - metrics::gap_medium, label_area.y,
                                        label_area.w + metrics::gap_medium, label_area.h),
                       label,
                       !enabled ? metrics::text_faint
                                : lit ? color(UiColor::text_bright) : color(UiColor::text),
                       NK_TEXT_LEFT);
    tooltip_if_hovered(context, hit, tooltip);
    return enabled && hit.clicked;
}

auto segment_at(struct nk_rect bounds, int count, float x) -> int {
    if (count <= 0 || bounds.w <= 0.0f || x < bounds.x || x >= bounds.x + bounds.w) return -1;
    const auto index = static_cast<int>((x - bounds.x) / (bounds.w / static_cast<float>(count)));
    return std::clamp(index, 0, count - 1);
}

auto segment_step(int selected, int count, int delta) -> int {
    if (count <= 0) return -1;
    return std::clamp(selected + delta, 0, count - 1);
}

namespace {

// Keyboard focus follows clicks: a click inside takes it, a click outside gives it up, and
// Left/Right step the selection while it is held.
auto segmented_input(const nk_context* context, const chrome::Segmented& control,
                     struct nk_rect bounds, const hit_t& hit) -> int {
    int result = control.selected;
    if (!control.focused || !control.enabled) return result;
    const auto& input = context->input;
    if (nk_input_is_mouse_pressed(&input, NK_BUTTON_LEFT) &&
        !nk_input_is_mouse_hovering_rect(&input, bounds))
        *control.focused = false;
    if (hit.clicked) *control.focused = true;
    if (!*control.focused) return result;
    if (nk_input_is_key_pressed(&input, NK_KEY_LEFT))
        result = chrome::segment_step(result, control.count, -1);
    if (nk_input_is_key_pressed(&input, NK_KEY_RIGHT))
        result = chrome::segment_step(result, control.count, 1);
    return result;
}

void draw_segment(nk_context* context, const chrome::Segmented& control, struct nk_rect segment,
                  int index, int chosen_index, bool lit) {
    auto* canvas = nk_window_get_canvas(context);
    const bool chosen = index == chosen_index;
    const auto inset = nk_rect(segment.x + 2.0f, segment.y + 2.0f, segment.w - 4.0f, segment.h - 4.0f);
    if (chosen)
        nk_fill_rect(canvas, inset, metrics::radius_control - 1.0f,
                     control.enabled ? color(UiColor::selected) : color(UiColor::surface_active));
    else if (lit)
        nk_fill_rect(canvas, inset, metrics::radius_control - 1.0f, color(UiColor::surface_active));
    if (index > 0 && !chosen && index - 1 != chosen_index)
        nk_stroke_line(canvas, segment.x, segment.y + 5.0f, segment.x, segment.y + segment.h - 5.0f,
                       1.0f, color(UiColor::border));
    const auto text = !control.enabled ? metrics::text_faint
                      : chosen         ? color(UiColor::selected_text)
                      : lit            ? color(UiColor::text_bright)
                                       : color(UiColor::text);
    draw_centered_text(context, segment, control.labels[index], text);
}

} // namespace

auto Segmented::render(nk_context* context) const -> int {
    struct nk_rect bounds{};
    const auto cell = nk_widget(&bounds, context);
    if (cell == NK_WIDGET_INVALID || count <= 0 || !labels) return selected;
    const auto hit = hit_test(context, cell, bounds, enabled);
    int result = segmented_input(context, *this, bounds, hit);
    const auto hovered_segment =
        hit.hovered ? segment_at(bounds, count, context->input.mouse.pos.x) : -1;
    if (hit.clicked && hovered_segment >= 0) result = hovered_segment;

    auto* canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, bounds, metrics::radius_control, color(UiColor::surface_hover));
    const bool has_focus = focused && *focused && enabled;
    nk_stroke_rect(canvas, bounds, metrics::radius_control, 1.0f,
                   has_focus ? color(UiColor::border_hover) : color(UiColor::border));
    const auto width = bounds.w / static_cast<float>(count);
    for (int index = 0; index < count; ++index) {
        const auto segment =
            nk_rect(bounds.x + width * static_cast<float>(index), bounds.y, width, bounds.h);
        draw_segment(context, *this, segment, index, result, enabled && index == hovered_segment);
    }
    tooltip_if_hovered(context, hit, tooltip);
    return enabled ? result : selected;
}

namespace {

// The add chip is outlined only (no fill) until hovered, which sets it apart from value chips.
void draw_chip(nk_context* context, struct nk_rect chip, const char* label, bool outlined,
               ControlState state, const ControlLook& look) {
    auto* canvas = nk_window_get_canvas(context);
    const bool filled = !outlined || state != ControlState::normal;
    if (filled) nk_fill_rect(canvas, chip, metrics::radius_chip, look.fill);
    nk_stroke_rect(canvas, chip, metrics::radius_chip, 1.0f, look.stroke);
    draw_centered_text(context, chip, label, look.text);
}

} // namespace

auto Chip::render(nk_context* context) const -> bool {
    struct nk_rect bounds{};
    const auto cell = nk_widget(&bounds, context);
    if (cell == NK_WIDGET_INVALID) return false;
    const auto height = std::min(bounds.h, metrics::control_compact);
    const auto chip = nk_rect(bounds.x + metrics::gap_small * 0.5f,
        bounds.y + (bounds.h - height) * 0.5f,
        std::max(0.0f, bounds.w - metrics::gap_small), height);
    const auto hit = hit_test(context, cell, chip, enabled);
    const auto state = state_of(hit, enabled);
    draw_chip(context, chip, label, false, state, chip_look(chosen, state));
    tooltip_if_hovered(context, hit, tooltip);
    return enabled && hit.clicked;
}

auto ChipGrid::render(nk_context* context) const -> Result {
    Result result;
    const int slots = count + (add_label ? 1 : 0);
    const int per_row = std::max(1, columns);
    for (int first = 0; first < slots; first += per_row) {
        nk_layout_row_dynamic(context, metrics::control_compact, per_row);
        for (int column = 0; column < per_row; ++column) {
            const int slot = first + column;
            struct nk_rect bounds{};
            const auto cell = nk_widget(&bounds, context);
            if (slot >= slots || cell == NK_WIDGET_INVALID) continue;
            const bool is_add = slot >= count;
            const auto chip = nk_rect(bounds.x + metrics::gap_small * 0.5f, bounds.y,
                                      std::max(0.0f, bounds.w - metrics::gap_small), bounds.h);
            const auto hit = hit_test(context, cell, chip, enabled);
            const auto chip_state = state_of(hit, enabled);
            const auto look = chip_look(!is_add && slot == chosen, chip_state);
            draw_chip(context, chip, is_add ? add_label : labels[slot], is_add, chip_state, look);
            if (!enabled || !hit.clicked) continue;
            if (is_add) result.add = true;
            else result.chosen = slot;
        }
    }
    return result;
}

auto SelectionBar::render(nk_context* context) const -> int {
    nk_layout_row_dynamic(context, height, 1);
    struct nk_rect bounds{};
    const auto cell = nk_widget(&bounds, context);
    if (cell == NK_WIDGET_INVALID) return -1;
    auto* canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, bounds, metrics::radius_bar, color(UiColor::surface));
    nk_stroke_rect(canvas, bounds, metrics::radius_bar, 1.0f, color(UiColor::border_hover));
    draw_centered_text(context,
                       nk_rect(bounds.x + metrics::gap_small, bounds.y, bounds.w, bounds.h),
                       summary, color(UiColor::text), NK_TEXT_LEFT);

    int clicked = -1;
    auto right = bounds.x + bounds.w - metrics::gap_medium;
    const auto top = bounds.y + (bounds.h - metrics::control_default) * 0.5f;
    for (int index = action_count - 1; index >= 0; --index) {
        const auto& action = actions[index];
        const auto length = static_cast<int>(std::strlen(action.label));
        const auto width = text_width(context, action.label, length) + 2.0f * metrics::gap_large;
        const auto area = nk_rect(right - width, top, width, metrics::control_default);
        const auto hit = hit_test(context, cell, area, action.enabled);
        const auto look = button_look(action.kind, state_of(hit, action.enabled));
        paint_look(context, area, metrics::radius_control, look);
        draw_centered_text(context, area, action.label, look.text);
        tooltip_if_hovered(context, hit, action.tooltip);
        if (action.enabled && hit.clicked) clicked = index;
        right = area.x - metrics::gap_small;
    }
    return clicked;
}

} // namespace wormedit::ui::chrome
