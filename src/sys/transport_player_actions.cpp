#include "sys/transport_player_actions.h"


namespace wormedit::sys {
namespace {

auto publish_transport_feedback(const notification_sink_t& notifications,
                                notification_severity_t severity, std::string message,
                                notification_source_t source = notification_source_t::audio)
    -> std::string {
    notifications.post_feedback(severity, source, message);
    return message;
}

auto toggle_metronome_status(application_command_state_t& state,
                             const notification_sink_t& notifications) -> std::string {
    if (!toggle_transport_metronome(state))
        return publish_transport_feedback(notifications, notification_severity_t::warning, "Metronome is unavailable.");
    return publish_transport_feedback(notifications, notification_severity_t::success,
        state.metronome_settings().enabled ? "Metronome enabled." : "Metronome disabled.");
}

} // namespace

auto dispatch_transport_player_action(const wormedit::ui::workspace_action_t& action,
                                      application_command_state_t& application_state,
                                      notification_sink_t notifications)
    -> std::optional<std::string> {
    switch (action.kind) {
    case WORM_UI_WORKSPACE_ACTION_TRANSPORT_TOGGLE_METRONOME:
        return toggle_metronome_status(application_state, notifications);
    case WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_RATE:
        if (set_transport_rate(application_state, static_cast<audio::playback_rate_id_t>(action.transport_rate_id)))
            return publish_transport_feedback(notifications, notification_severity_t::success, "Playback rate changed.");
        return publish_transport_feedback(notifications, notification_severity_t::warning, "Playback rate unchanged.");
    case WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_KEEP_PITCH:
        if (!application_state.preferences.transport_keep_pitch_available ||
            !application_state.services.transport_set_keep_pitch ||
            !application_state.services.transport_set_keep_pitch(action.transport_keep_pitch))
            return publish_transport_feedback(notifications, notification_severity_t::warning, "Keep pitch is unavailable.");
        application_state.preferences.transport_keep_pitch = action.transport_keep_pitch;
        return publish_transport_feedback(notifications, notification_severity_t::success,
            action.transport_keep_pitch ? "Keep pitch enabled." : "Keep pitch disabled.");
    case WORM_UI_WORKSPACE_ACTION_TRANSPORT_PLAY_PAUSE:
        if (application_state.services.transport_play_pause) return application_state.services.transport_play_pause();
        return publish_transport_feedback(notifications, notification_severity_t::warning, "Chart preview is unavailable.");
    case WORM_UI_WORKSPACE_ACTION_TRANSPORT_STOP:
        if (application_state.services.transport_stop) {
            application_state.services.transport_stop();
            return publish_transport_feedback(notifications, notification_severity_t::hint, "Audio preview stopped.");
        }
        return publish_transport_feedback(notifications, notification_severity_t::warning, "Audio preview is unavailable.");
    case WORM_UI_WORKSPACE_ACTION_TRANSPORT_SEEK:
        application_state.invalidate_playing_seek_records();
        if (application_state.services.transport_seek) {
            auto outcome = finish_compose_action(
                notifications,
                application_state.services.transport_seek(action.transport_seek_frame));
            return std::move(outcome.message);
        }
        return publish_transport_feedback(notifications, notification_severity_t::warning, "Chart preview seek is unavailable.");
    case WORM_UI_WORKSPACE_ACTION_TOGGLE_PLAYER_PREVIEW:
        application_state.project.compose_view.player_preview_visible = !application_state.project.compose_view.player_preview_visible;
        return publish_transport_feedback(notifications, notification_severity_t::info,
            application_state.project.compose_view.player_preview_visible ? "Player preview shown." : "Player preview hidden.");
    case WORM_UI_WORKSPACE_ACTION_SELECT_PLAYER_PREVIEW_SKIN:
        if (action.player_preview_skin > wormedit::player_preview_skin_t::arrows) {
            return publish_transport_feedback(notifications, notification_severity_t::warning, "Unknown player-preview skin ignored.");
        }
        application_state.project.compose_view.player_preview_skin =
            static_cast<wormedit::player_preview_skin_t>(action.player_preview_skin);
        return publish_transport_feedback(notifications, notification_severity_t::success, "Player-preview skin changed.");
    default:
        return std::nullopt;
    }
}

} // namespace wormedit::sys
