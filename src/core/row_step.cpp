#include <wormedit/core/row_step.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace wormedit {

auto adjacent_step_row(const chart_document_t& chart, const measure_index_t& index,
                       long double row, row_step_t step, std::uint64_t canvas_rows,
                       bool forward) noexcept -> std::optional<std::uint64_t> {
    if (step.resolution == row_step_resolution_t::snap)
        return adjacent_snap_canvas_row(chart, index, row, step.grid, canvas_rows, forward);
    const auto standard = chart.divisions_per_standard_measure;
    if (standard == 0) return std::nullopt;
    const auto beat = static_cast<long double>(standard) / 4.0L;
    const auto next = adjacent_measure_grid_row(index, row, beat, standard, canvas_rows, forward);
    if (!next) return std::nullopt;
    // Low-resolution charts have fractional beats: always make at least one row of progress.
    const auto rounded = forward ? std::max(*next, std::floor(row) + 1.0L)
                                 : std::min(*next, std::ceil(row) - 1.0L);
    const auto bounded = std::floor(std::clamp(rounded, 0.0L, static_cast<long double>(canvas_rows - 1)) + 0.5L);
    return static_cast<std::uint64_t>(bounded);
}

auto visible_step_row(const chart_document_t& chart, const measure_index_t& index,
                      const export_cut_axis_t* cut_axis, row_step_t step,
                      std::uint64_t canvas_rows, std::uint64_t row, bool forward)
    -> std::uint64_t {
    if (!cut_axis || !canvas_rows || !cut_axis->contains_row(row)) return row;
    const auto inf = std::numeric_limits<long double>::infinity();
    auto candidate = std::optional{row};
    while (candidate && cut_axis->contains_row(*candidate)) {
        const auto edge = forward ? cut_axis->next_kept_row(*candidate)
                                  : cut_axis->previous_kept_row(*candidate);
        // Step from just short of the visible edge so a grid row on the edge is kept.
        candidate = adjacent_step_row(chart, index, std::nextafter(edge, forward ? -inf : inf),
                                      step, canvas_rows, forward);
    }
    if (!candidate && forward)
        candidate = adjacent_step_row(chart, index,
            std::nextafter(cut_axis->previous_kept_row(row), inf), step, canvas_rows, false);
    return candidate.value_or(row);
}

auto step_visible_rows(const chart_document_t& chart, const measure_index_t& index,
                       const export_cut_axis_t* cut_axis, row_step_t step,
                       std::uint64_t canvas_rows, long double row, bool forward,
                       std::uint64_t count) -> std::optional<std::uint64_t> {
    const auto usable = step.resolution == row_step_resolution_t::snap
        ? snap_step(chart, step.grid, 0).has_value() : chart.divisions_per_standard_measure != 0;
    if (!usable || canvas_rows == 0 || !std::isfinite(row)) return std::nullopt;
    const auto last = canvas_rows - 1;
    auto current = std::clamp(row, 0.0L, static_cast<long double>(last));
    std::optional<std::uint64_t> landed;
    for (std::uint64_t taken = 0; taken < count; ++taken) {
        // Running into the canvas edge stops on the edge.
        const auto next = adjacent_step_row(chart, index, current, step, canvas_rows, forward)
                              .value_or(forward ? last : 0);
        landed = visible_step_row(chart, index, cut_axis, step, canvas_rows, next, forward);
        current = static_cast<long double>(*landed);
    }
    return landed ? landed : std::optional{static_cast<std::uint64_t>(current)};
}

} // namespace wormedit
