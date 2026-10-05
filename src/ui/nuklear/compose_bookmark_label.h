#pragma once

#include <functional>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace wormedit::ui {

struct bookmark_label_box_t {
    float x{};
    float y{};
    float w{};
    float h{};
    // The text to draw, already shortened with "…" when it did not fit.
    std::string text;
};

struct bookmark_label_field_t {
    float x{};
    float y{};
    float w{};
    float h{};
};

using bookmark_text_width_t = std::function<float(std::string_view)>;

struct bookmark_rename_line_t {
    std::uint64_t bookmark_id{};
    double y{};
    // Only a bookmark with a drawn name has a label zone to double-click.
    bool labelled{true};
};

// The hover and double-click target: a band across the whole viewport width (`field_width`
// document pixels), 6 px each side of a line plus `label_height` above a labelled line, where the
// name is drawn. An exact line hit always beats a label zone; among label zones the line nearest
// below the pointer wins, and equal distances go to the lower ID. Hover and click share this so
// the highlight is exactly what responds.
[[nodiscard]] auto bookmark_double_click_target_at(std::span<const bookmark_rename_line_t> lines,
                                                   double x, double y, double field_width,
                                                   double label_height)
    -> std::optional<std::uint64_t>;

// Places a bookmark name right-aligned on the field's right edge (4 px padding),
// sitting just above its line at `line_y` and inside the field. Names longer
// than half the field width end in "…". Returns nothing for an empty or invalid
// UTF-8 name, when even "…" does not fit, or when the box would overlap
// `previous` (the earlier label wins).
[[nodiscard]] auto layout_bookmark_label(std::string_view label, const bookmark_text_width_t& width_of,
                                         float font_height, float line_y,
                                         const bookmark_label_field_t& field,
                                         const std::optional<bookmark_label_box_t>& previous)
    -> std::optional<bookmark_label_box_t>;

} // namespace wormedit::ui
