#include <wormedit/core/rational.h>
#include "sys/compose_navigation.h"
#include "sys/compose_minimap_navigation.h"
#include "sys/compose_scroll_geometry.h"
#include <wormedit/audio/decoder.h>
#include <wormedit/core/snap.h>
#include <algorithm>
#include <cmath>
#include <limits>
namespace wormedit::sys {
namespace {
auto target_at_frame(std::uint64_t frame, std::uint32_t preview_rate,
    const timing_index_t& timing, const measure_index_t& measures, std::uint64_t snap_denominator)
    -> std::optional<compose_minimap_target_t> {
    if (measures.entries.empty() || measures.total_rows != timing.total_rows ||
        preview_rate == 0 || timing.total_rows == 0 ||
        timing.divisions_per_standard_measure == 0 ||
        frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    const auto position = timing.row_at({static_cast<std::int64_t>(frame), preview_rate},
                                        chart_end_policy_t::clamp_to_end);
    const auto row = position.absolute_row ? wormedit::to_long_double(*position.absolute_row) : 0.0L;
    auto snapped = static_cast<std::uint64_t>(std::floor(row));
    // The chart endpoint has no editable row; choose its last active grid row.
    snapped = std::min(snapped, timing.total_rows - 1);
    if (snap_denominator != 0) {
        chart_document_t chart;
        chart.divisions_per_standard_measure = timing.divisions_per_standard_measure;
        const auto source = std::min(row, static_cast<long double>(timing.total_rows - 1));
        auto nearest = snap_canvas_row(chart, measures, source, snap_denominator, timing.total_rows);
        if (!nearest) return std::nullopt;
        if (*nearest > source)
            nearest = adjacent_snap_canvas_row(chart, measures, source, snap_denominator,
                                              timing.total_rows, false);
        if (!nearest) return std::nullopt;
        snapped = *nearest;
    }
    return compose_minimap_target_t{frame, row, snapped};
}
}
auto compose_minimap_target(double fraction, std::uint64_t total_frames,
    std::uint32_t waveform_rate, std::uint32_t preview_rate, const timing_index_t& timing,
    const measure_index_t& measures, std::uint64_t snap_denominator, std::optional<std::uint64_t> compiled_end)
    -> std::optional<compose_minimap_target_t> {
    if (!std::isfinite(fraction) || fraction < 0.0 || fraction > 1.0 || total_frames == 0 ||
        waveform_rate == 0 || preview_rate == 0 || timing.divisions_per_standard_measure == 0 ||
        timing.total_rows == 0) return std::nullopt;
    const auto source_frame = std::round(static_cast<long double>(fraction) * (total_frames - 1));
    const auto converted = std::round(source_frame * preview_rate / waveform_rate);
    if (converted > std::numeric_limits<std::int64_t>::max()) return std::nullopt;
    auto frame = static_cast<std::uint64_t>(converted);
    if (compiled_end) frame = std::min(frame, *compiled_end);
    return target_at_frame(frame, preview_rate, timing, measures, snap_denominator);
}
auto move_compose_minimap_cursor(document_history_t& history, editor_interaction_state_t& state,
    const measure_index_t& measures, const timing_index_t& timing,
    const compose_minimap_target_t& target) -> editor_interaction_result_t {
    const auto after = std::ranges::upper_bound(measures.entries, target.snapped_row, {},
                                               &measure_index_entry_t::absolute_begin_row);
    if (after == measures.entries.begin()) return {};
    const auto& measure = *std::prev(after);
    (void)timing;
    return move_compose_navigation_cursor(history, state,
        {.position = {measure.id, target.snapped_row - measure.absolute_begin_row}},
        history.document.revision);
}
auto seek_stopped_compose_transport(document_history_t& history, editor_interaction_state_t& state,
                                    const compose_scene_cache_t& cache, std::uint64_t frame)
    -> compose_action_outcome_t {
    const auto chart = history.document.find_chart(state.chart);
    if (chart == nullptr) {
        if (cache.scene.chart != state.chart || cache.scene.revision != history.document.revision ||
            !cache.timing.marker_timing_index || !cache.scene.cached_measure_index)
            return compose_action_outcome_t::blocked(
                "Compose seek is unavailable until the chart is ready.",
                notification_source_t::input, compose_feedback_delivery_t::notification);
        return compose_action_outcome_t::blocked("The selected chart is unavailable.",
                                                 notification_source_t::input,
                                                 compose_feedback_delivery_t::notification);
    }
    if (!cache.is_scene_key_current(history.document, *chart, compose_scene_key_policy_t::transport) ||
        !cache.timing.marker_timing_index || !cache.scene.cached_measure_index)
        return compose_action_outcome_t::blocked(
            "Compose seek is unavailable until the chart is ready.", notification_source_t::input,
            compose_feedback_delivery_t::notification);
    const auto target = target_at_frame(frame, audio::output_sample_rate,
        *cache.timing.marker_timing_index, *cache.scene.cached_measure_index, edit_snap_denominator(*chart, state));
    if (!target)
        return compose_action_outcome_t::blocked("The requested chart position is unavailable.",
                                                 notification_source_t::input,
                                                 compose_feedback_delivery_t::notification);
    const auto moved = move_compose_minimap_cursor(history, state, *cache.scene.cached_measure_index,
        *cache.timing.marker_timing_index, *target);
    return moved.accepted
               ? compose_action_outcome_t::from(
                     "Compose cursor moved to the seek position.", notification_severity_t::hint,
                     notification_source_t::input, compose_feedback_delivery_t::notification)
               : compose_action_outcome_t::blocked(
                     "The requested chart position could not be reached.",
                     notification_source_t::input, compose_feedback_delivery_t::notification);
}

void retain_compose_minimap_start(application_command_state_t& application,
    const document_history_t& history, const editor_interaction_state_t& state,
    const compose_minimap_target_t& target, const timing_index_t& timing) {
    application.project.transport.minimap_start_frame.reset();
    const bool playing = application.services.transport_preview_playing && application.services.transport_preview_playing();
    const bool paused = application.services.transport_preview_paused && application.services.transport_preview_paused();
    const auto end = timing.seconds_at(timing.total_rows);
    if (playing || paused || !end || !end.seconds) return;
    if (static_cast<long double>(target.frame) / audio::output_sample_rate <=
        wormedit::to_long_double(*end.seconds)) return;
    application.project.transport.minimap_start_frame = application_command_state_t::minimap_start_frame_t{
        state.chart, history.document.revision, state.cursor, target.frame, state.cursor_subrow};
}
auto compose_minimap_start_frame(application_command_state_t& application,
    const document_history_t& history, const editor_interaction_state_t* state)
    -> std::optional<std::uint64_t> {
    const auto& pending = application.project.transport.minimap_start_frame;
    if (!pending) return std::nullopt;
    if (!state || state->chart != pending->chart || history.document.revision != pending->revision ||
        state->cursor != pending->cursor || state->cursor_subrow != pending->cursor_subrow ||
        state->virtual_bms_cursor_row) {
        application.project.transport.minimap_start_frame.reset();
        return std::nullopt;
    }
    return pending->frame;
}
auto navigate_compose_minimap_transport(application_command_state_t& state,
    std::uint64_t frame, bool shift, bool primary) -> std::string {
    if (shift) return {};
    return apply_compose_navigation_transport(
               state, compose_navigation_transport_policy(state, true, primary), frame)
        .message;
}
}
