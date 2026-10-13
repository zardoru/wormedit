#pragma once
// Compatibility of existing fixture setup only; no old dispatcher signatures remain in production.
#include "sys/application_command_dispatch.h"
#include "sys/application_dispatch_context.h"
#include "sys/application_frame_services.h"
#include "sys/compose_actions.h"
#include "sys/compose_input.h"
#include "sys/metadata_actions.h"
namespace wormedit::sys {
namespace test_dispatch {
struct defaults_t {
    notification_center_t center;
    program_preferences_runtime_t program_preferences{};
    bool running{true};
    notification_sink_t notification_sink{center};
    std::string close_feedback{};
    document_history_t history{};
    workspace_controller_t workspace{};
    project_identity_t project_identity;
    application_command_state_t application_command_state{};
    stepmania_export_policy_t stepmania_export_policy{};
    std::optional<audio_offset_drag_t> audio_offset_drag{};
    std::optional<workspace_tab_t> audio_offset_drag_tab{};
    command_registry_t command_registry{};
    std::optional<editor_interaction_state_t> compose_interaction_state{};
    metadata_drop_state_t metadata_drop{};
    runtime::metadata_asset_t metadata_asset_runtime{};
    task_scheduler_t resource_copy_scheduler{1, 1};
    runtime::resource_copy_t resource_copy_runtime{};
    runtime::output_group_export_t export_runtime{};
    timing_grid_state_t timing_grid{};
    runtime::timing_edit_t timing_edit{};
    std::optional<timing_lane_pseudonote_preview_t> timing_stage{};
    timing_scene_cache_t timing_scene_cache{};
    compose_scene_cache_t compose_scene_cache{};
    viewport_input_controller_t compose_viewport_input{};
    editor_navigation_input_controller_t compose_navigation_input{};
    native_project_autosave_loop_t autosave_loop{native_project_autosave_loop_t::clock_t::now(),
                                                 resource_copy_scheduler};
    native_project_recovery_controller_t recovery_controller{};
    external_player_runtime_t external_player_runtime{};
    project_chart_runtime_t project_chart_runtime{};
};
} // namespace test_dispatch

inline auto dispatch_compose_view_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, wormedit::editor_interaction_state_t& state,
    double viewport_height, application_command_state_t& application_state,
    const std::filesystem::path& asset_base, wormedit::task_scheduler_t& resource_copy_scheduler,
    runtime::resource_copy_t& resource_copy_runtime, runtime::output_group_export_t& export_runtime,
    bool metadata_import_busy, std::optional<wormedit::notification_sink_t> notifications = {})
    -> std::string {
    test_dispatch::defaults_t defaults;
    defaults.project_identity.asset_base = asset_base;
    auto& sink = notifications ? *notifications : defaults.notification_sink;
    application_dispatch_context_t environment{
        defaults.program_preferences,
        defaults.running,
        sink,
        defaults.close_feedback,
        history,
        workspace,
        defaults.project_identity,
        application_state,
        defaults.stepmania_export_policy,
        defaults.audio_offset_drag,
        defaults.audio_offset_drag_tab,
        defaults.command_registry,
        defaults.compose_interaction_state,
        defaults.metadata_drop,
        defaults.metadata_asset_runtime,
        resource_copy_scheduler,
        resource_copy_runtime,
        export_runtime,
        defaults.timing_grid,
        defaults.timing_edit,
        defaults.timing_stage,
        defaults.timing_scene_cache,
        defaults.compose_scene_cache,
        defaults.compose_viewport_input,
        defaults.compose_navigation_input,
        defaults.autosave_loop,
        defaults.recovery_controller,
        defaults.external_player_runtime,
        defaults.project_chart_runtime,
    };
    return dispatch_compose_view_action(action, environment.compose_actions(), state, viewport_height,
                                        metadata_import_busy, bool(notifications));
}

inline auto dispatch_compose_workspace_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& interaction_state, double viewport_height,
    application_command_state_t& application_state, const std::filesystem::path& asset_base,
    wormedit::task_scheduler_t& resource_copy_scheduler,
    runtime::resource_copy_t& resource_copy_runtime, runtime::output_group_export_t& export_runtime,
    bool metadata_import_busy, metadata_drop_state_t& metadata_drop,
    runtime::metadata_asset_t& metadata_asset_runtime, wormedit::notification_sink_t notifications)
    -> bool {
    test_dispatch::defaults_t defaults;
    defaults.project_identity.asset_base = asset_base;
    application_dispatch_context_t environment{
        defaults.program_preferences,
        defaults.running,
        notifications,
        defaults.close_feedback,
        history,
        workspace,
        defaults.project_identity,
        application_state,
        defaults.stepmania_export_policy,
        defaults.audio_offset_drag,
        defaults.audio_offset_drag_tab,
        defaults.command_registry,
        interaction_state,
        metadata_drop,
        metadata_asset_runtime,
        resource_copy_scheduler,
        resource_copy_runtime,
        export_runtime,
        defaults.timing_grid,
        defaults.timing_edit,
        defaults.timing_stage,
        defaults.timing_scene_cache,
        defaults.compose_scene_cache,
        defaults.compose_viewport_input,
        defaults.compose_navigation_input,
        defaults.autosave_loop,
        defaults.recovery_controller,
        defaults.external_player_runtime,
        defaults.project_chart_runtime,
    };
    return dispatch_compose_workspace_action(action, environment.compose_actions(), viewport_height,
                                             metadata_import_busy);
}

