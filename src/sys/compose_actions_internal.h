#pragma once
#include "sys/application_dispatch_context.h"
#include "sys/application_edit_helpers.h"
#include "sys/bookmark_rename_commands.h"
#include "sys/compose_actions.h"
#include "sys/compose_area_actions.h"
#include "sys/compose_bga_actions.h"
#include "sys/compose_bookmark_commands.h"
#include "sys/compose_label_search.h"
#include "sys/compose_measure_commands.h"
#include "sys/compose_navigation.h"
#include "sys/compose_quantize_request.h"
#include "sys/compose_resource_presenter.h"
#include "sys/compose_scroll_geometry.h"
#include "sys/compose_selection_transform.h"
#include "sys/compose_semantic_layer_commands.h"
#include "sys/compose_sound_actions.h"
#include "sys/recipe_form.h"
#include "sys/transport_player_actions.h"
#include "ui/frontend.h"
#include "wormedit/audio.h"
#include <wormedit/core/bmson_lane_mode.h>
#include <wormedit/core/sv_recipe.h>

#include <unordered_set>
#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/bgm_columns.h>
#include <wormedit/core/bms_branch_editor.h>
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/command.h>
#include <wormedit/core/editor_scene.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/lane_layout_editor.h>
#include <wormedit/core/lane_profile_switch.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/marker_editor.h>
#include <wormedit/core/marker_navigation.h>
#include <wormedit/core/note_label_editor.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/virtual_canvas.h>
#include <wormedit/formats/conversion.h>
#include <wormedit/formats/conversion_publication.h>

#if defined(WORMEDIT_HAS_IMAGE_DECODER)
#include <wormedit/visual/image_decoder.h>
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
#include <wormedit/visual/video_job.h>
#endif
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/decoder.h>
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "sys/audio_fallback.h"

