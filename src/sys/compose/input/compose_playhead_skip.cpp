#include "sys/compose/input/compose_navigation.h"
#include "sys/compose/input/compose_playhead_skip.h"
#include "sys/audio/preview_cursor_frame.h"

#include <wormedit/core/editor_axis.h>
#include <wormedit/core/snap.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <variant>

namespace wormedit::sys {
namespace {

auto clamp_row(long double row, const measure_index_t& measures) -> std::uint64_t {
    const auto last = static_cast<long double>(measures.total_rows - 1);
    return static_cast<std::uint64_t>(std::clamp(std::floor(row + 0.5L), 0.0L, last));
}

// Advance within one measure arithmetically, visiting each crossed head once.
auto snap_target(const measure_index_t& measures, double playhead, double amount,
                 long double step, std::uint64_t standard_rows) -> long double {
    auto remaining = std::abs(std::round(static_cast<long double>(amount)));
    auto row = std::clamp(static_cast<long double>(playhead), 0.0L,
                          static_cast<long double>(measures.total_rows - 1));
    const bool forward = amount > 0;
    while (remaining > 0) {
        const auto next = adjacent_measure_grid_row(measures, row, step, standard_rows,
                                                    measures.total_rows, forward);
        if (!next) return forward ? static_cast<long double>(measures.total_rows - 1) : 0.0L;
        row = *next;
        --remaining;
        const auto after = std::ranges::upper_bound(measures.entries, row, {},
                                                    &measure_index_entry_t::absolute_begin_row);
        if (after == measures.entries.begin()) continue;
        const auto& measure = *std::prev(after);
        const auto boundary = static_cast<long double>(measure.absolute_begin_row) +
                              (forward ? measure.row_capacity : 0);
        const auto available = std::floor(std::abs(boundary - row) / step);
        const auto count = std::min(remaining, available);
        row += (forward ? step : -step) * count;
        remaining -= count;
    }
    return row;
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
    const auto& record = application.project.transport.last_playhead_skip;
    if (record && record->chart == chart && record->forward == forward &&
        application.bookmarks.now() - record->at <= playing_seek_repeat_window)
        return static_cast<double>(record->row);
    return *application.project.transport.preview_playhead_absolute_row;
}

auto playhead_skip_target_row_uncut(const measure_index_t& measures, double playhead_row,
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
        return clamp_row(snap_target(measures, playhead_row, amount, step, divisions_per_measure), measures);
    }
    case playhead_skip_unit_t::beat: {
        if (divisions_per_measure == 0) return std::nullopt;
        auto target = snap_target(measures, playhead_row, amount,
                                  static_cast<long double>(divisions_per_measure) / 4.0L,
                                  divisions_per_measure);
        if (amount >= 0.5) target = std::max(target, std::floor(static_cast<long double>(playhead_row)) + 1.0L);
        if (amount <= -0.5) target = std::min(target, std::ceil(static_cast<long double>(playhead_row)) - 1.0L);
        return clamp_row(target, measures);
    }
    case playhead_skip_unit_t::measure:
        return clamp_row(measure_target(measures, playhead_row, amount), measures);
    case playhead_skip_unit_t::page: {
        if (divisions_per_measure == 0) return std::nullopt;
        const auto distance = std::ceil(static_cast<double>(divisions_per_measure) / 2.0 *
                                        std::abs(std::round(amount)));
        const auto target = measure_target(measures, playhead_row, amount);
        return clamp_row(amount > 0 ? std::max(target, playhead_row + distance)
                                    : std::min(target, playhead_row - distance), measures);
    }
    case playhead_skip_unit_t::rows:
        return clamp_row(playhead_row + std::round(amount), measures);
    }
    return std::nullopt;
}

// The nearest visible row from `row` in the travel direction, else the other way.
auto visible_row(const export_cut_axis_t& axis, std::uint64_t row, bool forward,
                 const measure_index_t& measures) -> std::optional<std::uint64_t> {
    const auto resolve = [&](std::uint64_t start, bool ahead) -> std::optional<std::uint64_t> {
        auto candidate = start;
        while (axis.contains_row(candidate)) {
            const auto edge = ahead ? std::ceil(axis.next_kept_row(candidate))
                                    : std::floor(axis.previous_kept_row(candidate));
            if (edge < 0.0L || edge > static_cast<long double>(measures.total_rows - 1)) return std::nullopt;
            const auto next = static_cast<std::uint64_t>(edge);
            if (next == candidate) return std::nullopt;
            candidate = next;
        }
        return candidate;
    };
    if (const auto found = resolve(row, forward)) return found;
    return resolve(row, !forward);
}

} // namespace

auto is_preview_playing(const application_command_state_t& application) -> bool {
    return application.services.transport_preview_playing && application.services.transport_preview_playing();
}

