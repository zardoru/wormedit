#include "text_util.h"
#include <wormedit/core/sound_group_proposal.h>

#include <wormedit/core/natural_order.h>

#include <algorithm>
#include <map>
#include <string_view>
#include <utility>

namespace wormedit {
namespace {
using wormedit::text_util::ascii_lowercase;

auto filename_part(std::string_view name) -> std::string_view {
    const auto separator = name.find_last_of("/\\");
    return separator == std::string_view::npos ? name : name.substr(separator + 1);
}

auto filename_order_less(std::string_view left, std::string_view right) -> bool {
    const auto left_filename = filename_part(left);
    const auto right_filename = filename_part(right);
    if (natural_name_less(left_filename, right_filename)) return true;
    if (natural_name_less(right_filename, left_filename)) return false;
    return natural_name_less(left, right);
}

auto without_extension(std::string_view name) -> std::string_view {
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0) return name;
    return name.substr(0, dot);
}

struct stem_t {
    std::string value;
    bool numbered = false;
};

struct candidate_t {
    std::vector<std::pair<std::string, std::string>> members;
    std::optional<sound_group_id_t> existing_group;
    std::string existing_name;
};

auto filename_stem(std::string_view name) -> stem_t {
    auto stem = without_extension(filename_part(name));
    auto end = stem.size();
    while (end > 0 && stem[end - 1] >= '0' && stem[end - 1] <= '9') --end;
    if (end == stem.size()) return {.value = std::string(stem)};
    auto prefix_end = end;
    while (prefix_end > 0 && (stem[prefix_end - 1] == '_' || stem[prefix_end - 1] == '-' ||
                              stem[prefix_end - 1] == '.' || stem[prefix_end - 1] == ' ')) {
        --prefix_end;
    }
    return {.value = std::string(stem.substr(0, prefix_end)), .numbered = true};
}

auto existing_group_by_name(const std::vector<sound_group_t>& groups)
    -> std::map<std::string, sound_group_id_t> {
    auto result = std::map<std::string, sound_group_id_t>{};
    for (const auto& group : groups) result.emplace(ascii_lowercase(group.name), group.id);
    return result;
}

} // namespace

auto propose_sound_groups(const std::vector<std::string>& names,
                          const std::vector<sound_group_t>& existing_groups)
    -> sound_group_proposals_t {
    auto result = sound_group_proposals_t{};
    auto group_ids = existing_group_by_name(existing_groups);
    auto candidates = std::map<std::string, candidate_t>{};
    for (const auto& name : names) {
        auto stem = filename_stem(name);
        const auto key = ascii_lowercase(stem.value);
        const auto existing = group_ids.find(key);
        if (stem.value.empty() || (!stem.numbered && existing == group_ids.end())) {
            result.ungrouped_names.push_back(name);
            continue;
        }
        auto& candidate = candidates[key];
        if (existing != group_ids.end()) {
            candidate.existing_group = existing->second;
            candidate.existing_name = std::find_if(
                existing_groups.begin(), existing_groups.end(), [&](const auto& item) {
                    return item.id == existing->second;
                })->name;
        }
        candidate.members.emplace_back(name, stem.value);
    }

    for (auto& entry : candidates) {
        auto& candidate = entry.second;
        auto& members = candidate.members;
        if (members.size() < 2 && !candidate.existing_group) {
            result.ungrouped_names.push_back(members.front().first);
            continue;
        }
        std::sort(members.begin(), members.end(), [](const auto& left, const auto& right) {
            return filename_order_less(left.first, right.first);
        });
        auto proposal = proposed_sound_group_t{};
        proposal.name = candidate.existing_group ? candidate.existing_name : members.front().second;
        proposal.existing_group = candidate.existing_group;
        for (const auto& member : members) proposal.members.push_back(member.first);
        result.groups.push_back(std::move(proposal));
    }

    std::sort(result.groups.begin(), result.groups.end(), [](const auto& left, const auto& right) {
        return natural_name_less(left.name, right.name);
    });
    std::sort(result.ungrouped_names.begin(), result.ungrouped_names.end(),
              filename_order_less);
    return result;
}

} // namespace wormedit
