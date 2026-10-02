#include "sys/compose_actions.h"
#include "sys/compose_label_search.h"
#include "sys/compose_measure_commands.h"
#include "sys/compose_selection_transform.h"
#include "sys/compose_area_actions.h"
#include "sys/application_edit_helpers.h"
#include "sys/compose_resource_presenter.h"
#include "sys/compose_scroll_geometry.h"
#include "sys/transport_player_actions.h"
#include "ui/frontend.h"
#include "wormedit/audio.h"

#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/bgm_columns.h>
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/bms_branch_editor.h>
#include <wormedit/core/command.h>
#include <wormedit/core/editor_scene.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/lane_profile_switch.h>
#include <wormedit/core/marker_editor.h>
#include <wormedit/core/marker_navigation.h>
#include <wormedit/core/note_label_editor.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/semantic_layer_editor.h>
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

namespace wormedit::sys {

namespace {

struct compose_action_context_t {
    const WormUiWorkspaceAction& action;
    wormedit::document_history_t& history;
    wormedit::workspace_controller_t& workspace;
    wormedit::editor_interaction_state_t& state;
    double viewport_height;
    application_command_state_t& application_state;
    const std::filesystem::path& asset_base;
    wormedit::task_scheduler_t& resource_copy_scheduler;
    runtime::resource_copy_t& resource_copy_runtime;
    runtime::output_group_export_t& export_runtime;
    bool metadata_import_busy;
    wormedit::editor_field_scroll_direction_t scroll_direction;
};

std::optional<std::string> dispatch_compose_measure_navigation(compose_action_context_t& context) {
    using namespace wormedit;
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_GO_TO_MEASURE)
        return std::nullopt;
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before going to a measure.";
    const auto chart = std::ranges::find(context.history.document.charts, context.state.chart,
                                         &chart_document_t::id);
    if (chart == context.history.document.charts.end())
        return "The selected difficulty is no longer available.";
    const auto number = context.action.compose_measure_number;
    editor_interaction_result_t result;
    if (chart_has_virtual_tail(*chart)) {
        // BMS measures are numbered from #000; other charts count from 1.
        const auto is_bms = chart->source_family == format_family_t::bms;
        if (!is_bms && number == 0)
            return "The requested measure is outside this chart.";
        const auto canvas_number = is_bms ? number : number - 1;
        if (canvas_number >= virtual_tail_measure_limit(*chart))
            return is_bms ? "The requested BMS measure is outside #000-#999."
                          : "The requested measure is outside this chart.";
        // Reuse virtual-canvas navigation without changing the current selection.
        auto candidate = context.state;
        result = dispatch_editor_action(context.history, candidate,
                                         select_bms_canvas_measure_action_t{
                                             static_cast<std::size_t>(canvas_number)});
        if (result.accepted) {
            restore_cursor_pose(context.state, capture_cursor_pose(candidate));
            context.state.active_lane = candidate.active_lane;
        }
    } else {
        if (number == 0 || number > chart->measures.size())
            return "The requested measure is outside this chart.";
        result = dispatch_editor_action(context.history, context.state,
            jump_edit_cursor_action_t{{chart->measures[static_cast<std::size_t>(number - 1)].id, 0},
                                     context.history.document.revision});
    }
    return compose_result_status(result, "Compose cursor moved to the requested measure.");
}

std::optional<std::string> dispatch_compose_bgm_column_count(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_SET_BGM_COLUMN_COUNT)
        return std::nullopt;
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before changing BGM columns.";
    const auto applied = wormedit::apply_bgm_column_count(
        context.history, context.action.compose_input_document_revision, context.state.chart,
        context.action.compose_bgm_column_count);
    if (!applied.applied)
        return applied.diagnostics.empty() ? "BGM column count unchanged."
                                           : applied.diagnostics.front().message;
    const auto rebound = wormedit::rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "BGM columns changed, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    return "BGM column count set to " + std::to_string(context.action.compose_bgm_column_count) +
           ".";
}

std::optional<std::string> dispatch_compose_lane_profile(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE)
        return std::nullopt;
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before changing the lane profile.";
    const auto* chart = selected_chart(context.history, context.workspace);
    const auto* segment = chart ? wormedit::active_lane_layout_segment(*chart, context.state.cursor)
                                : nullptr;
    if (!segment) return "No lane layout is active at the cursor.";
    const auto choices = wormedit::lane_profile_choices(segment->layout.lanes.size());
    if (context.action.compose_lane_profile_index >= choices.size())
        return "Choose a lane profile first.";
    const auto& target = choices[context.action.compose_lane_profile_index].layout;
    const auto applied = wormedit::apply_lane_profile_switch(
        context.history, context.action.compose_input_document_revision, context.state.chart,
        context.state.cursor, target);
    if (!applied.applied)
        return applied.diagnostics.empty() ? "Lane profile unchanged."
                                           : applied.diagnostics.front().message;
    const auto rebound = wormedit::rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "Lane profile changed, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    return "Lane profile set to " + target.display_name + ".";
}

std::optional<std::string> dispatch_compose_audio_offset(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_APPLY_AUDIO_OFFSET)
        return std::nullopt;
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before editing audio offset.";
    const auto applied = wormedit::apply_audio_offset_edit(context.history,
        context.action.compose_input_document_revision, context.state.chart,
        {context.action.compose_audio_offset_numerator, context.action.compose_audio_offset_denominator},
        context.application_state.audio_offset_edits_locked());
    if (!applied.applied)
        return applied.diagnostics.empty() ? "Audio offset unchanged." : applied.diagnostics.front().message;
    const auto rebound = wormedit::rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "Audio offset changed, but workspace rebinding failed."
                                          : rebound.diagnostics.front().message;
    return "Audio offset changed.";
}

std::optional<std::string> dispatch_compose_view_action_group(compose_action_context_t& context) {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    auto& state = context.state;
    auto& viewport_height = context.viewport_height;
    auto& application_state = context.application_state;
    auto& scroll_direction = context.scroll_direction;
    editor_interaction_result_t result;
    std::string status;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ZOOM_IN:
        result = dispatch_editor_action(
            history, state,
            adjust_editor_zoom_action_t{.factor = 1.25, .anchor_screen_y_pixels = std::nullopt});
        status = "Compose field zoomed in.";
        break;
    case WORM_UI_COMPOSE_ACTION_ZOOM_OUT:
        result = dispatch_editor_action(
            history, state,
            adjust_editor_zoom_action_t{.factor = 1.0 / 1.25,
                                        .anchor_screen_y_pixels = std::nullopt});
        status = "Compose field zoomed out.";
        break;
    case WORM_UI_COMPOSE_ACTION_SCROLL_UP:
    case WORM_UI_COMPOSE_ACTION_SCROLL_DOWN: {
        const auto direction =
            action.compose_action == WORM_UI_COMPOSE_ACTION_SCROLL_UP ? -1.0 : 1.0;
        result = dispatch_editor_action(
            history, state,
            scroll_editor_view_action_t{direction * std::max(64.0, viewport_height * 0.75),
                                        compose_cursor_screen_anchor(
                                            viewport_height,
                                            scroll_direction == editor_field_scroll_direction_t::down),
                                        scroll_direction});
        status = direction < 0.0 ? "Compose field scrolled up." : "Compose field scrolled down.";
        break;
    }
    case WORM_UI_COMPOSE_ACTION_RESET_VIEW: {
        const auto factor = default_pixels_per_standard_measure / state.pixels_per_standard_measure;
        result = dispatch_editor_action(
            history, state,
            adjust_editor_zoom_action_t{.factor = factor, .anchor_screen_y_pixels = std::nullopt});
        if (result) {
            result = dispatch_editor_action(
                history, state, scroll_editor_view_action_t{-state.viewport_scroll_pixels});
        }
        status = "Compose field view reset.";
        break;
    }
    case WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE:
        if (!wormedit::ui::compose_notes_editable(compose_mode_of(application_state)))
            return "Note painting is unavailable in Timing focus.";
        if (editor_interaction_has_pending_gesture(state)) {
            result = dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
            if (!result.accepted) {
                return compose_result_status(result, "Compose gesture canceled.");
            }
        }
        application_state.note_paint_mode_active = !application_state.note_paint_mode_active;
        if (application_state.note_paint_mode_active) {
            application_state.semantic_layer_paint_mode_active = false;
        }
        status = application_state.note_paint_mode_active ? "Note-paint mode enabled."
                                                          : "Note-paint mode disabled.";
        return status;
    case WORM_UI_COMPOSE_ACTION_RATE_SLOWER:
    case WORM_UI_COMPOSE_ACTION_RATE_FASTER: {
        const auto stepped = step_transport_rate_status(
            application_state, action.compose_action == WORM_UI_COMPOSE_ACTION_RATE_FASTER);
        return stepped ? *stepped : "Playback output rejected the rate change.";
    }
    case WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_FOCUS: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before changing the Compose mode.";
        }
        application_state.compose_timing_focus = !application_state.compose_timing_focus;
        if (application_state.compose_timing_focus) {
            // Timing focus never edits notes, so leave any note tool.
            application_state.note_paint_mode_active = false;
            application_state.semantic_layer_paint_mode_active = false;
        }
        return application_state.compose_timing_focus ? "Timing focus on."
                                                      : "Timing focus off.";
    }
    case WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_LANE_VISIBILITY: {
        if (action.compose_timing_lane >= compose_timing_lane_count) {
            return "The requested timing lane does not exist.";
        }
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before changing timing lanes.";
        }
        const auto bit = static_cast<std::uint8_t>(1U << action.compose_timing_lane);
        application_state.compose_timing_lane_visibility_mask ^= bit;
        return (application_state.compose_timing_lane_visibility_mask & bit) != 0
                   ? "Compose timing lane shown."
                   : "Compose timing lane hidden.";
    }
    default:
        return std::nullopt;
    }
    return compose_result_status(result, status);
}

