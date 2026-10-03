#include "sys/compose_speed_spans.h"

namespace wormedit::sys {
namespace {

template <typename Rational>
auto as_double(Rational value) -> double {
    return value.denominator == 0 ? 0.0
                                  : static_cast<double>(static_cast<long double>(value.numerator) /
                                                        static_cast<long double>(value.denominator));
}

} // namespace

auto project_speed_spans(const chart_document_t& chart, const timing_index_t& timing,
                         std::size_t limit) -> speed_spans_t {
    speed_spans_t result;
    if (chart.speeds.empty()) return result;
    const auto built = build_scroll_speed_index(chart, timing);
    if (!built.index) {
        result.truncated = true;
        return result;
    }
    const auto spans = scroll_speed_transition_spans(*built.index, timing, {.maximum_spans = limit});
    result.truncated = spans.truncated || !spans;
    const auto& segments = built.index->speed_segments;
    for (std::size_t index = 0; index < spans.spans.size() && index < segments.size(); ++index) {
        const auto& span = spans.spans[index];
        const auto& segment = segments[index];
        result.spans.push_back({.object = segment.source_event,
                                .begin_row = as_double(span.begin_row),
                                .end_row = as_double(span.end_row),
                                .multiplier = as_double(segment.target_multiplier),
                                .initial_multiplier = as_double(segment.initial_multiplier),
                                .transition_length = as_double(segment.transition_duration),
                                .transition_is_seconds = segment.transition_is_seconds,
                                .clipped = span.clipped});
    }
    return result;
}

} // namespace wormedit::sys