inline auto dispatch_compose_input_action(
    WormUiWorkspaceAction& ui_action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, wormedit::editor_interaction_state_t& state,
    wormedit::viewport_input_controller_t& viewport_input,
    wormedit::editor_navigation_input_controller_t& navigation_input,
    application_command_state_t& application_state, compose_scene_cache_t& scene_cache,
    double viewport_delta_seconds, const wormedit::notification_sink_t* notifications = nullptr)
    -> std::string {
    test_dispatch::defaults_t defaults;
    auto sink = notifications ? *notifications : defaults.notification_sink;
    application_dispatch_context_t environment{
        defaults.program_preferences,
        defaults.running,
        sink,
        defaults.close_feedback,
        history,
        workspace,
        defaults.project_identity,
        application_state,
        defaults.stepmania_export_policy,
        defaults.audio_offset_drag,
        defaults.audio_offset_drag_tab,
        defaults.command_registry,
        defaults.compose_interaction_state,
        defaults.metadata_drop,
        defaults.metadata_asset_runtime,
        defaults.resource_copy_scheduler,
        defaults.resource_copy_runtime,
        defaults.export_runtime,
        defaults.timing_grid,
        defaults.timing_edit,
        defaults.timing_stage,
        defaults.timing_scene_cache,
        scene_cache,
        viewport_input,
        navigation_input,
        defaults.autosave_loop,
        defaults.recovery_controller,
        defaults.external_player_runtime,
        defaults.project_chart_runtime,
    };
    return dispatch_compose_input_action(ui_action, environment.compose_inputs(), state, viewport_delta_seconds,
                                         notifications != nullptr);
}

inline auto
invoke_application_command(application_command_t command, wormedit::document_history_t& history,
                           wormedit::workspace_controller_t& workspace,
                           std::optional<wormedit::editor_interaction_state_t>& compose_state,
                           std::optional<wormedit::timing_lane_pseudonote_preview_t>& timing_stage,
                           application_command_state_t& application_state,
                           notification_sink_t& sink) -> wormedit::command_invocation_result_t {
    test_dispatch::defaults_t defaults;
    application_dispatch_context_t environment{
        defaults.program_preferences,
        defaults.running,
        sink,
        defaults.close_feedback,
        history,
        workspace,
        defaults.project_identity,
        application_state,
        defaults.stepmania_export_policy,
        defaults.audio_offset_drag,
        defaults.audio_offset_drag_tab,
        defaults.command_registry,
        compose_state,
        defaults.metadata_drop,
        defaults.metadata_asset_runtime,
        defaults.resource_copy_scheduler,
        defaults.resource_copy_runtime,
        defaults.export_runtime,
        defaults.timing_grid,
        defaults.timing_edit,
        timing_stage,
        defaults.timing_scene_cache,
        defaults.compose_scene_cache,
        defaults.compose_viewport_input,
        defaults.compose_navigation_input,
        defaults.autosave_loop,
        defaults.recovery_controller,
        defaults.external_player_runtime,
        defaults.project_chart_runtime,
    };
    return invoke_application_command(command, environment.command_inputs());
}

inline auto dispatch_metadata_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, bool timing_locked,
    metadata_drop_state_t& metadata_drop, runtime::metadata_asset_t& metadata_asset_runtime,
    const std::filesystem::path& asset_base, wormedit::task_scheduler_t& resource_copy_scheduler,
    bool resource_copy_busy, bool export_busy,
    std::optional<notification_sink_t> notifications = {}) -> std::optional<std::string> {
    test_dispatch::defaults_t defaults;
    defaults.project_identity.asset_base = asset_base;
    auto& sink = notifications ? *notifications : defaults.notification_sink;
    application_dispatch_context_t environment{
        defaults.program_preferences,
        defaults.running,
        sink,
        defaults.close_feedback,
        history,
        workspace,
        defaults.project_identity,
        defaults.application_command_state,
        defaults.stepmania_export_policy,
        defaults.audio_offset_drag,
        defaults.audio_offset_drag_tab,
        defaults.command_registry,
        defaults.compose_interaction_state,
        metadata_drop,
        metadata_asset_runtime,
        resource_copy_scheduler,
        defaults.resource_copy_runtime,
        defaults.export_runtime,
        defaults.timing_grid,
        defaults.timing_edit,
        defaults.timing_stage,
        defaults.timing_scene_cache,
        defaults.compose_scene_cache,
        defaults.compose_viewport_input,
        defaults.compose_navigation_input,
        defaults.autosave_loop,
        defaults.recovery_controller,
        defaults.external_player_runtime,
        defaults.project_chart_runtime,
    };
    return dispatch_metadata_action(action, environment.metadata_inputs(), timing_locked, resource_copy_busy,
                                    export_busy, bool(notifications));
}
} // namespace wormedit::sys
