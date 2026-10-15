#include <wormedit/core/snap.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <numeric>

namespace wormedit {

auto fitting_snap_denominator(const chart_document_t& chart, const measure_index_t& index,
                               std::span<const local_position_t> positions)
    -> std::optional<std::uint64_t> {
    if (positions.empty() || !chart.divisions_per_standard_measure) return std::nullopt;
    auto divisor = chart.divisions_per_standard_measure;
    for (const auto position : positions) {
        if (!index.row_of(position)) return std::nullopt;
        divisor = std::gcd(divisor, position.row);
    }
    return chart.divisions_per_standard_measure / divisor;
}

auto effective_edit_snap_denominator(const chart_document_t& chart, std::uint64_t grid_denominator,
                                     bool snap_enabled) noexcept -> std::uint64_t {
    return snap_enabled ? grid_denominator : chart.divisions_per_standard_measure;
}

namespace {

struct grid_measure_t {
    long double begin{};
    long double end{};
};

auto grid_measure(const measure_index_t& index, long double row,
                  std::uint64_t standard_rows) noexcept -> std::optional<grid_measure_t> {
    if (row >= index.total_rows) {
        const auto offset = std::floor((row - index.total_rows) / standard_rows) * standard_rows;
        const auto begin = static_cast<long double>(index.total_rows) + offset;
        return grid_measure_t{begin, begin + standard_rows};
    }
    const auto after = std::ranges::upper_bound(index.entries, row, {},
                                                &measure_index_entry_t::absolute_begin_row);
    if (after == index.entries.begin()) return std::nullopt;
    const auto& entry = *std::prev(after);
    return grid_measure_t{static_cast<long double>(entry.absolute_begin_row),
                          static_cast<long double>(entry.absolute_begin_row) + entry.row_capacity};
}

auto is_valid_grid_input(long double row, long double step, std::uint64_t standard_rows,
                      std::uint64_t canvas_rows) noexcept -> bool {
    return std::isfinite(row) && std::isfinite(step) && step > 0 && standard_rows > 0 &&
           row >= 0 && row < canvas_rows;
}

auto snap_step(const chart_document_t& chart, std::uint64_t denominator) noexcept
    -> std::optional<std::uint64_t> {
    if (!denominator || !chart.divisions_per_standard_measure ||
        chart.divisions_per_standard_measure % denominator) return std::nullopt;
    return chart.divisions_per_standard_measure / denominator;
}

} // namespace

auto adjacent_measure_grid_row(const measure_index_t& index, long double row,
    long double step, std::uint64_t standard_rows, std::uint64_t canvas_rows,
    bool forward) noexcept -> std::optional<long double> {
    if (!is_valid_grid_input(row, step, standard_rows, canvas_rows)) return std::nullopt;
    if (!forward && row == 0) return std::nullopt;
    // Looking just before an exact head makes backward navigation use the preceding
    // measure's grid, rather than subtracting this measure's step past its origin.
    const auto probe = forward ? row : std::nextafter(row, -std::numeric_limits<long double>::infinity());
    const auto measure = grid_measure(index, probe, standard_rows);
    if (!measure) return std::nullopt;
    const auto local = (row - measure->begin) / step;
    const auto target = forward
        ? std::min(measure->begin + (std::floor(local) + 1) * step, measure->end)
        : measure->begin + (std::ceil(local) - 1) * step;
    if (target < 0 || target >= canvas_rows) return std::nullopt;
    return target;
}

auto snap_canvas_row(const chart_document_t& chart, const measure_index_t& index,
    long double row, std::uint64_t denominator, std::uint64_t canvas_rows) noexcept
    -> std::optional<std::uint64_t> {
    const auto step = snap_step(chart, denominator);
    if (!step || !is_valid_grid_input(row, *step, chart.divisions_per_standard_measure, canvas_rows))
        return std::nullopt;
    const auto measure = grid_measure(index, row, chart.divisions_per_standard_measure);
    if (!measure) return std::nullopt;
    const auto lower = measure->begin + std::floor((row - measure->begin) / *step) * *step;
    const auto upper = std::min(lower + *step, measure->end);
    const auto target = upper < canvas_rows && row - lower > upper - row ? upper : lower;
    return static_cast<std::uint64_t>(target);
}

auto adjacent_snap_canvas_row(const chart_document_t& chart, const measure_index_t& index,
    long double row, std::uint64_t denominator, std::uint64_t canvas_rows,
    bool forward) noexcept -> std::optional<std::uint64_t> {
    const auto step = snap_step(chart, denominator);
    if (!step) return std::nullopt;
    const auto target = adjacent_measure_grid_row(index, row, *step,
        chart.divisions_per_standard_measure, canvas_rows, forward);
    return target ? std::optional{static_cast<std::uint64_t>(*target)} : std::nullopt;
}

auto snap_absolute_row(const chart_document_t& chart, const measure_index_t& index,
    std::uint64_t row, std::uint64_t denominator) noexcept -> std::optional<std::uint64_t> {
    return snap_canvas_row(chart, index, row, denominator,
                           std::numeric_limits<std::uint64_t>::max());
}

auto snap_absolute_row(const chart_document_t& chart, std::uint64_t row,
    std::uint64_t denominator) -> std::optional<std::uint64_t> {
    const auto built = measure_index_t::from(chart);
    return built ? snap_absolute_row(chart, *built, row, denominator) : std::nullopt;
}

auto position_at_absolute_row(const measure_index_t& index, std::uint64_t absolute)
    -> std::optional<local_position_t> {
    return index.position_at(absolute);
}

auto snap_position(const chart_document_t& chart, const measure_index_t& index,
                   local_position_t position, std::uint64_t denominator)
    -> std::optional<local_position_t> {
    const auto absolute = index.row_of(position);
    if (!absolute) return std::nullopt;
    const auto snapped = snap_absolute_row(chart, index, *absolute, denominator);
    if (!snapped) return std::nullopt;
    return index.position_at(*snapped);
}

} // namespace wormedit