auto stage_output_music(const wormedit::song_document_t& snapshot,
                        const wormedit::output_group_t& group,
                        const std::filesystem::path& asset_base, std::stop_token stop)
    -> std::optional<std::string> {
    using namespace wormedit;
    if (group.family != format_family_t::osu_mania &&
        group.family != format_family_t::stepmania) return std::nullopt;
    const resource_entry_t* music{};
    for (const auto& resource : snapshot.resources) {
        if (std::ranges::find(resource.roles, resource_role_t::music) == resource.roles.end())
            continue;
        if (music) return "The output has more than one music resource.";
        music = &resource;
    }
    if (!music) return std::nullopt;
    const auto asset = std::ranges::find(snapshot.audio_assets, music->asset, &audio_asset_t::id);
    if (asset == snapshot.audio_assets.end()) return "The music resource has no audio asset.";
    const auto copied = copy_resource_file_into_export(
        {.project_directory = asset_base,
         .source_path = asset->source_path,
         .relative_output_path = group.relative_output_path,
         .asset = asset->id}, stop);
    if (!copied) return "Could not stage music for export: " + copied.message;
    return std::nullopt;
}

task_result_t run_output_group_export_task(const wormedit::song_document_t& snapshot,
                                           const wormedit::output_group_t& group,
                                           wormedit::conversion_request_t& request,
                                           const std::filesystem::path& asset_base,
                                           std::stop_token stop) {
    using namespace wormedit;
    auto completed = std::make_shared<output_group_export_task_result_t>();
    if (stop.stop_requested()) {
        completed->message = "Export cancelled before conversion.";
    } else if (auto staging_error = stage_output_music(snapshot, group, asset_base, stop)) {
        completed->message = *staging_error;
    } else {
        const auto discovered = discover_staged_export_assets(snapshot, group, asset_base, stop);
        if (!discovered) {
            completed->message = discovered.errors.front();
            return task_result_t::success(make_task_value(
                std::shared_ptr<const output_group_export_task_result_t>{std::move(completed)}));
        }
        request.staged_assets = discovered.mappings;
        const auto converted = convert_document(snapshot, request);
        if (!converted) {
            const auto error = std::ranges::find_if(converted.diagnostics, [](const auto& diagnostic) {
                return diagnostic.severity == diagnostic_severity_t::error;
            });
            completed->message = error == converted.diagnostics.end()
                                     ? "Output conversion failed."
                                     : error->message;
        } else if (stop.stop_requested()) {
            completed->message = "Export cancelled before publication.";
        } else {
            const auto published =
                publish_conversion_group_atomic(asset_base, group, request, converted);
            completed->published = published.published;
            completed->message = published.published ? "Output group published from the selected revision."
                                 : published.errors.empty() ? "Output group publication failed."
                                                             : published.errors.front();
            if (published.published && !published.warnings.empty())
                completed->message += " " + published.warnings.front();
        }
    }
    return task_result_t::success(make_task_value(
        std::shared_ptr<const output_group_export_task_result_t>{std::move(completed)}));
}

struct imported_resource_metadata_t {
    std::optional<wormedit::audio_stream_info_t> audio;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> image;
};

auto import_resource_role(const WormUiWorkspaceAction& action)
    -> std::optional<wormedit::resource_role_t> {
    using namespace wormedit;
    switch (action.compose_resource_role) {
    case WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE: return resource_role_t::sample;
    case WORM_UI_COMPOSE_RESOURCE_ROLE_BGM: return resource_role_t::bgm;
    case WORM_UI_COMPOSE_RESOURCE_ROLE_BGA: return resource_role_t::bga;
    case WORM_UI_COMPOSE_RESOURCE_ROLE_MUSIC: return resource_role_t::music;
    default: return std::nullopt;
    }
}

auto inspect_imported_resource(const std::filesystem::path& path, wormedit::resource_role_t role)
    -> std::variant<imported_resource_metadata_t, std::string> {
    imported_resource_metadata_t metadata;
    if (role == wormedit::resource_role_t::bga) {
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        const auto decoded = wormedit::visual::load_image_rgba(
            {.kind = wormedit::asset_kind_t::image, .path = path});
        if (!decoded) return decoded.message.empty() ? "The image could not be decoded."
                                                      : decoded.message;
        metadata.image = std::pair{decoded.image->width, decoded.image->height};
#else
        return "Image import is unavailable in this build.";
#endif
    } else {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        wormedit::audio::decoder_t decoder;
        std::string error;
        if (!decoder.open(path, &error))
            return error.empty() ? "The audio file could not be decoded." : error;
        const auto info = decoder.info();
        if (!info) return "The audio decoder did not report source geometry.";
        metadata.audio = {.sample_rate = info->source_sample_rate,
                          .channels = info->source_channel_count,
                          .frame_count = info->source_frame_count,
                          .seekable = info->seekable};
#else
        return "Audio import is unavailable in this build.";
#endif
    }
    return metadata;
}

std::optional<std::string> commit_primary_music(compose_action_context_t& context,
                                                const std::filesystem::path& path,
                                                const imported_resource_metadata_t& metadata) {
    using namespace wormedit;
    const auto chart = std::ranges::find(context.history.document.charts, context.state.chart,
                                         &chart_document_t::id);
    auto name = path.filename().generic_string();
    if (name.size() > 255) name.clear();
    const auto applied = apply_primary_music_set(
        context.history, context.action.compose_input_document_revision,
        {.source_path = path, .name = std::move(name),
         .allocate_bms_identifier = chart != context.history.document.charts.end() &&
                                    chart->source_family == format_family_t::bms,
         .audio_stream = metadata.audio});
    if (!applied.diagnostics.empty()) return applied.diagnostics.front().message;
    if (!applied.applied) return "That file is already the song audio.";
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "Song audio set, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    const auto music = std::ranges::find_if(context.history.document.resources, [](const auto& r) {
        return std::ranges::find(r.roles, resource_role_t::music) != r.roles.end();
    });
    if (music != context.history.document.resources.end())
        (void)apply_workspace_action(context.workspace, context.history.document,
            context.history.document.revision, workspace_select_audio_asset_action_t{music->asset});
    return "Song audio set to " + path.filename().string() + ".";
}

std::optional<std::string> commit_resource_import(
    compose_action_context_t& context, wormedit::resource_role_t role,
    const std::filesystem::path& path, imported_resource_metadata_t metadata) {
    using namespace wormedit;
    if (role == resource_role_t::music) return commit_primary_music(context, path, metadata);
    const auto chart = std::ranges::find(context.history.document.charts, context.state.chart,
                                         &chart_document_t::id);
    auto name = path.filename().generic_string();
    if (name.size() > 255) name.clear();
    const auto applied = apply_resource_asset_import(
        context.history, context.action.compose_input_document_revision,
        {.source_path = path, .role = role, .name = std::move(name),
         .allocate_bms_identifier = chart != context.history.document.charts.end() &&
                                    chart->source_family == format_family_t::bms,
         .audio_stream = metadata.audio, .image_geometry = metadata.image});
    if (!applied.diagnostics.empty()) return applied.diagnostics.front().message;
    if (!applied.applied) return "Resource import made no document change.";
    const auto imported = context.history.document.resources.back().asset;
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "Resource imported, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    if (role == resource_role_t::bga)
        (void)apply_workspace_action(context.workspace, context.history.document,
            context.history.document.revision, workspace_select_visual_asset_action_t{imported});
    else
        (void)apply_workspace_action(context.workspace, context.history.document,
            context.history.document.revision, workspace_select_audio_asset_action_t{imported});
    return "External file linked as a resource.";
}

std::optional<std::string> import_resource_asset(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before importing a resource.";
    const auto role = import_resource_role(context.action);
    if (!role) return "Resource import used an unknown catalog role.";
    const std::filesystem::path path =
        std::filesystem::path{context.action.compose_import_path}.lexically_normal();
    std::error_code error;
    if (!path.is_absolute() || !std::filesystem::is_regular_file(path, error) || error)
        return "Resource import requires an existing absolute file path.";
    auto metadata = inspect_imported_resource(path, *role);
    if (const auto* failure = std::get_if<std::string>(&metadata)) return *failure;
    return commit_resource_import(context, *role, path,
                                  std::get<imported_resource_metadata_t>(std::move(metadata)));
}

std::optional<std::string> dispatch_compose_import_resource_action(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET)
        return std::nullopt;
    return import_resource_asset(context);
}

struct resource_copy_plan_t {
    wormedit::asset_id_t asset;
    std::filesystem::path source;
    std::filesystem::path output;
    bool export_copy{};
};

auto prepare_resource_copy(compose_action_context_t& context)
    -> std::variant<resource_copy_plan_t, std::string> {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    if (context.resource_copy_runtime.busy() || context.export_runtime.busy() ||
        context.metadata_import_busy)
        return "Wait for the current resource copy, import, or export to finish.";
    if (context.asset_base.empty())
        return "Open a saved native project before copying resource files into it.";
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before copying a resource.";
    const asset_id_t id{action.compose_resource_asset_id};
    const auto audio = std::ranges::find(history.document.audio_assets, id, &audio_asset_t::id);
    const auto visual = std::ranges::find(history.document.visual_assets, id, &visual_asset_t::id);
    if (!id || (audio == history.document.audio_assets.end() &&
                visual == history.document.visual_assets.end()))
        return "The selected resource asset is no longer available.";
    const auto& source = audio != history.document.audio_assets.end()
        ? audio->source_path : visual->source_path;
    const bool exporting = action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT;
    if (!exporting && !source.is_absolute() && source.parent_path() == "assets" &&
        source.filename().string().starts_with(std::to_string(id.value) + "-"))
        return "Resource source is already in the project assets directory.";
    std::filesystem::path output;
    if (exporting) {
        if (action.compose_output_group_index >= history.document.output_groups.size())
            return "The selected output group is no longer available.";
        const auto& group = history.document.output_groups[action.compose_output_group_index];
        if (std::ranges::find(group.charts, context.state.chart) == group.charts.end())
            return "The selected output group does not include this chart.";
        output = group.relative_output_path;
    }
    return resource_copy_plan_t{id, source, output, exporting};
}

