#pragma once
#include <wormedit/core/document.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace wormedit::sys {
[[nodiscard]] inline auto timing_count_in_pulses(const chart_document_t& chart,
                                                std::uint8_t measures)
    -> std::optional<std::uint32_t> {
    if (measures > 4) return std::nullopt;
    if (measures == 0) return 0;
    if (chart.measures.empty()) return std::nullopt;
    const auto& first = chart.measures.front().definition;
    const auto validated = measure_capacity_t::from(chart.divisions_per_standard_measure, first);
    if (!validated) return std::nullopt;
    const auto quarter = std::max<std::uint64_t>(1, chart.divisions_per_standard_measure / 4);
    const auto pulses = first.meter ? static_cast<std::uint64_t>(first.meter->numerator)
        : validated->row_capacity / quarter + (validated->row_capacity % quarter != 0);
    if (pulses == 0 || (first.meter && validated->row_capacity % pulses != 0) ||
        pulses > std::numeric_limits<std::uint32_t>::max() / measures) return std::nullopt;
    return static_cast<std::uint32_t>(pulses * measures);
}
} // namespace wormedit::sys
