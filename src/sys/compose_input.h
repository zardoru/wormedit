#pragma once

#include "sys/application_edit_inputs.h"

#include "sys/compose_view.h"

#include <wormedit/core/editor_navigation_input.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/viewport_input.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace wormedit::sys {

auto compose_result_status(const wormedit::editor_interaction_result_t& result,
                           std::string_view accepted) -> std::string;
auto dispatch_compose_navigation_actions(const wormedit::editor_navigation_action_batch_t& actions,
                                         wormedit::document_history_t& history,
                                         wormedit::editor_interaction_state_t& state,
                                         application_command_state_t* application = nullptr,
                                         const wormedit::notification_sink_t* notifications = nullptr)
    -> std::string;
auto stage_bmson_bgm_marker(const wormedit::document_history_t& history,
                            const wormedit::editor_interaction_state_t& state,
                            application_command_state_t& application_state,
                            wormedit::object_id_t reference, wormedit::local_position_t position,
                            bool restart, wormedit::notification_severity_t* severity = nullptr) -> std::string;
// Commit frontend geometry after routing input against the displayed frame.
void refresh_compose_editor_width(const WormUiWorkspaceAction& action,
    const wormedit::document_history_t& history, const wormedit::workspace_controller_t& workspace,
    application_command_state_t& application);
// Consumes runtime mix controls even for BGM-only charts without editor interaction state.
auto dispatch_compose_bgm_mix_inputs(WormUiWorkspaceAction& action,
    const wormedit::document_history_t& history, const wormedit::workspace_controller_t& workspace,
    application_command_state_t& application,
    const wormedit::notification_sink_t* notifications = nullptr) -> std::string;
auto dispatch_compose_input_action(WormUiWorkspaceAction& ui_action,
                                   const compose_input_inputs_t& environment,
                                   wormedit::editor_interaction_state_t& state,
                                   double viewport_delta_seconds, bool post_notifications = true)
    -> std::string;
auto selected_bmson_waveform_source(
    compose_scene_cache_t& cache, const wormedit::chart_document_t& chart,
    std::uint64_t revision, wormedit::object_id_t bgm_event)
    -> std::optional<compose_scene_cache_t::channel_waveform_selection_t>;
auto selected_chart(const wormedit::document_history_t& history,
                    const wormedit::workspace_controller_t& workspace)
    -> const wormedit::chart_document_t*;
auto visible_semantic_layer_by_id(const wormedit::chart_document_t& chart,
                                  wormedit::layer_id_t id)
    -> const wormedit::semantic_layer_t*;

} // namespace wormedit::sys
