#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/note_to_bgm.h>

namespace wormedit {

enum class bmson_channel_lane_mode_t { stem, packed };

// Channel ownership for display edits includes offline audio and uniquely named
// empty channels. Ambiguous resource reuse is never resolved arbitrarily.
[[nodiscard]] auto bmson_lane_mode_resource_bindings(const song_document_t& document,
    chart_id_t chart) -> std::vector<bmson_channel_resource_binding_t>;

// Display-only channel conversion. Continuations refuse packing unless the caller
// explicitly confirms converting every continuation in this channel to a restart.
// The complete conversion, including resource kind, is one atomic undo step.
[[nodiscard]] auto set_bmson_channel_lane_mode(
    document_history_t& history, chart_id_t chart,
    object_id_t channel, bmson_channel_lane_mode_t mode,
    bool convert_continuations_to_restarts = false, note_to_bgm_policy_t policy = {})
    -> document_command_result_t;

} // namespace wormedit
