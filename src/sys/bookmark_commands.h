#pragma once

#include "sys/compose_view.h"
#include "sys/bookmark_navigation.h"

#include <wormedit/core/command.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/marker_navigation.h>
#include <wormedit/core/timing.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace wormedit::sys {

// The active-snap row nearest `absolute_row` among rows 0 .. total_rows-1; ties go to the
// earlier row. Returns nothing when the snap does not divide the measure or there are no rows.
[[nodiscard]] auto nearest_snap_row(const chart_document_t& chart, const measure_index_t& index,
                                    double absolute_row, std::uint64_t snap_denominator)
    -> std::optional<std::uint64_t>;

// Preview frame of a beat position, clamped to [0, end_frame] when an end is known.
[[nodiscard]] auto bookmark_preview_frame(const timing_index_t& timing,
                                          const measure_index_t& measures, local_position_t position,
                                          std::optional<std::uint64_t> end_frame)
    -> std::optional<std::uint64_t>;

} // namespace wormedit::sys
