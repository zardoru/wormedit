#include <wormedit/core/generator.h>
#include <wormedit/core/bms_program_edit.h>
#include "chart_object_ids.h"
#include "interaction_internal.h"
#include "lane_occupancy.h"
#include "text_util.h"
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <nlohmann/json.hpp>
#include <map>
#include <set>

namespace wormedit {
namespace {
auto old_recipe(const song_document_t& song, recipe_id_t id) -> const generator_recipe_t* {
    const auto found = std::ranges::find(song.generator_recipes, id, &generator_recipe_t::id);
    return found == song.generator_recipes.end() ? nullptr : &*found;
}
auto check_baseline(const chart_document_t& chart, const generator_recipe_t* old) -> diagnostic_list_t {
    if (!old) return {};
    for (const auto& binding : old->generated_notes) {
        const auto* current = chart.find_note(binding.value.id);
        if (!current || *current != binding.value)
            return diagnostic_list_t{error(diagnostic_code_t::invalid_binding, "Generated note was edited or deleted. Detach the recipe before replacing its ownership.")};
    }
    for (const auto& binding : old->generated_layers) {
        const auto current = std::ranges::find(chart.semantic_layers, binding.value.id, &semantic_layer_t::id);
        if (current == chart.semantic_layers.end() || *current != binding.value)
            return diagnostic_list_t{error(diagnostic_code_t::invalid_binding, "Generated layer was edited or deleted. Detach the recipe before replacing its ownership.")};
    }
    return {};
}
template<class Binding, class Value>
auto bind_value(const std::vector<Binding>& old, std::string key, Value value,
    std::uint64_t& next, generator_diff_t& diff) -> Binding {
    const auto found = std::ranges::find(old, key, &Binding::key);
    if (found == old.end()) {
        value.id.value = next++;
        ++diff.added;
    } else {
        value.id = found->value.id;
        if (value == found->value) ++diff.unchanged;
        else ++diff.changed;
    }
    return {.key = std::move(key), .value = std::move(value)};
}
auto bind_layers(generator_preview_t& report, generator_output_t& output,
    const generator_recipe_t& old, std::uint64_t& next) -> diagnostic_list_t {
    std::set<std::string> keys;
    for (auto& layer : output.layers) {
        if (layer.key.empty() || layer.key.size() > 128 || !keys.insert(layer.key).second)
            return diagnostic_list_t{error(diagnostic_code_t::invalid_binding, "Generated layers need unique stable keys of 1 to 128 bytes.")};
        report.recipe.generated_layers.push_back(bind_value(old.generated_layers,
            std::move(layer.key), std::move(layer.value), next, report.layers));
    }
    for (const auto& binding : old.generated_layers)
        if (!keys.contains(binding.key)) ++report.layers.removed;
    return {};
}
auto bind_notes(generator_preview_t& report, generator_output_t& output,
    const generator_recipe_t& old, std::uint64_t& next) -> diagnostic_list_t {
    std::map<std::string, layer_id_t> layers;
    for (const auto& layer : report.recipe.generated_layers) layers.emplace(layer.key, layer.value.id);
    std::set<std::string> keys;
    for (auto& note : output.notes) {
        if (note.key.empty() || note.key.size() > 128 || !keys.insert(note.key).second)
            return diagnostic_list_t{error(diagnostic_code_t::invalid_binding, "Generated notes need unique stable keys of 1 to 128 bytes.")};
        for (const auto& key : note.layers) {
            const auto layer = layers.find(key);
            if (layer == layers.end()) return diagnostic_list_t{error(diagnostic_code_t::invalid_binding, "Generated note refers to an unknown layer key.")};
            note.value.semantic_layers.push_back(layer->second);
        }
        report.recipe.generated_notes.push_back(bind_value(old.generated_notes,
            std::move(note.key), std::move(note.value), next, report.notes));
    }
    for (const auto& binding : old.generated_notes)
        if (!keys.contains(binding.key)) ++report.notes.removed;
    return {};
}
auto check_placement(const chart_document_t& chart, const generator_recipe_t& candidate,
    const generator_recipe_t& old) -> diagnostic_list_t {
    const auto index = measure_index_t::from(chart);
    if (!index) return index.error();
    std::unordered_set<std::uint64_t> excluded;
    for (const auto& note : old.generated_notes) excluded.insert(note.value.id.value);
    auto entries = lane_occupancy_t::from(chart, *index, &excluded).into_entries();
    for (const auto& binding : candidate.generated_notes) {
        const auto& note = binding.value;
        auto interval = lane_occupant_t::from(note, *index);
        if (!interval || !interaction_detail::is_lane_available_across(chart, *index, interval->begin, interval->end, note.lane) ||
            (note.kind != note_kind_t::tap && note.kind != note_kind_t::hold) ||
            (note.kind == note_kind_t::hold) != note.end.has_value() || interval->end < interval->begin ||
            (note.kind == note_kind_t::hold && interval->end == interval->begin))
            return diagnostic_list_t{error(diagnostic_code_t::invalid_binding, "Generated note has an invalid lane, position or hold endpoint.")};
        interval->proposed = true;
        entries.push_back(*interval);
    }
    if (lane_occupancy_t::from(std::move(entries), lane_occupancy_order_t::paste)
        .first_collision(lane_occupancy_pairs_t::proposed))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_binding, "Generated notes collide with one another or existing chart notes.")};
    return {};
}
void replace_layers(chart_document_t& chart, const generator_recipe_t& recipe, const generator_recipe_t& old) {
    std::erase_if(chart.semantic_layers, [&](const auto& layer) {
        const bool owned = std::ranges::any_of(old.generated_layers, [&](const auto& binding) { return binding.value.id == layer.id; });
        const bool retained = std::ranges::any_of(recipe.generated_layers, [&](const auto& binding) { return binding.value.id == layer.id; });
        return owned && !retained;
    });
    for (const auto& layer : recipe.generated_layers) {
        const auto found = std::ranges::find(chart.semantic_layers, layer.value.id, &semantic_layer_t::id);
        if (found == chart.semantic_layers.end()) chart.semantic_layers.push_back(layer.value);
        else *found = layer.value;
    }
}
void replace_notes(chart_document_t& chart, const generator_recipe_t& recipe, const generator_recipe_t& old) {
    for (auto& measure : chart.measures) std::erase_if(measure.notes, [&](const auto& note) {
        return std::ranges::any_of(old.generated_notes, [&](const auto& binding) { return binding.value.id == note.id; });
    });
    for (const auto& note : recipe.generated_notes) {
        const auto measure = std::ranges::find(chart.measures, note.value.position.measure, &measure_chunk_t::id);
        if (measure != chart.measures.end()) measure->notes.push_back(note.value);
    }
}
auto stage_layers(document_command_transaction_t& transaction, const generator_recipe_t& recipe,
    const generator_recipe_t& old) -> bool {
    const auto change = [&](chart_document_t& chart) -> attempt_t<document_edit_effect_t> {
        replace_layers(chart, recipe, old);
        if (!old.generated_layers.empty() || !recipe.generated_layers.empty())
            if (auto errors = validate_semantic_layer_definitions(chart.semantic_layers); !errors.empty())
                return {.diagnostics = std::move(errors)};
        return {.value = document_edit_effect_t{.changed = true, .command_label = "Run Lua generator"}};
    };
    if (transaction.has_bms_program(recipe.chart)) {
        std::vector<layer_id_t> insertion_ids;
        for (const auto& layer : recipe.generated_layers)
            if (std::ranges::find(old.generated_layers, layer.key, &generator_layer_binding_t::key) == old.generated_layers.end())
                insertion_ids.push_back(layer.value.id);
        const auto result = transaction.stage_bms_layer_settings(recipe.chart, change, insertion_ids);
        return result.value.has_value() && result.ok();
    }
    const auto result = transaction.stage(recipe.chart, "Generator chart disappeared.",
        [&](chart_document_t& chart, song_document_t&) { return change(chart); });
    return result.value.has_value() && result.ok();
}
auto stage_scoped_notes(document_command_transaction_t& transaction, const generator_recipe_t& recipe,
    const generator_recipe_t& old) -> bool {
    std::vector<object_id_t> updates, removals, insertion_ids;
    std::vector<bms_native_event_t> additions;
    for (const auto& previous : old.generated_notes) {
        const auto found = std::ranges::find(recipe.generated_notes, previous.key, &generator_note_binding_t::key);
        if (found == recipe.generated_notes.end()) removals.push_back(previous.value.id);
        else updates.push_back(previous.value.id);
    }
    for (const auto& binding : recipe.generated_notes)
        if (std::ranges::find(old.generated_notes, binding.key, &generator_note_binding_t::key) == old.generated_notes.end()) {
            additions.push_back(binding.value);
            insertion_ids.push_back(binding.value.id);
        }
    if (!updates.empty()) {
        auto result = transaction.stage_bms_native_objects(recipe.chart, updates, bms_existing_edit_kind_t::update,
            [&](chart_document_t& chart) -> attempt_t<document_edit_effect_t> {
                for (const auto& binding : recipe.generated_notes)
                    if (auto* note = chart.find_note(binding.value.id)) *note = binding.value;
                return {.value = document_edit_effect_t{.changed = true, .command_label = "Run Lua generator"}};
            });
        if (!result.value || !result.ok()) return false;
    }
    const auto result = transaction.stage_bms_native_events(recipe.chart, removals, additions, "Run Lua generator", insertion_ids);
    return result.value.has_value() && result.ok();
}
auto stage_notes(document_command_transaction_t& transaction, const generator_recipe_t& recipe,
    const generator_recipe_t& old) -> bool {
    if (transaction.has_bms_program(recipe.chart)) return stage_scoped_notes(transaction, recipe, old);
    const auto staged = transaction.stage(recipe.chart, "Generator chart disappeared.",
        [&](chart_document_t& chart, song_document_t&) -> attempt_t<document_edit_effect_t> {
            replace_notes(chart, recipe, old);
            return {.value = document_edit_effect_t{.changed = true, .command_label = "Run Lua generator"}};
        });
    return staged.value.has_value() && staged.ok();
}
auto stage_generator_record(document_command_transaction_t& transaction, generator_recipe_t recipe)
    -> attempt_t<document_edit_effect_t> {
    const auto* target = transaction.read_chart(recipe.chart);
    if (!target) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Generator chart disappeared before materialization.")}};
    for (auto& note : recipe.generated_notes) {
        const auto* actual = target->find_note(note.value.id);
        if (!actual) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Generated identity was not materialized.")}};
        note.value = *actual;
    }
    return transaction.stage([&](song_document_t& document) -> attempt_t<document_edit_effect_t> {
        auto& recipes = document.generator_recipes;
        const auto stored = std::ranges::find(recipes, recipe.id, &generator_recipe_t::id);
        if (stored == recipes.end()) recipes.push_back(std::move(recipe));
        else *stored = std::move(recipe);
        return {.value = document_edit_effect_t{.changed = true, .command_label = "Run Lua generator"}};
    });
}
auto prepare_generator_target(document_command_transaction_t& transaction, generator_recipe_t& recipe,
    bool capture) -> diagnostic_list_t {
    if (!transaction.has_bms_program(recipe.chart)) {
        if (recipe.bms_scope || recipe.bms_chart) return diagnostic_list_t{error(diagnostic_code_t::invalid_binding, "Generator target no longer has its BMS owner.")};
        return {};
    }
    const auto target = transaction.bms_target(recipe.chart);
    if (!target) return {error(diagnostic_code_t::invalid_binding, target.error().message)};
    if (capture) {
        recipe.bms_scope = target->scope;
        recipe.bms_chart = target->chart;
    } else if (recipe.bms_scope != target->scope || recipe.bms_chart != target->chart)
        return {error(diagnostic_code_t::invalid_binding, "Select the saved generator's BMS Chart owner before regenerating.")};
    return {};
}
} // namespace