auto playhead_skip_target_row(const measure_index_t& measures, double playhead_row,
                              playhead_skip_unit_t unit, double amount,
                              std::uint64_t divisions_per_measure, std::uint64_t snap_denominator,
                              const export_cut_axis_t* cut_axis) -> std::optional<std::uint64_t> {
    if (!cut_axis || cut_axis->is_empty())
        return playhead_skip_target_row_uncut(measures, playhead_row, unit, amount,
                                              divisions_per_measure, snap_denominator);
    if (!std::isfinite(amount) || !std::isfinite(playhead_row)) return std::nullopt;
    const auto forward = amount > 0.0;
    const auto stepped = unit == playhead_skip_unit_t::snap || unit == playhead_skip_unit_t::beat;
    // Count only visible steps: resolve after every single step.
    const auto steps = stepped ? std::max<long long>(1, std::llabs(std::llround(amount))) : 1;
    const auto each = stepped ? (forward ? 1.0 : -1.0) : amount;
    auto row = playhead_row;
    for (long long step = 0; step < steps; ++step) {
        auto target = playhead_skip_target_row_uncut(measures, row, unit, each,
                                                     divisions_per_measure, snap_denominator);
        if (!target) return std::nullopt;
        target = visible_row(*cut_axis, *target, forward, measures);
        if (!target) return std::nullopt;
        row = static_cast<double>(*target);
    }
    return static_cast<std::uint64_t>(row);
}

auto skip_playing_playhead(const document_history_t& history,
                           application_command_state_t& application, const chart_document_t& chart,
                           std::uint64_t snap_denominator, playhead_skip_unit_t unit, double amount,
                           const export_cut_axis_t* cut_axis)
    -> compose_action_outcome_t {
    if (!application.project.transport.preview_playhead_absolute_row)
        return compose_action_outcome_t::from("The playhead row is not available yet.",
                                              notification_severity_t::warning,
                                              notification_source_t::input, false, false);
    const auto* indices = history.indices(chart.id);
    if (!indices || !indices->measures)
        return compose_action_outcome_t::from("The playhead could not be moved.",
                                              notification_severity_t::warning,
                                              notification_source_t::input, false, false);
    const auto& measures = *indices->measures;
    const auto forward = amount > 0.0;
    const auto row = playhead_skip_target_row(measures,
                                              skip_origin_row(application, chart.id, forward),
                                              unit, amount, chart.divisions_per_standard_measure,
                                              snap_denominator, cut_axis);
    const auto position = row ? (measures).position_at(*row) : std::nullopt;
    if (!position) {
        application.project.transport.last_playhead_skip.reset();
        return compose_action_outcome_t::from("The playhead could not be moved.",
                                              notification_severity_t::warning,
                                              notification_source_t::input, false, false);
    }
    auto outcome = seek_compose_navigation_position(history, chart, application, {.position = *position}, &measures);
    if (!outcome.accepted) {
        application.project.transport.last_playhead_skip.reset();
        return outcome;
    }
    // Chain several skips of one frame from where this one landed.
    application.project.transport.preview_playhead_absolute_row = static_cast<double>(*row);
    application.project.transport.last_playhead_skip = {chart.id, *row, forward, application.bookmarks.now()};
    return outcome;
}

auto scroll_playing_playhead(const document_history_t& history,
    application_command_state_t& application, const chart_document_t& chart,
    const editor_interaction_state_t& state, double delta_pixels) -> compose_action_outcome_t {
    const auto playhead = application.project.transport.preview_playhead_absolute_row;
    if (!playhead || !std::isfinite(*playhead) || !std::isfinite(delta_pixels) ||
        !std::isfinite(state.pixels_per_standard_measure) ||
        state.pixels_per_standard_measure <= 0.0 || chart.divisions_per_standard_measure == 0)
        return compose_action_outcome_t::blocked("The playhead scroll position is unavailable.",
                                                 notification_source_t::input);
    const auto* indices = history.indices(chart.id);
    if (!indices || !indices->measures || indices->measures->total_rows == 0)
        return compose_action_outcome_t::blocked("The playhead could not be moved.",
                                                 notification_source_t::input);
    const auto fallback_timing = indices->timing.value ? std::nullopt
        : timing_index_t::from(chart, {.allow_bounded_clock = true}).value;
    const auto* timing = indices->timing.value ? &*indices->timing.value
        : fallback_timing ? &*fallback_timing : nullptr;
    if (!timing)
        return compose_action_outcome_t::blocked("The playhead has no playable timing position.",
                                                 notification_source_t::input);
    const auto axis = editor_axis_projection_t::from(state, chart.divisions_per_standard_measure);
    const auto target = std::clamp(axis.row_at_y(axis.y_for_row(*playhead) + delta_pixels),
        0.0L, static_cast<long double>(indices->measures->total_rows - 1));
    const auto row = static_cast<std::uint64_t>(std::floor(target));
    const auto position = indices->measures->position_at(row);
    if (!position)
        return compose_action_outcome_t::blocked("The playhead could not be moved.",
                                                 notification_source_t::input);
    const auto frame = preview_cursor_frame(chart, *timing, *position,
        static_cast<double>(target - row), &indices->measures);
    if (const auto* error = std::get_if<std::string>(&frame))
        return compose_action_outcome_t::blocked(*error, notification_source_t::input);
    auto outcome = seek_compose_navigation_position(history, chart, application,
        {.position = *position, .preview_frame = std::get<std::uint64_t>(frame)},
        &*indices->measures);
    if (outcome.accepted)
        application.project.transport.preview_playhead_absolute_row = static_cast<double>(target);
    return outcome;
}

} // namespace wormedit::sys
