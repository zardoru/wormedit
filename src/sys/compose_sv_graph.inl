#pragma once

#include <algorithm>

namespace wormedit::sys {

template <typename RowToY>
auto build_sv_graph(const sv_steps_view_t& steps, std::uint64_t first_row, std::uint64_t last_row,
                    RowToY&& row_to_y) -> sv_graph_t {
    sv_graph_t graph;
    if (last_row <= first_row) return graph;
    // The value in force at first_row, then every step strictly inside the window.
    double current = 1.0;
    std::size_t next = 0;
    while (next < steps.steps.size() && steps.steps[next].row <= first_row)
        current = steps.steps[next++].value;
    std::vector<std::pair<std::uint64_t, double>> held{{first_row, current}};
    for (; next < steps.steps.size() && steps.steps[next].row < last_row; ++next)
        held.emplace_back(steps.steps[next].row, steps.steps[next].value);
    graph.minimum = graph.maximum = held.front().second;
    for (const auto& [row, value] : held) {
        graph.minimum = std::min(graph.minimum, value);
        graph.maximum = std::max(graph.maximum, value);
    }
    const auto span = graph.maximum - graph.minimum;
    const auto normalize = [&](double value) { return span > 0.0 ? (value - graph.minimum) / span : 0.5; };
    if (1.0 >= graph.minimum && 1.0 <= graph.maximum) graph.one_x = normalize(1.0);
    for (std::size_t index = 0; index < held.size(); ++index) {
        const auto y = row_to_y(held[index].first);
        const auto x = normalize(held[index].second);
        // The jump to the new value happens at the event's row, then the value holds.
        if (index != 0) graph.points.push_back({normalize(held[index - 1].second), y});
        graph.points.push_back({x, y});
    }
    graph.points.push_back({normalize(held.back().second), row_to_y(last_row)});
    return graph;
}

} // namespace wormedit::sys
