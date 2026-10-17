#pragma once

#include "ui/nuklear/chrome.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

namespace wormedit::ui::nuklear {

enum class catalog_layout_t { sounds, bga };
inline constexpr float catalog_header_height = 22.0f;

struct catalog_columns_t {
    float body_width{};
    float id_offset{};
    float name_offset{};
    float name_width{};
    auto operator==(const catalog_columns_t&) const -> bool = default;

    static auto from(float width, float spacing, catalog_layout_t layout) -> catalog_columns_t {
        switch (layout) {
        case catalog_layout_t::sounds: {
            const auto body = width - 44 - 2 * spacing;
            return {.body_width = body, .id_offset = 6, .name_offset = 42,
                    .name_width = std::max(24.0f, body - 134)};
        }
        case catalog_layout_t::bga: {
            const auto body = width - 22 - spacing;
            return {.body_width = body, .id_offset = 4, .name_offset = 64,
                    .name_width = std::max(12.0f, body - 158)};
        }
        }
        return {};
    }
};

inline void render_catalog_header(nk_context* context, catalog_layout_t layout) {
    nk_layout_row_dynamic(context, catalog_header_height, 1);
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    auto* canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, bounds, 0, chrome::color(theme::UiColor::surface_hover));
    constexpr std::array labels{"ID", "Name", "Uses", "State"};
    const auto columns = catalog_columns_t::from(bounds.w, context->style.window.spacing.x, layout);
    const std::array offsets{columns.id_offset, columns.name_offset,
                             columns.body_width - 86, columns.body_width - 48};
    const std::array widths{30.0f, columns.name_width, 32.0f, 48.0f};
    for (std::size_t index = 0; index < labels.size(); ++index)
        nk_draw_text(canvas, nk_rect(bounds.x + offsets[index], bounds.y + 3, widths[index], bounds.h - 6),
            labels[index], static_cast<int>(std::strlen(labels[index])), context->style.font,
            nk_rgba(0, 0, 0, 0), chrome::color(theme::UiColor::text_muted));
}

// A click on the selected choice is still a choice: callers retain their reset policy.
inline auto render_catalog_use_filter(nk_context* context, std::uint8_t selected, bool blocked)
    -> std::optional<std::uint8_t> {
    constexpr std::array labels{"All", "Used", "Unused", "Missing"};
    std::optional<std::uint8_t> chosen;
    if (blocked) nk_widget_disable_begin(context);
    nk_layout_row_dynamic(context, 24, 4);
    for (std::size_t index = 0; index < labels.size(); ++index) {
        if (!chrome::mode_button(context, labels[index], nullptr, selected == index)) continue;
        selected = static_cast<std::uint8_t>(index);
        chosen = selected;
    }
    if (blocked) nk_widget_disable_end(context);
    return chosen;
}

struct catalog_capacity_t {
    std::size_t used{};
    std::size_t total{};
    std::size_t largest_free_run{};
    auto operator==(const catalog_capacity_t&) const -> bool = default;

    // Keep the existing size_t arithmetic, including its unsigned overflow behavior.
    auto is_near_capacity() const -> bool { return total != 0 && used * 10 > total * 9; }
};

inline auto grouped_catalog_count(std::size_t value) -> std::string {
    auto text = std::to_string(value);
    for (auto position = text.size(); position > 3; position -= 3)
        text.insert(position - 3, ",");
    return text;
}

inline void render_catalog_capacity(nk_context* context, const catalog_capacity_t& capacity) {
    nk_layout_row_dynamic(context, 20, 1);
    char label[64];
    const auto used = grouped_catalog_count(capacity.used);
    const auto total = grouped_catalog_count(capacity.total);
    std::snprintf(label, sizeof(label), "%s / %s IDs", used.c_str(), total.c_str());
    nk_label_colored(context, label, NK_TEXT_LEFT,
        capacity.is_near_capacity() ? nk_rgb(226, 166, 64) : context->style.text.color);
    nk_layout_row_dynamic(context, 20, 1);
    char free_run[64];
    std::snprintf(free_run, sizeof(free_run), "Largest free run: %zu", capacity.largest_free_run);
    nk_label(context, free_run, NK_TEXT_LEFT);
}

} // namespace wormedit::ui::nuklear
