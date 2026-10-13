#pragma once

#include <wormedit/core/document.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wormedit::sys {

// A stored SV recipe as Compose shows it: an area over its rows with the recipe's own relative
// value drawn as a line graph. Recipes are continuous, so unlike the latched SV step graph the
// graph connects the sampled points with straight lines.
struct recipe_value_point_t {
    std::uint64_t row{};
    double value{};

    auto operator==(const recipe_value_point_t&) const -> bool = default;
};

struct recipe_area_t {
    recipe_id_t recipe;
    std::string name;
    sv_recipe_output_t output{sv_recipe_output_t::sv};
    sv_recipe_curve_t curve{sv_recipe_curve_t::linear};
    bool enabled{true};
    // False when the recipe cannot be sampled (a Lua expression curve, or invalid data); the area
    // is still shown, without a graph.
    bool evaluable{false};
    // Absolute rows, half-open [begin_row, end_row).
    std::uint64_t begin_row{};
    std::uint64_t end_row{};
    std::vector<recipe_value_point_t> points;

    auto operator==(const recipe_area_t&) const -> bool = default;
};

struct recipe_areas_t {
    // In stored recipe order.
    std::vector<recipe_area_t> areas;
    bool truncated{false};
};

// One area per stored recipe whose range lies inside the chart. Sampling is read-only; nothing
// is written to the chart.
[[nodiscard]] auto build_recipe_areas_view(const chart_document_t& chart,
                                        const measure_index_t& measures, std::size_t limit)
    -> recipe_areas_t;

// A vertex of an area's graph: `x` is the value normalized to the visible range in [0, 1] and
// `y` the viewport pixel row.
struct recipe_graph_point_t {
    double x{};
    double y{};
};

struct recipe_graph_t {
    std::vector<recipe_graph_point_t> points;
    double minimum{1.0};
    double maximum{1.0};
};

// The part of `area` between first_row and last_row (clipped by interpolating at the window
// edges), normalized to the values that remain visible; a single value centers at 0.5.
// `row_to_y` maps an absolute row to viewport pixels in drawn orientation.
template <typename RowToY>
[[nodiscard]] auto build_recipe_graph(const recipe_area_t& area, std::uint64_t first_row,
                                      std::uint64_t last_row, RowToY&& row_to_y) -> recipe_graph_t;

} // namespace wormedit::sys

#include "sys/compose_recipe_areas.inl"
