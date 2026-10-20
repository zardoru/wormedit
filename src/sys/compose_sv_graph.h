#pragma once

#include <wormedit/core/document.h>

#include <cstddef>
#include <cstdint>
#include <vector>
#include <utility>

namespace wormedit::sys {

// The SV (scroll) multiplier as a latched step function over absolute rows: each value holds
// until the next event. Before the first event the multiplier is 1.
struct sv_step_t {
    std::uint64_t row{};
    double value{1.0};
    object_id_t object;

    auto operator==(const sv_step_t&) const -> bool = default;
};

struct sv_steps_view_t {
    // Ordered by row; events sharing a row keep the last one in source order.
    std::vector<sv_step_t> steps;
    bool truncated{false};

    [[nodiscard]] static auto from(const chart_document_t& chart, const measure_index_t& measures,
                                   std::size_t limit) -> sv_steps_view_t;
};

// A point of the drawn graph. `x` is the value normalized to the visible range in [0, 1] and `y`
// is the viewport pixel row, so a step is a horizontal run followed by a vertical jump.
struct sv_graph_point_t {
    double x{};
    double y{};
};

struct sv_graph_t {
    std::vector<sv_graph_point_t> points;
    // Retained workspace for the step and continuous-curve projections.
    std::vector<std::pair<std::uint64_t, double>> held;
    std::vector<std::pair<double, double>> curve;
    std::vector<std::pair<double, double>> window;
    // Value range the x axis covers, and where the 1.0x reference sits (negative when outside).
    double minimum{1.0};
    double maximum{1.0};
    double one_x{-1.0};
};

// The graph of `steps` between the viewport's first and last source row. The range is normalized to
// the values in force inside the window (a single value centers at 0.5). `row_to_y` maps an
// absolute row to viewport pixels in drawn orientation.
template <typename RowToY>
[[nodiscard]] auto build_sv_graph(const sv_steps_view_t& steps, std::uint64_t first_row,
                                  std::uint64_t last_row, RowToY&& row_to_y) -> sv_graph_t;

} // namespace wormedit::sys

#include "sys/compose_sv_graph.inl"
