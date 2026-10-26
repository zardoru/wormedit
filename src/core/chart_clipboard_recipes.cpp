#include "chart_clipboard_recipes.h"
#include "id_ceiling.h"
#include <wormedit/core/rational.h>
#include <wormedit/core/sv_recipe.h>

#include <algorithm>
#include <limits>

namespace wormedit {
auto append_clipboard_recipe(chart_clipboard_t& clipboard, const chart_document_t& chart,
    const measure_index_t& index, recipe_id_t id) -> bool {
    if (!id) return true;
    const auto found = std::ranges::find(chart.sv_recipes, id, &sv_recipe_t::id);
    if (found == chart.sv_recipes.end()) return false;
    const auto begin = index.row_of(found->range.begin);
    const auto end = index.row_of(found->range.end, measure_chart_end_policy_t::allow);
    if (!begin || !end || *begin >= *end) return false;
    auto value = *found;
    if (!value.definition) value.definition = value.id;
    clipboard.recipes.push_back({
        .offset = rows_to_fraction(*begin, chart.divisions_per_standard_measure),
        .duration = rows_to_fraction(*end - *begin, chart.divisions_per_standard_measure),
        .value = std::move(value)});
    return true;
}

namespace {
auto clipboard_definition(const song_document_t& document, const chart_clipboard_t& clipboard,
    const sv_recipe_t& value) -> sv_recipe_definition_t {
    if (const auto* definition = document.find_sv_recipe_definition(value.definition)) return *definition;
    const auto* chart = document.find_chart(clipboard.source_chart);
    if (chart) {
        const auto source = std::ranges::find(chart->sv_recipes, value.id, &sv_recipe_t::id);
        if (source != chart->sv_recipes.end())
            return sv_recipe_definition_t::from(value.definition, *source);
    }
    return sv_recipe_definition_t::from(value.definition, value);
}

auto clipboard_recipe_range(const chart_clipboard_recipe_t& recipe,
    const chart_document_t& chart, const measure_index_t& index, std::uint64_t anchor)
    -> std::optional<position_span_t> {
    const auto offset = exact_clipboard_rows(recipe.offset, chart.divisions_per_standard_measure);
    const auto duration = exact_clipboard_rows(recipe.duration, chart.divisions_per_standard_measure);
    if (!offset || !duration || !*duration || *offset > std::numeric_limits<std::uint64_t>::max() - anchor)
        return std::nullopt;
    const auto begin_row = anchor + *offset;
    if (*duration > std::numeric_limits<std::uint64_t>::max() - begin_row) return std::nullopt;
    const auto begin = index.position_at(begin_row);
    const auto end = index.position_at(begin_row + *duration, measure_chart_end_policy_t::allow);
    if (!begin || !end) return std::nullopt;
    return position_span_t{*begin, *end};
}
} // namespace

auto place_clipboard_recipes(const song_document_t& document, const chart_document_t& chart,
    const measure_index_t& index, const chart_clipboard_t& clipboard, std::uint64_t anchor,
    chart_paste_proposal_t& proposal) -> result_t<void, std::string> {
    if (chart.sv_recipes.size() + clipboard.recipes.size() > maximum_sv_recipes_per_chart)
        return std::unexpected("Paste would exceed the SV recipe limit.");
    auto id = id_ceiling_t<recipe_id_t>::from(document).maximum;
    for (const auto& source : clipboard.recipes) {
        if (!source.value.definition) return std::unexpected("Clipboard recipe has no definition reference.");
        // Reserve the reference even when Cut removed its final placement.
        id = std::max(id, source.value.definition.value);
        if (id == std::numeric_limits<std::uint64_t>::max()) return std::unexpected("SV recipe IDs are exhausted.");
        const auto range = clipboard_recipe_range(source, chart, index, anchor);
        if (!range) return std::unexpected("A recipe range cannot be represented exactly in the target chart.");
        const auto definition = clipboard_definition(document, clipboard, source.value);
        auto value = definition.make_placement(recipe_id_t{++id}, *range, source.value.enabled);
        const auto diagnostics = validate_sv_recipe(chart, value);
        if (diagnostic_queries::has_errors(diagnostics)) return std::unexpected(diagnostics.front().message);
        if (!document.find_sv_recipe_definition(definition.id)) proposal.definitions.push_back(definition);
        proposal.report.recipes.push_back(std::move(value));
    }
    return {};
}

void stage_clipboard_recipes(song_document_t& document, chart_id_t target,
    const chart_paste_proposal_t& proposal) {
    for (const auto& definition : proposal.definitions) {
        if (!document.find_sv_recipe_definition(definition.id)) document.sv_recipe_definitions.push_back(definition);
        // An independent source acquires a shared definition on its first paste.
        for (auto& chart : document.charts) {
            for (auto& recipe : chart.sv_recipes) {
                if (recipe.id == definition.id && !recipe.definition) recipe.definition = definition.id;
            }
        }
    }
    auto* chart = document.find_chart(target);
    chart->sv_recipes.insert(chart->sv_recipes.end(), proposal.report.recipes.begin(), proposal.report.recipes.end());
}
} // namespace wormedit