std::optional<std::string> start_resource_copy(compose_action_context_t& context,
                                               resource_copy_plan_t plan) {
    using namespace wormedit;
    task_graph_t graph{1};
    const project_asset_copy_request_t request{
        .project_directory = context.asset_base, .source_path = plan.source, .asset = plan.asset};
    const auto added = graph.add_task({}, [request, plan](std::span<const task_result_t>,
                                                          std::stop_token stop) {
        auto copied = std::make_shared<const project_asset_copy_result_t>(
            plan.export_copy
                ? copy_resource_file_into_export(
                      {.project_directory = request.project_directory,
                       .source_path = request.source_path,
                       .relative_output_path = plan.output,
                       .asset = request.asset,
                       .maximum_bytes = request.maximum_bytes}, stop)
                : copy_resource_file_into_project(request, stop));
        return task_result_t::success(make_task_value(std::move(copied)));
    });
    if (!added) return added.error ? added.error->message : "Resource copy could not start.";
    auto submitted = context.resource_copy_scheduler.submit(std::move(graph));
    if (!submitted) return submitted.error ? submitted.error->message : "Resource copy could not start.";
    auto pending = pending_project_resource_copy_t{
        .graph = std::move(submitted.graph), .task = *added.id, .asset = plan.asset,
        .song = context.history.document.id, .revision = context.history.document.revision,
        .source_path = plan.source, .output_path = plan.output, .export_copy = plan.export_copy};
    if (!context.resource_copy_runtime.start(std::move(pending))) {
        pending.graph.cancel();
        return "Wait for the current resource copy, import, or export to finish.";
    }
    return plan.export_copy ? "Export copy started in the background."
                            : "Resource copy started in the background.";
}

std::optional<std::string> dispatch_compose_copy_resource_action(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT &&
        context.action.compose_action != WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT)
        return std::nullopt;
    auto plan = prepare_resource_copy(context);
    if (const auto* error = std::get_if<std::string>(&plan)) return *error;
    return start_resource_copy(context, std::get<resource_copy_plan_t>(std::move(plan)));
}

struct output_export_plan_t {
    wormedit::output_group_t group;
    wormedit::conversion_request_t request;
};

auto prepare_output_export(compose_action_context_t& context)
    -> std::variant<output_export_plan_t, std::string> {
    using namespace wormedit;
    const auto& doc = context.history.document;
    if (context.asset_base.empty())
        return "Open a saved native project before exporting an output group.";
    if (context.resource_copy_runtime.busy() || context.export_runtime.busy() ||
        context.metadata_import_busy)
        return "Wait for the current resource copy, import, or export to finish.";
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before exporting.";
    if (context.action.compose_output_group_index >= doc.output_groups.size())
        return "The selected output group is no longer available.";
    auto group = doc.output_groups[context.action.compose_output_group_index];
    if (group.charts.empty() ||
        std::ranges::find(group.charts, context.state.chart) == group.charts.end())
        return "The selected output group has no matching charts.";
    conversion_request_t request;
    request.target = group.family;
    if (group.family != format_family_t::native_project) request.charts = group.charts;
    request.overwrite = context.action.compose_export_overwrite;
    request.loss_policy = loss_policy_t::reject;
    if (group.family == format_family_t::stepmania) {
        request.stepmania = stepmania_export_policy_t{
            .file_profile = group.relative_output_path.extension() == ".sm" ||
                                    group.relative_output_path == "sm"
                ? stepmania_profile_t::sm : stepmania_profile_t::ssc};
    } else if (group.family == format_family_t::bms) {
        const auto extension = group.relative_output_path.extension().string();
        if (extension == ".bme" || extension == ".bml" || extension == ".pms")
            request.target_profile = extension.substr(1);
    }
    return output_export_plan_t{std::move(group), std::move(request)};
}

std::optional<std::string> start_output_export(compose_action_context_t& context,
                                                output_export_plan_t plan) {
    using namespace wormedit;
    auto snapshot = context.history.document;
    task_graph_t graph{1};
    const auto added = graph.add_task({}, [snapshot = std::move(snapshot), group = std::move(plan.group),
        request = std::move(plan.request), base = context.asset_base](
        std::span<const task_result_t>, std::stop_token stop) mutable {
        return run_output_group_export_task(snapshot, group, request, base, stop);
    });
    if (!added) return added.error ? added.error->message : "Export could not start.";
    auto submitted = context.export_runtime.scheduler.submit(std::move(graph));
    if (!submitted) return submitted.error ? submitted.error->message : "Export could not start.";
    auto pending = pending_output_group_export_t{
        .graph = std::move(submitted.graph), .task = *added.id,
        .song = context.history.document.id, .revision = context.history.document.revision};
    if (!context.export_runtime.start(std::move(pending))) {
        pending.graph.cancel();
        return "Wait for the current resource copy, import, or export to finish.";
    }
    return "Output group conversion started in the background.";
}

std::optional<std::string> dispatch_compose_export_output_group_action(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP)
        return std::nullopt;
    auto plan = prepare_output_export(context);
    if (const auto* error = std::get_if<std::string>(&plan)) return *error;
    return start_output_export(context, std::get<output_export_plan_t>(std::move(plan)));
}

auto compose_placement_kind(const WormUiWorkspaceAction& action)
    -> std::optional<wormedit::resource_placement_kind_t> {
    using namespace wormedit;
    switch (action.compose_resource_role) {
    case WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE: return resource_placement_kind_t::sample_note;
    case WORM_UI_COMPOSE_RESOURCE_ROLE_BGM: return resource_placement_kind_t::bgm_event;
    case WORM_UI_COMPOSE_RESOURCE_ROLE_BGA: return resource_placement_kind_t::bga_background;
    default: return std::nullopt;
    }
}

auto apply_compose_resource_placement(compose_action_context_t& context,
                                      wormedit::resource_placement_kind_t kind)
    -> wormedit::document_command_result_t {
    using namespace wormedit;
    const auto resource = resource_id_t{context.action.compose_resource_id};
    if (context.state.virtual_bms_cursor_row)
        return apply_virtual_bms_resource_placement(
            context.history, context.action.compose_input_document_revision,
            {.chart = context.state.chart, .resource = resource,
             .canvas_row = *context.state.virtual_bms_cursor_row,
             .lane = context.state.active_lane, .kind = kind});
    return apply_resource_placement(
        context.history, context.action.compose_input_document_revision,
        {.chart = context.state.chart, .resource = resource,
         .position = context.state.cursor, .lane = context.state.active_lane, .kind = kind});
}

std::optional<std::string> finish_compose_resource_placement(
    compose_action_context_t& context, wormedit::resource_placement_kind_t kind) {
    using namespace wormedit;
    auto& state = context.state;
    if (state.virtual_bms_cursor_row) {
        const auto* chart = selected_chart(context.history, context.workspace);
        std::optional<local_position_t> position;
        if (chart) {
            const auto built = build_measure_index(*chart);
            if (const auto* index = std::get_if<measure_index_t>(&built))
                position = timing_position_at_row(*index, *state.virtual_bms_cursor_row);
        }
        place_cursor_at(state, position.value_or(state.cursor));
    }
    const auto role = kind == resource_placement_kind_t::sample_note ? resource_role_t::sample
        : kind == resource_placement_kind_t::bgm_event ? resource_role_t::bgm : resource_role_t::bga;
    const auto next = next_eligible_placement_resource(
        context.history.document, resource_id_t{context.action.compose_resource_id}, role);
    worm_ui_frontend_resource_placed(context.action.compose_resource_id,
                                     next.value_or(resource_id_t{}).value);
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "Resource placed, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    return "Resource placed at the edit cursor.";
}

std::optional<std::string> dispatch_compose_place_resource_action(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_PLACE_RESOURCE)
        return std::nullopt;
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before placing a resource.";
    const auto kind = compose_placement_kind(context.action);
    if (!kind) return "The selected resource role cannot be placed in the chart.";
    const auto applied = apply_compose_resource_placement(context, *kind);
    if (!applied.diagnostics.empty()) return applied.diagnostics.front().message;
    if (!applied.applied) return "Resource placement made no document change.";
    return finish_compose_resource_placement(context, *kind);
}

std::optional<std::string> select_resource_uses(compose_action_context_t& context) {
    using namespace wormedit;
    const resource_id_t resource{context.action.compose_resource_id};
    if (!resource || std::ranges::find(context.history.document.resources, resource,
                                       &resource_entry_t::id) == context.history.document.resources.end())
        return "The selected resource is no longer available.";
    const auto chart = std::ranges::find(context.history.document.charts, context.state.chart,
                                         &chart_document_t::id);
    if (chart == context.history.document.charts.end())
        return "The selected chart is no longer available.";
    auto uses = find_compose_resource_uses(*chart, resource);
    const auto count = uses.size();
    const auto result = dispatch_editor_action(context.history, context.state,
        select_editor_objects_action_t{.objects = std::move(uses),
                                       .mode = editor_area_selection_mode_t::replace});
    return compose_result_status(result, count == 0
        ? "No uses of this resource exist in the active chart."
        : std::to_string(count) + (count == 1 ? " resource use selected."
                                               : " resource uses selected."));
}

std::optional<std::string> assign_resource_to_selection(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before assigning a resource.";
    const auto applied = apply_resource_assignment(context.history,
        context.action.compose_input_document_revision,
        {.chart = context.state.chart,
         .resource = resource_id_t{context.action.compose_resource_id},
         .objects = context.state.selected_objects});
    if (!applied.diagnostics.empty()) return applied.diagnostics.front().message;
    if (!applied.applied) return "Selected objects already use that resource.";
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "Resource assigned, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    return "Resource assigned to selected objects.";
}

std::optional<std::string> create_resource_from_asset(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before creating a resource.";
    const auto role = import_resource_role(context.action).value_or(resource_role_t::music);
    const auto chart = std::ranges::find(context.history.document.charts, context.state.chart,
                                         &chart_document_t::id);
    const auto applied = apply_resource_creation(context.history,
        context.action.compose_input_document_revision,
        {.asset = asset_id_t{context.action.compose_resource_asset_id}, .role = role,
         .allocate_bms_identifier = chart != context.history.document.charts.end() &&
                                    chart->source_family == format_family_t::bms});
    if (!applied.diagnostics.empty()) return applied.diagnostics.front().message;
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "Resource created, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    return role == resource_role_t::bga ? "BGA resource created from the selected image."
                                        : "Resource created from the selected audio asset.";
}

