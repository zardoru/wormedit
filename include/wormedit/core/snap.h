#pragma once

#include <wormedit/core/document.h>

#include <cstdint>
#include <optional>

namespace wormedit {

// The one place pointer and cursor positions are rounded onto the edit grid. Every object class
// (notes, holds, timing markers, areas, resources, BGM, recipes) goes through these helpers, so
// turning snapping off affects them all at once.

// The denominator edits should snap to: the user's grid while snapping is on, or the chart's
// finest division (one row) while it is off. Zero when the chart has no usable divisions.
[[nodiscard]] auto effective_edit_snap_denominator(const chart_document_t& chart,
                                                   std::uint64_t grid_denominator,
                                                   bool snap_enabled) noexcept -> std::uint64_t;

// Rounds an absolute row to the nearest multiple of one 1/denominator step; ties go to the
// earlier row. Nullopt when the denominator is zero or does not divide the chart's divisions.
[[nodiscard]] auto snap_absolute_row(const chart_document_t& chart, std::uint64_t absolute_row,
                                     std::uint64_t denominator) noexcept
    -> std::optional<std::uint64_t>;

// The position of an absolute row, or nullopt when it lies beyond the chart.
[[nodiscard]] auto position_at_absolute_row(const measure_index_t& index, std::uint64_t absolute)
    -> std::optional<local_position_t>;

// Snaps a chart position. The result is nullopt when the position, denominator or resulting row
// is outside the chart.
[[nodiscard]] auto snap_position(const chart_document_t& chart, const measure_index_t& index,
                                 local_position_t position, std::uint64_t denominator)
    -> std::optional<local_position_t>;

} // namespace wormedit
