#include "sys/transport_player_actions.h"

#include <wormedit/formats/stepmania.h>

namespace wormedit::sys {
namespace {

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

} // namespace

auto dispatch_transport_player_action(
    const WormUiWorkspaceAction& action, application_command_state_t& application_state,
    wormedit::stepmania_export_policy_t& stepmania_export_policy)
    -> std::optional<std::string> {
    switch (action.kind) {
    case WORM_UI_WORKSPACE_ACTION_TRANSPORT_PLAY_PAUSE:
        return application_state.transport_play_pause
                   ? application_state.transport_play_pause()
                   : "Chart preview is unavailable.";
    case WORM_UI_WORKSPACE_ACTION_TRANSPORT_STOP:
        if (application_state.transport_stop) {
            application_state.transport_stop();
            return "Audio preview stopped.";
        }
        return "Audio preview is unavailable.";
    case WORM_UI_WORKSPACE_ACTION_TRANSPORT_SEEK:
        return application_state.transport_seek
                   ? application_state.transport_seek(action.transport_seek_frame)
                   : "Chart preview seek is unavailable.";
    case WORM_UI_WORKSPACE_ACTION_TOGGLE_PLAYER_PREVIEW:
        application_state.player_preview_visible = !application_state.player_preview_visible;
        return application_state.player_preview_visible ? "Player preview shown."
                                                        : "Player preview hidden.";
    case WORM_UI_WORKSPACE_ACTION_SELECT_PLAYER_PREVIEW_SKIN:
        if (action.player_preview_skin > WORM_UI_PLAYER_PREVIEW_SKIN_ARROWS) {
            return "Unknown player-preview skin ignored.";
        }
        application_state.player_preview_skin =
            static_cast<wormedit::player_preview_skin_t>(action.player_preview_skin);
        return "Player-preview skin changed.";
    case WORM_UI_WORKSPACE_ACTION_SET_STEPMANIA_EXPORT_POLICY: {
        const auto candidate = stepmania_policy_from_ui(action);
        if (!candidate) {
            return "Unknown StepMania export policy ignored.";
        }
        stepmania_export_policy = *candidate;
        const auto resolved =
            wormedit::resolve_stepmania_export_policy(stepmania_export_policy);
        return resolved ? "StepMania export policy updated."
                        : "StepMania policy needs attention: " + resolved.message;
    }
    default:
        return std::nullopt;
    }
}

} // namespace wormedit::sys