std::optional<std::string> dispatch_compose_resource_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_SELECT_RESOURCE_USES: return select_resource_uses(context);
    case WORM_UI_COMPOSE_ACTION_ASSIGN_RESOURCE_TO_SELECTION:
        return assign_resource_to_selection(context);
    case WORM_UI_COMPOSE_ACTION_CREATE_RESOURCE: return create_resource_from_asset(context);
    case WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET:
        return dispatch_compose_import_resource_action(context);
    case WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT:
    case WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT:
        return dispatch_compose_copy_resource_action(context);
    case WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP:
        return dispatch_compose_export_output_group_action(context);
    case WORM_UI_COMPOSE_ACTION_PLACE_RESOURCE:
        return dispatch_compose_place_resource_action(context);
    default: return std::nullopt;
    }
}

std::optional<std::string> assign_bmson_selection(compose_action_context_t& context) {
    using namespace wormedit;
    auto& state = context.state;
    const bool to_lane = context.action.compose_action ==
                         WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_BGM_TO_ACTIVE_LANE;
    if (editor_interaction_has_pending_gesture(state))
        return "Finish or cancel the active Compose gesture before assigning BMSON sound.";
    const object_id_t event{context.action.compose_bmson_bgm_event_id};
    const auto kind = to_lane ? editor_selection_object_kind_t::bgm_event
                              : editor_selection_object_kind_t::note;
    if (state.selected_objects.size() != 1 || state.selected_objects.front().kind != kind ||
        state.selected_objects.front().id != event)
        return "Select one linked BMSON tap or BGM marker before assigning sound.";
    const auto assigned = assign_bmson_sound_event(
        context.history, context.action.compose_input_document_revision, state.chart, event,
        to_lane ? std::optional<lane_id_t>{state.active_lane} : std::nullopt);
    if (!assigned.diagnostics.empty()) return assigned.diagnostics.front().message;
    if (!assigned.applied) return "BMSON sound is already assigned to that destination.";
    state.selected_objects = {{to_lane ? editor_selection_object_kind_t::note
                                       : editor_selection_object_kind_t::bgm_event, event}};
    state.selected_notes = to_lane ? std::vector<object_id_t>{event} : std::vector<object_id_t>{};
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "BMSON sound assigned, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    return to_lane ? "BMSON BGM assigned to the active playable lane."
                   : "BMSON tap assigned to BGM.";
}

auto selected_bmson_source_event(const compose_action_context_t& context,
                                 wormedit::object_id_t bgm_event)
    -> std::optional<wormedit::object_id_t> {
    const auto chart = std::ranges::find(context.history.document.charts, context.state.chart,
                                         &wormedit::chart_document_t::id);
    if (chart == context.history.document.charts.end() ||
        chart->source_family != wormedit::format_family_t::bmson) return std::nullopt;
    for (const auto& measure : chart->measures) {
        const auto event = std::ranges::find(measure.bgm_events, bgm_event,
                                             &wormedit::bgm_event_t::id);
        if (event != measure.bgm_events.end()) return event->sound_channel_event;
    }
    return std::nullopt;
}

std::optional<std::string> set_bmson_selection_restart(compose_action_context_t& context) {
    using namespace wormedit;
    const object_id_t event{context.action.compose_bmson_bgm_event_id};
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before changing a BMSON slice.";
    if (!event || context.state.selected_objects.size() != 1 ||
        context.state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
        context.state.selected_objects.front().id != event)
        return "Select one BMSON BGM marker before changing its restart state.";
    const auto source_event = selected_bmson_source_event(context, event);
    if (!source_event)
        return "The selected BGM marker has no BMSON sound-channel slice.";
    const bool restart = context.action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BMSON_BGM_RESTART;
    const auto applied = set_bmson_sound_channel_restart(
        context.history, context.action.compose_input_document_revision, context.state.chart,
        *source_event, restart);
    if (!applied.diagnostics.empty()) return applied.diagnostics.front().message;
    if (!applied.applied)
        return restart ? "BMSON slice already restarts." : "BMSON slice already continues.";
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "BMSON slice changed, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    return restart ? "BMSON slice set to restart." : "BMSON slice set to continue.";
}

std::optional<std::string> move_bmson_selection_to_cursor(compose_action_context_t& context) {
    using namespace wormedit;
    const object_id_t event{context.action.compose_bmson_bgm_event_id};
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before moving a BMSON marker.";
    if (context.state.selected_objects.size() != 1 ||
        context.state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
        context.state.selected_objects.front().id != event)
        return "Select one BMSON BGM marker before moving it.";
    const auto moved = move_bmson_bgm_marker(
        context.history, context.action.compose_input_document_revision, context.state.chart,
        event, context.state.cursor);
    if (!moved.diagnostics.empty()) return moved.diagnostics.front().message;
    if (!moved.applied) return "BMSON BGM marker is already at the cursor.";
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "BMSON marker moved, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    return "BMSON BGM marker moved to the edit cursor.";
}

std::optional<std::string> insert_bmson_marker_at_cursor(compose_action_context_t& context) {
    using namespace wormedit;
    const auto event = object_id_t{context.action.compose_bmson_bgm_event_id};
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before inserting a BMSON marker.";
    if (context.state.selected_objects.size() != 1 ||
        context.state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
        context.state.selected_objects.front().id != event)
        return "Select one BMSON BGM marker to choose its sound channel.";
    const auto inserted = insert_bmson_bgm_marker(
        context.history, context.action.compose_input_document_revision, context.state.chart,
        event, context.state.cursor,
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_RESTART_AT_CURSOR);
    if (!inserted.diagnostics.empty()) return inserted.diagnostics.front().message;
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "BMSON marker inserted, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    return "BMSON BGM marker inserted at the edit cursor.";
}

std::optional<std::string> stage_bmson_marker_at_cursor(compose_action_context_t& context) {
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before staging a BMSON marker.";
    return stage_bmson_bgm_marker(
        context.history, context.state, context.application_state,
        wormedit::object_id_t{context.action.compose_bmson_bgm_event_id}, context.state.cursor,
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_RESTART_AT_CURSOR);
}

std::optional<std::string> commit_bmson_marker_batch(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before committing BMSON markers.";
    auto& draft = context.application_state.bmson_marker_draft;
    if (!draft || draft->markers.empty() || draft->chart != context.state.chart ||
        draft->revision != context.history.document.revision) {
        draft.reset();
        return "The BMSON marker batch is no longer available for this chart revision.";
    }
    const auto inserted = insert_bmson_sound_markers(
        context.history, draft->revision, draft->chart, draft->reference, draft->markers);
    if (!inserted.diagnostics.empty()) return inserted.diagnostics.front().message;
    draft.reset();
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "BMSON markers inserted, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    return "BMSON markers inserted in one undoable edit.";
}

std::optional<std::string> delete_bmson_selected_marker(compose_action_context_t& context) {
    using namespace wormedit;
    const object_id_t event{context.action.compose_bmson_bgm_event_id};
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before deleting a BMSON marker.";
    if (context.state.selected_objects.size() != 1 ||
        context.state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
        context.state.selected_objects.front().id != event)
        return "Select one BMSON BGM marker before deleting it.";
    const auto deleted = delete_bmson_bgm_marker(
        context.history, context.action.compose_input_document_revision, context.state.chart, event);
    if (!deleted.diagnostics.empty()) return deleted.diagnostics.front().message;
    if (!deleted.applied) return "BMSON marker deletion made no document change.";
    context.state.selected_objects.clear();
    context.state.selected_notes.clear();
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "BMSON marker deleted, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    return "BMSON BGM marker deleted.";
}

std::optional<std::string> dispatch_compose_bmson_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_SET_BMSON_BGM_RESTART:
    case WORM_UI_COMPOSE_ACTION_SET_BMSON_BGM_CONTINUE:
        return set_bmson_selection_restart(context);
    case WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_BGM_TO_ACTIVE_LANE:
    case WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_NOTE_TO_BGM:
        return assign_bmson_selection(context);
    case WORM_UI_COMPOSE_ACTION_MOVE_BMSON_BGM_TO_CURSOR:
        return move_bmson_selection_to_cursor(context);
    case WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_CONTINUE_AT_CURSOR:
    case WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_RESTART_AT_CURSOR:
        return insert_bmson_marker_at_cursor(context);
    case WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_CONTINUE_AT_CURSOR:
    case WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_RESTART_AT_CURSOR:
        return stage_bmson_marker_at_cursor(context);
    case WORM_UI_COMPOSE_ACTION_COMMIT_STAGED_BMSON_BGM:
        return commit_bmson_marker_batch(context);
    case WORM_UI_COMPOSE_ACTION_CANCEL_STAGED_BMSON_BGM:
        context.application_state.bmson_marker_draft.reset();
        return "BMSON marker batch canceled.";
    case WORM_UI_COMPOSE_ACTION_DELETE_BMSON_BGM_MARKER:
        return delete_bmson_selected_marker(context);
    default:
        return std::nullopt;
    }
}

struct beat_section_edit_t {
    std::optional<wormedit::marker_edit_action_t> edit;
    std::string error;
    bool from_selection{};
};

beat_section_edit_t make_beat_section_insertion(compose_action_context_t& context,
                                                  const wormedit::chart_document_t& chart) {
    using namespace wormedit;
    const auto built = build_measure_index(chart);
    const auto* measures = std::get_if<measure_index_t>(&built);
    if (!measures) return {.error = "The chart measure geometry cannot define a section span."};
    position_span_t span;
    bool from_selection{};
    if (context.state.retained_selection_range) {
        const auto& selection = *context.state.retained_selection_range;
        const auto begin = absolute_row(*measures, selection.begin);
        const auto end = absolute_row(*measures, selection.end);
        if (!begin || !end || *begin >= *end)
            return {.error = "The retained area/Space interval is no longer valid; select it again."};
        span = selection;
        from_selection = true;
    } else {
        const auto begin = absolute_row(*measures, context.state.cursor);
        const auto denominator = context.state.active_snap_denominator;
        if (!begin || denominator == 0 || chart.divisions_per_standard_measure % denominator != 0)
            return {.error = "The active Compose grid cannot define a section span."};
        const auto step = chart.divisions_per_standard_measure / denominator;
        if (step == 0 || *begin >= measures->total_rows || step >= measures->total_rows - *begin)
            return {.error = "Move the cursor earlier to create a one-snap section."};
        const auto end = timing_position_at_row(*measures, *begin + step);
        if (!end) return {.error = "The next active-snap row is outside the chart."};
        span = {context.state.cursor, *end};
    }
    return {.edit = insert_section_action_t{context.state.chart, context.action.compose_marker_label,
                                             marker_anchor_t::beat, span, std::nullopt},
            .from_selection = from_selection};
}

