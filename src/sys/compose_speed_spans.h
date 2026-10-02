#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/timing.h>

#include <cstddef>
#include <vector>

namespace wormedit::sys {

// The extent of one StepMania SPEEDS event's transition toward its target multiplier, in absolute
// chart rows (possibly fractional: a seconds-clock transition ends between rows). The span is cut
// short by a replacement speed event or the chart end (`clipped`).
struct speed_span_t {
    object_id_t object;
    double begin_row{};
    double end_row{};
    double multiplier{1.0};
    // Length of the transition in beats, or in seconds when `transition_is_seconds`.
    double transition_length{};
    bool transition_is_seconds{false};
    bool clipped{false};

    auto operator==(const speed_span_t&) const -> bool = default;
};

struct speed_spans_t {
    // In compiled SPEEDS order.
    std::vector<speed_span_t> spans;
    // True when the limit cut the list or the evaluator rejected part of the chart.
    bool truncated{false};
};

// Evaluates the chart's SPEEDS through the exact core evaluator. Never mutates the chart; a chart
// whose SPEEDS cannot be evaluated yields no spans.
[[nodiscard]] auto project_speed_spans(const chart_document_t& chart, const timing_index_t& timing,
                                       std::size_t limit) -> speed_spans_t;

} // namespace wormedit::sys
