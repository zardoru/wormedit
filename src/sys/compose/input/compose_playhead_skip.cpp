#include "sys/compose/input/compose_navigation.h"
#include "sys/compose/input/compose_playhead_skip.h"
#include "sys/audio/preview_cursor_frame.h"

#include <wormedit/core/editor_axis.h>
#include <wormedit/core/row_step.h>
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

// The nearest measure from `from` in `direction` that the cut leaves any row of, or -1 past the
// chart. A hidden measure sends the search to the cut's far edge instead of one measure at a time.
auto next_visible_measure(const measure_index_t& measures, const export_cut_axis_t& cut_axis,
                          long long from, long long direction) -> long long {
    const auto count = static_cast<long long>(measures.entries.size());
    const auto containing = [&](long double row) {
        const auto at = std::ranges::upper_bound(measures.entries,
            static_cast<std::uint64_t>(std::max(0.0L, std::floor(row))), {},
            &measure_index_entry_t::absolute_begin_row);
        return static_cast<long long>(std::distance(measures.entries.begin(), at)) - 1;
    };
    auto next = from + direction;
    while (next >= 0 && next < count) {
        const auto& entry = measures.entries[static_cast<std::size_t>(next)];
        if (!is_measure_hidden(entry, &cut_axis)) return next;
        // The far edge of the cut; stepping at least one measure keeps the search moving.
        const auto edge = direction > 0
            ? containing(cut_axis.next_kept_row(static_cast<long double>(entry.absolute_begin_row)))
            : containing(cut_axis.previous_kept_row(
                  static_cast<long double>(entry.absolute_begin_row + entry.row_capacity - 1)));
        next = direction > 0 ? std::max(next + 1, edge) : std::min(next - 1, edge);
    }
    return -1;
}

auto measure_target(const measure_index_t& measures, double playhead, double amount,
                    const export_cut_axis_t* cut_axis = nullptr) -> double {
    const auto count = std::llround(amount);
    const auto row = static_cast<std::uint64_t>(std::max(0.0, std::floor(playhead)));
    const auto after = std::ranges::upper_bound(measures.entries, row, {},
                                                &measure_index_entry_t::absolute_begin_row);
    if (after == measures.entries.begin()) return playhead;
    const auto current = static_cast<long long>(std::distance(measures.entries.begin(), after)) - 1;
    const auto last = static_cast<long long>(measures.entries.size()) - 1;
    auto destination = std::clamp(current + count, 0LL, last);
    if (cut_axis && !cut_axis->is_empty()) {
        // Count only visible measures, stopping at the first or last chart measure.
        destination = current;
        const auto direction = count >= 0 ? 1LL : -1LL;
        for (auto remaining = std::llabs(count); remaining > 0; --remaining) {
            const auto next = next_visible_measure(measures, *cut_axis, destination, direction);
            if (next < 0) break;
            destination = next;
        }
    }
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

// A kept row whose own time lies inside an export cut cannot hold the playhead: seeking to it
// collapses onto the cut seam, which reads back as the end of the cut.
auto cut_unusable(const timing_index_t* timing, const export_cut_axis_t* cut_axis)
    -> std::function<bool(std::uint64_t)> {
    if (!timing || !cut_axis || cut_axis->is_empty()) return {};
    return [timing, cut_axis](std::uint64_t row) {
        const auto at = timing->query_seconds(exact_chart_row_t{row, {0, 1}});
        return at && cut_axis->timeline().contains_seconds(at->value);
    };
}

auto skip_target_row(const measure_index_t& measures, double playhead_row,
                     playhead_skip_unit_t unit, double amount,
                     std::uint64_t divisions_per_measure, std::uint64_t snap_denominator,
                     const export_cut_axis_t* cut_axis, const timing_index_t* timing)
    -> std::optional<std::uint64_t> {
    if (measures.total_rows == 0 || !std::isfinite(playhead_row) || !std::isfinite(amount))
        return std::nullopt;
    switch (unit) {
    case playhead_skip_unit_t::snap:
    case playhead_skip_unit_t::beat: {
        // Both resolutions go through the shared stepper the parked cursor uses.
        const auto chart_like = chart_document_t{.divisions_per_standard_measure = divisions_per_measure};
        const auto resolution = unit == playhead_skip_unit_t::snap ? row_step_resolution_t::snap
                                                                    : row_step_resolution_t::beat;
        // Every step moves at least one row or stops on an edge, so more steps than rows change
        // nothing; clamping first also avoids llround overflow, which MSVC answers with 0.
        const auto count = static_cast<std::uint64_t>(std::llround(
            std::min(std::abs(amount), static_cast<double>(measures.total_rows))));
        const auto start = std::clamp(static_cast<long double>(playhead_row), 0.0L,
                                      static_cast<long double>(measures.total_rows - 1));
        const auto unusable = cut_unusable(timing, cut_axis);
        const auto stepped = step_visible_rows(chart_like, measures, cut_axis,
            {.resolution = resolution, .grid = {snap_denominator, quantization_space_t::standard_measure},
             .unusable = unusable ? &unusable : nullptr},
            measures.total_rows, start, amount > 0, count);
        if (stepped && count == 0) return clamp_row(playhead_row, measures);
        return stepped;
    }
    case playhead_skip_unit_t::measure:
        return clamp_row(measure_target(measures, playhead_row, amount, cut_axis), measures);
    case playhead_skip_unit_t::page: {
        if (divisions_per_measure == 0) return std::nullopt;
        const auto distance = std::ceil(static_cast<double>(divisions_per_measure) / 2.0 *
                                        std::abs(std::round(amount)));
        const auto target = measure_target(measures, playhead_row, amount, cut_axis);
        return clamp_row(amount > 0 ? std::max(target, playhead_row + distance)
                                    : std::min(target, playhead_row - distance), measures);
    }
    case playhead_skip_unit_t::rows:
        return clamp_row(playhead_row + std::round(amount), measures);
    }
    return std::nullopt;
}

} // namespace

