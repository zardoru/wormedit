#include "ui/nuklear/tooltip.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <string>
#include <utility>

namespace wormedit::ui::nuklear::tooltip {
namespace {

struct nk_rect app_viewport{};
// Space between the popup's clip edge and its text. Nuklear scissors popup content at the
// layout edge, so text starting exactly there loses the left side of its first glyph.
constexpr float text_inset = 4.0F;

auto codepoint_size(std::string_view text, std::size_t offset) -> std::size_t {
    const auto first = static_cast<unsigned char>(text[offset]);
    const std::size_t length = first < 0x80 ? 1 : (first & 0xE0) == 0xC0 ? 2
                              : (first & 0xF0) == 0xE0 ? 3
                              : (first & 0xF8) == 0xF0 ? 4 : 1;
    if (length > text.size() - offset) return 1;
    for (std::size_t index = 1; index < length; ++index)
        if ((static_cast<unsigned char>(text[offset + index]) & 0xC0) != 0x80) return 1;
    return length;
}

auto text_width(nk_context* context, std::string_view text) -> float {
    const auto* font = context->style.font;
    return font->width(font->userdata, font->height, text.data(), static_cast<int>(text.size()));
}

auto split_lines(nk_context* context, std::string_view text, float max_width)
    -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::string line;
    for (std::size_t offset = 0; offset < text.size();) {
        if (text[offset] == '\n') {
            lines.push_back(std::exchange(line, {}));
            ++offset;
            continue;
        }
        const auto length = codepoint_size(text, offset);
        line.append(text.substr(offset, length));
        offset += length;
        if (text_width(context, line) <= max_width || line.size() == length) continue;

        const auto break_at = line.find_last_of(" \t", line.size() - length);
        if (break_at != std::string::npos && break_at != 0) {
            lines.push_back(line.substr(0, break_at));
            line.erase(0, break_at);
        } else {
            const auto last = line.substr(line.size() - length);
            line.resize(line.size() - length);
            if (!line.empty()) lines.push_back(std::exchange(line, {}));
            line = last;
        }
    }
    lines.push_back(std::move(line));
    return lines;
}

auto natural_width(nk_context* context, std::string_view text) -> float {
    auto widest = 0.0F;
    std::size_t begin{};
    for (std::size_t offset = 0; offset <= text.size(); ++offset) {
        if (offset != text.size() && text[offset] != '\n') continue;
        widest = std::max(widest, text_width(context, text.substr(begin, offset - begin)));
        begin = offset + 1;
    }
    return widest;
}

// Nuklear draws windows in list order, so the last visible window containing the pointer is the
// one on top. Minimized windows are hit only on their header.
auto topmost_window_at_pointer(const nk_context* context) -> const struct nk_window* {
    const struct nk_window* top{};
    for (const auto* window = context->begin; window; window = window->next) {
        if ((window->flags & NK_WINDOW_HIDDEN) != 0) continue;
        auto bounds = window->bounds;
        if ((window->flags & NK_WINDOW_MINIMIZED) != 0) bounds.h = window->layout->header_height;
        if (nk_input_is_mouse_hovering_rect(&context->input, bounds)) top = window;
    }
    return top;
}

auto initial_padding(const nk_context* context) -> struct nk_vec2 {
    return {
        std::max(context->style.window.popup_padding.x, context->style.window.menu_padding.x),
        std::max(context->style.window.popup_padding.y, context->style.window.menu_padding.y),
    };
}

auto initial_border(const nk_context* context) -> float {
    return std::max(context->style.window.popup_border, context->style.window.menu_border);
}

} // namespace

void set_viewport(struct nk_rect viewport) noexcept {
    app_viewport = viewport;
}

auto viewport() noexcept -> const struct nk_rect& {
    return app_viewport;
}

