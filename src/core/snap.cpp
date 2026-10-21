#include <wormedit/core/snap.h>
#include <wormedit/core/rational.h>

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

auto effective_edit_snap_grid(const chart_document_t& chart, snap_grid_t grid,
                              bool snap_enabled) noexcept -> snap_grid_t {
    return snap_enabled ? grid : snap_grid_t{.denominator = chart.divisions_per_standard_measure};
}

auto snap_step(const chart_document_t& chart, snap_grid_t grid,
               std::uint64_t measure_capacity) noexcept -> std::optional<std::uint64_t> {
    return snap_step(chart.divisions_per_standard_measure, grid, measure_capacity);
}

auto snap_step(std::uint64_t standard, snap_grid_t grid,
               std::uint64_t measure_capacity) noexcept -> std::optional<std::uint64_t> {
    if (!grid.denominator || !standard || standard % grid.denominator) return std::nullopt;
    if (grid.space == quantization_space_t::authored_measure && measure_capacity &&
        measure_capacity % grid.denominator == 0)
        return measure_capacity / grid.denominator;
    return standard / grid.denominator;
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

auto measure_snap_step(const chart_document_t& chart, snap_grid_t grid,
                       const grid_measure_t& measure) noexcept -> std::optional<std::uint64_t> {
    return snap_step(chart, grid, static_cast<std::uint64_t>(measure.end - measure.begin));
}

} // namespace

namespace {

// The measure whose grid a next/previous step uses. Looking just before an exact head makes
// backward navigation use the preceding measure's grid, rather than subtracting this measure's
// step past its origin.
auto adjacent_grid_measure(const measure_index_t& index, long double row,
                           std::uint64_t standard_rows, bool forward) noexcept
    -> std::optional<grid_measure_t> {
    const auto probe = forward ? row : std::nextafter(row, -std::numeric_limits<long double>::infinity());
    return grid_measure(index, probe, standard_rows);
}

auto adjacent_row_in_measure(const grid_measure_t& measure, long double row, long double step,
                             std::uint64_t canvas_rows, bool forward) noexcept
    -> std::optional<long double> {
    const auto local = (row - measure.begin) / step;
    const auto target = forward
        ? std::min(measure.begin + (std::floor(local) + 1) * step, measure.end)
        : measure.begin + (std::ceil(local) - 1) * step;
    if (target < 0 || target >= canvas_rows) return std::nullopt;
    return target;
}

} // namespace

auto adjacent_measure_grid_row(const measure_index_t& index, long double row,
    long double step, std::uint64_t standard_rows, std::uint64_t canvas_rows,
    bool forward) noexcept -> std::optional<long double> {
    if (!is_valid_grid_input(row, step, standard_rows, canvas_rows)) return std::nullopt;
    if (!forward && row == 0) return std::nullopt;
    const auto measure = adjacent_grid_measure(index, row, standard_rows, forward);
    if (!measure) return std::nullopt;
    return adjacent_row_in_measure(*measure, row, step, canvas_rows, forward);
}

auto snap_canvas_row(const chart_document_t& chart, const measure_index_t& index,
    long double row, snap_grid_t grid, std::uint64_t canvas_rows) noexcept
    -> std::optional<std::uint64_t> {
    const auto standard_step = snap_step(chart, grid, 0);
    if (!standard_step ||
        !is_valid_grid_input(row, *standard_step, chart.divisions_per_standard_measure, canvas_rows))
        return std::nullopt;
    const auto measure = grid_measure(index, row, chart.divisions_per_standard_measure);
    if (!measure) return std::nullopt;
    const auto step = *measure_snap_step(chart, grid, *measure);
    const auto lower = measure->begin + std::floor((row - measure->begin) / step) * step;
    const auto upper = std::min(lower + step, measure->end);
    const auto target = upper < canvas_rows && row - lower > upper - row ? upper : lower;
    return static_cast<std::uint64_t>(target);
}

auto adjacent_snap_canvas_row(const chart_document_t& chart, const measure_index_t& index,
    long double row, snap_grid_t grid, std::uint64_t canvas_rows,
    bool forward) noexcept -> std::optional<std::uint64_t> {
    const auto standard_rows = chart.divisions_per_standard_measure;
    const auto standard_step = snap_step(chart, grid, 0);
    if (!standard_step || !is_valid_grid_input(row, *standard_step, standard_rows, canvas_rows))
        return std::nullopt;
    if (!forward && row == 0) return std::nullopt;
    const auto measure = adjacent_grid_measure(index, row, standard_rows, forward);
    if (!measure) return std::nullopt;
    const auto target = adjacent_row_in_measure(
        *measure, row, *measure_snap_step(chart, grid, *measure), canvas_rows, forward);
    return target ? std::optional{static_cast<std::uint64_t>(*target)} : std::nullopt;
}

