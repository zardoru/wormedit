#include "generator_chart.h"
#include "chart_id_visitor.h"
#include "chart_memberships.h"
#include "chart_position_storage.h"
#include "id_ceiling.h"
#include "lane_occupancy.h"
#include "text_util.h"
#include <map>
#include <set>
namespace wormedit {
auto generator_chart_content_t::from(const chart_document_t& chart) -> generator_chart_content_t {
    return {.name = chart.metadata.name,
            .resolution = chart.divisions_per_standard_measure,
            .offset = chart.beat_zero_audio_offset_seconds,
            .measures = chart.measures,
            .layouts = chart.lane_layouts,
            .input = chart.input_profile,
            .bgm_lanes = chart.bgm_source_lanes,
            .bpms = chart.bpms,
            .pauses = chart.pauses,
            .warps = chart.warps,
            .scrolls = chart.scrolls};
}
void generator_chart_content_t::apply_to(chart_document_t& chart) const {
    chart.metadata.name = name;
    chart.divisions_per_standard_measure = resolution;
    chart.beat_zero_audio_offset_seconds = offset;
    chart.measures = measures;
    chart.lane_layouts = layouts;
    chart.input_profile = input;
    chart.bgm_source_lanes = bgm_lanes;
    chart.bpms = bpms;
    chart.pauses = pauses;
    chart.warps = warps;
    chart.scrolls = scrolls;
}
namespace {
auto failure(std::string message) -> diagnostic_list_t {
    return {error(diagnostic_code_t::invalid_binding, std::move(message))};
}
auto find_chart_binding(const generator_recipe_t& recipe, std::string_view key)
    -> const generator_chart_binding_t* {
    const auto found =
        std::ranges::find(recipe.generated_charts, key, &generator_chart_binding_t::key);
    return found == recipe.generated_charts.end() ? nullptr : &*found;
}
auto check_chart_witnesses(const song_document_t& song, const generator_recipe_t& recipe)
    -> diagnostic_list_t {
    for (const auto& binding : recipe.generated_charts) {
        const auto* current = song.find_chart(binding.chart);
        if (!current || generator_chart_content_t::from(*current) != binding.content)
            return failure("Generated difficulty was edited or deleted. Detach its generator "
                           "before replacing owned measures or timing.");
    }
    return {};
}
struct remap_chart_ids_t {
    const std::map<std::uint64_t, std::uint64_t>& ids;
    bool missing{};
    template <class Id> void operator()(Id& id) {
        if (!id)
            return;
        const auto found = ids.find(id.value);
        if (found == ids.end())
            missing = true;
        else
            id.value = found->second;
    }
};
auto bind_chart_ids(generator_chart_binding_t& candidate, const generator_chart_binding_t* old,
                    std::uint64_t& next) -> diagnostic_list_t {
    std::set<std::string> keys;
    std::map<std::uint64_t, std::uint64_t> remap;
    for (auto& identity : candidate.identities) {
        if (identity.key.empty() || !text_util::is_valid_bounded_text(identity.key, 512) ||
            !keys.insert(identity.key).second || !identity.value)
            return failure("Generated chart identities need unique stable declaration keys.");
        std::uint64_t value{};
        if (old) {
            const auto found =
                std::ranges::find(old->identities, identity.key, &generator_chart_identity_t::key);
            if (found != old->identities.end())
                value = found->value;
        }
        if (!value) {
            if (!next)
                return failure("Generator identities exhausted.");
            value = next++;
        }
        if (!remap.emplace(identity.value, value).second)
            return failure("Duplicate provisional generated identity.");
        identity.value = value;
    }
    chart_document_t chart;
    candidate.content.apply_to(chart);
    remap_chart_ids_t visitor{.ids = remap};
    chart_id_detail::visit_chart_ids(chart, visitor);
    if (visitor.missing)
        return failure("Generated chart has an undeclared object reference.");
    candidate.content = generator_chart_content_t::from(chart);
    return {};
}
auto validate_chart_content(const generator_chart_binding_t& binding) -> diagnostic_list_t {
    chart_document_t chart{.id = binding.chart};
    binding.content.apply_to(chart);
    const auto index = measure_index_t::from(chart);
    if (!index)
        return index.error();
    if (lane_occupancy_t::from(chart, *index).first_collision(lane_occupancy_pairs_t::all))
        return failure("Generated difficulty contains overlapping playable notes or holds.");
    return {};
}
auto check_fresh_identity(std::uint64_t value, std::uint64_t old, std::uint64_t first,
                          std::set<std::uint64_t>& allocated) -> bool {
    if (old)
        return value == old;
    return value >= first && value != 0 && allocated.insert(value).second;
}
auto check_materialization_ids(document_command_transaction_t& transaction,
                               const generator_recipe_t& recipe, const generator_recipe_t& old)
    -> diagnostic_list_t {
    const auto first = transaction.next_bms_program_item_id();
    if (!first && count_new_chart_declarations(recipe.generated_charts, old))
        return failure("Generator identity space exhausted.");
    const auto floor = first ? first->value : UINT64_MAX;
    std::set<std::uint64_t> allocated;
    if (!old.id)
        allocated.insert(recipe.id.value);
    for (const auto& item : recipe.generated_notes)
        allocated.insert(item.value.id.value);
    for (const auto& item : recipe.generated_layers)
        allocated.insert(item.value.id.value);
    for (const auto& chart : recipe.generated_charts) {
        const auto* previous = find_chart_binding(old, chart.key);
        if (!check_fresh_identity(chart.chart.value, previous ? previous->chart.value : 0, floor,
                                  allocated))
            return failure("Generated chart identity is stale or reused.");
        for (const auto& identity : chart.identities) {
            std::uint64_t prior{};
            if (previous) {
                const auto found = std::ranges::find(previous->identities, identity.key,
                                                     &generator_chart_identity_t::key);
                if (found != previous->identities.end())
                    prior = found->value;
            }
            if (!check_fresh_identity(identity.value, prior, floor, allocated))
                return failure("Generated declaration identity is stale or reused.");
        }
    }
    return {};
}
auto can_remove_chart(const chart_document_t& chart) -> bool {
    return chart.semantic_layers.empty() && chart.sound_layer_defaults.empty() &&
           chart.speeds.empty() && chart.sv_recipes.empty() && chart.kiai.empty() &&
           chart.timing_anchors.empty() && chart.bookmarks.empty() && chart.sections.empty() &&
           chart.bga_events.empty() && chart.sound_channels.empty() &&
           chart.metadata.author.empty() && !chart.metadata.rating &&
           chart.metadata.format_groups.empty() && !chart.metadata.preview_override &&
           chart.metadata.overrides == metadata_overrides_t{} &&
           chart.timing_links == timing_links_t{};
}
auto has_authored_positions(const chart_document_t& chart) -> bool {
    auto authored = chart;
    generator_chart_content_t{}.apply_to(authored);
    bool positioned = false;
    for_each_chart_position(
        authored, [&](const local_position_t&, measure_chart_end_policy_t) { positioned = true; });
    return positioned;
}
auto check_chart_grid(const chart_document_t& chart, const generator_chart_content_t& content)
    -> diagnostic_list_t {
    if (chart.divisions_per_standard_measure != content.resolution && has_authored_positions(chart))
        return failure("Generated difficulty resolution cannot change while authored positions "
                       "remain. Detach the generator or remove that content first.");
    return {};
}
auto replace_charts(song_document_t& song, const generator_recipe_t& recipe,
                    const generator_recipe_t& old) -> attempt_t<document_edit_effect_t> {
    for (const auto& previous : old.generated_charts) {
        if (find_chart_binding(recipe, previous.key))
            continue;
        const auto* current = song.find_chart(previous.chart);
        if (!current || !can_remove_chart(*current))
            return {.diagnostics =
                        failure("An omitted generated difficulty has additional authored content. "
                                "Detach or remove that content first.")};
        remove_memberships(song, previous.chart);
        std::erase(song.chart_order, previous.chart);
        std::erase_if(song.charts, [&](const auto& chart) { return chart.id == previous.chart; });
    }
    for (const auto& binding : recipe.generated_charts) {
        auto* current = song.find_chart(binding.chart);
        if (current) {
            if (auto errors = check_chart_grid(*current, binding.content); !errors.empty())
                return {.diagnostics = std::move(errors)};
        }
        if (!current) {
            if (!song.prepared_bms_programs.empty())
                song.ensure_chart_order();
            if (!song.chart_order.empty())
                song.chart_order.push_back(binding.chart);
            song.charts.push_back({.id = binding.chart});
            current = &song.charts.back();
        }
        binding.content.apply_to(*current);
    }
    return {.value = document_edit_effect_t{.changed = true, .command_label = "Run Lua generator"}};
}
} // namespace
auto count_new_chart_declarations(std::span<const generator_chart_binding_t> values,
                                  const generator_recipe_t& old) -> std::size_t {
    std::size_t count{};
    for (const auto& chart : values) {
        const auto* previous = find_chart_binding(old, chart.key);
        if (!previous)
            ++count;
        for (const auto& identity : chart.identities)
            if (!previous ||
                std::ranges::find(previous->identities, identity.key,
                                  &generator_chart_identity_t::key) == previous->identities.end())
                ++count;
    }
    return count;
}
auto validate_generator_charts(const generator_recipe_t& recipe) -> diagnostic_list_t {
    if (recipe.generated_charts.size() > maximum_generator_charts)
        return failure("Too many historical generated difficulties.");
    std::set<std::string> keys;
    std::set<std::uint64_t> ids;
    for (const auto& binding : recipe.generated_charts) {
        if (!binding.chart || binding.chart == recipe.chart ||
            !ids.insert(binding.chart.value).second || binding.key.empty() ||
            !text_util::is_valid_bounded_text(binding.key, 128) || !keys.insert(binding.key).second)
            return failure(
                "Historical generated difficulties need unique keys and chart identities.");
        if (binding.content.measures.empty() ||
            binding.content.measures.size() > maximum_generator_measures ||
            binding.identities.size() > maximum_generator_notes + maximum_generator_measures + 64)
            return failure("Historical generated chart exceeds its declaration bounds.");
        std::set<std::string> declaration_keys;
        for (const auto& identity : binding.identities) {
            if (identity.key.empty() || !text_util::is_valid_bounded_text(identity.key, 512) ||
                !declaration_keys.insert(identity.key).second || !identity.value ||
                !ids.insert(identity.value).second)
                return failure(
                    "Historical generated declarations need unique stable keys and identities.");
        }
    }
    return {};
}
auto generator_preview_t::bind_charts(const document_history_t& history,
                                      std::vector<generator_chart_binding_t> output,
                                      const generator_recipe_t& old, std::uint64_t& next)
    -> diagnostic_list_t {
    if (output.size() > maximum_generator_charts)
        return failure("Too many generated difficulties.");
    if (auto errors = check_chart_witnesses(history.document, old); !errors.empty())
        return errors;
    std::set<std::string> keys;
    recipe.generated_charts.clear();
    for (auto& chart : output) {
        if (chart.key.empty() || !text_util::is_valid_bounded_text(chart.key, 128) ||
            !keys.insert(chart.key).second)
            return failure("Generated difficulties need unique stable keys.");
        const auto* previous = find_chart_binding(old, chart.key);
        chart.chart = previous ? previous->chart : chart_id_t{next++};
        if (!chart.chart)
            return failure("Chart identity space exhausted.");
        if (auto errors = bind_chart_ids(chart, previous, next); !errors.empty())
            return errors;
        // Resource bindings are validated against the full song during preview/application.
        if (auto errors = validate_chart_content(chart); !errors.empty())
            return errors;
        if (!previous)
            ++charts.added;
        else if (chart == *previous)
            ++charts.unchanged;
        else
            ++charts.changed;
        recipe.generated_charts.push_back(std::move(chart));
    }
    for (const auto& previous : old.generated_charts)
        if (!keys.contains(previous.key))
            ++charts.removed;
    return {};
}
auto generator_preview_t::stage_charts(document_command_transaction_t& transaction,
                                       const generator_recipe_t& old) const
    -> attempt_t<document_edit_effect_t> {
    if (auto errors = check_chart_witnesses(transaction.source_document(), old); !errors.empty())
        return {.diagnostics = std::move(errors)};
    if (auto errors = check_materialization_ids(transaction, recipe, old); !errors.empty())
        return {.diagnostics = std::move(errors)};
    return transaction.stage(
        [&](song_document_t& song) { return replace_charts(song, recipe, old); });
}
} // namespace wormedit
