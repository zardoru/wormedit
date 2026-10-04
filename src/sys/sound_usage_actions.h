#pragma once

#include "sys/sound_usage_projection.h"

#include <ui/frontend.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/workspace.h>

#include <optional>
#include <string>

namespace wormedit::sys {

struct sound_usage_source_selection_t {
    std::uint64_t project_id{};
    std::uint64_t resource_id{};
    std::uint64_t channel_id{};
    std::uint64_t clip_id{};
};

struct sound_usage_action_result_t {
    bool accepted{};
    std::string message;
    std::optional<sound_usage_source_selection_t> source_selection;
};

// Validates a projected row against the current document and applies its navigation
// transactionally. Source rows return a UI selection payload without moving the cursor.
[[nodiscard]] auto dispatch_sound_usage_action(
    const WormUiWorkspaceAction& action, const WormUiSoundUsageRequest& request,
    document_history_t& history, workspace_controller_t& workspace,
    std::optional<editor_interaction_state_t>& interaction) -> sound_usage_action_result_t;

} // namespace wormedit::sys
