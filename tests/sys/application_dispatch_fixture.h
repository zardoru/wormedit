#pragma once
#include "sys/preferences_runtime.h"
// Compatibility of existing fixture setup only; no old dispatcher signatures remain in production.
#include "sys/application_command_dispatch.h"
#include "sys/application_edit_inputs.h"
#include "sys/application_frame_services.h"
#include "sys/compose_actions.h"
#include "sys/compose_input.h"
#include "sys/external_player_runtime.h"
#include "sys/metadata_actions.h"
#include "sys/project_open.h"
#include "sys/project_view.h"
#include "sys/timing_edit_runtime.h"
#include "sys/timing_view.h"
#include <wormedit/core/autosave_loop.h>
#include <wormedit/core/recovery_controller.h>
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
    std::optional<proposal_t<audio_offset_drag_t>> audio_offset_drag{};
    std::optional<workspace_tab_t> audio_offset_drag_tab{};
    command_registry_t command_registry{};
    std::optional<editor_interaction_state_t> compose_interaction_state{};
    metadata_drop_state_t metadata_drop{};
    runtime::metadata_asset_t metadata_asset_runtime{};
    task_scheduler_t resource_copy_scheduler{1, 1};
    runtime::resource_copy_t resource_copy_runtime{resource_copy_scheduler};
    runtime::output_group_export_t export_runtime{};
    timing_grid_state_t timing_grid{};
    runtime::timing_edit_t timing_edit{};
    std::optional<timing_lane_pseudonote_outcome_t> timing_stage{};
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
    const wormedit::ui::workspace_action_t& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, wormedit::editor_interaction_state_t& state,
    double viewport_height, application_command_state_t& application_state,
    const std::filesystem::path& asset_base, wormedit::task_scheduler_t& resource_copy_scheduler,
    runtime::resource_copy_t& resource_copy_runtime, runtime::output_group_export_t& export_runtime,
    bool metadata_import_busy,
    wormedit::notification_sink_t notifications = wormedit::notification_sink_t::muted(),
    bool post_notifications = true) -> std::string {
    test_dispatch::defaults_t defaults;
    defaults.project_identity.asset_base = asset_base;
    auto& sink = notifications;
    const compose_action_inputs_t environment{
        {history, workspace, sink},
        {application_state, defaults.compose_interaction_state},
        compose_resource_inputs_t{.asset_base = defaults.project_identity.asset_base,
                                  .jobs = {resource_copy_scheduler, resource_copy_runtime, export_runtime},
                                  .metadata = {defaults.metadata_drop, defaults.metadata_asset_runtime}}};
    return dispatch_compose_view_action(action, environment, state, viewport_height,
                                        metadata_import_busy, post_notifications);
}

inline auto dispatch_compose_workspace_action(
    const wormedit::ui::workspace_action_t& action, wormedit::document_history_t& history,
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
    const compose_action_inputs_t environment{
        {history, workspace, notifications},
        {application_state, interaction_state},
        compose_resource_inputs_t{.asset_base = defaults.project_identity.asset_base,
                                  .jobs = {resource_copy_scheduler, resource_copy_runtime, export_runtime},
                                  .metadata = {metadata_drop, metadata_asset_runtime}}};
    return dispatch_compose_workspace_action(action, environment, viewport_height,
                                             metadata_import_busy);
}

inline auto dispatch_compose_input_action(
    wormedit::ui::workspace_action_t& ui_action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, wormedit::editor_interaction_state_t& state,
    wormedit::viewport_input_controller_t& viewport_input,
    wormedit::editor_navigation_input_controller_t& navigation_input,
    application_command_state_t& application_state, compose_scene_cache_t& scene_cache,
    double viewport_delta_seconds, const wormedit::notification_sink_t* notifications = nullptr)
    -> std::string {
    test_dispatch::defaults_t defaults;
    auto sink = notifications ? *notifications : defaults.notification_sink;
    const compose_input_inputs_t environment{{history, workspace, sink},
                                             application_state,
                                             {scene_cache, viewport_input, navigation_input}};
    return dispatch_compose_input_action(ui_action, environment, state, viewport_delta_seconds,
                                         notifications != nullptr);
}

inline auto
invoke_application_command(application_command_t command, wormedit::document_history_t& history,
                           wormedit::workspace_controller_t& workspace,
                           std::optional<wormedit::editor_interaction_state_t>& compose_state,
                           std::optional<wormedit::timing_lane_pseudonote_outcome_t>& timing_stage,
                           application_command_state_t& application_state,
                           notification_sink_t& sink) -> wormedit::dispatch_outcome_t {
    test_dispatch::defaults_t defaults;
    const command_dispatch_inputs_t environment{{history, workspace, sink},
                                                {application_state, compose_state, timing_stage}};
    return invoke_application_command(command, environment);
}

inline auto dispatch_metadata_action(
    const wormedit::ui::workspace_action_t& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, bool timing_locked,
    metadata_drop_state_t& metadata_drop, runtime::metadata_asset_t& metadata_asset_runtime,
    const std::filesystem::path& asset_base, wormedit::task_scheduler_t& resource_copy_scheduler,
    bool resource_copy_busy, bool export_busy,
    notification_sink_t notifications = notification_sink_t::muted())
    -> std::optional<std::string> {
    test_dispatch::defaults_t defaults;
    defaults.project_identity.asset_base = asset_base;
    auto& sink = notifications;
    const metadata_action_inputs_t environment{
        {history, workspace, sink},
        {metadata_drop, metadata_asset_runtime, resource_copy_scheduler},
        defaults.project_identity.asset_base};
    return dispatch_metadata_action(action, environment, timing_locked, resource_copy_busy,
                                    export_busy, true);
}
} // namespace wormedit::sys
