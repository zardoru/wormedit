#pragma once

#include <algorithm>
#include <utility>

namespace wormedit::sys {

namespace recipe_area_detail {

// The recipe's value at `row`: interpolated linearly between its sampled points, or, when
// `latched`, held at the last sample at or before the row.
inline auto value_at(const std::vector<recipe_value_point_t>& points, std::uint64_t row,
                     bool latched) -> double {
    if (row <= points.front().row) return points.front().value;
    if (row >= points.back().row) return points.back().value;
    const auto after = std::ranges::upper_bound(points, row, {}, &recipe_value_point_t::row);
    const auto& right = *after;
    const auto& left = *(after - 1);
    if (latched) return left.value;
    const auto span = static_cast<double>(right.row - left.row);
    const auto t = span > 0.0 ? static_cast<double>(row - left.row) / span : 0.0;
    return left.value + (right.value - left.value) * t;
}

} // namespace recipe_area_detail

template <typename RowToY>
auto build_recipe_graph(const recipe_area_t& area, std::uint64_t first_row, std::uint64_t last_row,
                        RowToY&& row_to_y) -> recipe_graph_t {
    recipe_graph_t graph;
    const auto begin = std::max(first_row, area.begin_row);
    const auto end = std::min(last_row, area.end_row);
    if (!area.evaluable || area.points.empty() || end <= begin) return graph;
    // SV (scroll) output is a set of instantaneous events, so its values hold until the next
    // sample; only SPEEDS output interpolates.
    const bool latched = area.output != sv_recipe_output_t::speeds;
    // Window edges plus every sampled point strictly inside.
    std::vector<std::pair<std::uint64_t, double>> kept{
        {begin, recipe_area_detail::value_at(area.points, begin, latched)}};
    for (const auto& point : area.points)
        if (point.row > begin && point.row < end) kept.emplace_back(point.row, point.value);
    kept.emplace_back(end, recipe_area_detail::value_at(area.points, end, latched));
    graph.minimum = graph.maximum = kept.front().second;
    for (const auto& [row, value] : kept) {
        graph.minimum = std::min(graph.minimum, value);
        graph.maximum = std::max(graph.maximum, value);
    }
    const auto span = graph.maximum - graph.minimum;
    const auto normalize = [&](double value) {
        return span > 0.0 ? (value - graph.minimum) / span : 0.5;
    };
    for (std::size_t index = 0; index < kept.size(); ++index) {
        const auto y = row_to_y(kept[index].first);
        // A latched value holds, then jumps at the next sample's row.
        if (latched && index != 0) graph.points.push_back({normalize(kept[index - 1].second), y});
        graph.points.push_back({normalize(kept[index].second), y});
    }
    return graph;
}

} // namespace wormedit::sys
