#include <wormedit/core/sound_groups.h>

#include <algorithm>
#include <ranges>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace wormedit {
namespace {

auto reject(document_command_result_t& result, std::string message) -> void {
    result.diagnostics.push_back({.severity = diagnostic_severity_t::error,
                                  .code = diagnostic_code_t::invalid_id,
                                  .message = std::move(message)});
}

auto trim(std::string value) -> std::string {
    constexpr std::string_view whitespace = " \t\n\r\f\v";
    const auto begin = value.find_first_not_of(whitespace);
    if (begin == std::string::npos) return {};
    const auto end = value.find_last_not_of(whitespace);
    return value.substr(begin, end - begin + 1);
}

auto fold_ascii(std::string_view value) -> std::string {
    std::string result(value);
    for (auto& character : result) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte >= 'A' && byte <= 'Z') character = static_cast<char>(byte + ('a' - 'A'));
    }
    return result;
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
    if (name.empty() || name.size() > maximum_sound_group_name_bytes ||
        name.find('\0') != std::string::npos) {
        reject(result, "sound group name must contain 1 to 64 bytes");
        return false;
    }
    const auto folded = fold_ascii(name);
    for (const auto& group : song.sound_groups) {
        if (group.id != except && fold_ascii(group.name) == folded) {
            reject(result, "sound group name is already in use");
            return false;
        }
    }
    return true;
}

auto selected_resources(song_document_t& song, const std::vector<resource_id_t>& ids,
                        document_command_result_t& result)
    -> std::vector<resource_entry_t*> {
    if (ids.empty()) {
        reject(result, "sound group edit needs at least one sound");
        return {};
    }
    std::unordered_set<std::uint64_t> seen;
    std::vector<resource_entry_t*> selected;
    selected.reserve(ids.size());
    for (const auto id : ids) {
        if (!id || !seen.insert(id.value).second) {
            reject(result, "sound group edit contains an invalid or repeated sound");
            return {};
        }
        const auto found = std::ranges::find(song.resources, id, &resource_entry_t::id);
        if (found == song.resources.end() || !is_sound(*found)) {
            reject(result, "sound group edit refers to an unknown sound");
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
        reject(result, "sound group ID is invalid or already in use");
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
        reject(result, "sound group edit refers to an unknown group");
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
        reject(result, "sound group edit refers to an unknown group");
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
        reject(result, "sound group edit refers to an unknown group");
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

} // namespace

auto apply_sound_group_edit(document_history_t& history, std::uint64_t expected_revision,
                            const sound_group_edit_action_t& action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (expected_revision != history.document.revision) {
        reject(result, "sound group edit document revision is stale");
        return result;
    }
    auto proposed = history.document;
    const auto label = edit_existing_group(proposed, action, result);
    if (!result.diagnostics.empty() || label.empty()) return result;
    return commit_document_command(history, std::move(proposed), label,
                                   {.preserve_retained_source_flags = true});
}

} // namespace wormedit
