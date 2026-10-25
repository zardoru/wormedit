#include "sys/compose/views/compose_view_internal.h"
#include <wormedit/core/editor_axis.h>
#include <wormedit/core/interaction_cursor_pose.h>

#include <algorithm>
#include <cmath>

namespace wormedit::sys::compose_projection_detail {
namespace {

auto cut_source_duration(const song_document_t& document, const timing_index_t& timing) -> double {
    auto seconds = timing.rows.empty() ? 0.0 : to_double(timing.rows.back().after_stops_seconds);
    if (const auto* stream = document.longest_music_or_bgm_stream(); stream && stream->frame_count && stream->sample_rate)
        seconds = std::max(seconds, static_cast<double>(*stream->frame_count) / stream->sample_rate);
    return std::max(0.0, seconds);
}

auto build_cut_axis(const compose_interaction_population_context_t& input)
    -> std::shared_ptr<const export_cut_axis_t> {
    auto& cache = input.frame.cache;
    cache.timing.cut_axis_error.clear();
    if (input.frame.view().timing_focus || input.frame.history.document.export_cuts.empty()) return {};
    const auto timeline = export_cut_timeline_t::from(input.frame.history.document.export_cuts);
    if (!timeline.ok() || !cache.timing.marker_timing_index) {
        cache.timing.cut_axis_error = "Export cuts cannot be projected; Compose keeps the full source timeline.";
        return {};
    }
    const auto& timing = *cache.timing.marker_timing_index;
    if (!(timeline.value.collapsed_seconds(cut_source_duration(input.frame.history.document, timing)) > 0.0)) {
        cache.timing.cut_axis_error = "Export cuts cover the whole song; Compose keeps the full source timeline.";
        return {};
    }
    auto axis = export_cut_axis_t::from(timing, timeline.value);
    if (!axis.ok()) {
        cache.timing.cut_axis_error = axis.diagnostics.front().message;
        return {};
    }
    return std::make_shared<const export_cut_axis_t>(std::move(axis.value));
}

void retain_cut_axis(const compose_interaction_population_context_t& input,
                     std::shared_ptr<const export_cut_axis_t> axis) {
    auto& state = *input.frame.retained_state;
    input.frame.view().cut_axis = axis;
    if (state.cut_axis == axis) return;
    const auto row = state.cursor_canvas_row(input.scope.measures).value_or(0) + state.cursor_subrow;
    const auto old_y = editor_axis_projection_t::from(state, input.scope.chart.divisions_per_standard_measure).y_for_row(row);
    state.cut_axis = std::move(axis);
    const auto new_y = editor_axis_projection_t::from(state, input.scope.chart.divisions_per_standard_measure).y_for_row(row);
    set_viewport_scroll_pixels(state, state.viewport_scroll_pixels + new_y - old_y);
    (void)synchronize_editor_cursor_to_viewport(input.scope.chart, input.scope.measures, state);
}

} // namespace

void prepare_compose_cut_axis(const compose_interaction_population_context_t& input) {
    const auto key = std::tuple{input.frame.history.document.id, input.scope.chart.id,
        input.frame.history.document.revision, input.frame.view().timing_focus};
    const auto& axis = input.frame.cache.timing.cut_axis.get_or_build(key, [&input] { return build_cut_axis(input); });
    retain_cut_axis(input, axis);
    if (!input.frame.cache.timing.cut_axis_error.empty())
        report_compose_projection(input.frame.flattened, projection_feedback_kind_t::field_scene,
            notification_severity_t::warning, input.frame.cache.timing.cut_axis_error);
}

} // namespace wormedit::sys::compose_projection_detail
