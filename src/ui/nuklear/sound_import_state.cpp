#include "ui/nuklear/sound_import_state.h"

#include "core/text_util.h"

#include <algorithm>
#include <array>
#include <optional>
#include <filesystem>
#include <string_view>
#include <utility>

namespace {
using wormedit::text_util::lowercase_extension;

constexpr std::size_t maximum_name_bytes = 255;

auto is_stem_friendly(const std::string& extension) -> bool {
    return extension == ".wav" || extension == ".ogg" || extension == ".flac";
}

auto display_name(const std::string& path) -> std::string {
    auto name = std::filesystem::path{path}.stem().string();
    name.resize(wormedit::text_util::truncate_utf8(name, maximum_name_bytes).size());
    return name;
}

void choose_phase(worm_ui_sound_import_state_t& state, bool chart_has_bgm) {
    if (state.items.empty()) {
        state.phase = worm_ui_sound_import_phase_t::closed;
        return;
    }
    if (state.items.size() > 1) {
        state.phase = worm_ui_sound_import_phase_t::review_list;
        return;
    }
    const bool ambiguous = !state.fixed_kind && !chart_has_bgm &&
                           is_stem_friendly(lowercase_extension(state.items.front().path));
    state.phase = ambiguous ? worm_ui_sound_import_phase_t::choose_kind
                            : worm_ui_sound_import_phase_t::ready;
}

auto default_kind(const std::string& path, bool chart_has_bgm) -> std::uint8_t {
    return !chart_has_bgm && is_stem_friendly(lowercase_extension(path))
               ? WORM_UI_SOUND_IMPORT_STEM
               : WORM_UI_SOUND_IMPORT_KEYSOUND;
}

auto valid_kind(std::uint8_t kind) -> bool {
    return kind >= WORM_UI_SOUND_IMPORT_STEM && kind <= WORM_UI_SOUND_IMPORT_SAMPLE;
}

} // namespace

auto worm_ui_nuklear_sound_import_is_audio_path(const std::string& path) -> bool {
    static constexpr std::array<std::string_view, 4> extensions{".wav", ".ogg", ".flac", ".mp3"};
    const auto extension = lowercase_extension(path);
    return std::ranges::find(extensions, extension) != extensions.end();
}

auto worm_ui_nuklear_sound_import_add(worm_ui_sound_import_state_t& state,
                                      std::span<const std::string> paths, bool chart_has_bgm)
    -> std::size_t {
    state.message.clear();
    std::size_t added = 0;
    std::size_t skipped_type = 0;
    std::size_t skipped_duplicate = 0;
    for (const auto& path : paths) {
        if (!worm_ui_nuklear_sound_import_is_audio_path(path)) {
            ++skipped_type;
        } else if (std::ranges::any_of(state.items, [&](const auto& item) {
                       return item.path == path;
                   })) {
            ++skipped_duplicate;
        } else {
            state.items.push_back({.path = path, .name = display_name(path), .kind = state.fixed_kind
                ? state.fixed_kind : default_kind(path, chart_has_bgm)});
            ++added;
        }
    }
    choose_phase(state, chart_has_bgm);
    if (skipped_type != 0)
        state.message = std::to_string(skipped_type) + " file(s) skipped: not wav, ogg, flac or mp3.";
    else if (skipped_duplicate != 0)
        state.message = std::to_string(skipped_duplicate) + " file(s) skipped: already in the list.";
    else if (state.items.size() > worm_ui_sound_import_capacity)
        state.message = "This import has " + std::to_string(state.items.size()) +
                        " files; the maximum is 1295. Remove files before confirming.";
    return added;
}

void worm_ui_nuklear_sound_import_set_target(worm_ui_sound_import_state_t& state,
                                            std::uint64_t project, std::uint64_t chart,
                                            std::uint64_t revision) {
    state.project_id = project;
    state.chart_id = chart;
    state.document_revision = revision;
}

void worm_ui_nuklear_sound_import_suggest_groups(
    worm_ui_sound_import_state_t& state,
    std::span<const worm_ui_sound_import_existing_group_t> existing_groups) {
    state.groups.clear();
    for (auto& item : state.items) item.group_index.reset();
    auto names = std::vector<std::string>{};
    names.reserve(state.items.size());
    for (const auto& item : state.items) names.push_back(item.path);
    auto groups = std::vector<wormedit::sound_group_t>{};
    groups.reserve(existing_groups.size());
    for (const auto& group : existing_groups)
        groups.push_back({.id = group.id, .name = group.name});
    const auto proposals = wormedit::propose_sound_groups(names, groups);
    for (const auto& proposal : proposals.groups) {
        const auto group_index = state.groups.size();
        state.groups.push_back({.name = proposal.name, .existing_group = proposal.existing_group});
        for (const auto& member : proposal.members) {
            const auto item = std::ranges::find(state.items, member, &worm_ui_sound_import_item_t::path);
            if (item != state.items.end()) item->group_index = group_index;
        }
    }
    if (!state.groups.empty()) state.phase = worm_ui_sound_import_phase_t::review_groups;
}

void worm_ui_nuklear_sound_import_set_group_accepted(worm_ui_sound_import_state_t& state,
                                                     std::size_t group, bool accepted) {
    if (group < state.groups.size()) state.groups[group].accepted = accepted;
}

void worm_ui_nuklear_sound_import_set_group_expanded(worm_ui_sound_import_state_t& state,
                                                    std::size_t group, bool expanded) {
    if (group >= state.groups.size()) return;
    if (expanded) {
        for (auto& row : state.groups) row.expanded = false;
        state.ungrouped_expanded = false;
    }
    state.groups[group].expanded = expanded;
}

