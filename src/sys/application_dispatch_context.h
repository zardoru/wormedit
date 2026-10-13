#pragma once

#include "sys/application_commands.h"
#include "sys/compose_view.h"
#include "sys/external_player.h"
#include "sys/metadata_asset_runtime.h"
#include "sys/metadata_drop_state.h"
#include "sys/preferences_runtime.h"
#include "sys/project_view.h"
#include "sys/project_open.h"
#include "sys/resource_job_state.h"
#include "sys/timing_edit_runtime.h"
#include "sys/timing_view.h"
#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/autosave_loop.h>
#include <wormedit/core/command_registry.h>
#include <wormedit/core/editor_navigation_input.h>
#include <wormedit/core/recovery_controller.h>
#include <wormedit/core/viewport_input.h>
#include <wormedit/formats/stepmania.h>

namespace wormedit::sys {
struct external_player_runtime_t {
    wormedit::sys::external_player_t player;
    wormedit::sys::external_player_preferences_t preferences =
        wormedit::sys::default_external_player_preferences();
    wormedit::sys::program_preferences_runtime_t* program_preferences{};
    bool visible{};
    std::uint64_t revision{};
    std::vector<WormUiExternalPlayerDefinitionView> views;
};

// Live references only. Build once per synchronous frame or registry invocation.
// Never capture this object in a callback or worker job.
struct application_dispatch_context_t {
    program_preferences_runtime_t& program_preferences_ref;
    bool& running_ref;
    notification_sink_t& notification_sink_ref;
    std::string& close_feedback_ref;
    document_history_t& history_ref;
    workspace_controller_t& workspace_ref;
    project_identity_t& project_identity_ref;
    application_command_state_t& application_command_state_ref;
    stepmania_export_policy_t& stepmania_export_policy_ref;
    std::optional<audio_offset_drag_t>& audio_offset_drag_ref;
    std::optional<workspace_tab_t>& audio_offset_drag_tab_ref;
    command_registry_t& command_registry_ref;
    std::optional<editor_interaction_state_t>& compose_interaction_state_ref;
    metadata_drop_state_t& metadata_drop_ref;
    runtime::metadata_asset_t& metadata_asset_runtime_ref;
    task_scheduler_t& resource_copy_scheduler_ref;
    runtime::resource_copy_t& resource_copy_runtime_ref;
    runtime::output_group_export_t& export_runtime_ref;
    timing_grid_state_t& timing_grid_ref;
    runtime::timing_edit_t& timing_edit_ref;
    std::optional<timing_lane_pseudonote_preview_t>& timing_stage_ref;
    timing_scene_cache_t& timing_scene_cache_ref;
    compose_scene_cache_t& compose_scene_cache_ref;
    viewport_input_controller_t& compose_viewport_input_ref;
    editor_navigation_input_controller_t& compose_navigation_input_ref;
    native_project_autosave_loop_t& autosave_loop_ref;
    native_project_recovery_controller_t& recovery_controller_ref;
    external_player_runtime_t& external_player_runtime_ref;
    project_chart_runtime_t& project_chart_runtime_ref;
};
} // namespace wormedit::sys