auto make_existing_beat_section_edit(compose_action_context_t& context,
                                     const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string> {
    using namespace wormedit;
    if (!context.action.compose_section_id)
        return "Choose a beat-anchored section before editing it.";
    const auto section = std::ranges::find(chart.sections,
        object_id_t{context.action.compose_section_id}, &section_t::id);
    if (section == chart.sections.end() || section->anchor != marker_anchor_t::beat ||
        !section->beat_span)
        return "That beat-anchored section is no longer available.";
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_BEAT_SECTION)
        return marker_edit_action_t{delete_section_action_t{context.state.chart, section->id}};
    auto span = *section->beat_span;
    auto label = section->label;
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL)
        label = context.action.compose_marker_label;
    else if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN)
        span.begin = context.state.cursor;
    else
        span.end = context.state.cursor;
    return marker_edit_action_t{update_section_action_t{
        context.state.chart, section->id, std::move(label), marker_anchor_t::beat, span,
        std::nullopt}};
}

std::string beat_section_success(const WormUiWorkspaceAction& action, bool from_selection) {
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION:
        return from_selection ? "Section added from the retained area/Space interval."
                              : "One-snap section added at the Compose cursor.";
    case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL: return "Section renamed.";
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN: return "Section start moved to the Compose cursor.";
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_END: return "Section end moved to the Compose cursor.";
    default: return "Section deleted.";
    }
}

std::optional<std::string> edit_beat_section(compose_action_context_t& context) {
    using namespace wormedit;
    const bool adding = context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION;
    if (context.state.virtual_bms_cursor_row && adding)
        return "Beat sections require a materialized cursor measure.";
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before editing sections.";
    const auto* chart = selected_chart(context.history, context.workspace);
    if (!chart) return "Choose a chart before editing sections.";
    marker_edit_action_t edit;
    bool from_selection{};
    if (adding) {
        auto made = make_beat_section_insertion(context, *chart);
        if (!made.error.empty()) return made.error;
        edit = std::move(*made.edit);
        from_selection = made.from_selection;
    } else {
        auto made = make_existing_beat_section_edit(context, *chart);
        if (const auto* error = std::get_if<std::string>(&made)) return *error;
        edit = std::get<marker_edit_action_t>(std::move(made));
    }
    const auto applied = apply_marker_edit(
        context.history, context.action.compose_input_document_revision, std::move(edit));
    if (!applied.applied)
        return applied.diagnostics.empty() ? "Section edit made no changes."
                                           : applied.diagnostics.front().message;
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "Section changed, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    return beat_section_success(context.action, from_selection);
}

std::optional<std::string> dispatch_compose_beat_section_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION:
    case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_END:
    case WORM_UI_COMPOSE_ACTION_DELETE_BEAT_SECTION:
        return edit_beat_section(context);
    default:
        return std::nullopt;
    }
}

auto make_audio_bookmark_edit(compose_action_context_t& context,
                              const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string> {
    using namespace wormedit;
    const auto deleting = context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK;
    const auto milliseconds = deleting ? std::optional<std::int64_t>{}
        : parse_timing_signed(context.action.compose_marker_audio_begin);
    if (!deleting && !milliseconds)
        return "Enter the audio bookmark position as exact signed milliseconds.";
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK)
        return marker_edit_action_t{insert_bookmark_action_t{
            context.state.chart, context.action.compose_marker_label, marker_anchor_t::audio_time,
            std::nullopt, milliseconds}};
    if (!context.action.compose_bookmark_id)
        return "Choose an audio-time bookmark before editing it.";
    const auto found = std::ranges::find(chart.bookmarks,
        object_id_t{context.action.compose_bookmark_id}, &bookmark_t::id);
    if (found == chart.bookmarks.end() || found->anchor != marker_anchor_t::audio_time ||
        !found->audio_milliseconds || found->position)
        return "That audio-time bookmark is no longer available.";
    if (deleting)
        return marker_edit_action_t{delete_bookmark_action_t{context.state.chart, found->id}};
    return marker_edit_action_t{update_bookmark_action_t{
        context.state.chart, found->id, context.action.compose_marker_label,
        marker_anchor_t::audio_time, std::nullopt, milliseconds}};
}

std::optional<std::string> edit_audio_bookmark(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before editing bookmarks.";
    const auto* chart = selected_chart(context.history, context.workspace);
    if (!chart) return "Choose a chart before editing audio-time bookmarks.";
    auto built = make_audio_bookmark_edit(context, *chart);
    if (const auto* error = std::get_if<std::string>(&built)) return *error;
    const auto applied = apply_marker_edit(context.history,
        context.action.compose_input_document_revision,
        std::get<marker_edit_action_t>(std::move(built)));
    if (!applied.applied)
        return applied.diagnostics.empty() ? "Audio bookmark edit made no changes."
                                           : applied.diagnostics.front().message;
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "Audio bookmark changed, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK)
        return "Audio-time bookmark added.";
    return context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK
        ? "Audio-time bookmark deleted." : "Audio-time bookmark updated.";
}

std::optional<std::string> dispatch_compose_audio_bookmark_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK:
        return edit_audio_bookmark(context);
    default:
        return std::nullopt;
    }
}

auto make_audio_section_edit(compose_action_context_t& context,
                             const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string> {
    using namespace wormedit;
    const auto deleting = context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION;
    const auto begin = deleting ? std::optional<std::int64_t>{}
        : parse_timing_signed(context.action.compose_marker_audio_begin);
    const auto end = deleting ? std::optional<std::int64_t>{}
        : parse_timing_signed(context.action.compose_marker_audio_end);
    if (!deleting && (!begin || !end))
        return "Enter both audio section bounds as exact signed milliseconds.";
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION)
        return marker_edit_action_t{insert_section_action_t{
            context.state.chart, context.action.compose_marker_label, marker_anchor_t::audio_time,
            std::nullopt, std::pair{*begin, *end}}};
    if (!context.action.compose_section_id)
        return "Choose an audio-time section before editing it.";
    const auto found = std::ranges::find(chart.sections,
        object_id_t{context.action.compose_section_id}, &section_t::id);
    if (found == chart.sections.end() || found->anchor != marker_anchor_t::audio_time ||
        !found->audio_millisecond_span || found->beat_span)
        return "That audio-time section is no longer available.";
    if (deleting)
        return marker_edit_action_t{delete_section_action_t{context.state.chart, found->id}};
    return marker_edit_action_t{update_section_action_t{
        context.state.chart, found->id, context.action.compose_marker_label,
        marker_anchor_t::audio_time, std::nullopt, std::pair{*begin, *end}}};
}

std::optional<std::string> edit_audio_section(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before editing sections.";
    const auto* chart = selected_chart(context.history, context.workspace);
    if (!chart) return "Choose a chart before editing audio-time sections.";
    auto built = make_audio_section_edit(context, *chart);
    if (const auto* error = std::get_if<std::string>(&built)) return *error;
    const auto applied = apply_marker_edit(context.history,
        context.action.compose_input_document_revision,
        std::get<marker_edit_action_t>(std::move(built)));
    if (!applied.applied)
        return applied.diagnostics.empty() ? "Audio section edit made no changes."
                                           : applied.diagnostics.front().message;
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "Audio section changed, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION)
        return "Audio-time section added.";
    return context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION
        ? "Audio-time section deleted." : "Audio-time section updated.";
}

std::optional<std::string> dispatch_compose_audio_section_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION:
    case WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_SECTION:
    case WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION:
        return edit_audio_section(context);
    default:
        return std::nullopt;
    }
}

auto make_beat_bookmark_edit(compose_action_context_t& context,
                             const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string> {
    using namespace wormedit;
    const auto operation = context.action.compose_action;
    if (operation == WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK)
        return marker_edit_action_t{insert_bookmark_action_t{
            context.state.chart, context.action.compose_marker_label, marker_anchor_t::beat,
            context.state.cursor, std::nullopt}};
    if (!context.action.compose_bookmark_id)
        return "Choose a beat-anchored bookmark before editing it.";
    const auto found = std::ranges::find(chart.bookmarks,
        object_id_t{context.action.compose_bookmark_id}, &bookmark_t::id);
    if (found == chart.bookmarks.end() || found->anchor != marker_anchor_t::beat || !found->position)
        return "That beat-anchored bookmark is no longer available.";
    if (operation == WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK)
        return marker_edit_action_t{delete_bookmark_action_t{context.state.chart, found->id}};
    const bool rename = operation == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK;
    return marker_edit_action_t{update_bookmark_action_t{
        context.state.chart, found->id, rename ? context.action.compose_marker_label : found->label,
        marker_anchor_t::beat,
        rename ? found->position : std::optional{context.state.cursor}, std::nullopt}};
}

std::optional<std::string> edit_beat_bookmark(compose_action_context_t& context) {
    using namespace wormedit;
    const auto operation = context.action.compose_action;
    if (context.state.virtual_bms_cursor_row &&
        (operation == WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK ||
         operation == WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION))
        return "Beat bookmarks require a materialized cursor measure.";
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before editing bookmarks.";
    const auto* chart = selected_chart(context.history, context.workspace);
    if (!chart) return "Choose a chart before editing bookmarks.";
    auto built = make_beat_bookmark_edit(context, *chart);
    if (const auto* error = std::get_if<std::string>(&built)) return *error;
    const auto applied = apply_marker_edit(context.history,
        context.action.compose_input_document_revision,
        std::get<marker_edit_action_t>(std::move(built)));
    if (!applied.applied)
        return applied.diagnostics.empty()
            ? (operation == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK
                   ? "Bookmark label was unchanged." : "Bookmark edit made no changes.")
            : applied.diagnostics.front().message;
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "Bookmark changed, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    if (operation == WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK)
        return "Bookmark added at the Compose cursor.";
    if (operation == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK) return "Bookmark renamed.";
    return operation == WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION
        ? "Bookmark moved to the Compose cursor." : "Bookmark deleted.";
}

std::optional<std::string> dispatch_compose_beat_bookmark_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION:
    case WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK:
        return edit_beat_bookmark(context);
    default:
        return std::nullopt;
    }
}

