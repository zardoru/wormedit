#include <wormedit/core/rational.h>
#include "sys/compose/compose_speed_spans.h"

namespace wormedit::sys {


auto speed_spans_view_t::from(const chart_document_t& chart, const timing_index_t& timing,
                         std::size_t limit) -> speed_spans_view_t {
    speed_spans_view_t result;
    if (chart.speeds.empty()) return result;
    const auto built = scroll_speed_index_t::from(chart, timing);
    if (!built.value) {
        result.truncated = true;
        return result;
    }
    const auto spans = scroll_speed_transition_spans_t::from(*built.value, timing, {.maximum_spans = limit});
    result.truncated = spans.value.truncated || !spans.ok();
    const auto& segments = built.value->speed_segments;
    for (std::size_t index = 0; index < spans.value.spans.size() && index < segments.size(); ++index) {
        const auto& span = spans.value.spans[index];
        const auto& segment = segments[index];
        std::optional<double> clipped_multiplier;
        if (span.clipped && index + 1 < segments.size()) {
            clipped_multiplier = to_double(segments[index + 1].initial_multiplier);
        } else if (span.clipped) {
            const auto side = span.transition_is_seconds ? timing_boundary_side_t::after_stops
                                                         : timing_boundary_side_t::before_delays;
            const auto end = scroll_speed_at(*built.value, timing, span.end_row, side);
            if (!end) {
                result.truncated = true;
                break;
            }
            clipped_multiplier = to_double(end.state->speed_multiplier);
        }
        result.spans.push_back({.object = segment.source_event,
                                .begin_row = to_double(span.begin_row),
                                .end_row = to_double(span.end_row),
                                .multiplier = to_double(segment.target_multiplier),
                                .initial_multiplier = to_double(segment.initial_multiplier),
                                .transition_length = to_double(segment.transition_duration),
                                .transition_is_seconds = segment.transition_is_seconds,
                                .clipped = span.clipped,
                                .clipped_multiplier = clipped_multiplier});
    }
    return result;
}

} // namespace wormedit::sys
