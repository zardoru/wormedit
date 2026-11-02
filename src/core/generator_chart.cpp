#include "generator_chart.h"
#include "chart_id_visitor.h"
#include "chart_memberships.h"
#include "chart_position_storage.h"
#include "id_ceiling.h"
#include "lane_occupancy.h"
#include "text_util.h"
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/bms_program_edit.h>
#include <map>
#include <set>
#include <type_traits>
namespace wormedit {
namespace {

template<class Event> auto matches_audio_receipt(const Event& event, asset_id_t asset,
    const audio_file_reference_t& receipt) -> bool {
    if (!event.sample_reference || event.sound != event.sample_reference->bound_sound ||
        event.sample_reference->bound_asset != asset) return false;
    const auto* existing = std::get_if<audio_file_reference_t>(&event.sample_reference->reference);
    return existing && existing->path == receipt.path && existing->content_hash == receipt.content_hash &&
        existing->stream == receipt.stream;
}
template<class Measures> auto measures_have_audio_receipt(const Measures& measures, asset_id_t asset,
    const audio_file_reference_t& receipt) -> bool {
    for (const auto& measure : measures) {
        for (const auto& note : measure.notes) if (matches_audio_receipt(note, asset, receipt)) return true;
        for (const auto& event : measure.bgm_events) if (matches_audio_receipt(event, asset, receipt)) return true;
    }
    return false;
}
auto has_audio_receipt(const song_document_t& song, const generator_recipe_t& recipe,
    asset_id_t asset, const audio_file_reference_t& receipt) -> bool {
    for (const auto& chart : song.charts)
        if (measures_have_audio_receipt(chart.measures, asset, receipt)) return true;
    for (const auto& chart : recipe.generated_charts)
        if (measures_have_audio_receipt(chart.content.measures, asset, receipt)) return true;
    return false;
}

auto intern_sample_audio(song_document_t& song, const std::filesystem::path& path,
    const audio_stream_info_t& stream, const generator_recipe_t& recipe,
    const audio_file_reference_t* receipt = nullptr) -> result_t<resource_id_t, error_t<failure_code_t>> {
    auto asset = std::ranges::find_if(song.audio_assets, [&](const auto& value) {
        return value.source_path.lexically_normal() == path.lexically_normal() && value.stream == stream &&
            (!receipt || (receipt->asset ? value.id == *receipt->asset : has_audio_receipt(song, recipe, value.id, *receipt)));
    });
    asset_id_t id;
    if (asset != song.audio_assets.end()) id = asset->id;
    else {
        const auto next = next_resource_asset_id(song);
        if (!next) return std::unexpected(error_t{failure_code_t::capacity_exceeded, "Audio asset IDs exhausted"});
        id = *next;
        song.audio_assets.push_back({.id = id, .source_path = path, .stream = stream});
    }
    const auto existing = std::ranges::find_if(song.resources, [&](const auto& value) {
        return value.asset == id && !value.missing && value.has_role(resource_role_t::sample);
    });
    if (existing != song.resources.end()) return existing->id;
    const auto next = id_ceiling_t<resource_id_t>::from(song, 100'000).next();
    if (!next) return std::unexpected(error_t{failure_code_t::capacity_exceeded, "Audio resource IDs exhausted"});
    song.resources.push_back({.id = *next, .name = text_util::path_utf8(path), .asset = id,
        .roles = {resource_role_t::sample, resource_role_t::bgm}});
    return *next;
}

auto materialize_bms_sample(song_document_t& song, const bms_wav_reference_t& reference,
    const generator_recipe_t& recipe) -> result_t<resource_id_t, error_t<failure_code_t>> {
    const auto source = resolve_bms_sample_reference(song, reference);
    if (!source) return std::unexpected(source.error());
    const auto resource = std::ranges::find(song.resources, source->first.id, &resource_entry_t::id);
    if (resource == song.resources.end())
        return intern_sample_audio(song, source->second.source_path, source->second.stream, recipe);
    const auto* asset = song.find_audio_asset(resource->asset);
    if (!asset || resource->asset != source->second.id ||
        asset->source_path.lexically_normal() != source->second.source_path.lexically_normal() ||
        asset->stream != source->second.stream)
        return std::unexpected(error_t{failure_code_t::conflict, "Scoped WAV resource ID collides with a different catalog asset"});
    return resource->id;
}
auto materialize_audio_file(song_document_t& song, const audio_file_reference_t& reference,
    const generator_recipe_t& recipe) -> result_t<resource_id_t, error_t<failure_code_t>> {
    if (!reference.asset)
        return intern_sample_audio(song, text_util::path_from_utf8(reference.path), reference.stream, recipe, &reference);
    const auto* asset = song.find_audio_asset(*reference.asset);
    if (!asset || asset->stream != reference.stream)
        return std::unexpected(error_t{failure_code_t::conflict, "Declared audio asset changed after discovery"});
    return intern_sample_audio(song, asset->source_path, asset->stream, recipe, &reference);
}
template<class Event> auto materialize_sample(song_document_t& song, Event& event, const generator_recipe_t& recipe) -> diagnostic_list_t {
    if (!event.sample_reference) return {};
    auto& binding = *event.sample_reference;
    if (!binding.is_valid()) return {error(diagnostic_code_t::invalid_binding, "Invalid sample reference receipt")};
    const auto sound = std::visit([&](const auto& reference) {
        if constexpr (std::is_same_v<std::decay_t<decltype(reference)>, bms_wav_reference_t>)
            return materialize_bms_sample(song, reference, recipe);
        else return materialize_audio_file(song, reference, recipe);
    }, binding.reference);
    if (!sound) return {error(diagnostic_code_t::invalid_binding, sound.error().message)};
    const auto resource = std::ranges::find(song.resources, *sound, &resource_entry_t::id);
    event.sound = *sound;
    binding.bound_sound = *sound;
    binding.bound_asset = resource->asset;
    if (auto* audio = std::get_if<audio_file_reference_t>(&binding.reference)) audio->asset = resource->asset;
    return {};
}
} // namespace

namespace {
auto materialize_chart_samples(song_document_t& song, generator_chart_binding_t& chart, const generator_recipe_t& recipe) -> diagnostic_list_t {
    for (auto& measure : chart.content.measures) {
        for (auto& note : measure.notes)
            if (auto errors = materialize_sample(song, note, recipe); !errors.empty()) return errors;
        for (auto& event : measure.bgm_events)
            if (auto errors = materialize_sample(song, event, recipe); !errors.empty()) return errors;
    }
    return {};
}
}
auto materialize_generator_sample_references(document_command_transaction_t& transaction,
    generator_recipe_t& recipe) -> diagnostic_list_t {
    const auto result = transaction.stage([&](song_document_t& song) -> attempt_t<document_edit_effect_t> {
        const auto asset_count = song.audio_assets.size();
        const auto resource_count = song.resources.size();
        for (auto& chart : recipe.generated_charts)
            if (auto errors = materialize_chart_samples(song, chart, recipe); !errors.empty()) return {.diagnostics = std::move(errors)};
        return {.value = document_edit_effect_t{.changed = song.audio_assets.size() != asset_count || song.resources.size() != resource_count,
            .command_label = "Materialize generated samples"}};
    });
    return result.diagnostics;
}

auto generator_chart_content_t::from(const chart_document_t& chart, bool owns_sv) -> generator_chart_content_t {
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
            .scrolls = chart.scrolls,
            .sv_recipes = owns_sv ? std::optional{chart.sv_recipes} : std::nullopt};
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
    if (sv_recipes) chart.sv_recipes = *sv_recipes;
}
namespace {
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
        if (!current || generator_chart_content_t::from(*current, binding.content.sv_recipes.has_value()) != binding.content)
            return {error(diagnostic_code_t::invalid_binding,
                          "Generated difficulty was edited or deleted. Detach its generator "
                          "before replacing owned measures or timing.")};
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
            return {error(diagnostic_code_t::invalid_binding,
                          "Generated chart identities need unique stable declaration keys.")};
        std::uint64_t value{};
        if (old) {
            const auto found =
                std::ranges::find(old->identities, identity.key, &generator_chart_identity_t::key);
            if (found != old->identities.end())
                value = found->value;
        }
        if (!value) {
            if (!next)
                return {
                    error(diagnostic_code_t::invalid_binding, "Generator identities exhausted.")};
            value = next++;
        }
        if (!remap.emplace(identity.value, value).second)
            return {error(diagnostic_code_t::invalid_binding,
                          "Duplicate provisional generated identity.")};
        identity.value = value;
    }
    chart_document_t chart;
    candidate.content.apply_to(chart);
    remap_chart_ids_t visitor{.ids = remap};
    chart_id_detail::visit_chart_ids(chart, visitor);
    if (visitor.missing)
        return {error(diagnostic_code_t::invalid_binding,
                      "Generated chart has an undeclared object reference.")};
    candidate.content = generator_chart_content_t::from(chart, candidate.content.sv_recipes.has_value());
    return {};
}
auto validate_chart_content(const generator_chart_binding_t& binding) -> diagnostic_list_t {
    chart_document_t chart{.id = binding.chart};
    binding.content.apply_to(chart);
    const auto index = measure_index_t::from(chart);
    if (!index)
        return index.error();
    if (lane_occupancy_t::from(chart, *index).first_collision(lane_occupancy_pairs_t::all))
        return {error(diagnostic_code_t::invalid_binding,
                      "Generated difficulty contains overlapping playable notes or holds.")};
    return {};
}
auto check_recipe_ownership(const song_document_t& song, const generator_chart_binding_t& candidate,
    const generator_chart_binding_t* old) -> diagnostic_list_t {
    if (!old) return {};
    if (old->content.sv_recipes && !candidate.content.sv_recipes)
        return {error(diagnostic_code_t::invalid_binding, "Generated difficulty cannot silently discard SV recipe ownership.")};
    if (old->content.sv_recipes || !candidate.content.sv_recipes) return {};
    const auto* chart = song.find_chart(old->chart);
    if (chart && !chart->sv_recipes.empty())
        return {error(diagnostic_code_t::invalid_binding,
            "Generated spans would replace independently authored SV recipes. Detach the generator first.")};
    return {};
}
auto prepare_recipe_ownership(const song_document_t& song, generator_chart_binding_t& candidate,
    const generator_chart_binding_t* old) -> diagnostic_list_t {
    // Once acquired, an omitted span list means removing the previously owned spans.
    if (old && old->content.sv_recipes && !candidate.content.sv_recipes)
        candidate.content.sv_recipes.emplace();
    return check_recipe_ownership(song, candidate, old);
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
        return {error(diagnostic_code_t::invalid_binding, "Generator identity space exhausted.")};
    const auto floor = first ? first->value : UINT64_MAX;
    std::set<std::uint64_t> allocated;
    if (!old.id)
        allocated.insert(recipe.id.value);
    for (const auto& item : recipe.generated_notes)
        allocated.insert(item.value.id.value);
    for (const auto& item : recipe.generated_layers)
        allocated.insert(item.value.id.value);
    for (const auto& item : recipe.generated_scrolls) allocated.insert(item.value.id.value);
    for (const auto& item : recipe.generated_sv_recipes) allocated.insert(item.value.id.value);
    for (const auto& chart : recipe.generated_charts) {
        const auto* previous = find_chart_binding(old, chart.key);
        if (!check_fresh_identity(chart.chart.value, previous ? previous->chart.value : 0, floor,
                                  allocated))
            return {error(diagnostic_code_t::invalid_binding,
                          "Generated chart identity is stale or reused.")};
        for (const auto& identity : chart.identities) {
            std::uint64_t prior{};
            if (previous) {
                const auto found = std::ranges::find(previous->identities, identity.key,
                                                     &generator_chart_identity_t::key);
                if (found != previous->identities.end())
                    prior = found->value;
            }
            if (!check_fresh_identity(identity.value, prior, floor, allocated))
                return {error(diagnostic_code_t::invalid_binding,
                              "Generated declaration identity is stale or reused.")};
        }
    }
    return {};
}
auto can_remove_chart(const chart_document_t& chart, bool owns_sv) -> bool {
    return chart.semantic_layers.empty() && chart.sound_layer_defaults.empty() &&
           chart.volumes.empty() && chart.sample_sets.empty() &&
           chart.speeds.empty() && (owns_sv || chart.sv_recipes.empty()) && chart.kiai.empty() &&
           chart.timing_anchors.empty() && chart.bookmarks.empty() && chart.sections.empty() &&
           chart.bga_events.empty() && chart.sound_channels.empty() &&
           chart.metadata.author.empty() && !chart.metadata.rating &&
           chart.metadata.format_groups.empty() && !chart.metadata.preview_override &&
           chart.metadata.overrides == metadata_overrides_t{} &&
           chart.timing_links == timing_links_t{};
}
auto has_authored_positions(const chart_document_t& chart, bool owns_sv) -> bool {
    auto authored = chart;
    generator_chart_content_t{}.apply_to(authored);
    if (owns_sv) authored.sv_recipes.clear();
    bool positioned = false;
    for_each_chart_position(
        authored, [&](const local_position_t&, measure_chart_end_policy_t) { positioned = true; });
    return positioned;
}
auto check_chart_grid(const chart_document_t& chart, const generator_chart_content_t& content)
    -> diagnostic_list_t {
    if (chart.divisions_per_standard_measure != content.resolution && has_authored_positions(chart, content.sv_recipes.has_value()))
        return {error(diagnostic_code_t::invalid_binding,
                      "Generated difficulty resolution cannot change while authored positions "
                      "remain. Detach the generator or remove that content first.")};
    return {};
}
auto replace_charts(song_document_t& song, const generator_recipe_t& recipe,
                    const generator_recipe_t& old) -> attempt_t<document_edit_effect_t> {
    for (const auto& previous : old.generated_charts) {
        if (find_chart_binding(recipe, previous.key))
            continue;
        const auto* current = song.find_chart(previous.chart);
        if (!current || !can_remove_chart(*current, previous.content.sv_recipes.has_value()))
            return {.diagnostics = {
                        error(diagnostic_code_t::invalid_binding,
                              "An omitted generated difficulty has additional authored content. "
                              "Detach or remove that content first.")}};
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
        return {error(diagnostic_code_t::invalid_binding,
                      "Too many historical generated difficulties.")};
    std::set<std::string> keys;
    std::set<std::uint64_t> ids;
    for (const auto& binding : recipe.generated_charts) {
        if (!binding.chart || binding.chart == recipe.chart ||
            !ids.insert(binding.chart.value).second || binding.key.empty() ||
            !text_util::is_valid_bounded_text(binding.key, 128) || !keys.insert(binding.key).second)
            return {
                error(diagnostic_code_t::invalid_binding,
                      "Historical generated difficulties need unique keys and chart identities.")};
        if (binding.content.measures.empty() ||
            binding.content.measures.size() > maximum_generator_measures ||
            binding.identities.size() > maximum_generator_notes + maximum_generator_measures + 64)
            return {error(diagnostic_code_t::invalid_binding,
                          "Historical generated chart exceeds its declaration bounds.")};
        std::set<std::string> declaration_keys;
        for (const auto& identity : binding.identities) {
            if (identity.key.empty() || !text_util::is_valid_bounded_text(identity.key, 512) ||
                !declaration_keys.insert(identity.key).second || !identity.value ||
                !ids.insert(identity.value).second)
                return {error(
                    diagnostic_code_t::invalid_binding,
                    "Historical generated declarations need unique stable keys and identities.")};
        }
    }
    return {};
}
auto generator_preview_t::bind_charts(const document_history_t& history,
                                      std::vector<generator_chart_binding_t> output,
                                      const generator_recipe_t& old, std::uint64_t& next)
    -> diagnostic_list_t {
    if (output.size() > maximum_generator_charts)
        return {error(diagnostic_code_t::invalid_binding, "Too many generated difficulties.")};
    if (auto errors = check_chart_witnesses(history.document, old); !errors.empty())
        return errors;
    std::set<std::string> keys;
    recipe.generated_charts.clear();
    for (auto& chart : output) {
        if (chart.key.empty() || !text_util::is_valid_bounded_text(chart.key, 128) ||
            !keys.insert(chart.key).second)
            return {error(diagnostic_code_t::invalid_binding,
                          "Generated difficulties need unique stable keys.")};
        const auto* previous = find_chart_binding(old, chart.key);
        if (auto errors = prepare_recipe_ownership(history.document, chart, previous); !errors.empty()) return errors;
        chart.chart = previous ? previous->chart : chart_id_t{next++};
        if (!chart.chart)
            return {error(diagnostic_code_t::invalid_binding, "Chart identity space exhausted.")};
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
    for (const auto& binding : recipe.generated_charts)
        if (auto errors = check_recipe_ownership(transaction.source_document(), binding, find_chart_binding(old, binding.key)); !errors.empty())
            return {.diagnostics = std::move(errors)};
    if (auto errors = check_materialization_ids(transaction, recipe, old); !errors.empty())
        return {.diagnostics = std::move(errors)};
    return transaction.stage(
        [&](song_document_t& song) { return replace_charts(song, recipe, old); });
}
} // namespace wormedit
