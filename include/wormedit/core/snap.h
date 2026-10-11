#pragma once

#include <wormedit/core/document.h>

#include <cstdint>
#include <optional>
#include <span>

namespace wormedit {

// The one place pointer and cursor positions are rounded onto the edit grid. Every object class
// (notes, holds, timing markers, areas, resources, BGM, recipes) goes through these helpers, so
// turning snapping off affects them all at once.

// Smallest denominator containing every supplied position on the measure-local grid.
// Includes hold ends supplied by the caller. Rejects empty or invalid positions.
[[nodiscard]] auto fitting_snap_denominator(const chart_document_t& chart,
    const measure_index_t& index, std::span<const local_position_t> positions)
    -> std::optional<std::uint64_t>;

// The denominator edits should snap to: the user's grid while snapping is on, or the chart's
// finest division (one row) while it is off. Zero when the chart has no usable divisions.
[[nodiscard]] auto effective_edit_snap_denominator(const chart_document_t& chart,
                                                   std::uint64_t grid_denominator,
                                                   bool snap_enabled) noexcept -> std::uint64_t;

// Rounds onto a measure-relative grid, including measure heads; ties go earlier.
// Nullopt when the denominator is zero or does not divide the chart's divisions.
[[nodiscard]] auto snap_absolute_row(const chart_document_t& chart, std::uint64_t absolute_row,
                                     std::uint64_t denominator)
    -> std::optional<std::uint64_t>;

// Indexed overload avoids rebuilding measure geometry. Like the compatibility overload,
// it can round to the chart end or its arithmetic standard-measure tail.
[[nodiscard]] auto snap_absolute_row(const chart_document_t& chart, const measure_index_t& index,
                                     std::uint64_t absolute_row, std::uint64_t denominator) noexcept
    -> std::optional<std::uint64_t>;

// Nearest valid grid row in [0, canvas_rows), with ties going earlier. A shortened
// measure's next head is a grid point even when its length is less than one step.
[[nodiscard]] auto snap_canvas_row(const chart_document_t& chart, const measure_index_t& index,
                                   long double absolute_row, std::uint64_t denominator,
                                   std::uint64_t canvas_rows) noexcept
    -> std::optional<std::uint64_t>;

// Strict next/previous measure-local grid point. Fractional steps support playback
// beats in low-resolution charts. The virtual tail starts at index.total_rows.
[[nodiscard]] auto adjacent_measure_grid_row(const measure_index_t& index,
    long double absolute_row, long double step, std::uint64_t standard_measure_rows,
    std::uint64_t canvas_rows, bool forward) noexcept -> std::optional<long double>;

[[nodiscard]] auto adjacent_snap_canvas_row(const chart_document_t& chart,
    const measure_index_t& index, long double absolute_row, std::uint64_t denominator,
    std::uint64_t canvas_rows, bool forward) noexcept -> std::optional<std::uint64_t>;

// The position of an absolute row, or nullopt when it lies beyond the chart.
[[nodiscard]] auto position_at_absolute_row(const measure_index_t& index, std::uint64_t absolute)
    -> std::optional<local_position_t>;

// Snaps a chart position. The result is nullopt when the position, denominator or resulting row
// is outside the chart.
[[nodiscard]] auto snap_position(const chart_document_t& chart, const measure_index_t& index,
                                 local_position_t position, std::uint64_t denominator)
    -> std::optional<local_position_t>;

} // namespace wormedit
