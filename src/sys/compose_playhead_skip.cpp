#include "sys/compose_playhead_skip.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <variant>

namespace wormedit::sys {
namespace {

constexpr double grid_epsilon = 1e-6;

auto clamp_row(double row, const measure_index_t& measures) -> std::uint64_t {
    const auto last = static_cast<double>(measures.total_rows - 1);
    return static_cast<std::uint64_t>(std::clamp(std::floor(row + 0.5), 0.0, last));
}

auto snap_target(double playhead, double amount, double step) -> double {
    const auto count = std::llround(amount);
    if (count == 0) return playhead;
    const auto position = playhead / step;
    const auto on_grid = std::abs(position - std::round(position)) < grid_epsilon;
    const auto nearest = std::round(position);
    // Strictly past the playhead: from a grid row take a full step, else go to the next line.
    const auto first = count > 0 ? (on_grid ? nearest + 1.0 : std::ceil(position))
                                 : (on_grid ? nearest - 1.0 : std::floor(position));
    return (first + static_cast<double>(count > 0 ? count - 1 : count + 1)) * step;
}

auto measure_target(const measure_index_t& measures, double playhead, double amount) -> double {
    const auto count = std::llround(amount);
    const auto row = static_cast<std::uint64_t>(std::max(0.0, std::floor(playhead)));
    const auto after = std::ranges::upper_bound(measures.entries, row, {},
                                                &measure_index_entry_t::absolute_begin_row);
    if (after == measures.entries.begin()) return playhead;
    const auto current = static_cast<long long>(std::distance(measures.entries.begin(), after)) - 1;
    const auto last = static_cast<long long>(measures.entries.size()) - 1;
    const auto destination = std::clamp(current + count, 0LL, last);
    const auto& from = measures.entries[static_cast<std::size_t>(current)];
    const auto& to = measures.entries[static_cast<std::size_t>(destination)];
    const auto offset = playhead - static_cast<double>(from.absolute_begin_row);
    const auto capped = std::min(offset, static_cast<double>(to.row_capacity) - 1.0);
    return static_cast<double>(to.absolute_begin_row) + std::max(capped, 0.0);
}

// The row a skip starts from: the previous skip's target while a same-direction repeat is
// within the window, else the live playhead.
auto skip_origin_row(const application_command_state_t& application, chart_id_t chart,
                     bool forward) -> double {
    const auto& record = application.last_playhead_skip;
    if (record && record->chart == chart && record->forward == forward &&
        playing_seek_now(application) - record->at <= playing_seek_repeat_window)
        return static_cast<double>(record->row);
    return *application.preview_playhead_absolute_row;
}

} // namespace

auto is_preview_playing(const application_command_state_t& application) -> bool {
    return application.transport_preview_playing && application.transport_preview_playing();
}

auto playhead_skip_target_row(const measure_index_t& measures, double playhead_row,
                              playhead_skip_unit_t unit, double amount,
                              std::uint64_t divisions_per_measure, std::uint64_t snap_denominator)
    -> std::optional<std::uint64_t> {
    if (measures.total_rows == 0 || !std::isfinite(playhead_row) || !std::isfinite(amount))
        return std::nullopt;
    switch (unit) {
    case playhead_skip_unit_t::snap: {
        if (snap_denominator == 0 || divisions_per_measure % snap_denominator != 0)
            return std::nullopt;
        const auto step = static_cast<double>(divisions_per_measure / snap_denominator);
        if (step <= 0.0) return std::nullopt;
        return clamp_row(snap_target(playhead_row, amount, step), measures);
    }
    case playhead_skip_unit_t::measure:
        return clamp_row(measure_target(measures, playhead_row, amount), measures);
    case playhead_skip_unit_t::rows:
        return clamp_row(playhead_row + std::round(amount), measures);
    }
    return std::nullopt;
}

auto skip_playing_playhead(application_command_state_t& application, const chart_document_t& chart,
                           std::uint64_t snap_denominator, playhead_skip_unit_t unit,
                           double amount) -> bookmark_command_outcome_t {
    if (!application.preview_playhead_absolute_row)
        return {.status = "The playhead row is not available yet."};
    const auto built = build_measure_index(chart);
    const auto timing = build_timing_index(chart, {.allow_bounded_clock = true});
    if (!std::holds_alternative<measure_index_t>(built) || !timing.index)
        return {.status = "The playhead could not be moved."};
    const auto& measures = std::get<measure_index_t>(built);
    const auto forward = amount > 0.0;
    const auto row = playhead_skip_target_row(measures,
                                              skip_origin_row(application, chart.id, forward),
                                              unit, amount, chart.divisions_per_standard_measure,
                                              snap_denominator);
    const auto position = row ? local_position_for_row(measures, *row) : std::nullopt;
    if (!position) {
        application.last_playhead_skip.reset();
        return {.status = "The playhead could not be moved."};
    }
    const auto end = application.transport_preview_end_frame
                         ? application.transport_preview_end_frame() : std::nullopt;
    const auto frame = bookmark_preview_frame(*timing.index, measures, *position, end);
    if (!frame || !application.transport_seek) {
        application.last_playhead_skip.reset();
        return {.status = "Chart preview seek is unavailable."};
    }
    application.last_playing_bookmark_seek.reset();
    auto status = application.transport_seek(*frame);
    // Chain several skips of one frame from where this one landed.
    application.preview_playhead_absolute_row = static_cast<double>(*row);
    application.last_playhead_skip = {chart.id, *row, forward, playing_seek_now(application)};
    return {.accepted = true, .status = std::move(status)};
}

} // namespace wormedit::sys