namespace wormedit::sys::compose_action_detail {
struct compose_action_context_t {
    application_dispatch_context_t& environment;
    const WormUiWorkspaceAction& action;
    wormedit::editor_interaction_state_t& state;
    double viewport_height;
    bool metadata_import_busy;
    wormedit::editor_field_scroll_direction_t scroll_direction;
    std::optional<notification_sink_t> notifications;
};

struct compose_feedback_t {
    std::string message;
    notification_severity_t severity;
    notification_source_t source;
};

auto compose_notification_severity(diagnostic_severity_t severity) -> notification_severity_t;

void post_typed_compose_feedback(const notification_sink_t& notifications,
                                 notification_severity_t severity, notification_source_t source,
                                 std::string_view message);

auto publish_compose_feedback(compose_action_context_t& context, std::string message,
                              notification_severity_t severity, notification_source_t source)
    -> std::string;

auto publish_compose_diagnostic(compose_action_context_t& context, const diagnostic_t& diagnostic,
                                notification_source_t source = notification_source_t::edit)
    -> std::string;

auto compose_action_success(compose_action_context_t& context, std::string message,
                            notification_source_t source = notification_source_t::edit)
    -> std::string;

auto compose_action_info(compose_action_context_t& context, std::string message,
                         notification_source_t source = notification_source_t::input)
    -> std::string;

auto compose_action_hint(compose_action_context_t& context, std::string message,
                         notification_source_t source = notification_source_t::input)
    -> std::string;

auto compose_action_warning(compose_action_context_t& context, std::string message,
                            notification_source_t source = notification_source_t::edit)
    -> std::string;

auto compose_action_error(compose_action_context_t& context, std::string message,
                          notification_source_t source = notification_source_t::document)
    -> std::string;

auto compose_mutation_rebind_failure(
    compose_action_context_t& context, const workspace_action_result_t& rebound,
    std::string success, std::string failure,
    notification_source_t source = notification_source_t::edit,
    notification_severity_t severity = notification_severity_t::success) -> std::string;

auto compose_interaction_feedback(compose_action_context_t& context,
                                  const editor_interaction_result_t& result,
                                  std::string_view accepted,
                                  notification_source_t source = notification_source_t::input)
    -> std::string;

auto compose_bookmark_navigation_feedback(compose_action_context_t& context,
                                          const bookmark_command_outcome_t& outcome) -> std::string;

auto dispatch_compose_bookmark_action(compose_action_context_t& context)
    -> std::optional<std::string>;

std::optional<std::string> dispatch_compose_measure_navigation(compose_action_context_t& context);

std::optional<std::string> dispatch_compose_bgm_column_count(compose_action_context_t& context);

std::optional<std::string> dispatch_compose_lane_profile(compose_action_context_t& context);

auto layout_profile_from_id(const wormedit::chart_document_t& chart,
                            wormedit::local_position_t position, std::string_view profile_id)
    -> std::optional<wormedit::lane_layout_t>;

auto layout_with_preserved_lane_ids(const wormedit::chart_document_t& chart,
                                    const wormedit::lane_layout_segment_t* reference,
                                    wormedit::lane_layout_t target)
    -> std::optional<wormedit::lane_layout_t>;

auto initial_input_profile(const wormedit::chart_document_t& chart,
                           const wormedit::lane_layout_t& layout)
    -> std::optional<wormedit::input_profile_t>;

std::optional<std::string> materialize_layout_measure(compose_action_context_t& context,
                                                      const wormedit::chart_document_t*& chart,
                                                      bool& materialized);

std::optional<std::string> dispatch_compose_lane_layout_profile(compose_action_context_t& context);

std::optional<std::string> dispatch_compose_audio_offset(compose_action_context_t& context);

std::string select_compose_tool(compose_action_context_t& context);

auto toggle_compose_scroll_direction(compose_action_context_t& context) -> std::string;

auto reset_compose_view(compose_action_context_t& context) -> std::string;

std::optional<std::string> dispatch_compose_view_action_group(compose_action_context_t& context);

auto stage_output_music(const wormedit::song_document_t& snapshot,
                        const wormedit::output_group_t& group,
                        const std::filesystem::path& asset_base, std::stop_token stop)
    -> std::optional<std::string>;

void report_output_conversion_losses(const wormedit::conversion_result_t& converted,
                                     output_group_export_task_result_t& completed);

auto sliced_bms_destination_error(const wormedit::song_document_t& snapshot,
                                  const wormedit::output_group_t& group,
                                  const wormedit::conversion_request_t& request,
                                  const wormedit::conversion_result_t& converted)
    -> std::optional<std::string>;

void complete_output_group_conversion(const wormedit::song_document_t& snapshot,
                                      const wormedit::output_group_t& group,
                                      wormedit::conversion_request_t& request,
                                      const std::filesystem::path& asset_base, std::stop_token stop,
                                      output_group_export_task_result_t& completed);

void complete_sliced_output_group_conversion(const wormedit::song_document_t& snapshot,
                                             const wormedit::output_group_t& group,
                                             wormedit::conversion_request_t& request,
                                             const std::filesystem::path& asset_base,
                                             std::stop_token stop,
                                             output_group_export_task_result_t& completed);

task_result_t run_output_group_export_task(const wormedit::song_document_t& snapshot,
                                           const wormedit::output_group_t& group,
                                           wormedit::conversion_request_t& request,
                                           const std::filesystem::path& asset_base,
                                           std::stop_token stop);

struct imported_resource_metadata_t {
    std::optional<wormedit::audio_stream_info_t> audio;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> image;
};

auto import_resource_role(const WormUiWorkspaceAction& action)
    -> std::optional<wormedit::resource_role_t>;

auto inspect_imported_resource(const std::filesystem::path& path, wormedit::resource_role_t role)
    -> std::variant<imported_resource_metadata_t, std::string>;

std::optional<std::string> commit_primary_music(compose_action_context_t& context,
                                                const std::filesystem::path& path,
                                                const imported_resource_metadata_t& metadata);

std::optional<std::string> commit_resource_import(compose_action_context_t& context,
                                                  wormedit::resource_role_t role,
                                                  const std::filesystem::path& path,
                                                  imported_resource_metadata_t metadata);

std::optional<std::string> import_resource_asset(compose_action_context_t& context);

std::optional<std::string>
dispatch_compose_import_resource_action(compose_action_context_t& context);

struct resource_copy_plan_t {
    wormedit::asset_id_t asset;
    std::filesystem::path source;
    std::filesystem::path output;
    bool export_copy{};
};

auto prepare_resource_copy(compose_action_context_t& context)
    -> std::variant<resource_copy_plan_t, compose_feedback_t>;

std::optional<std::string> start_resource_copy(compose_action_context_t& context,
                                               resource_copy_plan_t plan);

std::optional<std::string> dispatch_compose_copy_resource_action(compose_action_context_t& context);

struct output_export_plan_t {
    wormedit::output_group_t group;
    wormedit::conversion_request_t request;
};

auto prepare_output_export(compose_action_context_t& context)
    -> std::variant<output_export_plan_t, compose_feedback_t>;

std::optional<std::string> start_output_export(compose_action_context_t& context,
                                               output_export_plan_t plan);

std::optional<std::string>
dispatch_compose_export_output_group_action(compose_action_context_t& context);

auto compose_placement_kind(const WormUiWorkspaceAction& action)
    -> std::optional<wormedit::resource_placement_kind_t>;

auto apply_compose_resource_placement(compose_action_context_t& context,
                                      wormedit::resource_placement_kind_t kind)
    -> wormedit::document_command_result_t;

std::optional<std::string>
finish_compose_resource_placement(compose_action_context_t& context,
                                  wormedit::resource_placement_kind_t kind);

std::optional<std::string>
dispatch_compose_place_resource_action(compose_action_context_t& context);

std::optional<std::string> select_resource_uses(compose_action_context_t& context);

std::optional<std::string> assign_resource_to_selection(compose_action_context_t& context);

std::optional<std::string> create_resource_from_asset(compose_action_context_t& context);

std::string relink_sound_fallback(compose_action_context_t& context);

std::optional<std::string> dispatch_compose_resource_action(compose_action_context_t& context);

std::optional<std::string> assign_bmson_selection(compose_action_context_t& context);

auto selected_bmson_source_event(const compose_action_context_t& context,
                                 wormedit::object_id_t bgm_event)
    -> std::optional<wormedit::object_id_t>;

std::optional<std::string> set_bmson_selection_restart(compose_action_context_t& context);

std::optional<std::string> move_bmson_selection_to_cursor(compose_action_context_t& context);

std::optional<std::string> insert_bmson_marker_at_cursor(compose_action_context_t& context);

std::optional<std::string> stage_bmson_marker_at_cursor(compose_action_context_t& context);

std::optional<std::string> commit_bmson_marker_batch(compose_action_context_t& context);

std::optional<std::string> delete_bmson_selected_marker(compose_action_context_t& context);

auto lane_mode_channel(const compose_action_context_t& context) -> wormedit::object_id_t;

std::optional<std::string> change_bmson_lane_mode(compose_action_context_t& context);

std::optional<std::string> dispatch_compose_bmson_action(compose_action_context_t& context);

struct beat_section_edit_t {
    std::optional<wormedit::marker_edit_action_t> edit;
    std::string error;
    bool from_selection{};
};

auto finish_marker_edit_feedback(compose_action_context_t& context,
                                 const wormedit::document_command_result_t& applied,
                                 std::string success, std::string unchanged,
                                 std::string rebind_failure,
                                 notification_source_t source = notification_source_t::edit)
    -> std::string;

beat_section_edit_t make_beat_section_insertion(compose_action_context_t& context,
                                                const wormedit::chart_document_t& chart);

auto make_existing_beat_section_edit(compose_action_context_t& context,
                                     const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string>;

std::string beat_section_success(const WormUiWorkspaceAction& action, bool from_selection);

std::optional<std::string> edit_beat_section(compose_action_context_t& context);

std::optional<std::string> dispatch_compose_beat_section_action(compose_action_context_t& context);

auto make_audio_bookmark_edit(compose_action_context_t& context,
                              const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string>;

std::optional<std::string> edit_audio_bookmark(compose_action_context_t& context);

std::optional<std::string>
dispatch_compose_audio_bookmark_action(compose_action_context_t& context);

auto make_audio_section_edit(compose_action_context_t& context,
                             const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string>;

std::optional<std::string> edit_audio_section(compose_action_context_t& context);

std::optional<std::string> dispatch_compose_audio_section_action(compose_action_context_t& context);

auto make_beat_bookmark_edit(compose_action_context_t& context,
                             const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string>;

std::optional<std::string> edit_beat_bookmark(compose_action_context_t& context);

std::optional<std::string> dispatch_compose_beat_bookmark_action(compose_action_context_t& context);

std::optional<std::string> edit_selected_note_labels(compose_action_context_t& context);

std::string navigate_audio_marker(compose_action_context_t& context);

std::string search_selected_note_labels(compose_action_context_t& context);

auto selection_transform_for(WormUiComposeActionKind kind) -> std::optional<selection_transform_t>;

std::string transform_selected_notes(compose_action_context_t& context,
                                     selection_transform_t transform);

std::string edit_cursor_measure(compose_action_context_t& context,
                                cursor_measure_command_t command);

struct recipe_form_target_t {
    const chart_document_t* chart{};
    const sv_recipe_t* recipe{};
};

auto recipe_form_target(compose_action_context_t& context) -> std::optional<recipe_form_target_t>;

std::string preview_recipe_form_action(compose_action_context_t& context);

std::string apply_recipe_form_action(compose_action_context_t& context);

std::string set_cursor_measure_meter(compose_action_context_t& context);

std::string set_measures_meter(compose_action_context_t& context);

std::string move_bar_line(compose_action_context_t& context);

std::optional<std::string> dispatch_compose_marker_action(compose_action_context_t& context);

auto catalog_semantic_layer_by_id(const wormedit::chart_document_t& chart, wormedit::layer_id_t id)
    -> const wormedit::semantic_layer_t*;

std::optional<std::string> assign_compose_layer_target(compose_action_context_t& context, bool none,
                                                       wormedit::layer_id_t target);

std::optional<std::string> add_selected_compose_sounds_to_layer(compose_action_context_t& context);

std::optional<std::string> dispatch_compose_note_menu_action(compose_action_context_t& context);

std::optional<std::string> choose_no_semantic_layer(compose_action_context_t& context);

std::optional<std::string>
dispatch_semantic_layer_palette_action(compose_action_context_t& context);

std::optional<std::string> semantic_layer_edit_status(compose_action_context_t& context,
                                                      wormedit::semantic_layer_edit_action_t edit,
                                                      std::string_view success,
                                                      std::string_view unchanged,
                                                      std::string_view rebind_failure);

std::optional<std::string> add_semantic_layer(compose_action_context_t& context);

auto selected_catalog_layer(compose_action_context_t& context)
    -> std::pair<const wormedit::chart_document_t*, const wormedit::semantic_layer_t*>;

std::optional<std::string> update_semantic_layer_catalog_entry(compose_action_context_t& context);

std::optional<std::string> delete_semantic_layer_catalog_entry(compose_action_context_t& context);

std::optional<std::string> move_semantic_layer_catalog_entry(compose_action_context_t& context);

std::optional<std::string>
dispatch_compose_semantic_layer_catalog_action(compose_action_context_t& context);

void clear_semantic_layer_selection(application_command_state_t& app, bool clear_modes);

auto target_semantic_layer_action_row(compose_action_context_t& context) -> bool;

auto semantic_layer_menu_action(WormUiComposeActionKind kind) -> bool;

auto locked_semantic_layer_menu_action(WormUiComposeActionKind kind) -> bool;

auto target_captured_semantic_layer_action(compose_action_context_t& context)
    -> std::optional<std::string>;

auto missing_semantic_layer_toggle_target(compose_action_context_t& context, bool toggle_lock)
    -> std::optional<std::string>;

auto semantic_layer_toggle_status(bool toggle_lock, bool previous) -> std::string;

std::optional<std::string>
toggle_semantic_layer_visibility_or_lock(compose_action_context_t& context, bool toggle_lock);

std::optional<std::string> toggle_semantic_layer_filter(compose_action_context_t& context);

std::optional<std::string> toggle_semantic_layer_paint_mode(compose_action_context_t& context);

std::optional<std::string> toggle_semantic_layer_solo(compose_action_context_t& context);

std::optional<std::string> select_semantic_layer_notes(compose_action_context_t& context);

std::optional<std::string> edit_semantic_layer_membership(compose_action_context_t& context);

auto toggle_layer_assist_tick(compose_action_context_t& context) -> std::string;

std::optional<std::string>
dispatch_compose_semantic_layer_state_action(compose_action_context_t& context);

std::string dispatch_bms_branch_choice_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    const std::optional<wormedit::editor_interaction_state_t>& interaction_state,
    notification_sink_t notifications);

struct bms_statement_target_t {
    const wormedit::bms_control_node_t* statement{};
    const wormedit::bms_control_node_t* leaf{};
};

auto bms_statement_leaf(wormedit::bms_control_kind_t kind) -> bool;

template <typename diagnostic_t_>
auto publish_bms_diagnostic(notification_sink_t notifications, const diagnostic_t_& diagnostic,
                            notification_source_t source = notification_source_t::format)
    -> std::string {
    notifications.post(compose_notification_severity(diagnostic.severity), source,
                       diagnostic.message);
    return diagnostic.message;
}

template <typename result_t_>
auto publish_bms_statement_diagnostic(notification_sink_t notifications, const result_t_& result)
    -> std::string {
    if (!result.command.diagnostics.empty())
        return publish_bms_diagnostic(notifications, result.command.diagnostics.front());
    if (!result.format_diagnostics.empty())
        return publish_bms_diagnostic(notifications, result.format_diagnostics.front());
    const std::string message{
        "BMS branch statement edit was rejected without changing the document."};
    notifications.post(notification_severity_t::warning, notification_source_t::format, message);
    return message;
}

void find_bms_statement_target(const wormedit::bms_control_node_t& node, std::uint64_t id,
                               const wormedit::bms_control_node_t* enclosing_leaf,
                               bms_statement_target_t& result);

void retain_bms_statement_feedback(application_command_state_t& application,
                                   std::optional<wormedit::chart_id_t> chart,
                                   std::uint64_t revision, const std::string& message);

std::string dispatch_bms_branch_statement_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    const std::optional<wormedit::editor_interaction_state_t>& interaction_state,
    notification_sink_t notifications);

struct bms_leaf_target_t {
    const wormedit::bms_control_node_t* block{};
    const wormedit::bms_control_node_t* leaf{};
};

void find_bms_leaf_target(const wormedit::bms_control_node_t& node, std::uint64_t id,
                          bms_leaf_target_t& result);

auto find_bms_control_node(const wormedit::bms_control_node_t& node, std::uint64_t id)
    -> const wormedit::bms_control_node_t*;

struct bms_block_outcome_t {
    wormedit::document_command_result_t result;
    std::string message; // success text, or the reason nothing was attempted
    bool attempted{true};
};

auto run_bms_block_command(const WormUiWorkspaceAction& action,
                           wormedit::document_history_t& history,
                           const wormedit::chart_document_t& chart, wormedit::chart_id_t chart_id)
    -> bms_block_outcome_t;

std::string dispatch_bms_branch_block_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    const std::optional<wormedit::editor_interaction_state_t>& interaction_state,
    notification_sink_t notifications);

auto quantize_settings_valid(const wormedit::quantize_request_t& request) -> bool;

auto quantize_editor_for(compose_action_context_t& context)
    -> application_command_state_t::quantize_editor_t&;

void share_quantize_grid_with_snap(compose_action_context_t& context, std::uint64_t divisions);

auto apply_quantize_settings_from_action(compose_action_context_t& context)
    -> std::optional<std::string>;

auto select_quantize_preset(compose_action_context_t& context) -> std::string;

auto validate_quantize_preset_save(const compose_action_context_t& context, std::size_t name_length,
                                   const std::optional<wormedit::quantize_request_t>& request)
    -> std::optional<std::string>;

auto save_quantize_preset(compose_action_context_t& context) -> std::string;

auto delete_quantize_preset(compose_action_context_t& context) -> std::string;

auto rename_quantize_preset(compose_action_context_t& context) -> std::string;

auto set_edit_snap_from_badge(compose_action_context_t& context, std::uint64_t divisions)
    -> std::string;

auto selected_snap_positions(const chart_document_t& chart, const editor_interaction_state_t& state)
    -> std::vector<local_position_t>;

auto find_selection_snap(compose_action_context_t& context) -> std::string;

auto dispatch_quantize_preset_action(compose_action_context_t& context)
    -> std::optional<std::string>;

auto dispatch_compose_area_with_quantize_report(const WormUiWorkspaceAction& action,
                                                wormedit::document_history_t& history,
                                                wormedit::editor_interaction_state_t& state,
                                                application_command_state_t& application_state,
                                                std::optional<notification_sink_t> notifications)
    -> std::optional<std::string>;
} // namespace wormedit::sys::compose_action_detail
