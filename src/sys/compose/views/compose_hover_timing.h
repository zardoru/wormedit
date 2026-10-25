#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/timing.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace wormedit::sys {

// One-based measure and metrical beat, including the virtual chart tail.
// Requires a nonempty measure index matching the chart.
[[nodiscard]] auto beat_at_row(const chart_document_t& chart, const measure_index_t& measures,
                              signed_rational_t row) -> std::pair<std::uint64_t, std::int64_t>;

// Writes a fixed-size hover label for a source audio frame. Returns false when
// the frame cannot be mapped through the current chart timing.
[[nodiscard]] auto format_compose_hover_timing(
    const chart_document_t& chart, const measure_index_t& measures,
    const timing_index_t& timing, std::int64_t source_frame,
    std::uint32_t sample_rate, char* output, std::size_t output_size) -> bool;

} // namespace wormedit::sys