namespace {
auto validate_generator_inputs(const generator_recipe_t& recipe, bool legacy) -> diagnostic_list_t {
    if (recipe.input_assets.size() > maximum_generator_parameters)
        return {error(diagnostic_code_t::invalid_resolution, "Generator input limit exceeded.")};
    std::set<std::uint64_t> ids;
    for (const auto& input : recipe.input_assets) {
        if (!input.asset || !ids.insert(input.asset.value).second ||
            (!(legacy && input.content_hash.empty()) && !text_util::is_sha256_digest(input.content_hash)))
            return {error(diagnostic_code_t::invalid_id, "Generator inputs need unique identities and SHA256 content hashes.")};
    }
    return {};
}
template<class Binding>
auto validate_generator_bindings(const std::vector<Binding>& bindings) -> diagnostic_list_t {
    std::set<std::string> keys;
    std::set<std::uint64_t> ids;
    for (const auto& binding : bindings) {
        if (!text_util::is_valid_bounded_text(binding.key, 128) || binding.key.empty() ||
            !keys.insert(binding.key).second || !binding.value.id || !ids.insert(binding.value.id.value).second)
            return {error(diagnostic_code_t::invalid_id, "Generator outputs need unique nonzero identities and stable UTF8 keys.")};
    }
    return {};
}
auto validate_generator_parameters(const generator_recipe_t& recipe) -> diagnostic_list_t {
    if (recipe.parameters.size() > maximum_generator_parameters)
        return {error(diagnostic_code_t::invalid_resolution, "Generator parameter limit exceeded.")};
    std::set<std::string> names;
    for (const auto& parameter : recipe.parameters) {
        if (parameter.name.empty() || !text_util::is_valid_bounded_text(parameter.name, 128) ||
            !names.insert(parameter.name).second || !text_util::is_valid_bounded_text(parameter.serialized_value, 16384) ||
            nlohmann::json::parse(parameter.serialized_value, nullptr, false).is_discarded())
            return {error(diagnostic_code_t::invalid_id, "Generator parameters need unique names and bounded JSON values.")};
    }
    return {};
}
}
auto validate_generator_recipe(const generator_recipe_t& recipe) -> diagnostic_list_t {
    const bool legacy = recipe.source.empty() && !recipe.chart && recipe.generated_notes.empty() &&
        recipe.generated_layers.empty() && !recipe.bms_scope && !recipe.bms_chart;
    if (auto errors = validate_generator_inputs(recipe, legacy); !errors.empty()) return errors;
    if (auto errors = validate_generator_parameters(recipe); !errors.empty()) return errors;
    if (legacy) return {};
    if (recipe.api_version != "1" || !recipe.chart || recipe.source.empty() ||
        !text_util::is_valid_bounded_text(recipe.source, maximum_generator_source_bytes) ||
        !text_util::is_sha256_digest(recipe.source_hash) ||
        recipe.bms_scope.has_value() != recipe.bms_chart.has_value() ||
        recipe.generated_notes.size() > maximum_generator_notes || recipe.generated_layers.size() > maximum_generator_layers)
        return {error(diagnostic_code_t::invalid_id, "Generator recipe metadata exceeds API-v1 bounds.")};
    if (auto errors = validate_generator_bindings(recipe.generated_notes); !errors.empty()) return errors;
    if (auto errors = validate_generator_bindings(recipe.generated_layers); !errors.empty()) return errors;
    for (const auto& note : recipe.generated_notes)
        if (note.value.kind != note_kind_t::tap && note.value.kind != note_kind_t::hold)
            return {error(diagnostic_code_t::invalid_id, "Generator historical note kind is invalid.")};
    for (const auto& layer : recipe.generated_layers)
        if (auto errors = validate_semantic_layer_definitions(std::span<const semantic_layer_t>{&layer.value, 1}); !errors.empty())
            return errors;
    if (!recipe.source_path.empty() && !export_asset_directory(recipe.source_path))
        return {error(diagnostic_code_t::invalid_id, "Generator source reference must stay within the project.")};
    return {};
}

