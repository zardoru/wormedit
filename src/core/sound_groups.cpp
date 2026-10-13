#include "text_util.h"
#include <wormedit/core/sound_groups.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/natural_order.h>

#include <algorithm>
#include <ranges>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace wormedit {
namespace {


auto trim(std::string text) -> std::string {
    return std::string(text_util::trim(text, text_util::ascii_whitespace));
}



auto find_group(song_document_t& song, sound_group_id_t id) {
    return std::ranges::find(song.sound_groups, id, &sound_group_t::id);
}

auto is_sound(const resource_entry_t& resource) -> bool {
    return std::ranges::any_of(resource.roles, [](const auto role) {
        return role == resource_role_t::sample || role == resource_role_t::bgm ||
               role == resource_role_t::music || role == resource_role_t::preview;
    });
}

auto normalize_name(song_document_t& song, sound_group_id_t except, std::string& name,
                    document_command_result_t& result) -> bool {
    name = trim(std::move(name));
    if (name.empty() || !text_util::is_valid_bounded_text(name, maximum_sound_group_name_bytes)) {
        reject(result, diagnostic_code_t::invalid_id, "sound group name must contain 1 to 64 bytes");
        return false;
    }
    for (const auto& group : song.sound_groups) {
        if (group.id != except && text_util::ascii_iequal(group.name, name)) {
            reject(result, diagnostic_code_t::invalid_id, "sound group name is already in use");
            return false;
        }
    }
    return true;
}

auto selected_resources(song_document_t& song, const std::vector<resource_id_t>& ids,
                        document_command_result_t& result)
    -> std::vector<resource_entry_t*> {
    if (ids.empty()) {
        reject(result, diagnostic_code_t::invalid_id, "sound group edit needs at least one sound");
        return {};
    }
    std::unordered_set<std::uint64_t> seen;
    std::vector<resource_entry_t*> selected;
    selected.reserve(ids.size());
    for (const auto id : ids) {
        if (!id || !seen.insert(id.value).second) {
            reject(result, diagnostic_code_t::invalid_id, "sound group edit contains an invalid or repeated sound");
            return {};
        }
        const auto found = std::ranges::find(song.resources, id, &resource_entry_t::id);
        if (found == song.resources.end() || !is_sound(*found)) {
            reject(result, diagnostic_code_t::invalid_id, "sound group edit refers to an unknown sound");
            return {};
        }
        selected.push_back(&*found);
    }
    return selected;
}

auto remove_empty_groups(song_document_t& song) -> void {
    std::erase_if(song.sound_groups, [&song](const auto& group) {
        return std::ranges::none_of(song.resources, [&group](const auto& resource) {
            return resource.sound_group == group.id;
        });
    });
}

auto create_group(song_document_t& song, const create_sound_group_action_t& action,
                  document_command_result_t& result) -> std::string {
    if (!action.group || find_group(song, action.group) != song.sound_groups.end()) {
        reject(result, diagnostic_code_t::invalid_id, "sound group ID is invalid or already in use");
        return {};
    }
    auto name = action.name;
    if (!normalize_name(song, {}, name, result)) return {};
    const auto selected = selected_resources(song, action.sounds, result);
    if (!result.diagnostics.empty()) return {};
    song.sound_groups.push_back({.id = action.group, .name = std::move(name)});
    for (auto* resource : selected) resource->sound_group = action.group;
    remove_empty_groups(song);
    return "Create sound group";
}

auto edit_group_action(song_document_t& song, const add_sounds_to_group_action_t& action,
                       document_command_result_t& result) -> std::string {
    const auto group = find_group(song, action.group);
    if (!action.group || group == song.sound_groups.end()) {
        reject(result, diagnostic_code_t::invalid_id, "sound group edit refers to an unknown group");
        return {};
    }
    const auto selected = selected_resources(song, action.sounds, result);
    if (!result.diagnostics.empty()) return {};
    for (auto* resource : selected) resource->sound_group = action.group;
    remove_empty_groups(song);
    return "Add sounds to group";
}

auto edit_group_action(song_document_t& song, const ungroup_sounds_action_t& action,
                       document_command_result_t& result) -> std::string {
    const auto selected = selected_resources(song, action.sounds, result);
    if (!result.diagnostics.empty()) return {};
    for (auto* resource : selected) resource->sound_group.reset();
    remove_empty_groups(song);
    return "Ungroup sounds";
}

auto edit_group_action(song_document_t& song, const rename_sound_group_action_t& action,
                       document_command_result_t& result) -> std::string {
    const auto group = find_group(song, action.group);
    if (!action.group || group == song.sound_groups.end()) {
        reject(result, diagnostic_code_t::invalid_id, "sound group edit refers to an unknown group");
        return {};
    }
    auto name = action.name;
    if (!normalize_name(song, action.group, name, result)) return {};
    if (group->name == name) return {};
    group->name = std::move(name);
    return "Rename sound group";
}

auto edit_group_action(song_document_t& song, const delete_sound_group_action_t& action,
                       document_command_result_t& result) -> std::string {
    const auto group = find_group(song, action.group);
    if (!action.group || group == song.sound_groups.end()) {
        reject(result, diagnostic_code_t::invalid_id, "sound group edit refers to an unknown group");
        return {};
    }
    for (auto& resource : song.resources) {
        if (resource.sound_group == action.group) resource.sound_group.reset();
    }
    song.sound_groups.erase(group);
    return "Delete sound group";
}

auto edit_group_action(song_document_t& song, const create_sound_group_action_t& action,
                       document_command_result_t& result) -> std::string {
    return create_group(song, action, result);
}

auto edit_existing_group(song_document_t& song, const sound_group_edit_action_t& action,
                         document_command_result_t& result) -> std::string {
    return std::visit([&](const auto& edit) { return edit_group_action(song, edit, result); }, action);
}

auto sound_group_for_edit(const song_document_t&,
                          const sound_group_edit_action_t& action)
    -> std::optional<sound_group_id_t> {
    if (const auto* create = std::get_if<create_sound_group_action_t>(&action)) return create->group;
    if (const auto* add = std::get_if<add_sounds_to_group_action_t>(&action)) return add->group;
    return std::nullopt;
}

auto is_wav_resource(const resource_entry_t& resource) -> bool {
    return std::ranges::any_of(resource.target_identities, [](const auto& identity) {
        return identity.family == format_family_t::bms && identity.namespace_name == "WAV";
    });
}

auto renumber_edited_groups(song_document_t& song,
                            const std::vector<sound_group_id_t>& groups,
                            document_command_result_t& result) -> bool {
    auto allocation = song;
    for (auto& resource : allocation.resources) {
        if (!resource.sound_group ||
            std::ranges::find(groups, *resource.sound_group) == groups.end() || !is_wav_resource(resource))
            continue;
        std::erase_if(resource.target_identities, [](const auto& identity) {
            return identity.family == format_family_t::bms && identity.namespace_name == "WAV";
        });
    }
    for (const auto group : groups) {
        auto members = std::vector<const resource_entry_t*>{};
        for (const auto& resource : song.resources)
            if (resource.sound_group == group && is_wav_resource(resource)) members.push_back(&resource);
        if (members.empty()) continue;
        std::ranges::sort(members, [](const auto* left, const auto* right) {
            if (natural_name_less(left->name, right->name)) return true;
            if (natural_name_less(right->name, left->name)) return false;
            return left->id.value < right->id.value;
        });
        const auto run = find_bms_identifier_run(allocation, "WAV", members.size());
        if (!run) {
            reject(result, diagnostic_code_t::invalid_id, "BMS WAV namespace has no contiguous run for the sound groups");
            return false;
        }
        for (std::size_t index = 0; index < members.size(); ++index) {
            auto item = std::ranges::find(allocation.resources, members[index]->id,
                                          &resource_entry_t::id);
            item->target_identities.push_back({format_family_t::bms, (*run)[index], "WAV"});
        }
    }
    song = std::move(allocation);
    return true;
}

auto wav_group_member_count(const song_document_t& song, sound_group_id_t group)
    -> std::size_t {
    return static_cast<std::size_t>(std::ranges::count_if(song.resources, [&](const auto& resource) {
        return resource.sound_group == group && is_wav_resource(resource);
    }));
}

auto action_sound_ids(const sound_group_edit_action_t& action)
    -> std::span<const resource_id_t> {
    return std::visit([](const auto& edit) -> std::span<const resource_id_t> {
        using action_t = std::decay_t<decltype(edit)>;
        if constexpr (std::is_same_v<action_t, create_sound_group_action_t> ||
                      std::is_same_v<action_t, add_sounds_to_group_action_t>) return edit.sounds;
        else if constexpr (std::is_same_v<action_t, ungroup_sounds_action_t>) return edit.sounds;
        else return {};
    }, action);
}

auto target_identity_equal(const target_resource_identity_t& left,
                 const target_resource_identity_t& right) -> bool {
    return left == right;
}

auto target_vectors_equal(const std::vector<resource_entry_t>& left,
                         const std::vector<resource_entry_t>& right) -> bool {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        if (left[index].id != right[index].id ||
            !std::ranges::equal(left[index].target_identities,
                                right[index].target_identities, target_identity_equal)) return false;
    return true;
}

} // namespace