auto snap_absolute_row(const chart_document_t& chart, const measure_index_t& index,
    std::uint64_t row, snap_grid_t grid) noexcept -> std::optional<std::uint64_t> {
    return snap_canvas_row(chart, index, row, grid,
                           std::numeric_limits<std::uint64_t>::max());
}

auto snap_absolute_row(const chart_document_t& chart, std::uint64_t row,
    snap_grid_t grid) -> std::optional<std::uint64_t> {
    const auto built = measure_index_t::from(chart);
    return built ? snap_absolute_row(chart, *built, row, grid) : std::nullopt;
}

auto snap_position(const chart_document_t& chart, const measure_index_t& index,
                   local_position_t position, snap_grid_t grid)
    -> std::optional<local_position_t> {
    const auto absolute = index.row_of(position);
    if (!absolute) return std::nullopt;
    const auto snapped = snap_absolute_row(chart, index, *absolute, grid);
    if (!snapped) return std::nullopt;
    return index.position_at(*snapped);
}

auto adjacent_snap_step(const chart_document_t& chart, const measure_index_t& index,
                        std::uint64_t row, snap_grid_t grid, bool forward) noexcept
    -> std::optional<std::uint64_t> {
    const auto standard_step = snap_step(chart, grid, 0);
    if (!standard_step || (!forward && row == 0)) return standard_step;
    const auto measure =
        adjacent_grid_measure(index, row, chart.divisions_per_standard_measure, forward);
    return measure ? measure_snap_step(chart, grid, *measure) : standard_step;
}

auto snap_length_step(std::uint64_t standard, snap_grid_t grid, rational_t length) noexcept
    -> std::optional<rational_t> {
    // A length that is not a whole number of rows has no authored grid of its own.
    const auto exact = length.denominator && (length.numerator * standard) % length.denominator == 0;
    const auto capacity = exact ? length.numerator * standard / length.denominator : 0;
    const auto rows = snap_step(standard, grid, capacity);
    if (!rows) return std::nullopt;
    return rows_to_fraction(*rows, standard);
}

auto grid_phase(const chart_document_t& chart, const measure_index_t& index,
                std::uint64_t row, quantization_space_t space) noexcept -> grid_phase_t {
    const auto standard = chart.divisions_per_standard_measure;
    if (!standard) return {};
    const auto measure = grid_measure(index, static_cast<long double>(row), standard);
    if (!measure) return {.row = row % standard, .period = standard};
    const auto begin = static_cast<std::uint64_t>(measure->begin);
    const auto capacity = static_cast<std::uint64_t>(measure->end - measure->begin);
    const auto authored = space == quantization_space_t::authored_measure && capacity;
    return {.row = row - begin, .period = authored ? capacity : standard};
}

auto snap_canvas_row_or_exact(const chart_document_t& chart, const measure_index_t& index,
    std::uint64_t row, snap_grid_t grid, std::uint64_t canvas_rows) noexcept
    -> std::optional<std::uint64_t> {
    if (row >= canvas_rows) return std::nullopt;
    if (!grid.denominator) return row;
    return snap_canvas_row(chart, index, row, grid, canvas_rows);
}

auto snap_position_or_exact(const chart_document_t& chart, const measure_index_t& index,
                            local_position_t position, snap_grid_t grid)
    -> std::optional<local_position_t> {
    if (!index.row_of(position)) return std::nullopt;
    if (!grid.denominator) return position;
    return snap_position(chart, index, position, grid);
}

auto snap_span_position(const chart_document_t& chart, const measure_index_t& index,
                        local_position_t target, snap_grid_t grid)
    -> std::optional<local_position_t> {
    const auto absolute = index.row_of(target, measure_chart_end_policy_t::allow);
    if (!absolute || !snap_step(chart, grid, 0)) {
        return std::nullopt;
    }
    if (*absolute == index.total_rows) {
        return index.position_at(index.total_rows, measure_chart_end_policy_t::allow);
    }
    const auto rounded = snap_absolute_row(chart, index, *absolute, grid);
    if (!rounded) {
        return std::nullopt;
    }
    auto snapped = *rounded;
    if (snapped > index.total_rows) {
        snapped = index.total_rows;
    }
    const auto distance_to_snapped =
        snapped >= *absolute ? snapped - *absolute : *absolute - snapped;
    const auto distance_to_chart_end = index.total_rows - *absolute;
    if (distance_to_chart_end < distance_to_snapped) {
        snapped = index.total_rows;
    }
    return index.position_at(snapped, measure_chart_end_policy_t::allow);
}

} // namespace wormedit
