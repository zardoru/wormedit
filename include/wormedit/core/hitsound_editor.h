#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/command_registry.h>
#include <array>
#include <span>

namespace wormedit {

struct hitsound_command_t {
    std::string_view id;
    std::string_view label;
    hitsound_change_t change;
    shortcut_key_t key;
    shortcut_modifiers_t modifiers{};
};
[[nodiscard]] auto hitsound_commands() -> std::span<const hitsound_command_t>;

[[nodiscard]] auto changed_hitsample(osu_hitsample_t sample, hitsound_change_t change)
    -> osu_hitsample_t;

[[nodiscard]] auto apply_hitsound_edit(document_history_t& history, chart_id_t chart,
                                      std::span<const object_id_t> notes, hitsound_change_t change)
    -> document_command_result_t;

} // namespace wormedit
