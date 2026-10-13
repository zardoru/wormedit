#include "sys/compose_navigation.h"
#include "sys/compose_playhead_skip.h"
#include <wormedit/core/virtual_canvas.h>
#include <algorithm>
#include <variant>

namespace wormedit::sys {
namespace {
auto navigation_frame(const chart_document_t& chart, const application_command_state_t& application,
                       const compose_navigation_target_t& target) -> std::optional<std::uint64_t> {
    if (target.preview_frame) return target.preview_frame;
    if (target.canvas_measure) return std::nullopt;
    const auto built = build_measure_index(chart);
    const auto timing = build_timing_index(chart, {.allow_bounded_clock = true});
    if (!std::holds_alternative<measure_index_t>(built) || !timing.index) return std::nullopt;
    const auto end = application.services.transport_preview_end_frame
        ? application.services.transport_preview_end_frame() : std::nullopt;
    return bookmark_preview_frame(*timing.index, std::get<measure_index_t>(built), target.position, end);
}

auto navigation_outcome(const editor_interaction_result_t& result, std::string status)
    -> bookmark_command_outcome_t {
    return {.accepted = result.accepted, .status = std::move(status), .diagnostics = result.diagnostics};
}
}

auto compose_navigation_transport_policy(const application_command_state_t& application,
    bool minimap, bool primary) -> compose_navigation_transport_t {
    if (is_preview_playing(application)) return compose_navigation_transport_t::seek;
    const bool paused = application.services.transport_preview_paused && application.services.transport_preview_paused();
    if (!minimap || !paused) return compose_navigation_transport_t::keep;
    return primary != application.preferences.minimap_click_moves_paused_playhead
        ? compose_navigation_transport_t::seek : compose_navigation_transport_t::stop;
}

auto apply_compose_navigation_transport(application_command_state_t& application,
    compose_navigation_transport_t policy, std::optional<std::uint64_t> frame,
    std::optional<std::int64_t> audio_milliseconds) -> bookmark_command_outcome_t {
    if (policy != compose_navigation_transport_t::seek) {
        if (policy == compose_navigation_transport_t::stop && application.services.transport_stop)
            application.services.transport_stop();
        return {.accepted = true};
    }
    if (audio_milliseconds) {
        if (!application.services.transport_seek_audio_milliseconds)
            return {.status = "Audio-time bookmark seek is unavailable."};
        application.invalidate_playing_seek_records();
        return {.accepted = true,
            .status = application.services.transport_seek_audio_milliseconds(*audio_milliseconds),
            .feedback_posted = application.preferences.transport_seek_posts_feedback};
    }
    if (!frame || !application.services.transport_seek) return {.status = "Chart preview seek is unavailable."};
    application.invalidate_playing_seek_records();
    return {.accepted = true, .status = application.services.transport_seek(*frame),
        .feedback_posted = application.preferences.transport_seek_posts_feedback};
}

auto move_compose_navigation_cursor(document_history_t& history, editor_interaction_state_t& state,
    const compose_navigation_target_t& target, std::uint64_t expected_revision) -> editor_interaction_result_t {
    auto candidate = state;
    set_cursor_screen_anchor(candidate,
        editor_cursor_screen_anchor(candidate.viewport_height_pixels, candidate.scroll_direction),
        candidate.scroll_direction);
    if (!target.canvas_measure) {
        auto result = dispatch_editor_action(history, candidate,
            jump_edit_cursor_action_t{target.position, expected_revision});
        if (result.accepted) state = std::move(candidate);
        return result;
    }
    if (expected_revision != history.document.revision) return {};
    auto result = dispatch_editor_action(history, candidate,
        select_bms_canvas_measure_action_t{*target.canvas_measure});
    if (result.accepted) {
        // Canvas-measure picking also selects objects; navigation retains the selection.
        restore_cursor_pose(state, capture_cursor_pose(candidate));
        state.active_lane = candidate.active_lane;
    }
    return result;
}

auto scroll_compose_navigation_view(document_history_t& history, editor_interaction_state_t& state,
    double target_scroll, std::optional<editor_field_scroll_direction_t> direction) -> editor_interaction_result_t {
    const auto scroll_direction = direction.value_or(state.scroll_direction);
    return dispatch_editor_action(history, state, scroll_editor_view_action_t{
        target_scroll - state.viewport_scroll_pixels,
        editor_cursor_screen_anchor(state.viewport_height_pixels, scroll_direction), scroll_direction});
}

auto reset_compose_navigation_view(document_history_t& history, editor_interaction_state_t& state)
    -> editor_interaction_result_t {
    auto candidate = state;
    auto result = dispatch_editor_action(history, candidate, adjust_editor_zoom_action_t{
        .factor = default_pixels_per_standard_measure / candidate.pixels_per_standard_measure,
        .anchor_screen_y_pixels = std::nullopt});
    if (result.accepted) result = scroll_compose_navigation_view(history, candidate, 0);
    if (result.accepted) state = std::move(candidate);
    return result;
}

auto seek_compose_navigation_position(const chart_document_t& chart,
    application_command_state_t& application, const compose_navigation_target_t& target)
    -> bookmark_command_outcome_t {
    const auto frame = target.audio_milliseconds ? std::nullopt : navigation_frame(chart, application, target);
    return apply_compose_navigation_transport(application, compose_navigation_transport_t::seek,
                                               frame, target.audio_milliseconds);
}

auto navigate_compose_position(document_history_t& history, editor_interaction_state_t& state,
    application_command_state_t& application, const compose_navigation_target_t& target,
    std::uint64_t expected_revision) -> bookmark_command_outcome_t {
    if (expected_revision != history.document.revision)
        return {.status = "The navigation request is stale."};
    if (has_pending_editor_gesture(state))
        return {.status = "Finish or cancel the active Compose gesture before navigating."};
    const auto* chart = history.document.find_chart(state.chart);
    if (!chart) return {.status = "The selected difficulty is no longer available."};
    const auto playing = is_preview_playing(application);
    const auto policy = compose_navigation_transport_policy(application, target.minimap, target.primary);
    auto candidate = state;
    if (!playing || target.minimap) {
        const auto moved = move_compose_navigation_cursor(history, candidate, target, expected_revision);
        if (!moved.accepted) return navigation_outcome(moved, "The requested chart position could not be reached.");
    }
    auto outcome = policy == compose_navigation_transport_t::seek
        ? seek_compose_navigation_position(*chart, application, target)
        : apply_compose_navigation_transport(application, policy, std::nullopt);
    if (outcome.accepted && outcome.status.empty())
        outcome.status = playing ? "Compose playhead moved." : "Compose cursor moved.";
    if (outcome.accepted && (!playing || target.minimap)) state = std::move(candidate);
    return outcome;
}

auto navigate_compose_measure(document_history_t& history, editor_interaction_state_t& state,
    application_command_state_t& application, std::uint64_t number) -> bookmark_command_outcome_t {
    const auto* chart = history.document.find_chart(state.chart);
    if (!chart) return {.status = "The selected difficulty is no longer available."};
    compose_navigation_target_t target;
    if (chart_has_virtual_tail(*chart)) {
        const auto bms = chart->source_family == format_family_t::bms;
        if (!bms && number == 0) return {.status = "The requested measure is outside this chart."};
        const auto index = bms ? number : number - 1;
        if (index >= virtual_tail_measure_limit(*chart))
            return {.status = "The requested measure is outside this chart."};
        target.canvas_measure = static_cast<std::size_t>(index);
        if (index < chart->measures.size()) {
            target.position = {chart->measures[static_cast<std::size_t>(index)].id, 0};
            target.canvas_measure.reset();
        }
    } else {
        if (number == 0 || number > chart->measures.size())
            return {.status = "The requested measure is outside this chart."};
        target.position = {chart->measures[static_cast<std::size_t>(number - 1)].id, 0};
    }
    auto outcome = navigate_compose_position(history, state, application, target, history.document.revision);
    if (outcome.accepted && !is_preview_playing(application))
        outcome.status = "Compose cursor moved to the requested measure.";
    return outcome;
}

} // namespace wormedit::sys
