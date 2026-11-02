#include <wormedit/core/generator.h>
#include "text_util.h"
#include <algorithm>
#include <set>

namespace wormedit {
namespace {
auto check_storyboard_witnesses(const song_document_t& song, const generator_recipe_t& old)
    -> diagnostic_list_t {
    for (const auto& binding : old.generated_storyboards) {
        const auto current = std::ranges::find(song.storyboards, binding.storyboard, &storyboard_document_t::id);
        if (current == song.storyboards.end() || current->osbx != binding.value || current->model ||
            !current->retained_source_lines.empty() || current->owning_generator_recipe != old.id)
            return {error(diagnostic_code_t::invalid_binding, "Generated storyboard '" +
                (current == song.storyboards.end() ? binding.key : current->name) +
                "' was edited or deleted. Detach the generator before replacing it.")};
    }
    return {};
}
template<class Binding>
void reserve_binding_ids(std::set<std::uint64_t>& allocated, const std::vector<Binding>& bindings) {
    for (const auto& binding : bindings) allocated.insert(binding.value.id.value);
}
auto check_storyboard_identities(document_command_transaction_t& transaction,
    const generator_recipe_t& recipe, const generator_recipe_t& old) -> diagnostic_list_t {
    const auto floor = transaction.next_bms_program_item_id();
    std::set<std::uint64_t> allocated{recipe.id.value};
    reserve_binding_ids(allocated, recipe.generated_notes);
    reserve_binding_ids(allocated, recipe.generated_layers);
    reserve_binding_ids(allocated, recipe.generated_scrolls);
    reserve_binding_ids(allocated, recipe.generated_sv_recipes);
    for (const auto& binding : recipe.generated_storyboards) {
        const auto previous = std::ranges::find(old.generated_storyboards, binding.key, &generator_storyboard_binding_t::key);
        if (previous != old.generated_storyboards.end()) {
            if (binding.storyboard != previous->storyboard)
                return {error(diagnostic_code_t::invalid_id, "Regeneration must preserve storyboard identities.")};
        } else if (!floor || binding.storyboard.value < floor->value ||
            !allocated.insert(binding.storyboard.value).second)
            return {error(diagnostic_code_t::invalid_id, "New storyboards need unique history-safe identities.")};
    }
    return {};
}
void replace_storyboards(song_document_t& song, const generator_recipe_t& recipe,
    const generator_recipe_t& old, bool keep_recipe) {
    std::erase_if(song.storyboards, [&](const auto& value) {
        const bool owned = std::ranges::any_of(old.generated_storyboards, [&](const auto& binding) {
            return value.id == binding.storyboard;
        });
        const bool retained = std::ranges::any_of(recipe.generated_storyboards, [&](const auto& binding) {
            return value.id == binding.storyboard;
        });
        return owned && !retained;
    });
    for (const auto& binding : recipe.generated_storyboards) {
        auto current = std::ranges::find(song.storyboards, binding.storyboard, &storyboard_document_t::id);
        if (current == song.storyboards.end()) {
            song.storyboards.push_back({.id = binding.storyboard, .name = binding.key});
            current = std::prev(song.storyboards.end());
        }
        current->osbx = binding.value;
        current->owning_generator_recipe = keep_recipe ? std::optional{recipe.id} : std::nullopt;
    }
}
} // namespace

auto validate_generator_storyboards(const generator_recipe_t& recipe) -> diagnostic_list_t {
    if (recipe.generated_storyboards.size() > maximum_generator_storyboards)
        return {error(diagnostic_code_t::invalid_binding, "Generator storyboard limit exceeded.")};
    std::set<std::string> keys;
    std::set<storyboard_id_t> ids;
    for (const auto& binding : recipe.generated_storyboards) {
        if (binding.key.empty() || !text_util::is_valid_bounded_text(binding.key, 128) ||
            !keys.insert(binding.key).second || !binding.storyboard || !ids.insert(binding.storyboard).second)
            return {error(diagnostic_code_t::invalid_binding, "Generated storyboards need unique stable keys and identities.")};
        const auto validation = validate_osbx_document(binding.value);
        if (!validation.value)
            return {error(diagnostic_code_t::invalid_binding, "Generated storyboard '" + binding.key + "' has an invalid scene.")};
    }
    return {};
}

auto generator_preview_t::bind_storyboards(const document_history_t& history,
    std::vector<generator_storyboard_t> output, const generator_recipe_t& old, std::uint64_t& next)
    -> diagnostic_list_t {
    if (output.size() > maximum_generator_storyboards)
        return {error(diagnostic_code_t::invalid_binding, "Generator storyboard limit exceeded.")};
    if (auto errors = check_storyboard_witnesses(history.document, old); !errors.empty()) return errors;
    recipe.generated_storyboards.clear();
    std::set<std::string> keys;
    for (auto& value : output) {
        if (value.key.empty() || !text_util::is_valid_bounded_text(value.key, 128) || !keys.insert(value.key).second)
            return {error(diagnostic_code_t::invalid_binding, "Generated storyboards need unique stable keys.")};
        const auto previous = std::ranges::find(old.generated_storyboards, value.key, &generator_storyboard_binding_t::key);
        const auto id = previous == old.generated_storyboards.end() ? storyboard_id_t{next++} : previous->storyboard;
        if (previous == old.generated_storyboards.end()) ++storyboards.added;
        else if (value.value == previous->value) ++storyboards.unchanged;
        else ++storyboards.changed;
        recipe.generated_storyboards.push_back({.key = std::move(value.key), .storyboard = id, .value = std::move(value.value)});
    }
    for (const auto& previous : old.generated_storyboards)
        if (!keys.contains(previous.key)) ++storyboards.removed;
    return validate_generator_storyboards(recipe);
}

auto generator_preview_t::stage_storyboards(document_command_transaction_t& transaction,
    const generator_recipe_t& old) const -> attempt_t<document_edit_effect_t> {
    if (auto errors = validate_generator_storyboards(recipe); !errors.empty()) return {.diagnostics = std::move(errors)};
    if (auto errors = check_storyboard_witnesses(transaction.source_document(), old); !errors.empty()) return {.diagnostics = std::move(errors)};
    if (auto errors = check_storyboard_identities(transaction, recipe, old); !errors.empty()) return {.diagnostics = std::move(errors)};
    return transaction.stage([&](song_document_t& song) -> attempt_t<document_edit_effect_t> {
        replace_storyboards(song, recipe, old, keep_recipe);
        return {.value = document_edit_effect_t{.changed = !recipe.generated_storyboards.empty() || !old.generated_storyboards.empty(),
            .command_label = "Run Lua generator"}};
    });
}
} // namespace wormedit