namespace {
template<class Value, class Binding>
auto count_new_keys(const std::vector<Value>& values, const std::vector<Binding>& old) -> std::size_t {
    std::set<std::string_view> keys;
    for (const auto& binding : old) keys.insert(binding.key);
    return static_cast<std::size_t>(std::ranges::count_if(values, [&](const auto& value) { return !keys.contains(value.key); }));
}
}
namespace {
template<class Binding>
auto check_binding_identities(const std::vector<Binding>& bindings, const std::vector<Binding>& old,
    std::optional<object_id_t> first, std::set<std::uint64_t>& allocated) -> diagnostic_list_t {
    for (const auto& binding : bindings) {
        const auto previous = std::ranges::find(old, binding.key, &Binding::key);
        if (previous != old.end()) {
            if (previous->value.id != binding.value.id)
                return {error(diagnostic_code_t::invalid_id, "Regeneration must preserve each retained key's identity.")};
        } else if (!binding.value.id || !first || binding.value.id.value < first->value ||
            !allocated.insert(binding.value.id.value).second)
            return {error(diagnostic_code_t::invalid_id, "New generator outputs need unique history-safe identities.")};
    }
    return {};
}
auto check_generator_identities(document_command_transaction_t& transaction,
    const generator_recipe_t& recipe, const generator_recipe_t& old) -> diagnostic_list_t {
    const auto needed = count_new_keys(recipe.generated_notes, old.generated_notes) +
        count_new_keys(recipe.generated_layers, old.generated_layers) + static_cast<std::size_t>(!old.id);
    const auto first = needed ? transaction.next_bms_program_item_id() : std::optional<object_id_t>{};
    std::set<std::uint64_t> allocated;
    if (!old.id) {
        if (!recipe.id || !first || recipe.id.value < first->value)
            return {error(diagnostic_code_t::invalid_id, "New generator recipes need history-safe identities.")};
        allocated.insert(recipe.id.value);
    }
    if (auto errors = check_binding_identities(recipe.generated_notes, old.generated_notes, first, allocated); !errors.empty()) return errors;
    return check_binding_identities(recipe.generated_layers, old.generated_layers, first, allocated);
}
}
auto generator_identity_floor(const document_history_t& history) -> std::uint64_t {
    return id_ceiling_t<object_id_t>::from(history).maximum;
}
auto generator_preview_t::propose(const document_history_t& history, generator_recipe_t recipe,
    generator_output_t output, bool keep, std::uint64_t identity_floor) -> generator_outcome_t {
    const auto* chart = history.read_chart(recipe.chart);
    if (!chart || output.notes.size() > maximum_generator_notes || output.layers.size() > maximum_generator_layers)
        return {.diagnostics = diagnostic_list_t{error(diagnostic_code_t::invalid_binding, "Generator output exceeds its limits or has no target chart.")}};
    const auto* previous = old_recipe(history.document, recipe.id);
    if (recipe.id && !previous)
        return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Generator recipe no longer exists; new runs need an unassigned recipe identity.")}};
    if (previous && previous->chart && (previous->chart != recipe.chart || previous->bms_scope != recipe.bms_scope ||
        previous->bms_chart != recipe.bms_chart))
        return {.diagnostics = {error(diagnostic_code_t::invalid_binding, "Generator recipes cannot be retargeted implicitly.")}};
    if (auto errors = check_baseline(*chart, previous); !errors.empty()) return {.diagnostics = std::move(errors)};
    const auto old = previous ? *previous : generator_recipe_t{};
    generator_preview_t report{.recipe = std::move(recipe), .keep_recipe = keep || previous};
    report.recipe.generated_notes.clear(); report.recipe.generated_layers.clear();
    auto identities = id_ceiling_t<object_id_t>::from(history);
    identities.maximum = std::max(identities.maximum, identity_floor);
    const auto needed = count_new_keys(output.notes, old.generated_notes) + count_new_keys(output.layers, old.generated_layers) +
        static_cast<std::size_t>(!report.recipe.id);
    const auto first = needed ? identities.next(needed) : std::optional{object_id_t{0}};
    if (!first) return {.diagnostics = diagnostic_list_t{error(diagnostic_code_t::invalid_binding, "Generator identities are exhausted.")}};
    auto next = first->value;
    if (auto errors = bind_layers(report, output, old, next); !errors.empty()) return {.diagnostics = std::move(errors)};
    if (auto errors = bind_notes(report, output, old, next); !errors.empty()) return {.diagnostics = std::move(errors)};
    if (!report.recipe.id) report.recipe.id = recipe_id_t{next++};
    if (auto errors = check_placement(*chart, report.recipe, old); !errors.empty()) return {.diagnostics = std::move(errors)};
    generator_proposal_t proposal{.stamp = chart_stamp_t::from(history, chart->id), .payload = std::move(report)};
    const auto checked = document_command_transaction_t::preview(history,
        [&](document_history_t& snapshot) {
            document_command_transaction_t target_scope{snapshot};
            if (auto errors = prepare_generator_target(target_scope, proposal.payload.recipe, !previous || !previous->chart);
                !errors.empty()) return document_command_transaction_t::rejection(snapshot.document.revision, std::move(errors));
            return apply_generator(snapshot, proposal);
        });
    if (!checked.diagnostics.empty()) return {.diagnostics = checked.diagnostics};
    return {.value = std::move(proposal)};
}

