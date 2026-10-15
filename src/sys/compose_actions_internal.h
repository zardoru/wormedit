#pragma once
#include "sys/application_edit_helpers.h"
#include "sys/application_edit_inputs.h"
#include "sys/bookmark_commands.h"
#include "sys/compose_action_context.h"
#include "sys/compose_action_outcome.h"
#include "sys/compose_actions.h"
#include "sys/compose_area_actions.h"
#include "sys/compose_bga_actions.h"
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

auto dispatch_compose_document_action_group(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t>;

auto layout_profile_from_id(const wormedit::chart_document_t& chart,
                            wormedit::local_position_t position, std::string_view profile_id)
    -> std::optional<wormedit::lane_layout_t>;

auto layout_with_preserved_lane_ids(const wormedit::chart_document_t& chart,
                                    const wormedit::lane_layout_segment_t* reference,
                                    wormedit::lane_layout_t target)
    -> std::optional<wormedit::lane_layout_t>;

std::optional<compose_action_outcome_t>
materialize_layout_measure(compose_action_context_t& context,
                           const wormedit::chart_document_t*& chart, bool& materialized);

std::optional<compose_action_outcome_t>
dispatch_compose_view_action_group(compose_action_context_t& context);

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

std::optional<compose_action_outcome_t> commit_primary_music(compose_action_context_t& context,
                                                const std::filesystem::path& path,
                                                const imported_resource_metadata_t& metadata);

std::optional<compose_action_outcome_t> commit_resource_import(compose_action_context_t& context,
                                                  wormedit::resource_role_t role,
                                                  const std::filesystem::path& path,
                                                  imported_resource_metadata_t metadata);

std::optional<compose_action_outcome_t> import_resource_asset(compose_action_context_t& context);

std::optional<compose_action_outcome_t>
dispatch_compose_import_resource_action(compose_action_context_t& context);

struct resource_copy_plan_t {
    wormedit::asset_id_t asset;
    std::filesystem::path source;
    std::filesystem::path output;
    bool export_copy{};
};

auto prepare_resource_copy(compose_action_context_t& context)
    -> std::variant<resource_copy_plan_t, compose_action_outcome_t>;

std::optional<compose_action_outcome_t> start_resource_copy(compose_action_context_t& context,
                                               resource_copy_plan_t plan);

std::optional<compose_action_outcome_t> dispatch_compose_copy_resource_action(compose_action_context_t& context);

struct output_export_plan_t {
    wormedit::output_group_t group;
    wormedit::conversion_request_t request;
};

auto prepare_output_export(compose_action_context_t& context)
    -> std::variant<output_export_plan_t, compose_action_outcome_t>;

std::optional<compose_action_outcome_t> start_output_export(compose_action_context_t& context,
                                               output_export_plan_t plan);

std::optional<compose_action_outcome_t>
dispatch_compose_export_output_group_action(compose_action_context_t& context);

auto compose_placement_kind(const WormUiWorkspaceAction& action)
    -> std::optional<wormedit::resource_placement_kind_t>;

auto apply_compose_resource_placement(compose_action_context_t& context,
                                      wormedit::resource_placement_kind_t kind)
    -> wormedit::document_command_result_t;

std::optional<compose_action_outcome_t>
finish_compose_resource_placement(compose_action_context_t& context,
                                  wormedit::resource_placement_kind_t kind);

std::optional<compose_action_outcome_t>
dispatch_compose_place_resource_action(compose_action_context_t& context);

std::optional<compose_action_outcome_t> select_resource_uses(compose_action_context_t& context);

std::optional<compose_action_outcome_t> assign_resource_to_selection(compose_action_context_t& context);

std::optional<compose_action_outcome_t> create_resource_from_asset(compose_action_context_t& context);

compose_action_outcome_t relink_sound_fallback(compose_action_context_t& context);

std::optional<compose_action_outcome_t> dispatch_compose_resource_action(compose_action_context_t& context);

auto selected_bmson_source_event(const compose_action_context_t& context,
                                 wormedit::object_id_t bgm_event)
    -> std::optional<wormedit::object_id_t>;

auto lane_mode_channel(const compose_action_context_t& context) -> wormedit::object_id_t;

std::optional<compose_action_outcome_t>
dispatch_compose_bmson_action(compose_action_context_t& context);

struct beat_section_edit_t {
    std::optional<wormedit::marker_edit_action_t> edit;
    std::string error;
    bool from_selection{};
};

beat_section_edit_t make_beat_section_insertion(compose_action_context_t& context,
                                                const wormedit::chart_document_t& chart);

auto make_existing_beat_section_edit(compose_action_context_t& context,
                                     const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string>;

std::string_view beat_section_success(const WormUiWorkspaceAction& action, bool from_selection);

struct edit_beat_section_action_t {
    const chart_document_t* chart{};
    bool adding{};
    auto operator==(const edit_beat_section_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<edit_beat_section_action_t, compose_action_context_t>);

std::optional<compose_action_outcome_t>
dispatch_compose_beat_section_action(compose_action_context_t& context);

auto make_audio_bookmark_edit(compose_action_context_t& context,
                              const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string>;

struct edit_audio_bookmark_action_t {
    const chart_document_t* chart{};
    auto operator==(const edit_audio_bookmark_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<edit_audio_bookmark_action_t, compose_action_context_t>);

std::optional<compose_action_outcome_t>
dispatch_compose_audio_bookmark_action(compose_action_context_t& context);

auto make_audio_section_edit(compose_action_context_t& context,
                             const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string>;

struct edit_audio_section_action_t {
    const chart_document_t* chart{};
    auto operator==(const edit_audio_section_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<edit_audio_section_action_t, compose_action_context_t>);

std::optional<compose_action_outcome_t>
dispatch_compose_audio_section_action(compose_action_context_t& context);

auto make_beat_bookmark_edit(compose_action_context_t& context,
                             const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string>;

struct edit_beat_bookmark_action_t {
    const chart_document_t* chart{};
    WormUiComposeActionKind operation{};
    auto operator==(const edit_beat_bookmark_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<edit_beat_bookmark_action_t, compose_action_context_t>);

std::optional<compose_action_outcome_t>
dispatch_compose_beat_bookmark_action(compose_action_context_t& context);

struct edit_selected_note_labels_action_t {
    auto operator==(const edit_selected_note_labels_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<edit_selected_note_labels_action_t, compose_action_context_t>);

struct navigate_audio_marker_action_t {
    std::optional<std::int64_t> cursor;
    auto operator==(const navigate_audio_marker_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<navigate_audio_marker_action_t, compose_action_context_t>);

struct search_selected_note_labels_action_t {
    auto operator==(const search_selected_note_labels_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<search_selected_note_labels_action_t, compose_action_context_t>);

auto selection_transform_for(WormUiComposeActionKind kind) -> std::optional<selection_transform_t>;

struct transform_selected_notes_action_t {
    selection_transform_t transform;
    auto operator==(const transform_selected_notes_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<transform_selected_notes_action_t, compose_action_context_t>);

struct edit_cursor_measure_action_t {
    cursor_measure_command_t command;
    auto operator==(const edit_cursor_measure_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<edit_cursor_measure_action_t, compose_action_context_t>);

struct recipe_form_target_t {
    const chart_document_t* chart{};
    const sv_recipe_t* recipe{};
    auto operator==(const recipe_form_target_t&) const -> bool = default;
};

auto recipe_form_target(compose_action_context_t& context) -> std::optional<recipe_form_target_t>;

struct propose_recipe_form_action_t {
    std::optional<recipe_form_target_t> target;
    auto operator==(const propose_recipe_form_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<propose_recipe_form_action_t, compose_action_context_t>);

struct apply_recipe_form_action_t {
    std::optional<recipe_form_target_t> target;
    auto operator==(const apply_recipe_form_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<apply_recipe_form_action_t, compose_action_context_t>);

struct clear_recipe_form_preview_action_t {
    auto operator==(const clear_recipe_form_preview_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<clear_recipe_form_preview_action_t, compose_action_context_t>);

struct set_cursor_measure_meter_action_t {
    auto operator==(const set_cursor_measure_meter_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<set_cursor_measure_meter_action_t, compose_action_context_t>);

struct set_measures_meter_action_t {
    auto operator==(const set_measures_meter_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<set_measures_meter_action_t, compose_action_context_t>);

struct move_compose_bar_line_action_t {
    auto operator==(const move_compose_bar_line_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<move_compose_bar_line_action_t, compose_action_context_t>);

std::optional<compose_action_outcome_t>
dispatch_compose_marker_action(compose_action_context_t& context);

auto catalog_semantic_layer_by_id(const wormedit::chart_document_t& chart, wormedit::layer_id_t id)
    -> const wormedit::semantic_layer_t*;

std::optional<compose_action_outcome_t>
dispatch_compose_note_menu_action(compose_action_context_t& context);

std::optional<compose_action_outcome_t>
dispatch_semantic_layer_palette_action(compose_action_context_t& context);

compose_action_outcome_t
semantic_layer_edit_status(compose_action_context_t& context,
                           wormedit::semantic_layer_edit_action_t edit, std::string_view success,
                           std::string_view unchanged, std::string_view rebind_failure);

auto selected_catalog_layer(compose_action_context_t& context)
    -> std::pair<const wormedit::chart_document_t*, const wormedit::semantic_layer_t*>;

std::optional<compose_action_outcome_t>
dispatch_compose_semantic_layer_catalog_action(compose_action_context_t& context);

void clear_semantic_layer_selection(application_command_state_t& app, bool clear_modes);

auto target_semantic_layer_action_row(compose_action_context_t& context) -> bool;

auto semantic_layer_menu_action(WormUiComposeActionKind kind) -> bool;

auto locked_semantic_layer_menu_action(WormUiComposeActionKind kind) -> bool;

auto target_captured_semantic_layer_action(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t>;

auto missing_semantic_layer_toggle_target(compose_action_context_t& context, bool toggle_lock)
    -> std::optional<compose_action_outcome_t>;

auto semantic_layer_toggle_status(bool toggle_lock, bool previous) -> std::string;

std::optional<compose_action_outcome_t>
dispatch_compose_semantic_layer_state_action(compose_action_context_t& context);

auto dispatch_bms_branch_choice_action(compose_action_context_t& context)
    -> compose_action_outcome_t;

struct bms_statement_target_t {
    const wormedit::bms_control_node_t* statement{};
    const wormedit::bms_control_node_t* leaf{};
};

void find_bms_statement_target(const wormedit::bms_control_node_t& node, std::uint64_t id,
                               const wormedit::bms_control_node_t* enclosing_leaf,
                               bms_statement_target_t& result);

auto dispatch_bms_branch_statement_action(compose_action_context_t& context)
    -> compose_action_outcome_t;

struct bms_leaf_target_t {
    const wormedit::bms_control_node_t* block{};
    const wormedit::bms_control_node_t* leaf{};
};

void find_bms_leaf_target(const wormedit::bms_control_node_t& node, std::uint64_t id,
                          bms_leaf_target_t& result);

auto find_bms_control_node(const wormedit::bms_control_node_t& node, std::uint64_t id)
    -> const wormedit::bms_control_node_t*;

auto run_bms_block_command(const WormUiWorkspaceAction& action,
                           wormedit::document_history_t& history,
                           const wormedit::chart_document_t& chart, wormedit::chart_id_t chart_id)
    -> compose_action_outcome_t;

auto dispatch_bms_branch_block_action(compose_action_context_t& context)
    -> compose_action_outcome_t;

auto dispatch_compose_bms_action(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t>;

auto quantize_settings_valid(const wormedit::quantize_request_t& request) -> bool;

auto quantize_editor_for(compose_action_context_t& context)
    -> application_command_state_t::quantize_editor_t&;

void share_quantize_grid_with_snap(compose_action_context_t& context, std::uint64_t divisions);

auto apply_quantize_settings_from_action(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t>;

auto select_quantize_preset(compose_action_context_t& context) -> compose_action_outcome_t;

auto validate_quantize_preset_save(const compose_action_context_t& context, std::size_t name_length,
                                   const std::optional<wormedit::quantize_request_t>& request)
    -> std::optional<std::string>;

auto save_quantize_preset(compose_action_context_t& context) -> compose_action_outcome_t;

auto delete_quantize_preset(compose_action_context_t& context) -> compose_action_outcome_t;

auto rename_quantize_preset(compose_action_context_t& context) -> compose_action_outcome_t;

auto set_edit_snap_from_badge(compose_action_context_t& context, std::uint64_t divisions)
    -> compose_action_outcome_t;

auto selected_snap_positions(const chart_document_t& chart, const editor_interaction_state_t& state)
    -> std::vector<local_position_t>;

auto find_selection_snap(compose_action_context_t& context) -> compose_action_outcome_t;

auto dispatch_quantize_preset_action(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t>;

auto dispatch_compose_area_with_quantize_report(compose_action_context_t& context)
    -> std::optional<std::string>;
} // namespace wormedit::sys::compose_action_detail