auto is_preview_playing(const application_command_state_t& application) -> bool {
    return application.services.transport_preview_playing && application.services.transport_preview_playing();
}

auto playhead_skip_target_row(const measure_index_t& measures, double playhead_row,
                              playhead_skip_unit_t unit, double amount,
                              std::uint64_t divisions_per_measure, std::uint64_t snap_denominator,
                              const export_cut_axis_t* cut_axis, const timing_index_t* timing)
    -> std::optional<std::uint64_t> {
    const auto target = skip_target_row(measures, playhead_row, unit, amount,
                                        divisions_per_measure, snap_denominator, cut_axis, timing);
    if (!target || !cut_axis || cut_axis->is_empty() ||
        unit == playhead_skip_unit_t::snap || unit == playhead_skip_unit_t::beat) return target;
    // Measure, page and row skips land anywhere; pull the landing out of a cut.
    const auto chart_like = chart_document_t{.divisions_per_standard_measure = divisions_per_measure};
    const auto unusable = cut_unusable(timing, cut_axis);
    return visible_step_row(chart_like, measures, cut_axis,
        {.grid = {snap_denominator ? snap_denominator : 4, quantization_space_t::standard_measure},
         .unusable = unusable ? &unusable : nullptr},
        measures.total_rows, *target, amount > 0);
}

auto skip_playing_playhead(const document_history_t& history,
                           application_command_state_t& application, const chart_document_t& chart,
                           std::uint64_t snap_denominator, playhead_skip_unit_t unit, double amount,
                           const export_cut_axis_t* cut_axis, const timing_index_t* timing)
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
    if (!timing && indices->timing.value) timing = &*indices->timing.value;
    const auto row = playhead_skip_target_row(measures,
                                              skip_origin_row(application, chart.id, forward),
                                              unit, amount, chart.divisions_per_standard_measure,
                                              snap_denominator, cut_axis, timing);
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
    auto playhead = application.project.transport.preview_playhead_absolute_row;
    // Continue a gesture from its last target while a seek is still reaching the audio clock.
    const auto& scrolled = application.project.transport.last_playhead_scroll;
    if (scrolled && scrolled->chart == chart.id &&
        application.bookmarks.now() - scrolled->at <= playing_seek_repeat_window)
        playhead = scrolled->row;
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
    auto target = std::clamp(axis.row_at_y(axis.y_for_row(*playhead) + delta_pixels),
        0.0L, static_cast<long double>(indices->measures->total_rows - 1));
    // A kept row inside the cut in time would collapse onto the seam: hop past it.
    if (const auto unusable = cut_unusable(timing, state.cut_axis.get())) {
        const auto forward = delta_pixels > 0.0;
        for (int guard = 0; guard < 8 && unusable(static_cast<std::uint64_t>(std::floor(target))); ++guard)
            target = std::clamp(forward ? std::floor(target) + 1.0L : std::floor(target) - 1.0L,
                                0.0L, static_cast<long double>(indices->measures->total_rows - 1));
    }
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
    if (outcome.accepted) {
        application.project.transport.preview_playhead_absolute_row = static_cast<double>(target);
        // After the seek, which clears the seek records.
        application.project.transport.last_playhead_scroll = {chart.id, static_cast<double>(target),
                                                             application.bookmarks.now()};
    }
    return outcome;
}

} // namespace wormedit::sys
