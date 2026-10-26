#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/timing.h>

#include "sys/compose/compose_sv_graph.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <span>
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
    // Effective endpoint from the core evaluator when the authored target was not reached.
    std::optional<double> clipped_multiplier;

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

namespace speed_graph_detail {

inline auto value_at(std::span<const std::pair<double, double>> curve, double row) -> double {
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
}

} // namespace speed_graph_detail

// The speed multiplier as a continuous curve over the viewport's source rows [first_row,
// last_row]: 1.0 before the first event, then each span interpolates linearly from the value in
// force at its begin to its target, which then stays latched. Zero-length spans jump. The point
// format and normalization match build_sv_graph (x in [0, 1], y from `row_to_y`).
template <typename RowToY>
auto build_speed_graph(const speed_spans_view_t& spans, double first_row, double last_row,
                                     RowToY&& row_to_y, sv_graph_t& graph) -> void {
    graph.points.clear();
    graph.minimum = graph.maximum = 1.0;
    graph.one_x = -1.0;
    if (!(last_row > first_row)) return;
    // The curve as (row, value) vertices; vertical jumps repeat a row.
    auto& curve = graph.curve;
    curve.clear();
    for (const auto& span : spans.spans) {
        if (span.begin_row > 0 || span.end_row > span.begin_row)
            curve.emplace_back(span.begin_row, span.initial_multiplier);
        curve.emplace_back(span.end_row, span.clipped_multiplier.value_or(span.multiplier));
    }
    graph.set_value_range(curve.empty() || curve.front().first > 0 ? 1.0 : curve.front().second,
        curve, [](const auto& vertex) { return vertex.second; });
    auto& window = graph.window;
    window.clear();
    window.emplace_back(first_row, speed_graph_detail::value_at(curve, first_row));
    // Vertices on the last row belong to the window too: the value at that row already includes
    // the jump there, so leaving them out would draw that jump as a slanted ramp.
    for (const auto& vertex : curve)
        if (vertex.first > first_row && vertex.first <= last_row) window.push_back(vertex);
    if (window.back().first < last_row) window.emplace_back(last_row, speed_graph_detail::value_at(curve, last_row));
    for (const auto& [row, value] : window) graph.points.push_back({graph.value_x(value), row_to_y(row)});
}

template <typename RowToY>
auto build_speed_graph(const speed_spans_view_t& spans, double first_row, double last_row,
                         RowToY&& row_to_y) -> sv_graph_t {
    sv_graph_t graph;
    build_speed_graph(spans, first_row, last_row, std::forward<RowToY>(row_to_y), graph);
    return graph;
}

} // namespace wormedit::sys
