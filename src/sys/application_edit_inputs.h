#pragma once

#include "sys/compose_view.h"
#include "sys/metadata_asset_runtime.h"
#include "sys/metadata_drop_state.h"
#include "sys/project_open.h"
#include "sys/resource_job_state.h"
#include <wormedit/core/editor_navigation_input.h>
#include <wormedit/core/viewport_input.h>

namespace wormedit::sys {
// Each bundle borrows one coherent operation's inputs. None may escape a synchronous call.
struct document_edit_inputs_t {
    document_history_t& history;
    workspace_controller_t& workspace;
    notification_sink_t& notifications;
};
struct command_editor_inputs_t {
    application_command_state_t& commands;
    std::optional<editor_interaction_state_t>& retained;
    std::optional<timing_lane_pseudonote_preview_t>& timing_stage;
};
struct command_dispatch_inputs_t {
    document_edit_inputs_t document;
    command_editor_inputs_t editor;
};
struct compose_editor_inputs_t {
    application_command_state_t& commands;
    std::optional<editor_interaction_state_t>& retained;
};
struct resource_job_inputs_t {
    task_scheduler_t& scheduler;
    runtime::resource_copy_t& copy;
    runtime::output_group_export_t& exporter;
};
struct metadata_resource_inputs_t {
    metadata_drop_state_t& drop;
    runtime::metadata_asset_t& runtime;
};
struct compose_resource_inputs_t {
    const std::filesystem::path& asset_base;
    resource_job_inputs_t jobs;
    metadata_resource_inputs_t metadata;
};
struct compose_action_inputs_t {
    document_edit_inputs_t document;
    compose_editor_inputs_t editor;
    compose_resource_inputs_t resources;
};
struct compose_input_controls_t {
    compose_scene_cache_t& cache;
    viewport_input_controller_t& viewport;
    editor_navigation_input_controller_t& navigation;
};
struct compose_input_inputs_t {
    document_edit_inputs_t document;
    application_command_state_t& commands;
    compose_input_controls_t controls;
};
struct metadata_import_inputs_t {
    metadata_drop_state_t& drop;
    runtime::metadata_asset_t& runtime;
    task_scheduler_t& scheduler;
};
struct metadata_action_inputs_t {
    document_edit_inputs_t document;
    metadata_import_inputs_t import;
    const std::filesystem::path& asset_base;
};
struct frontend_event_inputs_t {
    document_edit_inputs_t document;
    metadata_drop_state_t& metadata_drop;
    project_identity_t& project;
};
} // namespace wormedit::sys
