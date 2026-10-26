#include <wormedit/core/sv_recipe.h>

#include <algorithm>
#include <map>
#include <set>

namespace wormedit {

auto sv_recipe_definition_t::from(recipe_id_t id, sv_recipe_t value) -> sv_recipe_definition_t {
    value.id = {};
    value.range = {};
    value.enabled = true;
    value.definition = {};
    return {.id = id, .value = std::move(value)};
}

auto sv_recipe_definition_t::make_placement(recipe_id_t id, position_span_t range, bool enabled) const
    -> sv_recipe_t {
    auto result = value;
    result.id = id;
    result.range = range;
    result.enabled = enabled;
    result.definition = this->id;
    return result;
}

auto song_document_t::find_sv_recipe_definition(recipe_id_t id) const -> const sv_recipe_definition_t* {
    const auto found = std::ranges::find(sv_recipe_definitions, id, &sv_recipe_definition_t::id);
    return found == sv_recipe_definitions.end() ? nullptr : &*found;
}

auto song_document_t::find_sv_recipe_definition(recipe_id_t id) -> sv_recipe_definition_t* {
    const auto found = std::ranges::find(sv_recipe_definitions, id, &sv_recipe_definition_t::id);
    return found == sv_recipe_definitions.end() ? nullptr : &*found;
}

namespace {
using definition_changes_t = std::map<recipe_id_t, sv_recipe_definition_t>;

auto record_definition_change(definition_changes_t& changes, const sv_recipe_t& recipe)
    -> diagnostic_list_t {
    auto value = sv_recipe_definition_t::from(recipe.definition, recipe);
    const auto [found, inserted] = changes.emplace(recipe.definition, value);
    if (!inserted && found->second != value)
        return {error(diagnostic_code_t::collision,
            "One edit changes the same shared SV recipe definition in different ways.")};
    return {};
}

void retain_removed_recipe_definitions(const song_document_t& before, song_document_t& proposed) {
    std::set<recipe_id_t> surviving;
    for (const auto& chart : proposed.charts)
        for (const auto& recipe : chart.sv_recipes) surviving.insert(recipe.id);
    for (const auto& chart : before.charts) {
        for (const auto& recipe : chart.sv_recipes) {
            if (recipe.definition || surviving.contains(recipe.id) || proposed.find_sv_recipe_definition(recipe.id)) continue;
            proposed.sv_recipe_definitions.push_back(sv_recipe_definition_t::from(recipe.id, recipe));
        }
    }
}

auto collect_definition_changes(const song_document_t& before, const song_document_t& proposed)
    -> result_t<definition_changes_t, diagnostic_list_t> {
    definition_changes_t changes;
    for (const auto& definition : proposed.sv_recipe_definitions) {
        const auto* previous = before.find_sv_recipe_definition(definition.id);
        if (previous && *previous != definition) changes.emplace(definition.id, definition);
    }
    for (const auto& chart : proposed.charts) {
        const auto* previous_chart = before.find_chart(chart.id);
        if (!previous_chart) continue;
        for (const auto& recipe : chart.sv_recipes) {
            if (!recipe.definition) continue;
            const auto previous = std::ranges::find(previous_chart->sv_recipes, recipe.id, &sv_recipe_t::id);
            if (previous == previous_chart->sv_recipes.end() || previous->definition != recipe.definition) continue;
            if (*previous == recipe) continue;
            if (sv_recipe_definition_t::from(recipe.definition, *previous) ==
                sv_recipe_definition_t::from(recipe.definition, recipe)) continue;
            auto diagnostics = record_definition_change(changes, recipe);
            if (!diagnostics.empty()) return std::unexpected(std::move(diagnostics));
        }
    }
    return changes;
}
auto validate_definition_grid(const chart_document_t& chart, const sv_recipe_t& recipe,
    std::map<recipe_id_t, std::uint64_t>& grids) -> diagnostic_list_t {
    if (!recipe.sampling.fixed_step_rows) return {};
    const auto [found, inserted] = grids.emplace(recipe.definition, chart.divisions_per_standard_measure);
    if (!inserted && found->second != chart.divisions_per_standard_measure)
        return {error(diagnostic_code_t::unsupported_feature,
            "Shared row-step recipes require matching chart resolutions; duplicate as independent first.")};
    return {};
}
} // namespace

auto reconcile_sv_recipe_definitions(const song_document_t& before, song_document_t& proposed)
    -> diagnostic_list_t {
    retain_removed_recipe_definitions(before, proposed);
    auto changes = collect_definition_changes(before, proposed);
    if (!changes) return std::move(changes.error());
    for (const auto& [id, definition] : *changes) {
        auto* target = proposed.find_sv_recipe_definition(id);
        if (!target) return {error(diagnostic_code_t::invalid_id, "Shared SV recipe definition is missing.")};
        *target = definition;
    }
    for (auto& chart : proposed.charts) {
        for (auto& recipe : chart.sv_recipes) {
            if (!recipe.definition) continue;
            const auto* definition = proposed.find_sv_recipe_definition(recipe.definition);
            if (!definition) return {error(diagnostic_code_t::invalid_id, "Shared SV recipe definition is missing.")};
            recipe = definition->make_placement(recipe.id, recipe.range, recipe.enabled);
        }
    }
    return {};
}

auto validate_sv_recipe_definitions(const song_document_t& song) -> diagnostic_list_t {
    std::set<recipe_id_t> ids;
    std::map<recipe_id_t, std::uint64_t> grids;
    for (const auto& definition : song.sv_recipe_definitions) {
        if (!definition.id || !ids.insert(definition.id).second)
            return {error(diagnostic_code_t::duplicate_id, "Shared SV recipe definition has a zero or duplicate ID.")};
        if (definition != sv_recipe_definition_t::from(definition.id, definition.value))
            return {error(diagnostic_code_t::invalid_id, "Shared SV recipe definition contains placement data.")};
    }
    for (const auto& chart : song.charts) {
        for (const auto& recipe : chart.sv_recipes) {
            if (!recipe.definition) {
                if (song.find_sv_recipe_definition(recipe.id)) return {error(diagnostic_code_t::duplicate_id,
                    "Independent recipe reuses a shared definition identity.")};
                continue;
            }
            const auto* definition = song.find_sv_recipe_definition(recipe.definition);
            if (!definition || definition->make_placement(recipe.id, recipe.range, recipe.enabled) != recipe)
                return {error(diagnostic_code_t::invalid_id, "SV recipe placement does not match its shared definition.")};
            auto diagnostics = validate_definition_grid(chart, recipe, grids);
            if (!diagnostics.empty()) return diagnostics;
        }
    }
    return {};
}
} // namespace wormedit
