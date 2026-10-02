#include "sys/compose_sv_graph.h"

#include <algorithm>
#include <tuple>

namespace wormedit::sys {

auto project_sv_steps(const chart_document_t& chart, const measure_index_t& measures,
                      std::size_t limit) -> sv_steps_t {
    struct placed_t {
        std::uint64_t row;
        std::size_t source_order;
        std::uint64_t id;
        double value;
    };
    sv_steps_t result;
    std::vector<placed_t> placed;
    placed.reserve(chart.scrolls.size());
    for (const auto& scroll : chart.scrolls) {
        const auto row = absolute_row(measures, scroll.position);
        if (!row || scroll.multiplier.denominator == 0) {
            result.truncated = true;
            continue;
        }
        placed.push_back({*row, scroll.provenance.source_order, scroll.id.value,
                          static_cast<double>(scroll.multiplier.numerator) /
                              static_cast<double>(scroll.multiplier.denominator)});
    }
    std::ranges::sort(placed, [](const placed_t& left, const placed_t& right) {
        return std::tie(left.row, left.source_order, left.id) <
               std::tie(right.row, right.source_order, right.id);
    });
    for (const auto& event : placed) {
        const sv_step_t step{event.row, event.value, object_id_t{event.id}};
        // Events on one row replace each other: the last in source order wins.
        if (!result.steps.empty() && result.steps.back().row == event.row)
            result.steps.back() = step;
        else
            result.steps.push_back(step);
    }
    if (result.steps.size() > limit) {
        result.steps.resize(limit);
        result.truncated = true;
    }
    return result;
}

} // namespace wormedit::sys
