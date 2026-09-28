#pragma once

#include "sys/compose_view.h"
#include "ui/frontend.h"
#include <wormedit/formats/conversion.h>

#include <optional>
#include <string>

namespace wormedit::sys {

// Returns a status when the action belongs to transport/player/export-policy
// handling, and nullopt when the caller should try another action dispatcher.
[[nodiscard]] auto dispatch_transport_player_action(
    const WormUiWorkspaceAction& action, application_command_state_t& application_state,
    wormedit::stepmania_export_policy_t& stepmania_export_policy)
    -> std::optional<std::string>;

} // namespace wormedit::sys
