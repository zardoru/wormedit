#pragma once

#include <wormedit/core/command.h>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace wormedit {

inline constexpr std::size_t maximum_sound_group_name_bytes = 64;

struct create_sound_group_action_t {
    sound_group_id_t group;
    std::string name;
    std::vector<resource_id_t> sounds;
};
struct add_sounds_to_group_action_t {
    sound_group_id_t group;
    std::vector<resource_id_t> sounds;
};
struct ungroup_sounds_action_t { std::vector<resource_id_t> sounds; };
struct rename_sound_group_action_t { sound_group_id_t group; std::string name; };
struct delete_sound_group_action_t { sound_group_id_t group; };

using sound_group_edit_action_t =
    std::variant<create_sound_group_action_t, add_sounds_to_group_action_t,
                 ungroup_sounds_action_t, rename_sound_group_action_t,
                 delete_sound_group_action_t>;

// Applies one flat song-wide group edit. Names are trimmed, 1..64 bytes and
// unique ignoring ASCII case. Moving the last sound out removes its old group.
[[nodiscard]] auto apply_sound_group_edit(document_history_t& history,
                                          std::uint64_t expected_revision,
                                          const sound_group_edit_action_t& action,
                                          bool keep_bms_ids_together = false)
    -> document_command_result_t;

[[nodiscard]] auto apply_sound_group_edits(document_history_t& history,
                                           std::uint64_t expected_revision,
                                           const std::vector<sound_group_edit_action_t>& actions,
                                           bool keep_bms_ids_together)
    -> document_command_result_t;

} // namespace wormedit