auto plan_text(nk_context* context, std::string_view text, float preferred_outer_width)
    -> std::optional<text_plan_t> {
    if (!context || !context->style.font || app_viewport.w <= 0.0F || app_viewport.h <= 0.0F ||
        text.empty() || !std::isfinite(preferred_outer_width))
        return std::nullopt;
    const auto padding = initial_padding(context);
    const auto border = initial_border(context);
    const auto max_outer_width = std::min(std::max(1.0F, preferred_outer_width), app_viewport.w);
    const auto text_padding_x = 2.0F * context->style.text.padding.x + text_inset;
    const auto minimum_width = context->style.font->height + text_padding_x +
                               2.0F * padding.x + 2.0F * border;
    const auto outer_width = std::min(std::max(natural_width(context, text) +
                                                   text_padding_x + 2.0F * padding.x + 2.0F * border,
                                               minimum_width), max_outer_width);
    const auto content_width = outer_width - text_padding_x - 2.0F * padding.x - 2.0F * border;
    if (content_width < context->style.font->height) return std::nullopt;

    text_plan_t plan;
    plan.outer_width = outer_width;
    plan.lines = split_lines(context, text, content_width);
    const auto row_height = context->style.font->height + context->style.text.padding.y * 2.0F +
                            context->style.window.min_row_height_padding * 2.0F;
    plan.content_height = static_cast<float>(plan.lines.size()) *
        (std::max(context->style.font->height, row_height) + context->style.window.spacing.y);
    if (std::ranges::any_of(plan.lines, [&](const auto& line) {
            return text_width(context, line) > content_width;
        }))
        return std::nullopt;
    if (plan.lines.empty() || !place(context, plan.outer_width, plan.content_height))
        return std::nullopt;
    return plan;
}

auto place(nk_context* context, float outer_width, float content_height)
    -> std::optional<placement_t> {
    if (!context || !context->current || !context->current->layout ||
        app_viewport.w <= 0.0F || app_viewport.h <= 0.0F ||
        !std::isfinite(outer_width) || !std::isfinite(content_height))
        return std::nullopt;
    if (!context->style.font) return std::nullopt;
    const auto padding = initial_padding(context);
    const auto border = initial_border(context);
    const auto width = std::min(outer_width, app_viewport.w);
    const auto height = content_height + 2.0F * padding.y + 2.0F * border;
    if (width - 2.0F * context->style.text.padding.x - text_inset -
            2.0F * padding.x - 2.0F * border < context->style.font->height ||
        height > app_viewport.h)
        return std::nullopt;

    const auto mouse_x = std::floor(context->input.mouse.pos.x + 1.0F);
    const auto mouse_y = std::floor(context->input.mouse.pos.y + 1.0F);
    const auto right = app_viewport.x + app_viewport.w;
    const auto bottom = app_viewport.y + app_viewport.h;
    const auto preferred_x = mouse_x + 12.0F;
    const auto preferred_y = mouse_y + 12.0F;
    const auto x = std::clamp(preferred_x + width <= right ? preferred_x : mouse_x - width - 12.0F,
                              app_viewport.x, right - width);
    const auto y = std::clamp(preferred_y + height <= bottom ? preferred_y : mouse_y - height - 12.0F,
                              app_viewport.y, bottom - height);
    return placement_t{
        .outer_bounds = nk_rect(x, y, width, height),
        .offset = nk_vec2(x - mouse_x, y - mouse_y),
    };
}

auto begin(nk_context* context, const placement_t& placement) -> bool {
    return context && nk_tooltip_begin_offset(context, placement.outer_bounds.w, NK_TOP_LEFT,
                                              placement.offset);
}

void draw_text(nk_context* context, const text_plan_t& plan) {
    if (!context || !context->style.font) return;
    // The column gap would add to the inset, which plan_text does not measure, so it is zero here.
    const auto spacing = context->style.window.spacing;
    nk_style_push_vec2(context, &context->style.window.spacing, nk_vec2(0.0F, spacing.y));
    for (const auto& line : plan.lines) {
        nk_layout_row_template_begin(context, context->style.font->height);
        nk_layout_row_template_push_static(context, text_inset);
        nk_layout_row_template_push_dynamic(context);
        nk_layout_row_template_end(context);
        nk_spacer(context);
        nk_text(context, line.data(), static_cast<int>(line.size()), NK_TEXT_LEFT);
    }
    nk_style_pop_vec2(context);
}

void end(nk_context* context) {
    if (context) nk_tooltip_end(context);
}

auto hovered(const nk_context* context, struct nk_rect bounds) -> bool {
    if (!context || !context->current) return false;
    if (!nk_input_is_mouse_hovering_rect(&context->input, bounds)) return false;
    return topmost_window_at_pointer(context) == context->current;
}

void text(nk_context* context, std::string_view value, float preferred_outer_width) {
    const auto plan = plan_text(context, value, preferred_outer_width);
    if (!plan) return;
    const auto placement = place(context, plan->outer_width, plan->content_height);
    if (!placement || !begin(context, *placement)) return;
    draw_text(context, *plan);
    end(context);
}

} // namespace wormedit::ui::nuklear::tooltip
