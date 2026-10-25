#pragma once

#include <algorithm>
#include <utility>

namespace wormedit::sys {

namespace recipe_area_detail {

// SV and scroll values hold until the next point; SPEEDS ease between points.
inline auto is_stepped(const recipe_area_t& area) -> bool {
    return area.output != sv_recipe_output_t::speeds;
}

// The recipe's value at `row`: the latest point at or before it when stepped, otherwise linear
// between the surrounding points.
inline auto value_at(const recipe_area_t& area, std::uint64_t row) -> double {
    const auto& points = area.points;
    if (row <= points.front().row) return points.front().value;
    if (row >= points.back().row) return points.back().value;
    const auto after = std::ranges::upper_bound(points, row, {}, &recipe_value_point_t::row);
    const auto& right = *after;
    const auto& left = *(after - 1);
    if (is_stepped(area)) return left.value;
    const auto span = static_cast<double>(right.row - left.row);
    const auto t = span > 0.0 ? static_cast<double>(row - left.row) / span : 0.0;
    return left.value + (right.value - left.value) * t;
}

// The window edges plus every sampled point strictly inside; a stepped graph also keeps the held
// value at each point's row so the change draws as a vertical edge.
inline void keep_window_points(const recipe_area_t& area, std::uint64_t begin, std::uint64_t end,
                               std::vector<std::pair<std::uint64_t, double>>& kept) {
    kept.clear();
    kept.emplace_back(begin, value_at(area, begin));
    for (const auto& point : area.points) {
        if (point.row <= begin || point.row >= end) continue;
        if (is_stepped(area)) kept.emplace_back(point.row, kept.back().second);
        kept.emplace_back(point.row, point.value);
    }
    kept.emplace_back(end, value_at(area, is_stepped(area) && end > begin ? end - 1 : end));
}

} // namespace recipe_area_detail

template <typename RowToY>
auto build_recipe_graph(const recipe_area_t& area, std::uint64_t first_row, std::uint64_t last_row,
                        RowToY&& row_to_y, recipe_graph_t& graph) -> void {
    graph.points.clear();
    graph.minimum = graph.maximum = 1.0;
    const auto begin = std::max(first_row, area.begin_row);
    const auto end = std::min(last_row, area.end_row);
    if (!area.evaluable || area.points.empty() || end <= begin) return;
    auto& kept = graph.kept;
    recipe_area_detail::keep_window_points(area, begin, end, kept);
    graph.minimum = graph.maximum = kept.front().second;
    for (const auto& [row, value] : kept) {
        graph.minimum = std::min(graph.minimum, value);
        graph.maximum = std::max(graph.maximum, value);
    }
    const auto span = graph.maximum - graph.minimum;
    for (const auto& [row, value] : kept)
        graph.points.push_back({span > 0.0 ? (value - graph.minimum) / span : 0.5, row_to_y(row)});
}

template <typename RowToY>
auto build_recipe_graph(const recipe_area_t& area, std::uint64_t first_row, std::uint64_t last_row,
                         RowToY&& row_to_y) -> recipe_graph_t {
    recipe_graph_t graph;
    build_recipe_graph(area, first_row, last_row, std::forward<RowToY>(row_to_y), graph);
    return graph;
}

} // namespace wormedit::sys
