#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/timing.h>

#include "sys/compose_sv_graph.h"

#include <algorithm>
#include <cstddef>
#include <utility>
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
    // The value in force where the span begins: the previous span's value at that row, which is
    // only partway to its target when this event cut it short.
    double initial_multiplier{1.0};
    // Length of the transition in beats, or in seconds when `transition_is_seconds`.
    double transition_length{};
    bool transition_is_seconds{false};
    bool clipped{false};

    auto operator==(const speed_span_t&) const -> bool = default;
};

struct speed_spans_view_t {
    // In compiled SPEEDS order.
    std::vector<speed_span_t> spans;
    // True when the limit cut the list or the evaluator rejected part of the chart.
    bool truncated{false};

    // Evaluates the chart's SPEEDS through the exact core evaluator. Never mutates the chart; a chart
    // whose SPEEDS cannot be evaluated yields no spans.
    [[nodiscard]] static auto from(const chart_document_t& chart, const timing_index_t& timing,
                                   std::size_t limit) -> speed_spans_view_t;
};

// The speed multiplier as a continuous curve over the viewport's source rows [first_row,
// last_row]: 1.0 before the first event, then each span interpolates linearly from the value in
// force at its begin to its target, which then stays latched. Zero-length spans jump. The point
// format and normalization match build_sv_graph (x in [0, 1], y from `row_to_y`).
template <typename RowToY>
[[nodiscard]] auto build_speed_graph(const speed_spans_view_t& spans, double first_row, double last_row,
                                     RowToY&& row_to_y) -> sv_graph_t {
    sv_graph_t graph;
    if (!(last_row > first_row)) return graph;
    // The curve as (row, value) vertices; vertical jumps repeat a row.
    std::vector<std::pair<double, double>> curve;
    // A span cut short by a replacement ends at the value the replacement starts from, which is
    // partway to the target; a span that ran its course ends at its target.
    for (std::size_t index = 0; index < spans.spans.size(); ++index) {
        const auto& span = spans.spans[index];
        const bool cut_short = span.clipped && index + 1 < spans.spans.size();
        curve.emplace_back(span.begin_row, span.initial_multiplier);
        curve.emplace_back(span.end_row,
                           cut_short ? spans.spans[index + 1].initial_multiplier : span.multiplier);
    }
    const auto value_at = [&](double row) {
        double value = 1.0;
        for (std::size_t index = 0; index < curve.size(); ++index) {
            if (curve[index].first > row) {
                if (index == 0) return value;
                const auto& before = curve[index - 1];
                const auto& after = curve[index];
                return after.first > before.first
                    ? before.second + (after.second - before.second) *
                          (row - before.first) / (after.first - before.first)
                    : before.second;
            }
            value = curve[index].second;
        }
        return value;
    };
    std::vector<std::pair<double, double>> window{{first_row, value_at(first_row)}};
    // Vertices on the last row belong to the window too: value_at(last_row) already includes the
    // jump there, so leaving them out would draw that jump as a slanted ramp.
    for (const auto& vertex : curve)
        if (vertex.first > first_row && vertex.first <= last_row) window.push_back(vertex);
    if (window.back().first < last_row) window.emplace_back(last_row, value_at(last_row));
    graph.minimum = graph.maximum = window.front().second;
    for (const auto& [row, value] : window) {
        graph.minimum = std::min(graph.minimum, value);
        graph.maximum = std::max(graph.maximum, value);
    }
    const auto range = graph.maximum - graph.minimum;
    const auto normalize = [&](double value) { return range > 0.0 ? (value - graph.minimum) / range : 0.5; };
    if (1.0 >= graph.minimum && 1.0 <= graph.maximum) graph.one_x = normalize(1.0);
    for (const auto& [row, value] : window) graph.points.push_back({normalize(value), row_to_y(row)});
    return graph;
}

} // namespace wormedit::sys
