#pragma once

#include <wormedit/core/command.h>

namespace wormedit {

// Toggles continuation/restart for every retained event at one pulse in a
// BMSON sound channel. The semantic BGM projection retains its stable IDs.
[[nodiscard]] auto toggle_bmson_sound_channel_restart(
    document_history_t& history, std::uint64_t expected_revision,
    chart_id_t chart, object_id_t sound_channel_event) -> document_command_result_t;

// Moves one semantic BMSON BGM marker and its retained source event together.
// The target must be an exact chart row free within that sound channel.
[[nodiscard]] auto move_bmson_bgm_marker(document_history_t& history,
                                         std::uint64_t expected_revision,
                                         chart_id_t chart, object_id_t bgm_event,
                                         local_position_t target) -> document_command_result_t;

// Inserts a retained non-playable event and its semantic BGM projection in the
// selected marker's sound channel. The new pulse either continues or restarts.
[[nodiscard]] auto insert_bmson_bgm_marker(document_history_t& history,
                                           std::uint64_t expected_revision,
                                           chart_id_t chart, object_id_t reference_bgm_event,
                                           local_position_t target, bool restart)
    -> document_command_result_t;

// Removes the linked semantic BGM marker and retained source event atomically.
[[nodiscard]] auto delete_bmson_bgm_marker(document_history_t& history,
                                           std::uint64_t expected_revision,
                                           chart_id_t chart, object_id_t bgm_event)
    -> document_command_result_t;

} // namespace wormedit
