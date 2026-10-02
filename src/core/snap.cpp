#include <wormedit/core/snap.h>

#include <algorithm>
#include <iterator>
#include <limits>

namespace wormedit {

auto effective_edit_snap_denominator(const chart_document_t& chart, std::uint64_t grid_denominator,
                                     bool snap_enabled) noexcept -> std::uint64_t {
    return snap_enabled ? grid_denominator : chart.divisions_per_standard_measure;
}

auto snap_absolute_row(const chart_document_t& chart, std::uint64_t absolute_row,
                       std::uint64_t denominator) noexcept -> std::optional<std::uint64_t> {
    if (denominator == 0 || chart.divisions_per_standard_measure % denominator != 0)
        return std::nullopt;
    const auto step = chart.divisions_per_standard_measure / denominator;
    if (step == 0) return std::nullopt;
    const auto remainder = absolute_row % step;
    const auto distance_up = step - remainder;
    if (remainder > distance_up &&
        distance_up <= std::numeric_limits<std::uint64_t>::max() - absolute_row)
        return absolute_row + distance_up;
    return absolute_row - remainder;
}

auto position_at_absolute_row(const measure_index_t& index, std::uint64_t absolute)
    -> std::optional<local_position_t> {
    if (absolute >= index.total_rows) return std::nullopt;
    const auto after = std::ranges::upper_bound(index.entries, absolute, {},
                                                &measure_index_entry_t::absolute_begin_row);
    if (after == index.entries.begin()) return std::nullopt;
    const auto entry = std::prev(after);
    return local_position_t{.measure = entry->id, .row = absolute - entry->absolute_begin_row};
}

auto snap_position(const chart_document_t& chart, const measure_index_t& index,
                   local_position_t position, std::uint64_t denominator)
    -> std::optional<local_position_t> {
    const auto absolute = absolute_row(index, position);
    if (!absolute) return std::nullopt;
    const auto snapped = snap_absolute_row(chart, *absolute, denominator);
    if (!snapped) return std::nullopt;
    return position_at_absolute_row(index, *snapped);
}

} // namespace wormedit
