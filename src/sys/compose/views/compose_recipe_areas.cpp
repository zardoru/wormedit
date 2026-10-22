#include "sys/compose/views/compose_recipe_areas.h"

#include <wormedit/core/sv_recipe.h>

namespace wormedit::sys {
namespace {

// A recipe's end may be the exclusive one-past row of the final measure.
auto recipe_row(const measure_index_t& measures, local_position_t position, bool is_end)
    -> std::optional<std::uint64_t> {
    if (const auto row = (measures).row_of(position)) return row;
    if (is_end && !measures.entries.empty()) {
        const auto& last = measures.entries.back();
        if (position.measure == last.id && position.row == last.row_capacity)
            return measures.total_rows;
    }
    return std::nullopt;
}

} // namespace

auto recipe_areas_view_t::from(const chart_document_t& chart, const measure_index_t& measures,
                          std::size_t limit) -> recipe_areas_view_t {
    recipe_areas_view_t result;
    for (const auto& recipe : chart.sv_recipes) {
        const auto begin = recipe_row(measures, recipe.range.begin, false);
        const auto end = recipe_row(measures, recipe.range.end, true);
        if (!begin || !end || *end <= *begin) {
            result.truncated = true;
            continue;
        }
        if (result.areas.size() >= limit) {
            result.truncated = true;
            break;
        }
        recipe_area_t area{.recipe = recipe.id,
                           .name = recipe.name,
                           .output = recipe.output,
                           .curve = recipe.curve,
                           .enabled = recipe.enabled,
                           .begin_row = *begin,
                           .end_row = *end};
        const auto sampled = sv_recipe_samples_t::from(chart, recipe);
        area.evaluable = sampled.diagnostics.empty() && !sampled.value.points.empty();
        if (area.evaluable) {
            for (const auto& point : sampled.value.points)
                area.points.push_back({point.absolute_row, point.value});
        }
        result.areas.push_back(std::move(area));
    }
    return result;
}

} // namespace wormedit::sys