std::optional<std::string> edit_selected_note_labels(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before editing note labels.";
    if (context.state.selected_notes.empty())
        return "Select one or more notes before editing their label.";
    const auto label = context.action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL
        ? std::optional<std::string>{context.action.compose_note_label} : std::nullopt;
    const auto applied = apply_note_label_edit(context.history,
        context.action.compose_input_document_revision,
        {.chart = context.state.chart, .notes = context.state.selected_notes, .label = label});
    if (!applied.diagnostics.empty()) return applied.diagnostics.front().message;
    if (!applied.applied)
        return label ? "Selected notes already have that label."
                     : "Selected notes already have no label.";
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "Note label changed, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    return label ? "Note label applied." : "Note labels cleared.";
}

std::string navigate_audio_marker(compose_action_context_t& context) {
    using namespace wormedit;
    auto& app = context.application_state;
    if (!app.transport_audio_milliseconds || !app.transport_seek_audio_milliseconds)
        return "Start chart preview once before navigating audio-time markers.";
    const auto cursor = app.transport_audio_milliseconds();
    if (!cursor) return "Start chart preview once to compile its seekable timeline.";
    const auto direction = context.action.compose_action ==
            WORM_UI_COMPOSE_ACTION_NAVIGATE_PREVIOUS_AUDIO_MARKER
        ? marker_navigation_direction_t::previous : marker_navigation_direction_t::next;
    const auto marker = navigate_markers(context.history.document, context.history.document.revision,
                                         context.state.chart, *cursor, direction);
    if (marker.status == marker_navigation_status_t::no_result)
        return direction == marker_navigation_direction_t::previous
            ? "No previous audio-time bookmark or section boundary."
            : "No next audio-time bookmark or section boundary.";
    if (!marker || !marker.item->audio_milliseconds)
        return marker.diagnostics.empty()
            ? "Audio-time marker navigation could not resolve an exact millisecond."
            : marker.diagnostics.front().message;
    return app.transport_seek_audio_milliseconds(*marker.item->audio_milliseconds);
}

std::string search_selected_note_labels(compose_action_context_t& context) {
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before searching labels.";
    const auto step = context.action.compose_action == WORM_UI_COMPOSE_ACTION_SEARCH_NEXT_NOTE_LABEL
                          ? label_search_step_t::next
                      : context.action.compose_action ==
                              WORM_UI_COMPOSE_ACTION_SEARCH_PREVIOUS_NOTE_LABEL
                          ? label_search_step_t::previous
                          : label_search_step_t::first;
    return run_label_search(context.history, context.state, context.action.compose_note_label, step);
}

auto selection_transform_for(WormUiComposeActionKind kind) -> std::optional<selection_transform_t> {
    switch (kind) {
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_MIRROR: return selection_transform_t::mirror;
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_ROTATE_LEFT: return selection_transform_t::rotate_left;
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_ROTATE_RIGHT: return selection_transform_t::rotate_right;
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_EARLIER: return selection_transform_t::shift_earlier;
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_LATER: return selection_transform_t::shift_later;
    default: return std::nullopt;
    }
}

std::string transform_selected_notes(compose_action_context_t& context,
                                     selection_transform_t transform) {
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before transforming notes.";
    if (context.history.document.revision != context.action.compose_input_document_revision)
        return "Transform ignored because the document changed.";
    auto status = apply_selection_transform(context.history, context.state, transform);
    const auto rebound = rebind_workspace(context.workspace, context.history);
    return rebound ? status : "Transform applied, but workspace rebinding failed.";
}

auto cursor_measure_command_for(WormUiComposeActionKind kind)
    -> std::optional<cursor_measure_command_t> {
    switch (kind) {
    case WORM_UI_COMPOSE_ACTION_INSERT_MEASURE_BEFORE: return cursor_measure_command_t::insert_before;
    case WORM_UI_COMPOSE_ACTION_INSERT_MEASURE_AFTER: return cursor_measure_command_t::insert_after;
    case WORM_UI_COMPOSE_ACTION_REMOVE_EMPTY_MEASURE: return cursor_measure_command_t::remove_empty;
    default: return std::nullopt;
    }
}

std::string edit_cursor_measure(compose_action_context_t& context, cursor_measure_command_t command) {
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before editing measures.";
    if (context.history.document.revision != context.action.compose_input_document_revision)
        return "Measure edit ignored because the document changed.";
    auto status = apply_cursor_measure_command(context.history, context.state, command);
    const auto rebound = rebind_workspace(context.workspace, context.history);
    return rebound ? status : "Measure edited, but workspace rebinding failed.";
}

std::optional<std::string> dispatch_compose_marker_action(compose_action_context_t& context) {
    if (const auto measure_command = cursor_measure_command_for(context.action.compose_action))
        return edit_cursor_measure(context, *measure_command);
    if (const auto transform = selection_transform_for(context.action.compose_action))
        return transform_selected_notes(context, *transform);
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_SEARCH_NOTE_LABEL:
    case WORM_UI_COMPOSE_ACTION_SEARCH_NEXT_NOTE_LABEL:
    case WORM_UI_COMPOSE_ACTION_SEARCH_PREVIOUS_NOTE_LABEL:
        return search_selected_note_labels(context);
    case WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL:
    case WORM_UI_COMPOSE_ACTION_CLEAR_NOTE_LABEL:
        return edit_selected_note_labels(context);
    case WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION:
    case WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK:
        return dispatch_compose_beat_bookmark_action(context);
    case WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION:
    case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_END:
    case WORM_UI_COMPOSE_ACTION_DELETE_BEAT_SECTION:
        return dispatch_compose_beat_section_action(context);
    case WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK:
        return dispatch_compose_audio_bookmark_action(context);
    case WORM_UI_COMPOSE_ACTION_NAVIGATE_PREVIOUS_AUDIO_MARKER:
    case WORM_UI_COMPOSE_ACTION_NAVIGATE_NEXT_AUDIO_MARKER:
        return navigate_audio_marker(context);
    case WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION:
    case WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_SECTION:
    case WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION:
        return dispatch_compose_audio_section_action(context);
    default:
        return std::nullopt;
    }
}

std::optional<std::string> dispatch_semantic_layer_palette_action(
    compose_action_context_t& context) {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    auto& workspace = context.workspace;
    auto& application_state = context.application_state;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER: {
        const auto* chart = selected_chart(history, workspace);
        if (!chart || action.compose_semantic_layer_id == 0)
            return "Choose a visible semantic layer from the current Compose palette.";
        const auto* layer = visible_semantic_layer_by_id(
            *chart, layer_id_t{action.compose_semantic_layer_id});
        if (!layer) return "That semantic-layer palette entry is no longer visible.";
        application_state.semantic_layer_target = layer->id;
        return "Semantic-layer target: " + layer->name;
    }
    case WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE:
    case WORM_UI_COMPOSE_ACTION_NEXT_SEMANTIC_LAYER_PAGE: {
        const auto* chart = selected_chart(history, workspace);
        if (!chart) return "Choose a chart before navigating semantic-layer pages.";
        if (application_state.semantic_layer_palette_chart != chart->id) {
            application_state.semantic_layer_palette_chart = chart->id;
            application_state.semantic_layer_palette_page = 0;
        }
        const auto palette = make_semantic_layer_palette_window(
            *chart, application_state.semantic_layer_target,
            application_state.semantic_layer_palette_page, false,
            WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY);
        if (!palette) return "Semantic-layer palette navigation exceeded its bounded limit.";
        application_state.semantic_layer_palette_page = palette->page;
        if (action.compose_action == WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE) {
            if (application_state.semantic_layer_palette_page == 0)
                return "Semantic-layer palette is already on its first page.";
            --application_state.semantic_layer_palette_page;
        } else {
            if (palette->page_count == 0 ||
                application_state.semantic_layer_palette_page + 1 >= palette->page_count)
                return "Semantic-layer palette is already on its last page.";
            ++application_state.semantic_layer_palette_page;
        }
        return "Semantic-layer palette page " +
               std::to_string(application_state.semantic_layer_palette_page + 1) + " of " +
               std::to_string(palette->page_count) + ".";
    }
    default:
        return std::nullopt;
    }
}

std::optional<std::string> semantic_layer_edit_status(
    compose_action_context_t& context, wormedit::semantic_layer_edit_action_t edit,
    std::string_view success, std::string_view unchanged, std::string_view rebind_failure) {
    const bool inserting = std::holds_alternative<wormedit::insert_semantic_layer_action_t>(edit);
    const auto applied = wormedit::apply_semantic_layer_edit(
        context.history, context.action.compose_input_document_revision, std::move(edit));
    if (!applied.applied)
        return applied.diagnostics.empty() ? std::string{unchanged}
                                           : applied.diagnostics.front().message;
    if (inserting) {
        const auto* chart = selected_chart(context.history, context.workspace);
        if (chart && !chart->semantic_layers.empty())
            context.application_state.semantic_layer_target = chart->semantic_layers.back().id;
    }
    const auto rebound = wormedit::rebind_workspace(context.workspace, context.history);
    if (rebound) return std::string{success};
    return rebound.diagnostics.empty() ? std::string{rebind_failure}
                                       : rebound.diagnostics.front().message;
}

std::optional<std::string> add_semantic_layer(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before adding a semantic layer.";
    const auto* chart = selected_chart(context.history, context.workspace);
    if (!chart) return "Choose a chart before adding a semantic layer.";
    std::string name = "Layer";
    for (std::size_t suffix = 2; std::ranges::any_of(
             chart->semantic_layers, [&](const auto& item) { return item.name == name; }); ++suffix)
        name = "Layer " + std::to_string(suffix);
    constexpr std::array<semantic_layer_rgba_t, 6> colors{{
        {0.91, 0.35, 0.31, 1.0}, {0.95, 0.67, 0.24, 1.0},
        {0.36, 0.74, 0.36, 1.0}, {0.25, 0.62, 0.91, 1.0},
        {0.58, 0.42, 0.84, 1.0}, {0.87, 0.38, 0.68, 1.0},
    }};
    return semantic_layer_edit_status(
        context, insert_semantic_layer_action_t{
                    context.state.chart, std::move(name),
                    colors[chart->semantic_layers.size() % colors.size()]},
        "Semantic layer added.", "Semantic layer was not added.",
        "Semantic layer added, but workspace rebinding failed.");
}

