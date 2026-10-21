#pragma once

#include <wormedit/core/document.h>

#include <cstdint>
#include <optional>
#include <span>

namespace wormedit {

// The one place pointer and cursor positions are rounded onto the edit grid. Every object class
// (notes, holds, timing markers, areas, resources, BGM, recipes) goes through these helpers, so
// turning snapping off affects them all at once.

// The edit grid. Standard space steps by 1/denominator of a 4/4 measure from each measure head, so
// a 5/4 measure holds five 1/4 steps. Authored space divides each measure's own length into
// `denominator` equal steps; a measure whose rows it cannot divide exactly keeps standard steps.
// Rows past the chart end are standard measures, where both spaces agree.
struct snap_grid_t {
    std::uint64_t denominator{};
    quantization_space_t space{quantization_space_t::standard_measure};

    [[nodiscard]] auto operator==(const snap_grid_t&) const -> bool = default;
};

// Smallest denominator containing every supplied position on the measure-local grid.
// Includes hold ends supplied by the caller. Rejects empty or invalid positions.
[[nodiscard]] auto fitting_snap_denominator(const chart_document_t& chart,
    const measure_index_t& index, std::span<const local_position_t> positions)
    -> std::optional<std::uint64_t>;

// The grid edits should snap to: the user's grid while snapping is on, or the chart's finest
// division (one standard row) while it is off. Zero denominator when the chart has no divisions.
[[nodiscard]] auto effective_edit_snap_grid(const chart_document_t& chart, snap_grid_t grid,
                                            bool snap_enabled) noexcept -> snap_grid_t;

// Rows between grid points in the measure [begin, begin + capacity). Nullopt when the
// denominator is zero or does not divide the chart's standard-measure divisions.
[[nodiscard]] auto snap_step(std::uint64_t divisions_per_standard_measure, snap_grid_t grid,
                             std::uint64_t measure_capacity) noexcept
    -> std::optional<std::uint64_t>;
[[nodiscard]] auto snap_step(const chart_document_t& chart, snap_grid_t grid,
                             std::uint64_t measure_capacity) noexcept
    -> std::optional<std::uint64_t>;

// The step a next/previous move from `absolute_row` takes: the step of the measure it moves
// within (the preceding one when moving back from a measure head).
[[nodiscard]] auto adjacent_snap_step(const chart_document_t& chart, const measure_index_t& index,
                                      std::uint64_t absolute_row, snap_grid_t grid,
                                      bool forward) noexcept -> std::optional<std::uint64_t>;

// The same step as a length in whole notes, for a measure `measure_length` whole notes long.
[[nodiscard]] auto snap_length_step(std::uint64_t divisions_per_standard_measure, snap_grid_t grid,
                                    rational_t measure_length) noexcept
    -> std::optional<rational_t>;

// Where a row sits in its measure's grid: rows from the measure head, and the rows one whole
// grid spans (a standard measure, or in authored space the measure itself). Snap classes are
// fractions `row / period`. Rows past the chart end use standard measures.
struct grid_phase_t {
    std::uint64_t row{};
    std::uint64_t period{};
};
[[nodiscard]] auto grid_phase(const chart_document_t& chart, const measure_index_t& index,
                              std::uint64_t absolute_row, quantization_space_t space) noexcept
    -> grid_phase_t;

// Rounds onto a measure-relative grid, including measure heads; ties go earlier.
// Nullopt when the denominator is zero or does not divide the chart's divisions.
[[nodiscard]] auto snap_absolute_row(const chart_document_t& chart, std::uint64_t absolute_row,
                                     snap_grid_t grid)
    -> std::optional<std::uint64_t>;

// Indexed overload avoids rebuilding measure geometry. Like the compatibility overload,
// it can round to the chart end or its arithmetic standard-measure tail.
[[nodiscard]] auto snap_absolute_row(const chart_document_t& chart, const measure_index_t& index,
                                     std::uint64_t absolute_row, snap_grid_t grid) noexcept
    -> std::optional<std::uint64_t>;

// Nearest valid grid row in [0, canvas_rows), with ties going earlier. A shortened
// measure's next head is a grid point even when its length is less than one step.
[[nodiscard]] auto snap_canvas_row(const chart_document_t& chart, const measure_index_t& index,
                                   long double absolute_row, snap_grid_t grid,
                                   std::uint64_t canvas_rows) noexcept
    -> std::optional<std::uint64_t>;

// Strict next/previous measure-local grid point. Fractional steps support playback
// beats in low-resolution charts. The virtual tail starts at index.total_rows.
[[nodiscard]] auto adjacent_measure_grid_row(const measure_index_t& index,
    long double absolute_row, long double step, std::uint64_t standard_measure_rows,
    std::uint64_t canvas_rows, bool forward) noexcept -> std::optional<long double>;

[[nodiscard]] auto adjacent_snap_canvas_row(const chart_document_t& chart,
    const measure_index_t& index, long double absolute_row, snap_grid_t grid,
    std::uint64_t canvas_rows, bool forward) noexcept -> std::optional<std::uint64_t>;

// Snaps a chart position. The result is nullopt when the position, denominator or resulting row
// is outside the chart.
[[nodiscard]] auto snap_position(const chart_document_t& chart, const measure_index_t& index,
                                 local_position_t position, snap_grid_t grid)
    -> std::optional<local_position_t>;

// For edits that may be exact: a zero denominator keeps the row or position, which must
// still be inside the canvas or chart; otherwise these snap like the functions above.
[[nodiscard]] auto snap_canvas_row_or_exact(const chart_document_t& chart,
    const measure_index_t& index, std::uint64_t absolute_row, snap_grid_t grid,
    std::uint64_t canvas_rows) noexcept -> std::optional<std::uint64_t>;
[[nodiscard]] auto snap_position_or_exact(const chart_document_t& chart,
    const measure_index_t& index, local_position_t position, snap_grid_t grid)
    -> std::optional<local_position_t>;

// Snaps a span boundary, which may also be the chart end: the end is an extra grid point,
// taken when it is nearer than the measure-local one.
[[nodiscard]] auto snap_span_position(const chart_document_t& chart, const measure_index_t& index,
                                      local_position_t position, snap_grid_t grid)
    -> std::optional<local_position_t>;

} // namespace wormedit
