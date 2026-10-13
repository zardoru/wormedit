#include "sys/transport_player_actions.h"

#include <wormedit/formats/stepmania.h>

namespace wormedit::sys {
namespace {

auto publish_transport_feedback(const std::optional<notification_sink_t>& notifications,
    notification_severity_t severity, std::string message,
    notification_source_t source = notification_source_t::audio) -> std::string {
    if (notifications) {
        if (severity == notification_severity_t::hint) notifications->post_transient_hint(source, message);
        else notifications->post(severity, source, message);
    }
    return message;
}

auto stepmania_policy_from_ui(const WormUiWorkspaceAction& action)
    -> std::optional<wormedit::stepmania_export_policy_t> {
    wormedit::stepmania_export_policy_t policy;
    switch (action.stepmania_file_profile) {
    case WORM_UI_STEPMANIA_FILE_SM:
        policy.file_profile = wormedit::stepmania_profile_t::sm;
        break;
    case WORM_UI_STEPMANIA_FILE_SSC:
        policy.file_profile = wormedit::stepmania_profile_t::ssc;
        break;
    default:
        return std::nullopt;
    }
    switch (action.stepmania_timing_syntax) {
    case WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS:
        policy.timing_syntax = wormedit::stepmania_timing_syntax_t::native_warps;
        break;
    case WORM_UI_STEPMANIA_TIMING_LEGACY_NEGATIVE_BPM:
        policy.timing_syntax = wormedit::stepmania_timing_syntax_t::legacy_negative_bpm;
        break;
    case WORM_UI_STEPMANIA_TIMING_LEGACY_NEGATIVE_STOP:
        policy.timing_syntax = wormedit::stepmania_timing_syntax_t::legacy_negative_stop;
        break;
    default:
        return std::nullopt;
    }
    switch (action.stepmania_implementation) {
    case WORM_UI_STEPMANIA_IMPLEMENTATION_SYNTAX_ONLY:
        policy.implementation_profile =
            wormedit::stepmania_implementation_profile_t::unspecified;
        break;
    case WORM_UI_STEPMANIA_IMPLEMENTATION_STEP_MANIA_5_1:
        policy.implementation_profile =
            wormedit::stepmania_implementation_profile_t::stepmania_5_1;
        break;
    case WORM_UI_STEPMANIA_IMPLEMENTATION_ETTERNA_0_75_1:
        policy.implementation_profile =
            wormedit::stepmania_implementation_profile_t::etterna_0_75_1;
        break;
    default:
        return std::nullopt;
    }
    return policy;
}

auto toggle_metronome_status(application_command_state_t& state,
    const std::optional<notification_sink_t>& notifications) -> std::string {
    if (!toggle_transport_metronome(state))
        return publish_transport_feedback(notifications, notification_severity_t::warning, "Metronome is unavailable.");
    return publish_transport_feedback(notifications, notification_severity_t::success,
        state.metronome_settings().enabled ? "Metronome enabled." : "Metronome disabled.");
}



} // namespace

auto dispatch_transport_player_action(
    const WormUiWorkspaceAction& action, application_command_state_t& application_state,
    wormedit::stepmania_export_policy_t& stepmania_export_policy,
    std::optional<notification_sink_t> notifications)
    -> std::optional<std::string> {
    switch (action.kind) {
    case WORM_UI_WORKSPACE_ACTION_TRANSPORT_TOGGLE_METRONOME:
        return toggle_metronome_status(application_state, notifications);
    case WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_RATE:
        if (set_transport_rate(application_state, action.transport_rate_index))
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
        if (application_state.services.transport_seek) return application_state.services.transport_seek(action.transport_seek_frame);
        return publish_transport_feedback(notifications, notification_severity_t::warning, "Chart preview seek is unavailable.");
    case WORM_UI_WORKSPACE_ACTION_TOGGLE_PLAYER_PREVIEW:
        application_state.project.compose_view.player_preview_visible = !application_state.project.compose_view.player_preview_visible;
        return publish_transport_feedback(notifications, notification_severity_t::info,
            application_state.project.compose_view.player_preview_visible ? "Player preview shown." : "Player preview hidden.");
    case WORM_UI_WORKSPACE_ACTION_SELECT_PLAYER_PREVIEW_SKIN:
        if (action.player_preview_skin > WORM_UI_PLAYER_PREVIEW_SKIN_ARROWS) {
            return publish_transport_feedback(notifications, notification_severity_t::warning, "Unknown player-preview skin ignored.");
        }
        application_state.project.compose_view.player_preview_skin =
            static_cast<wormedit::player_preview_skin_t>(action.player_preview_skin);
        return publish_transport_feedback(notifications, notification_severity_t::success, "Player-preview skin changed.");
    case WORM_UI_WORKSPACE_ACTION_SET_STEPMANIA_EXPORT_POLICY: {
        const auto candidate = stepmania_policy_from_ui(action);
        if (!candidate) {
            return publish_transport_feedback(notifications, notification_severity_t::warning,
                "Unknown StepMania export policy ignored.", notification_source_t::export_);
        }
        stepmania_export_policy = *candidate;
        const auto resolved =
            wormedit::resolve_stepmania_export_policy(stepmania_export_policy);
        return publish_transport_feedback(notifications, resolved ? notification_severity_t::success
                                                                : notification_severity_t::warning,
            resolved ? "StepMania export policy updated." : "StepMania policy needs attention: " + resolved.message,
            notification_source_t::export_);
    }
    default:
        return std::nullopt;
    }
}

} // namespace wormedit::sys