auto generator_preview_t::apply(proposal_edit_scope_t<chart_stamp_t>& scope) const
    -> diagnosed_t<std::optional<std::string>> {
    auto& transaction = scope.transaction();
    auto materialization = recipe;
    if (materialization.chart != scope.captured_stamp().chart)
        return {.diagnostics = {error(diagnostic_code_t::invalid_binding, "Generator target does not match its captured chart.")}};
    if (auto errors = prepare_generator_target(transaction, materialization, false); !errors.empty()) return {.diagnostics = std::move(errors)};
    const auto* previous = old_recipe(transaction.source_document(), materialization.id);
    const auto old = previous ? *previous : generator_recipe_t{};
    if (previous && previous->chart && (previous->chart != materialization.chart ||
        previous->bms_scope != materialization.bms_scope || previous->bms_chart != materialization.bms_chart))
        return {.diagnostics = {error(diagnostic_code_t::invalid_binding, "Generator recipe target changed after preview.")}};
    if (auto errors = check_generator_identities(transaction, materialization, old); !errors.empty())
        return {.diagnostics = std::move(errors)};
    const auto* chart = transaction.read_chart(materialization.chart);
    if (!chart) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Generator chart disappeared.")}};
    if (auto errors = check_baseline(*chart, previous); !errors.empty()) return {.diagnostics = std::move(errors)};
    if (auto errors = check_placement(*chart, materialization, old); !errors.empty()) return {.diagnostics = std::move(errors)};
    if (!stage_layers(transaction, materialization, old)) return {.diagnostics = transaction.outcome().diagnostics};
    if (!stage_notes(transaction, materialization, old)) return {.diagnostics = transaction.outcome().diagnostics};
    if (keep_recipe) {
        const auto staged = stage_generator_record(transaction, std::move(materialization));
        if (!staged.value || !staged.ok()) return {.diagnostics = staged.diagnostics};
    }
    return {.value = "Run Lua generator"};
}
auto apply_generator(document_history_t& history, const generator_proposal_t& proposal) -> document_command_result_t {
    return apply_proposal(history, proposal);
}
namespace {
struct detach_generator_edit_t {
    recipe_id_t id;
    auto apply(document_edit_scope_t& scope) const -> diagnosed_t<std::optional<std::string>> {
        auto& transaction = scope.transaction();
        if (!old_recipe(transaction.source_document(), id)) return {};
        const auto staged = transaction.stage([&](song_document_t& document) -> attempt_t<document_edit_effect_t> {
            const auto removed = std::erase_if(document.generator_recipes, [&](const auto& recipe) { return recipe.id == id; });
            return {.value = document_edit_effect_t{.changed = removed != 0, .command_label = "Detach generator recipe"}};
        });
        if (!staged.value || !staged.ok()) return {.diagnostics = staged.diagnostics};
        return {.value = "Detach generator recipe"};
    }
};
}
auto detach_generator(document_history_t& history, recipe_id_t id) -> document_command_result_t {
    document_command_transaction_t transaction{history};
    document_edit_scope_t scope{transaction};
    return apply_proposal_edit(scope, detach_generator_edit_t{id});
}
} // namespace wormedit
