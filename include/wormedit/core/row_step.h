#pragma once

#include <wormedit/core/export_cut_axis.h>
#include <wormedit/core/snap.h>

#include <cstdint>
#include <optional>

namespace wormedit {

// The single row-stepping path for moving a cursor or playhead one step at a time. The parked
// edit cursor and the playing playhead both resolve through it, so the two never disagree about
// which rows exist: a step is a grid step (snap) or a quarter-note step (beat) from the row's
// own measure, and a landing inside an export cut moves to the nearest visible grid row in the
// travel direction.
enum class row_step_resolution_t { snap, beat };

struct row_step_t {
    row_step_resolution_t resolution{row_step_resolution_t::snap};
    snap_grid_t grid{}; // Used by the snap resolution only.
};

// Strict next/previous grid row of `row`; nullopt past the canvas or for an unusable grid.
[[nodiscard]] auto adjacent_step_row(const chart_document_t& chart, const measure_index_t& index,
                                     long double row, row_step_t step, std::uint64_t canvas_rows,
                                     bool forward) noexcept -> std::optional<std::uint64_t>;

// `row` if it is visible; otherwise the first grid row at or past the visible edge in the travel
// direction, falling back to the other direction when the cut reaches the canvas end.
[[nodiscard]] auto visible_step_row(const chart_document_t& chart, const measure_index_t& index,
                                    const export_cut_axis_t* cut_axis, row_step_t step,
                                    std::uint64_t canvas_rows, std::uint64_t row, bool forward)
    -> std::uint64_t;

// `count` steps from `row`, each one resolved to a visible row, so hidden rows are never
// counted. Running into the canvas edge stops there. Nullopt when the step is unusable.
[[nodiscard]] auto step_visible_rows(const chart_document_t& chart, const measure_index_t& index,
                                     const export_cut_axis_t* cut_axis, row_step_t step,
                                     std::uint64_t canvas_rows, long double row, bool forward,
                                     std::uint64_t count = 1) -> std::optional<std::uint64_t>;

} // namespace wormedit