auto apply_sound_group_edit(document_history_t& history, std::uint64_t expected_revision,
                            const sound_group_edit_action_t& action,
                            bool keep_bms_ids_together)
    -> document_command_result_t {
    return apply_sound_group_edits(history, expected_revision, {action}, keep_bms_ids_together);
}

auto apply_sound_group_edits(document_history_t& history, std::uint64_t expected_revision,
                             const std::vector<sound_group_edit_action_t>& actions,
                             bool keep_bms_ids_together) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id, "sound group edit document revision is stale");
        return result;
    }
    if (actions.empty()) {
        reject(result, diagnostic_code_t::invalid_id, "sound group edit needs at least one change");
        return result;
    }
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    auto labels = std::vector<std::string>{};
    auto groups = std::vector<sound_group_id_t>{};
    auto edited_ids = std::unordered_set<std::uint64_t>{};
    for (const auto& action : actions) {
        for (const auto id : action_sound_ids(action))
            if (!edited_ids.insert(id.value).second) {
                reject(result, diagnostic_code_t::invalid_id, "sound group batch repeats a sound");
                return result;
            }
        const auto label = edit_existing_group(proposed, action, result);
        if (!result.diagnostics.empty()) return result;
        if (!label.empty()) labels.push_back(label);
        if (const auto group = sound_group_for_edit(proposed, action))
            if (std::ranges::find(groups, *group) == groups.end()) groups.push_back(*group);
    }
    if (labels.empty()) return result;
    if (keep_bms_ids_together) {
        std::ranges::sort(groups, [&](const auto left, const auto right) {
            const auto left_count = wav_group_member_count(proposed, left);
            const auto right_count = wav_group_member_count(proposed, right);
            return left_count != right_count ? left_count > right_count : left.value < right.value;
        });
        if (!renumber_edited_groups(proposed, groups, result)) return result;
    }
    const auto preserve_sources = target_vectors_equal(history.document.resources,
                                                      proposed.resources);
    const auto label = labels.size() == 1 ? labels.front() : "Edit sound groups";
    return transaction.commit(label, {.preserve_retained_source_flags = preserve_sources});
}

} // namespace wormedit
