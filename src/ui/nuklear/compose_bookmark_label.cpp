#include "ui/field/rect.h"
#include "core/text_util.h"
#include "ui/nuklear/text_fit.h"
#include "ui/nuklear/compose_bookmark_label.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <vector>

namespace wormedit::ui {

auto bookmark_rename_line_t::hit_extent(double label_height) const -> std::pair<double, double> {
    const auto above = labelled ? std::max(compose_bookmark_line_reach, label_height)
                                : compose_bookmark_line_reach;
    return {y - above, y + compose_bookmark_line_reach};
}

namespace {

constexpr float padding = 4.0f;

struct bookmark_label_width_t {
    const bookmark_text_width_t& width_of;
    auto operator()(std::string_view prefix, std::string_view suffix) const -> float {
        return width_of(std::string(prefix) + std::string(suffix));
    }
};

auto shorten_to_fit(std::string_view label, const bookmark_text_width_t& width_of,
                    float maximum_width) -> std::optional<std::string> {
    if (width_of(label) <= maximum_width) return std::string(label);
    const auto prefix = chrome::fit_text_prefix_reverse(label, bookmark_label_width_t{width_of}, maximum_width);
    if (!prefix) return std::nullopt;
    return std::string(label.substr(0, *prefix)) + std::string(chrome::text_ellipsis);
}

auto overlaps(const bookmark_label_box_t& a, const bookmark_label_box_t& b) -> bool {
    // Preserve the callback's historical rule for a zero/negative measured box.
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0)
        return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
    return has_rect_overlap({a.x, a.y, a.w, a.h}, {b.x, b.y, b.w, b.h});
}

} // namespace

namespace {

// The line inside its hit reach, nearest first and then lowest ID.
auto nearest_line_within(std::span<const bookmark_rename_line_t> lines, double y)
    -> std::optional<std::uint64_t> {
    auto result = std::optional<std::uint64_t>{};
    auto best = compose_bookmark_line_reach;
    for (const auto& line : lines) {
        if (line.bookmark_id == 0 || !std::isfinite(line.y)) continue;
        const auto distance = std::abs(y - line.y);
        if (distance > best || (distance == best && result && line.bookmark_id >= *result)) continue;
        result = line.bookmark_id;
        best = distance;
    }
    return result;
}

// The labelled line whose name box (just above the line) contains `y`; the closest line wins.
auto label_zone_target(std::span<const bookmark_rename_line_t> lines, double y,
                       double label_height) -> std::optional<std::uint64_t> {
    auto result = std::optional<std::uint64_t>{};
    auto best = label_height;
    for (const auto& line : lines) {
        // The name sits above its line: y grows downward, so that is a smaller y.
        const auto above = line.y - y;
        if (line.bookmark_id == 0 || !line.labelled || !std::isfinite(line.y) || above <= 0.0 ||
            above > best || (above == best && result && line.bookmark_id >= *result)) continue;
        result = line.bookmark_id;
        best = above;
    }
    return result;
}

} // namespace

auto bookmark_double_click_target_at(std::span<const bookmark_rename_line_t> lines,
                                     double x, double y, double field_width, double label_height)
    -> std::optional<std::uint64_t> {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(field_width) ||
        !std::isfinite(label_height) || x < 0.0 || x > field_width) return std::nullopt;
    if (const auto exact = nearest_line_within(lines, y)) return exact;
    return label_zone_target(lines, y, label_height);
}

auto layout_bookmark_label(std::string_view label, const bookmark_text_width_t& width_of,
                           float font_height, float line_y, const bookmark_label_field_t& field,
                           const std::optional<bookmark_label_box_t>& previous)
    -> std::optional<bookmark_label_box_t> {
    if (label.empty() || field.w <= 2.0f * padding || font_height <= 0.0f) return std::nullopt;
    if (text_util::invalid_utf8_offset(label)) return std::nullopt;
    const auto text = shorten_to_fit(label, width_of,
                                     std::min(field.w * 0.5f, field.w - 2.0f * padding));
    if (!text) return std::nullopt;
    const auto width = width_of(*text);
    bookmark_label_box_t box{
        .x = field.x + field.w - padding - width,
        .y = std::clamp(line_y - font_height, field.y,
                        std::max(field.y, field.y + field.h - font_height)),
        .w = width,
        .h = font_height,
        .text = *text,
    };
    if (previous && overlaps(box, *previous)) return std::nullopt;
    return box;
}

} // namespace wormedit::ui
