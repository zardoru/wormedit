#pragma once

#include <wormedit/core/measure.h>

#include <cstdint>
#include <string>

// The pure arithmetic of Alt+dragging a bar line: a pointer delta in whole notes, snapped to the
// active grid, becomes a new measure length and the meter that states it. No widget toolkit.
struct worm_ui_measure_resize_t {
    // False when the length cannot be stated as a meter; `label` then holds the reason.
    bool valid{};
    std::uint32_t numerator{};
    std::uint32_t denominator{};
    // The snapped length in whole notes.
    double length_whole_notes{};
    // "7/8", or the reason the drag cannot commit.
    std::string label;
};

inline constexpr std::uint64_t worm_ui_measure_resize_maximum_measures = 4;

// `old_length` is numerator/denominator whole notes. The new length is the old one plus
// `delta_whole_notes`, rounded to a whole number of `step` whole notes (see
// wormedit::snap_length_step), at least one step and at most four standard measures. The meter is
// that fraction in lowest terms, widened to a denominator of at least 4; a denominator that is not
// a power of two cannot be written.
[[nodiscard]] auto worm_ui_resize_measure(std::uint64_t old_numerator, std::uint64_t old_denominator,
                                          double delta_whole_notes, wormedit::rational_t step)
    -> worm_ui_measure_resize_t;

// Shift+Alt drag: only the bar line between two measures moves. The first measure grows by the
// pointer delta and the second shrinks by the same amount. The line moves along the grid of the
// measure it moves into (`step_before` for the first, `step_after` for the second), leaving that
// measure at least one step. Each length must lie on its own grid, and both results must be
// writable as meters.
struct worm_ui_bar_line_move_t {
    bool valid{};
    std::uint32_t first_numerator{}, first_denominator{};
    std::uint32_t second_numerator{}, second_denominator{};
    // The first measure's snapped length in whole notes, for placing the ghost line.
    double first_length_whole_notes{};
    // "7/8 | 9/8", or the reason the drag cannot commit.
    std::string label;
};

[[nodiscard]] auto worm_ui_move_bar_line(std::uint64_t first_numerator, std::uint64_t first_denominator,
                                         std::uint64_t second_numerator, std::uint64_t second_denominator,
                                         double delta_whole_notes, wormedit::rational_t step_before,
                                         wormedit::rational_t step_after)
    -> worm_ui_bar_line_move_t;
