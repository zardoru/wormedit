#pragma once

#include <wormedit/core/command.h>

namespace wormedit {

// Toggles continuation/restart for every retained event at one pulse in a
// BMSON sound channel. The semantic BGM projection retains its stable IDs.
[[nodiscard]] auto toggle_bmson_sound_channel_restart(
    document_history_t& history, std::uint64_t expected_revision,
    chart_id_t chart, object_id_t sound_channel_event) -> document_command_result_t;

} // namespace wormedit
