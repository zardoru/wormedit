#include "sys/compose_minimap_navigation.h"
#include "sys/compose_scroll_geometry.h"
#include <wormedit/audio/decoder.h>
#include <algorithm>
#include <cmath>
#include <limits>
namespace wormedit::sys {
namespace {
auto row_for_time(const timing_index_t& timing, signed_rational_t seconds) -> long double {
    const auto position = musical_position_at(timing, seconds);
    if (position && position.absolute_row)
        return std::clamp(static_cast<long double>(position.absolute_row->numerator) /
                          position.absolute_row->denominator, 0.0L,
                          static_cast<long double>(timing.total_rows));
    const auto end = timing_at(timing, timing.total_rows);
    if (end && end.seconds && static_cast<long double>(seconds.numerator) / seconds.denominator >=
        static_cast<long double>(end.seconds->numerator) / end.seconds->denominator)
        return static_cast<long double>(timing.total_rows);
    return 0.0L;
}
auto target_at_frame(std::uint64_t frame, std::uint32_t preview_rate,
    const timing_index_t& timing, std::uint64_t snap_denominator)
    -> std::optional<compose_minimap_target_t> {
    if (preview_rate == 0 || timing.total_rows == 0 ||
        timing.divisions_per_standard_measure == 0 ||
        frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    const auto row = row_for_time(timing, {static_cast<std::int64_t>(frame), preview_rate});
    auto snapped = static_cast<std::uint64_t>(std::floor(row));
    // The chart endpoint has no editable row; choose its last active grid row.
    snapped = std::min(snapped, timing.total_rows - 1);
    if (snap_denominator != 0) {
        if (timing.divisions_per_standard_measure % snap_denominator != 0) return std::nullopt;
        const auto step = timing.divisions_per_standard_measure / snap_denominator;
        if (step == 0) return std::nullopt;
        snapped -= snapped % step;
    }
    return compose_minimap_target_t{frame, row, snapped};
}
}
auto compose_minimap_target(double fraction, std::uint64_t total_frames,
    std::uint32_t waveform_rate, std::uint32_t preview_rate, const timing_index_t& timing,
    std::uint64_t snap_denominator, std::optional<std::uint64_t> compiled_end)
    -> std::optional<compose_minimap_target_t> {
    if (!std::isfinite(fraction) || fraction < 0.0 || fraction > 1.0 || total_frames == 0 ||
        waveform_rate == 0 || preview_rate == 0 || timing.divisions_per_standard_measure == 0 ||
        timing.total_rows == 0) return std::nullopt;
    const auto source_frame = std::round(static_cast<long double>(fraction) * (total_frames - 1));
    const auto converted = std::round(source_frame * preview_rate / waveform_rate);
    if (converted > std::numeric_limits<std::int64_t>::max()) return std::nullopt;
    auto frame = static_cast<std::uint64_t>(converted);
    if (compiled_end) frame = std::min(frame, *compiled_end);
    return target_at_frame(frame, preview_rate, timing, snap_denominator);
}
auto move_compose_minimap_cursor(document_history_t& history, editor_interaction_state_t& state,
    const measure_index_t& measures, const timing_index_t& timing,
    const compose_minimap_target_t& target) -> editor_interaction_result_t {
    const auto after = std::ranges::upper_bound(measures.entries, target.snapped_row, {},
                                               &measure_index_entry_t::absolute_begin_row);
    if (after == measures.entries.begin()) return {};
    const auto& measure = *std::prev(after);
    auto result = dispatch_editor_action(history, state, jump_edit_cursor_action_t{
        {measure.id, target.snapped_row - measure.absolute_begin_row}, history.document.revision});
    if (!result.accepted) return result;
    const auto scroll = static_cast<double>(target.absolute_row) * state.pixels_per_standard_measure /
        timing.divisions_per_standard_measure - state.viewport_height_pixels * editor_cursor_edge_margin_fraction;
    // This scroll must preserve the explicit floor-snap cursor. A fixed-anchor
    // scroll would round it again and could enter the virtual BMS tail.
    return dispatch_editor_action(history, state, scroll_editor_view_action_t{
        scroll - state.viewport_scroll_pixels});
}
auto seek_stopped_compose_transport(document_history_t& history,
    editor_interaction_state_t& state, const compose_scene_cache_t& cache,
    std::uint64_t frame) -> std::string {
    if (cache.chart != state.chart || cache.revision != history.document.revision ||
        !cache.marker_timing_index || !cache.cached_measure_index)
        return "Compose seek is unavailable until the chart is ready.";
    const auto chart = std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    if (chart == history.document.charts.end()) return "The selected chart is unavailable.";
    const auto target = target_at_frame(frame, audio::output_sample_rate,
        *cache.marker_timing_index, edit_snap_denominator(*chart, state));
    if (!target) return "The requested chart position is unavailable.";
    const auto moved = move_compose_minimap_cursor(history, state, *cache.cached_measure_index,
        *cache.marker_timing_index, *target);
    return moved.accepted ? "Compose cursor moved to the seek position."
                          : "The requested chart position could not be reached.";
}

void retain_compose_minimap_start(application_command_state_t& application,
    const document_history_t& history, const editor_interaction_state_t& state,
    const compose_minimap_target_t& target, const timing_index_t& timing) {
    application.minimap_start_frame.reset();
    const bool playing = application.transport_preview_playing && application.transport_preview_playing();
    const bool paused = application.transport_preview_paused && application.transport_preview_paused();
    const auto end = timing_at(timing, timing.total_rows);
    if (playing || paused || !end || !end.seconds) return;
    if (static_cast<long double>(target.frame) / audio::output_sample_rate <=
        static_cast<long double>(end.seconds->numerator) / end.seconds->denominator) return;
    application.minimap_start_frame = application_command_state_t::minimap_start_frame_t{
        state.chart, history.document.revision, state.cursor, target.frame};
}
auto compose_minimap_start_frame(application_command_state_t& application,
    const document_history_t& history, const editor_interaction_state_t* state)
    -> std::optional<std::uint64_t> {
    const auto& pending = application.minimap_start_frame;
    if (!pending) return std::nullopt;
    if (!state || state->chart != pending->chart || history.document.revision != pending->revision ||
        state->cursor != pending->cursor || state->virtual_bms_cursor_row) {
        application.minimap_start_frame.reset();
        return std::nullopt;
    }
    return pending->frame;
}
auto navigate_compose_minimap_transport(application_command_state_t& state,
    std::uint64_t frame, bool shift, bool primary) -> std::string {
    if (shift) return {};
    const bool playing = state.transport_preview_playing && state.transport_preview_playing();
    const bool paused = state.transport_preview_paused && state.transport_preview_paused();
    if (playing || (paused && (primary != state.minimap_click_moves_paused_playhead)))
        return state.transport_seek ? state.transport_seek(frame) : "Chart preview seek is unavailable.";
    if (paused && state.transport_stop) state.transport_stop();
    return {};
}
}