auto selected_catalog_layer(compose_action_context_t& context)
    -> std::pair<const wormedit::chart_document_t*, const wormedit::semantic_layer_t*> {
    const auto* chart = selected_chart(context.history, context.workspace);
    if (!chart || !context.application_state.semantic_layer_target) return {chart, nullptr};
    return {chart, visible_semantic_layer_by_id(
                       *chart, *context.application_state.semantic_layer_target)};
}

std::optional<std::string> update_semantic_layer_catalog_entry(
    compose_action_context_t& context) {
    using namespace wormedit;
    const bool recolor = context.action.compose_action == WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR;
    if (editor_interaction_has_pending_gesture(context.state))
        return recolor ? "Finish or cancel the active Compose gesture before recoloring a semantic layer."
                       : "Finish or cancel the active Compose gesture before renaming a semantic layer.";
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || !layer)
        return !chart || !context.application_state.semantic_layer_target
                   ? (recolor ? "Choose a visible semantic layer before recoloring it."
                              : "Choose a visible semantic layer before renaming it.")
                   : "The selected semantic layer is no longer visible.";
    const auto color = recolor
        ? semantic_layer_rgba_t{static_cast<double>(context.action.compose_semantic_layer_red) / 255.0,
                                static_cast<double>(context.action.compose_semantic_layer_green) / 255.0,
                                static_cast<double>(context.action.compose_semantic_layer_blue) / 255.0,
                                static_cast<double>(context.action.compose_semantic_layer_alpha) / 255.0}
        : semantic_layer_rgba_t{static_cast<double>(layer->color.red) / 255.0,
                                static_cast<double>(layer->color.green) / 255.0,
                                static_cast<double>(layer->color.blue) / 255.0,
                                static_cast<double>(layer->color.alpha) / 255.0};
    const auto name = recolor ? layer->name : context.action.compose_semantic_layer_name;
    return semantic_layer_edit_status(
        context, update_semantic_layer_action_t{context.state.chart, layer->id, name, color,
                                                layer->visible, layer->locked},
        recolor ? "Semantic-layer color changed." : "Semantic layer renamed.",
        recolor ? "Semantic-layer color was unchanged." : "Semantic-layer name was unchanged.",
        recolor ? "Semantic-layer color changed, but workspace rebinding failed."
                : "Semantic layer renamed, but workspace rebinding failed.");
}

std::optional<std::string> delete_semantic_layer_catalog_entry(
    compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before deleting a semantic layer.";
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || !context.application_state.semantic_layer_target)
        return "Choose a visible semantic layer before deleting it.";
    if (!layer) return "The selected semantic layer is no longer visible.";
    const auto id = layer->id;
    const auto applied = apply_semantic_layer_edit(
        context.history, context.action.compose_input_document_revision,
        delete_semantic_layer_action_t{context.state.chart, id,
            context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS});
    if (!applied.applied)
        return applied.diagnostics.empty() ? "Semantic layer was not deleted."
                                           : applied.diagnostics.front().message;
    auto& state = context.application_state;
    std::erase(state.required_semantic_layer_filters, id);
    std::erase(state.excluded_semantic_layer_filters, id);
    state.semantic_layer_target.reset();
    state.semantic_layer_paint_mode_active = false;
    state.semantic_layer_solo_active = false;
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (rebound) return "Semantic layer deleted.";
    return rebound.diagnostics.empty() ? "Semantic layer deleted, but workspace rebinding failed."
                                       : rebound.diagnostics.front().message;
}

std::optional<std::string> move_semantic_layer_catalog_entry(
    compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before reordering semantic layers.";
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || !layer)
        return !chart || !context.application_state.semantic_layer_target
                   ? "Choose a visible semantic layer before reordering it."
                   : "The selected semantic layer is no longer available.";
    const auto found = std::ranges::find(chart->semantic_layers, layer->id, &semantic_layer_t::id);
    const auto index = static_cast<std::size_t>(std::distance(chart->semantic_layers.begin(), found));
    const bool up = context.action.compose_action == WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP;
    if ((up && index == 0) || (!up && index + 1 == chart->semantic_layers.size()))
        return "Semantic layer is already at that palette boundary.";
    std::vector<layer_id_t> order;
    order.reserve(chart->semantic_layers.size());
    for (const auto& item : chart->semantic_layers) order.push_back(item.id);
    std::swap(order[index], order[up ? index - 1 : index + 1]);
    return semantic_layer_edit_status(
        context, reorder_semantic_layers_action_t{context.state.chart, std::move(order)},
        "Semantic-layer order updated.", "Semantic-layer order was unchanged.",
        "Semantic-layer order updated, but workspace rebinding failed.");
}

std::optional<std::string> dispatch_compose_semantic_layer_catalog_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE:
    case WORM_UI_COMPOSE_ACTION_NEXT_SEMANTIC_LAYER_PAGE:
        return dispatch_semantic_layer_palette_action(context);
    case WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER:
        return add_semantic_layer(context);
    case WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR:
        return update_semantic_layer_catalog_entry(context);
    case WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS:
        return delete_semantic_layer_catalog_entry(context);
    case WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP:
    case WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_DOWN:
        return move_semantic_layer_catalog_entry(context);
    case WORM_UI_COMPOSE_ACTION_REMOVE_SEMANTIC_LAYER:
    default:
        return std::nullopt;
    }
}

void clear_semantic_layer_selection(application_command_state_t& app, bool clear_modes) {
    app.semantic_layer_target.reset();
    if (!clear_modes) return;
    app.semantic_layer_paint_mode_active = false;
    app.semantic_layer_solo_active = false;
}

std::optional<std::string> toggle_semantic_layer_visibility_or_lock(
    compose_action_context_t& context, bool toggle_lock) {
    using namespace wormedit;
    auto& app = context.application_state;
    if (editor_interaction_has_pending_gesture(context.state))
        return toggle_lock
            ? "Finish or cancel the active Compose gesture before changing layer lock state."
            : "Finish or cancel the active Compose gesture before changing layer visibility.";
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || !app.semantic_layer_target)
        return toggle_lock ? "Choose a visible semantic layer before changing its lock state."
                           : "Choose a semantic layer before changing its visibility.";
    if (!layer) {
        clear_semantic_layer_selection(app, toggle_lock);
        return "The selected semantic layer is no longer available.";
    }
    const bool previous = toggle_lock ? layer->locked : layer->visible;
    const auto color = semantic_layer_rgba_t{
        static_cast<double>(layer->color.red) / 255.0,
        static_cast<double>(layer->color.green) / 255.0,
        static_cast<double>(layer->color.blue) / 255.0,
        static_cast<double>(layer->color.alpha) / 255.0};
    const auto applied = apply_semantic_layer_edit(
        context.history, context.action.compose_input_document_revision,
        update_semantic_layer_action_t{context.state.chart, layer->id, layer->name, color,
                                       toggle_lock ? layer->visible : !previous,
                                       toggle_lock ? !previous : layer->locked});
    if (!applied.applied)
        return applied.diagnostics.empty() ? "Semantic-layer state was unchanged."
                                           : applied.diagnostics.front().message;
    if (!toggle_lock && previous) {
        app.semantic_layer_paint_mode_active = false;
        app.semantic_layer_solo_active = false;
    }
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "Semantic-layer state changed, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    if (toggle_lock && !previous) app.semantic_layer_paint_mode_active = false;
    if (toggle_lock) return previous ? "Semantic layer unlocked." : "Semantic layer locked.";
    return previous ? "Semantic layer hidden." : "Semantic layer shown.";
}

std::optional<std::string> toggle_semantic_layer_filter(compose_action_context_t& context) {
    using namespace wormedit;
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || !layer) return "Choose a visible semantic layer before filtering by it.";
    auto& app = context.application_state;
    if (app.semantic_layer_filter_chart != chart->id) {
        app.semantic_layer_filter_chart = chart->id;
        app.required_semantic_layer_filters.clear();
        app.excluded_semantic_layer_filters.clear();
    }
    const bool require =
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER;
    auto& enabled = require ? app.required_semantic_layer_filters : app.excluded_semantic_layer_filters;
    auto& opposite = require ? app.excluded_semantic_layer_filters : app.required_semantic_layer_filters;
    if (const auto found = std::ranges::find(enabled, layer->id); found != enabled.end()) {
        enabled.erase(found);
        return require ? "Required semantic-layer filter removed."
                       : "Excluded semantic-layer filter removed.";
    }
    std::erase(opposite, layer->id);
    enabled.push_back(layer->id);
    return require ? "Semantic layer required in the view filter."
                   : "Semantic layer excluded from the view filter.";
}

std::optional<std::string> toggle_semantic_layer_paint_mode(compose_action_context_t& context) {
    using namespace wormedit;
    auto& state = context.state;
    auto& app = context.application_state;
    if (editor_interaction_has_pending_gesture(state)) {
        const auto result = dispatch_editor_action(context.history, state, cancel_editor_gesture_action_t{});
        if (!result.accepted) return compose_result_status(result, "Compose gesture canceled.");
    }
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || !layer || layer->locked) {
        app.semantic_layer_paint_mode_active = false;
        return "Choose an unlocked visible semantic layer before painting note membership.";
    }
    app.semantic_layer_paint_mode_active = !app.semantic_layer_paint_mode_active;
    if (app.semantic_layer_paint_mode_active) app.note_paint_mode_active = false;
    return app.semantic_layer_paint_mode_active ? "Semantic-layer paint mode enabled."
                                                 : "Semantic-layer paint mode disabled.";
}

std::optional<std::string> toggle_semantic_layer_solo(compose_action_context_t& context) {
    using namespace wormedit;
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || !context.application_state.semantic_layer_target)
        return "Choose a visible semantic layer before soloing it.";
    if (!layer) {
        context.application_state.semantic_layer_solo_active = false;
        return "The selected semantic layer is no longer visible.";
    }
    auto& solo = context.application_state.semantic_layer_solo_active;
    solo = !solo;
    return solo ? "Semantic-layer solo enabled." : "Semantic-layer solo disabled.";
}

