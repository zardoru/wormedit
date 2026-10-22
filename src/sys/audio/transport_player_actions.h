#pragma once

#include "sys/compose_view.h"
#include "ui/frontend.h"
#include <wormedit/formats/conversion.h>

#include <wormedit/audio/playback_rate.h>
#include <wormedit/core/notifications.h>
#include <optional>
#include <string>

namespace wormedit::sys {

// Update the session preference only after the output accepts the exact rate.
inline auto set_transport_rate(application_command_state_t& state, std::size_t index) -> bool {
    if (index >= audio::playback_rates.size() || !state.services.transport_set_playback_rate ||
        !state.services.transport_set_playback_rate(audio::playback_rates[index]))
        return false;
    state.preferences.transport_rate_index = index;
    return true;
}

inline auto set_transport_rate(application_command_state_t& state, audio::playback_rate_id_t id) -> bool {
    // The core catalog asserts that dense enum ids are its storage keys.
    return set_transport_rate(state, static_cast<std::size_t>(id));
}

inline auto step_transport_rate(application_command_state_t& state, bool faster) -> bool {
    if (state.preferences.transport_rate_index >= audio::playback_rates.size() ||
        (faster && state.preferences.transport_rate_index == audio::playback_rates.size() - 1) ||
        (!faster && state.preferences.transport_rate_index == 0)) return false;
    return set_transport_rate(state, faster ? state.preferences.transport_rate_index + 1
                                           : state.preferences.transport_rate_index - 1);
}

// Steps the rate one notch and returns the status text; nullopt when the output
// rejected the change. Already at the slowest or fastest rate is not an error.
inline auto step_transport_rate_status(application_command_state_t& state, bool faster)
    -> std::optional<const char*> {
    const bool boundary = (faster && state.preferences.transport_rate_index == audio::playback_rates.size() - 1) ||
                          (!faster && state.preferences.transport_rate_index == 0);
    if (!boundary && !step_transport_rate(state, faster)) return std::nullopt;
    return boundary ? "Playback rate unchanged." : "Playback rate changed.";
}

// Plain Space uses hard Stop while playing, and starts/resumes only otherwise.
inline auto play_stop_transport(application_command_state_t& state)
    -> std::optional<std::string> {
    if (!state.services.transport_preview_playing) return std::nullopt;
    if (state.services.transport_preview_playing()) {
        if (!state.services.transport_stop) return std::nullopt;
        state.services.transport_stop();
        return "Audio preview stopped.";
    }
    if (!state.services.transport_play_pause) return std::nullopt;
    return state.services.transport_play_pause();
}

// Returns a status when the action belongs to transport/player
// handling, and nullopt when the caller should try another action dispatcher.
[[nodiscard]] auto dispatch_transport_player_action(
    const wormedit::ui::workspace_action_t& action, application_command_state_t& application_state,
    notification_sink_t notifications = notification_sink_t::muted()) -> std::optional<std::string>;

} // namespace wormedit::sys