void worm_ui_nuklear_sound_import_set_ungrouped_expanded(
    worm_ui_sound_import_state_t& state, bool expanded) {
    if (expanded) for (auto& group : state.groups) group.expanded = false;
    state.ungrouped_expanded = expanded;
}

void worm_ui_nuklear_sound_import_rename_group(worm_ui_sound_import_state_t& state,
                                              std::size_t group, std::string name) {
    constexpr std::size_t max_group_name_bytes = 64;
    if (group >= state.groups.size()) return;
    name.resize(wormedit::text_util::truncate_utf8(name, max_group_name_bytes).size());
    state.groups[group].name = std::move(name);
}

void worm_ui_nuklear_sound_import_move_member(worm_ui_sound_import_state_t& state,
                                             std::size_t member,
                                             std::optional<std::size_t> group) {
    if (member >= state.items.size() || (group && *group >= state.groups.size())) return;
    state.items[member].group_index = group;
}

void worm_ui_nuklear_sound_import_leave_all_ungrouped(worm_ui_sound_import_state_t& state) {
    for (auto& group : state.groups) group.accepted = false;
    for (auto& item : state.items) item.group_index.reset();
}

void worm_ui_nuklear_sound_import_set_kind(worm_ui_sound_import_state_t& state,
                                           std::size_t index, std::uint8_t kind) {
    if (!state.fixed_kind && index < state.items.size() && valid_kind(kind)) state.items[index].kind = kind;
}

void worm_ui_nuklear_sound_import_set_all_kinds(worm_ui_sound_import_state_t& state,
                                                std::uint8_t kind) {
    if (state.fixed_kind || !valid_kind(kind)) return;
    for (auto& item : state.items) item.kind = kind;
}

void worm_ui_nuklear_sound_import_rename(worm_ui_sound_import_state_t& state, std::size_t index,
                                         std::string name) {
    if (index >= state.items.size()) return;
    name.resize(wormedit::text_util::truncate_utf8(name, maximum_name_bytes).size());
    state.items[index].name = std::move(name);
}

void worm_ui_nuklear_sound_import_remove(worm_ui_sound_import_state_t& state, std::size_t index) {
    if (index >= state.items.size()) return;
    state.items.erase(state.items.begin() + static_cast<std::ptrdiff_t>(index));
    // A list that shrinks to nothing closes; one left over stays a review list.
    if (state.items.empty()) state.phase = worm_ui_sound_import_phase_t::closed;
}

void worm_ui_nuklear_sound_import_move(worm_ui_sound_import_state_t& state, std::size_t index,
                                       int delta) {
    if (index >= state.items.size() || (delta != -1 && delta != 1)) return;
    if (delta < 0 && index == 0) return;
    const auto target = delta < 0 ? index - 1 : index + 1;
    if (target >= state.items.size()) return;
    std::swap(state.items[index], state.items[target]);
}

auto worm_ui_nuklear_sound_import_confirm(worm_ui_sound_import_state_t& state)
    -> std::optional<worm_ui_sound_import_commit_t> {
    state.message.clear();
    if (state.items.empty()) {
        state.message = "Add at least one audio file before confirming.";
        return std::nullopt;
    }
    if (state.items.size() > worm_ui_sound_import_capacity) {
        state.message = "This import exceeds the 1295-file limit. Remove files before confirming.";
        return std::nullopt;
    }
    for (auto& item : state.items) item.name = std::string(wormedit::text_util::trim(item.name, " \t"));
    for (const auto& item : state.items) {
        if (!item.name.empty() && wormedit::text_util::is_valid_bounded_text(item.name, maximum_name_bytes)) continue;
        state.message = "Every sound needs a name.";
        return std::nullopt;
    }
    for (const auto& group : state.groups) {
        if (group.accepted && (wormedit::text_util::trim(group.name, " \t").empty() ||
                               !wormedit::text_util::is_valid_bounded_text(group.name, 64))) {
            state.message = "Group names must contain 1 to 64 bytes.";
            return std::nullopt;
        }
    }
    auto commit = worm_ui_sound_import_commit_t{.items = state.items};
    for (auto& group : state.groups) {
        if (!group.accepted) continue;
        auto row = worm_ui_sound_import_group_commit_t{
            .name = std::string(wormedit::text_util::trim(group.name, " \t")), .existing_group = group.existing_group};
        for (std::size_t index = 0; index < state.items.size(); ++index)
            if (state.items[index].group_index == std::optional<std::size_t>{
                    static_cast<std::size_t>(&group - state.groups.data())})
                row.member_indexes.push_back(index);
        if (!row.member_indexes.empty()) commit.groups.push_back(std::move(row));
    }
    return commit;
}

void worm_ui_nuklear_sound_import_commit_failed(worm_ui_sound_import_state_t& state,
                                               std::string reason) {
    state.message = std::move(reason);
}

void worm_ui_nuklear_sound_import_commit_succeeded(worm_ui_sound_import_state_t& state) {
    state = {};
}

void worm_ui_nuklear_sound_import_cancel(worm_ui_sound_import_state_t& state) {
    state = {};
}

void worm_ui_nuklear_sound_import_fix_kind(worm_ui_sound_import_state_t& state,
                                          std::uint8_t kind) {
    state.fixed_kind = valid_kind(kind) ? kind : 0;
    if (!state.fixed_kind) return;
    for (auto& item : state.items) item.kind = state.fixed_kind;
    if (state.phase == worm_ui_sound_import_phase_t::choose_kind)
        state.phase = worm_ui_sound_import_phase_t::review_list;
}