std::optional<std::string> select_semantic_layer_notes(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return "Finish or cancel the active Compose gesture before selecting by layer.";
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || !context.application_state.semantic_layer_target)
        return "Choose a visible semantic layer before selecting its notes.";
    if (!layer) {
        context.application_state.semantic_layer_target.reset();
        context.application_state.semantic_layer_paint_mode_active = false;
        context.application_state.semantic_layer_solo_active = false;
        return "The selected semantic layer is no longer visible.";
    }
    std::vector<object_id_t> notes;
    for (const auto& measure : chart->measures)
        for (const auto& note : measure.notes)
            if (std::ranges::find(note.semantic_layers, layer->id) != note.semantic_layers.end())
                notes.push_back(note.id);
    const auto result = dispatch_editor_action(
        context.history, context.state, select_editor_notes_action_t{std::move(notes)});
    return compose_result_status(result, "Semantic-layer notes selected.");
}

std::optional<std::string> edit_semantic_layer_membership(compose_action_context_t& context) {
    using namespace wormedit;
    auto& state = context.state;
    auto& app = context.application_state;
    if (editor_interaction_has_pending_gesture(state))
        return "Finish or cancel the active Compose gesture before editing semantic layers.";
    // Palette clicks can assign directly, carrying the clicked layer's stable ID.
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER &&
        context.action.compose_semantic_layer_id != 0) {
        const auto* chart = selected_chart(context.history, context.workspace);
        const auto* layer = chart ? visible_semantic_layer_by_id(
            *chart, layer_id_t{context.action.compose_semantic_layer_id}) : nullptr;
        if (!layer) return "That semantic-layer palette entry is no longer visible.";
        if (layer->locked)
            return "Unlock the selected semantic layer before changing note membership.";
        app.semantic_layer_target = layer->id;
    }
    const auto [chart, layer] = selected_catalog_layer(context);
    if (!chart || !app.semantic_layer_target || state.selected_notes.empty())
        return "Choose a visible semantic layer and select playable notes first.";
    if (!layer) {
        app.semantic_layer_target.reset();
        app.semantic_layer_paint_mode_active = false;
        app.semantic_layer_solo_active = false;
        return "The selected semantic layer is no longer visible.";
    }
    if (layer->locked)
        return "Unlock the selected semantic layer before changing note membership.";
    semantic_layer_edit_action_t edit;
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER)
        edit = assign_notes_to_semantic_layer_action_t{state.chart, layer->id, state.selected_notes};
    else
        edit = toggle_semantic_layer_membership_action_t{state.chart, layer->id, state.selected_notes};
    return semantic_layer_edit_status(context, std::move(edit), "Semantic-layer membership updated.",
        "Semantic-layer membership was unchanged.",
        "Semantic-layer membership changed, but workspace rebinding failed.");
}

std::optional<std::string> dispatch_compose_semantic_layer_state_action(
    compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY:
        return toggle_semantic_layer_visibility_or_lock(context, false);
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK:
        return toggle_semantic_layer_visibility_or_lock(context, true);
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER:
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_EXCLUDE_FILTER:
        return toggle_semantic_layer_filter(context);
    case WORM_UI_COMPOSE_ACTION_CLEAR_SEMANTIC_LAYER:
        context.application_state.semantic_layer_target.reset();
        context.application_state.semantic_layer_paint_mode_active = false;
        context.application_state.semantic_layer_solo_active = false;
        return "Semantic-layer target cleared.";
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_PAINT_MODE:
        return toggle_semantic_layer_paint_mode(context);
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_SOLO:
        return toggle_semantic_layer_solo(context);
    case WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER_NOTES:
        return select_semantic_layer_notes(context);
    case WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER:
        return edit_semantic_layer_membership(context);
    default:
        return std::nullopt;
    }
}

std::string dispatch_bms_branch_choice_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    const std::optional<wormedit::editor_interaction_state_t>& interaction_state) {
    using namespace wormedit;
    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
        workspace.selected_chart->value != action.compose_input_chart_id ||
        history.document.revision != action.compose_input_document_revision) {
        return "BMS branch action ignored because the chart or document revision changed.";
    }
    if (interaction_state && editor_interaction_has_pending_gesture(*interaction_state)) {
        return "Finish or cancel the active Compose gesture before changing a BMS branch.";
    }
    if (action.compose_branch_id == 0) {
        return "Choose a current BMS RANDOM or SWITCH control first.";
    }
    const auto applied = apply_bms_branch_choice_command(
        history, action.compose_input_document_revision,
        {.chart = *workspace.selected_chart,
         .branch = branch_id_t{action.compose_branch_id},
         .selected_value = action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE
                               ? std::optional<std::int64_t>{action.compose_branch_value}
                               : std::nullopt});
    if (!applied) {
        if (!applied.command.diagnostics.empty()) {
            return applied.command.diagnostics.front().message;
        }
        if (!applied.format_diagnostics.empty()) {
            return applied.format_diagnostics.front().message;
        }
        if (!applied.write_diagnostics.empty()) {
            return applied.write_diagnostics.front().message;
        }
        if (!applied.parse_diagnostics.empty()) {
            return applied.parse_diagnostics.front().message;
        }
        return "BMS branch choice was rejected without changing the document.";
    }
    const auto rebound = rebind_workspace(workspace, history);
    if (!rebound) {
        return rebound.diagnostics.empty() ? "BMS branch changed, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    }
    return action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE
               ? "BMS branch choice changed and its semantics were rematerialized."
               : "BMS branch choice cleared and its semantics were rematerialized.";
}

} // namespace

auto dispatch_compose_view_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, wormedit::editor_interaction_state_t& state,
    double viewport_height, application_command_state_t& application_state,
    const std::filesystem::path& asset_base, wormedit::task_scheduler_t& resource_copy_scheduler,
    runtime::resource_copy_t& resource_copy_runtime,
    runtime::output_group_export_t& export_runtime, bool metadata_import_busy) -> std::string {
    using namespace wormedit;
    const bool metadata_path_action =
        workspace.active_tab == workspace_tab_t::metadata &&
        (action.compose_action == WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET ||
         action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT ||
         action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT);
    if ((workspace.active_tab != workspace_tab_t::compose && !metadata_path_action) ||
        !workspace.selected_chart ||
        workspace.selected_chart->value != action.compose_input_chart_id ||
        state.chart.value != action.compose_input_chart_id ||
        history.document.revision != action.compose_input_document_revision) {
        return "Compose view action ignored because the chart or document revision changed.";
    }
    const auto selected_chart_it = std::ranges::find(history.document.charts, state.chart,
                                                     &chart_document_t::id);
    const auto scroll_direction =
        selected_chart_it != history.document.charts.end() &&
                compose_field_downscroll(*selected_chart_it)
            ? editor_field_scroll_direction_t::down
            : editor_field_scroll_direction_t::up;
    editor_interaction_result_t result;
    std::string status;

    if (auto response = dispatch_compose_area_action(action, history, state)) return *response;
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        return {};
    }
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE ||
        action.compose_action == WORM_UI_COMPOSE_ACTION_CLEAR_BMS_BRANCH_CHOICE) {
        return "BMS branch actions must use the workspace action boundary.";
    }
    compose_action_context_t context{
        action, history, workspace, state, viewport_height, application_state, asset_base,
        resource_copy_scheduler, resource_copy_runtime, export_runtime, metadata_import_busy,
        scroll_direction};
    if (auto response = dispatch_compose_audio_offset(context)) return *response;
    if (auto response = dispatch_compose_measure_navigation(context)) return *response;
    if (auto response = dispatch_compose_bgm_column_count(context)) return *response;
    if (auto response = dispatch_compose_lane_profile(context)) return *response;
    if (auto response = dispatch_compose_view_action_group(context)) return *response;
    if (auto response = dispatch_compose_resource_action(context)) return *response;
    if (auto response = dispatch_compose_bmson_action(context)) return *response;
    if (auto response = dispatch_compose_marker_action(context)) return *response;
    if (auto response = dispatch_compose_semantic_layer_catalog_action(context)) return *response;
    if (auto response = dispatch_compose_semantic_layer_state_action(context)) return *response;
    return compose_result_status(result, status);
}

auto dispatch_compose_workspace_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& interaction_state,
    double viewport_height, application_command_state_t& application_state,
    const std::filesystem::path& asset_base,
    wormedit::task_scheduler_t& resource_copy_scheduler,
    runtime::resource_copy_t& resource_copy_runtime,
    runtime::output_group_export_t& export_runtime, bool metadata_import_busy,
    metadata_drop_state_t& metadata_drop, runtime::metadata_asset_t& metadata_asset_runtime,
    std::string& status) -> bool {
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE ||
        action.compose_action == WORM_UI_COMPOSE_ACTION_CLEAR_BMS_BRANCH_CHOICE) {
        status = dispatch_bms_branch_choice_action(action, history, workspace, interaction_state);
        return true;
    }
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_NONE && !action.compose_area_clear_requested) {
        return false;
    }

    const bool metadata_path_action =
        workspace.active_tab == wormedit::workspace_tab_t::metadata &&
        (action.compose_action == WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET ||
         action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT ||
         action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT);
    wormedit::editor_interaction_state_t metadata_interaction{};
    if (metadata_path_action && workspace.selected_chart) {
        metadata_interaction.chart = *workspace.selected_chart;
    }
    if (interaction_state || metadata_path_action) {
        if (metadata_path_action) {
            metadata_asset_runtime.clear_copy_preview_key();
        }
        status = dispatch_compose_view_action(
            action, history, workspace,
            metadata_path_action ? metadata_interaction : *interaction_state,
            viewport_height, application_state, asset_base, resource_copy_scheduler,
            resource_copy_runtime, export_runtime, metadata_import_busy);
        if (workspace.active_tab == wormedit::workspace_tab_t::metadata &&
            (action.compose_action == WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET ||
             action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT ||
             action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT)) {
            metadata_drop.clear_music();
        }
    }
    return true;
}



} // namespace wormedit::sys
