#pragma once

#include <wormedit/core/document.h>

#include <optional>
#include <string>
#include <vector>

namespace wormedit {

struct proposed_sound_group_t {
    std::string name;
    std::optional<sound_group_id_t> existing_group;
    std::vector<std::string> members;
    auto operator==(const proposed_sound_group_t&) const -> bool = default;
};

struct sound_group_proposals_t {
    std::vector<proposed_sound_group_t> groups;
    std::vector<std::string> ungrouped_names;
};

// Suggests flat groups from numbered filename stems. Inputs and existing groups
// are not modified; returned members and groups use natural name order.
[[nodiscard]] auto propose_sound_groups(
    const std::vector<std::string>& names,
    const std::vector<sound_group_t>& existing_groups) -> sound_group_proposals_t;

} // namespace wormedit
