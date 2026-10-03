#pragma once

#include <optional>

struct worm_ui_text_selection_range_t {
    int start{};
    int end{};
};

// Pure selection decision used by the frontend adapter and its state test.
[[nodiscard]] inline auto worm_ui_nuklear_double_click_selection(bool single_line,
                                                                  bool double_clicked,
                                                                  int text_length)
    -> std::optional<worm_ui_text_selection_range_t> {
    if (!single_line || !double_clicked || text_length <= 0) return std::nullopt;
    return worm_ui_text_selection_range_t{0, text_length};
}
