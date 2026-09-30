#pragma once

#include "sys/compose_view.h"
#include "ui/frontend.h"
#include <wormedit/formats/conversion.h>

#include <wormedit/audio/playback_rate.h>
#include <optional>
#include <string>

namespace wormedit::sys {

// Update the session preference only after the output accepts the exact rate.
inline auto set_transport_rate(application_command_state_t& state, std::size_t index) -> bool {
    if (index >= audio::playback_rates.size() || !state.transport_set_playback_rate ||
        !state.transport_set_playback_rate(audio::playback_rates[index]))
        return false;
    state.transport_rate_index = index;
    return true;
}

inline auto step_transport_rate(application_command_state_t& state, bool faster) -> bool {
    if (state.transport_rate_index >= audio::playback_rates.size() ||
        (faster && state.transport_rate_index == audio::playback_rates.size() - 1) ||
        (!faster && state.transport_rate_index == 0)) return false;
    return set_transport_rate(state, faster ? state.transport_rate_index + 1
                                           : state.transport_rate_index - 1);
}

// Returns a status when the action belongs to transport/player/export-policy
// handling, and nullopt when the caller should try another action dispatcher.
[[nodiscard]] auto dispatch_transport_player_action(
    const WormUiWorkspaceAction& action, application_command_state_t& application_state,
    wormedit::stepmania_export_policy_t& stepmania_export_policy)
    -> std::optional<std::string>;

} // namespace wormedit::sys
