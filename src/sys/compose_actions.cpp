#include "sys/compose_actions.h"
#include "sys/application_edit_helpers.h"
#include "sys/compose_resource_presenter.h"
#include "ui/frontend.h"
#include "wormedit/audio.h"

#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/command.h>
#include <wormedit/core/editor_scene.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/interaction.h>
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
                                        viewport_height * 0.5, scroll_direction});
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

task_result_t run_output_group_export_task(const wormedit::song_document_t& snapshot,
                                           const wormedit::output_group_t& group,
                                           wormedit::conversion_request_t& request,
                                           const std::filesystem::path& asset_base,
                                           std::stop_token stop) {
    using namespace wormedit;
    auto completed = std::make_shared<output_group_export_task_result_t>();
    if (stop.stop_requested()) {
        completed->message = "Export cancelled before conversion.";
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

std::optional<std::string> dispatch_compose_import_resource_action(compose_action_context_t& context) {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    auto& workspace = context.workspace;
    auto& state = context.state;
    editor_interaction_result_t result;
    std::string status;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before importing a resource.";
        }
        const auto role = action.compose_resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE
                              ? resource_role_t::sample
                              : action.compose_resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM
                                    ? resource_role_t::bgm
                                    : action.compose_resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGA
                                          ? resource_role_t::bga
                                          : resource_role_t::music;
        if (role == resource_role_t::music) {
            return "Resource import used an unknown catalog role.";
        }
        const std::filesystem::path source_path{action.compose_import_path};
        const auto path = source_path.lexically_normal();
        std::error_code file_error;
        if (!path.is_absolute() || !std::filesystem::is_regular_file(path, file_error) ||
            file_error) {
            return "Resource import requires an existing absolute file path.";
        }
        std::optional<audio_stream_info_t> audio_stream;
        std::optional<std::pair<std::uint32_t, std::uint32_t>> image_geometry;
        if (role == resource_role_t::bga) {
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
            const auto decoded = visual::load_image_rgba(
                {.kind = asset_kind_t::image, .path = path});
            if (!decoded) {
                return decoded.message.empty() ? "The image could not be decoded."
                                               : decoded.message;
            }
            image_geometry = std::pair{decoded.image->width, decoded.image->height};
#else
            return "Image import is unavailable in this build.";
#endif
        } else {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
            audio::decoder_t decoder;
            std::string decode_error;
            if (!decoder.open(path, &decode_error)) {
                return decode_error.empty() ? "The audio file could not be decoded."
                                            : decode_error;
            }
            const auto info = decoder.info();
            if (!info) {
                return "The audio decoder did not report source geometry.";
            }
            audio_stream = {.sample_rate = info->source_sample_rate,
                            .channels = info->source_channel_count,
                            .frame_count = info->source_frame_count,
                            .seekable = info->seekable};
#else
            return "Audio import is unavailable in this build.";
#endif
        }
        const auto chart = std::ranges::find(history.document.charts, state.chart,
                                             &chart_document_t::id);
        auto name = path.filename().generic_string();
        if (name.size() > 255) {
            name.clear();
        }
        const auto applied = apply_resource_asset_import(
            history, action.compose_input_document_revision,
            {.source_path = path,
             .role = role,
             .name = std::move(name),
             .allocate_bms_identifier =
                 chart != history.document.charts.end() &&
                 chart->source_family == format_family_t::bms,
             .audio_stream = audio_stream,
             .image_geometry = image_geometry});
        if (!applied.diagnostics.empty()) {
            return applied.diagnostics.front().message;
        }
        if (!applied.applied) {
            return "Resource import made no document change.";
        }
        const auto imported_asset = history.document.resources.back().asset;
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? "Resource imported, but workspace rebinding failed."
                       : rebound.diagnostics.front().message;
        }
        if (role != resource_role_t::bga) {
            (void)apply_workspace_action(
                workspace, history.document, history.document.revision,
                workspace_select_audio_asset_action_t{imported_asset});
        } else {
            (void)apply_workspace_action(
                workspace, history.document, history.document.revision,
                workspace_select_visual_asset_action_t{imported_asset});
        }
        return "External file linked as a resource.";
    }
    default:
        return std::nullopt;
    }
    return compose_result_status(result, status);
}

std::optional<std::string> dispatch_compose_copy_resource_action(compose_action_context_t& context) {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    auto& state = context.state;
    auto& asset_base = context.asset_base;
    auto& resource_copy_scheduler = context.resource_copy_scheduler;
    auto& resource_copy_runtime = context.resource_copy_runtime;
    auto& export_runtime = context.export_runtime;
    auto& metadata_import_busy = context.metadata_import_busy;
    editor_interaction_result_t result;
    std::string status;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT:
    case WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT: {
        const auto export_copy =
            action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT;
        if (resource_copy_runtime.busy() || export_runtime.busy() || metadata_import_busy) {
            return "Wait for the current resource copy, import, or export to finish.";
        }
        if (asset_base.empty()) {
            return "Open a saved native project before copying resource files into it.";
        }
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before copying a resource.";
        }
        const asset_id_t id{action.compose_resource_asset_id};
        const auto audio = std::ranges::find(history.document.audio_assets, id,
                                              &audio_asset_t::id);
        const auto visual = std::ranges::find(history.document.visual_assets, id,
                                               &visual_asset_t::id);
        if (!id || (audio == history.document.audio_assets.end() &&
                    visual == history.document.visual_assets.end())) {
            return "The selected resource asset is no longer available.";
        }
        const auto& source_path = audio != history.document.audio_assets.end()
                                      ? audio->source_path : visual->source_path;
        const auto source_name = source_path.filename().string();
        if (!export_copy && !source_path.is_absolute() && source_path.parent_path() == "assets" &&
            source_name.starts_with(std::to_string(id.value) + "-")) {
            return "Resource source is already in the project assets directory.";
        }
        std::filesystem::path output_path;
        if (export_copy) {
            if (action.compose_output_group_index >= history.document.output_groups.size())
                return "The selected output group is no longer available.";
            const auto& group = history.document.output_groups[action.compose_output_group_index];
            if (std::ranges::find(group.charts, state.chart) == group.charts.end())
                return "The selected output group does not include this chart.";
            output_path = group.relative_output_path;
        }
        task_graph_t graph{1};
        const project_asset_copy_request_t request{
            .project_directory = asset_base, .source_path = source_path, .asset = id};
        const auto added =
            graph.add_task({}, [request, export_copy, output_path](std::span<const task_result_t>,
                                                                   std::stop_token stop) {
                auto copied = std::make_shared<const project_asset_copy_result_t>(
                    export_copy ? copy_resource_file_into_export(
                                      {.project_directory = request.project_directory,
                                       .source_path = request.source_path,
                                       .relative_output_path = output_path,
                                       .asset = request.asset,
                                       .maximum_bytes = request.maximum_bytes},
                                      stop)
                                : copy_resource_file_into_project(request, stop));
                return task_result_t::success(make_task_value(std::move(copied)));
            });
        if (!added) {
            return added.error ? added.error->message : "Resource copy could not start.";
        }
        auto submitted = resource_copy_scheduler.submit(std::move(graph));
        if (!submitted) {
            return submitted.error ? submitted.error->message : "Resource copy could not start.";
        }
        auto pending = pending_project_resource_copy_t{.graph = std::move(submitted.graph),
                                                       .task = *added.id,
                                                       .asset = id,
                                                       .song = history.document.id,
                                                       .revision = history.document.revision,
                                                       .source_path = source_path,
                                                       .output_path = output_path,
                                                       .export_copy = export_copy};
        const auto started = resource_copy_runtime.start(std::move(pending));
        if (!started) {
            pending.graph.cancel();
            return "Wait for the current resource copy, import, or export to finish.";
        }
        return export_copy ? "Export copy started in the background."
                           : "Resource copy started in the background.";
    }
    default:
        return std::nullopt;
    }
    return compose_result_status(result, status);
}

std::optional<std::string> dispatch_compose_export_output_group_action(compose_action_context_t& context) {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    auto& state = context.state;
    auto& asset_base = context.asset_base;
    auto& resource_copy_runtime = context.resource_copy_runtime;
    auto& export_runtime = context.export_runtime;
    auto& metadata_import_busy = context.metadata_import_busy;
    editor_interaction_result_t result;
    std::string status;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP: {
        if (asset_base.empty())
            return "Open a saved native project before exporting an output group.";
        if (resource_copy_runtime.busy() || export_runtime.busy() || metadata_import_busy)
            return "Wait for the current resource copy, import, or export to finish.";
        if (editor_interaction_has_pending_gesture(state))
            return "Finish or cancel the active Compose gesture before exporting.";
        if (action.compose_output_group_index >= history.document.output_groups.size())
            return "The selected output group is no longer available.";
        const auto group = history.document.output_groups[action.compose_output_group_index];
        if (group.charts.empty() ||
            std::ranges::find(group.charts, state.chart) == group.charts.end())
            return "The selected output group has no matching charts.";
        conversion_request_t request;
        request.target = group.family;
        if (group.family != format_family_t::native_project)
            request.charts = group.charts;
        request.overwrite = action.compose_export_overwrite;
        request.loss_policy = loss_policy_t::reject;
        if (group.family == format_family_t::stepmania) {
            const auto extension = group.relative_output_path.extension();
            request.stepmania = stepmania_export_policy_t{
                .file_profile =
                    extension == ".sm" ? stepmania_profile_t::sm : stepmania_profile_t::ssc};
        } else if (group.family == format_family_t::bms) {
            const auto extension = group.relative_output_path.extension().string();
            if (extension == ".bme" || extension == ".bml" || extension == ".pms")
                request.target_profile = extension.substr(1);
        }
        auto snapshot = history.document;
        task_graph_t graph{1};
        const auto added = graph.add_task({}, [snapshot = std::move(snapshot), group,
                                               request = std::move(request),
                                               asset_base](std::span<const task_result_t>,
                                                           std::stop_token stop) mutable {
            return run_output_group_export_task(snapshot, group, request, asset_base, stop);
        });
        if (!added)
            return added.error ? added.error->message : "Export could not start.";
        auto submitted = export_runtime.scheduler.submit(std::move(graph));
        if (!submitted)
            return submitted.error ? submitted.error->message : "Export could not start.";
        auto pending = pending_output_group_export_t{.graph = std::move(submitted.graph),
                                                     .task = *added.id,
                                                     .song = history.document.id,
                                                     .revision = history.document.revision};
        const auto started = export_runtime.start(std::move(pending));
        if (!started) {
            pending.graph.cancel();
            return "Wait for the current resource copy, import, or export to finish.";
        }
        return "Output group conversion started in the background.";
    }
    default:
        return std::nullopt;
    }
    return compose_result_status(result, status);
}

std::optional<std::string> dispatch_compose_place_resource_action(compose_action_context_t& context) {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    auto& workspace = context.workspace;
    auto& state = context.state;
    editor_interaction_result_t result;
    std::string status;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_PLACE_RESOURCE: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before placing a resource.";
        }
        const auto kind =
            action.compose_resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE
                ? resource_placement_kind_t::sample_note
                : action.compose_resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM
                      ? resource_placement_kind_t::bgm_event
                      : action.compose_resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGA
                            ? resource_placement_kind_t::bga_background
                            : static_cast<resource_placement_kind_t>(255);
        const auto virtual_row = state.virtual_bms_cursor_row;
        const auto applied = virtual_row
                                 ? apply_virtual_bms_resource_placement(
                                       history, action.compose_input_document_revision,
                                       {.chart = state.chart,
                                        .resource = resource_id_t{action.compose_resource_id},
                                        .canvas_row = *virtual_row,
                                        .lane = state.active_lane,
                                        .kind = kind})
                                 : apply_resource_placement(
                                       history, action.compose_input_document_revision,
                                       {.chart = state.chart,
                                        .resource = resource_id_t{action.compose_resource_id},
                                        .position = state.cursor,
                                        .lane = state.active_lane,
                                        .kind = kind});
        if (!applied.diagnostics.empty()) {
            return applied.diagnostics.front().message;
        }
        if (!applied.applied) {
            return "Resource placement made no document change.";
        }
        if (virtual_row) {
            const auto* updated_chart = selected_chart(history, workspace);
            if (updated_chart) {
                const auto built = build_measure_index(*updated_chart);
                if (const auto* index = std::get_if<measure_index_t>(&built)) {
                    if (const auto position = timing_position_at_row(*index, *virtual_row)) {
                        state.cursor = *position;
                    }
                }
            }
            state.virtual_bms_cursor_row.reset();
        }
        const auto role = kind == resource_placement_kind_t::sample_note
                              ? resource_role_t::sample
                          : kind == resource_placement_kind_t::bgm_event
                              ? resource_role_t::bgm
                              : resource_role_t::bga;
        const auto next = next_eligible_placement_resource(
            history.document, resource_id_t{action.compose_resource_id}, role);
        worm_ui_frontend_resource_placed(action.compose_resource_id,
                                         next.value_or(resource_id_t{}).value);
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? "Resource placed, but workspace rebinding failed."
                       : rebound.diagnostics.front().message;
        }
        return "Resource placed at the edit cursor.";
    }
    default:
        return std::nullopt;
    }
    return compose_result_status(result, status);
}

std::optional<std::string> dispatch_compose_resource_action(compose_action_context_t& context) {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    auto& workspace = context.workspace;
    auto& state = context.state;
    editor_interaction_result_t result;
    std::string status;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_SELECT_RESOURCE_USES: {
        const resource_id_t resource{action.compose_resource_id};
        if (resource.value == 0 ||
            std::ranges::find(history.document.resources, resource, &resource_entry_t::id) ==
                history.document.resources.end()) {
            return "The selected resource is no longer available.";
        }
        const auto chart =
            std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
        if (chart == history.document.charts.end()) {
            return "The selected chart is no longer available.";
        }
        auto uses = wormedit::sys::find_compose_resource_uses(*chart, resource);
        const auto use_count = uses.size();
        result = dispatch_editor_action(
            history, state,
            select_editor_objects_action_t{.objects = std::move(uses),
                                           .mode = editor_area_selection_mode_t::replace});
        status = use_count == 0 ? "No uses of this resource exist in the active chart."
                                : std::to_string(use_count) +
                                      (use_count == 1 ? " resource use selected."
                                                      : " resource uses selected.");
        break;
    }
    case WORM_UI_COMPOSE_ACTION_ASSIGN_RESOURCE_TO_SELECTION: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before assigning a resource.";
        }
        const auto applied = apply_resource_assignment(
            history, action.compose_input_document_revision,
            {.chart = state.chart,
             .resource = resource_id_t{action.compose_resource_id},
             .objects = state.selected_objects});
        if (!applied.diagnostics.empty()) {
            return applied.diagnostics.front().message;
        }
        if (!applied.applied) {
            return "Selected objects already use that resource.";
        }
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? "Resource assigned, but workspace rebinding failed."
                       : rebound.diagnostics.front().message;
        }
        return "Resource assigned to selected objects.";
    }
    case WORM_UI_COMPOSE_ACTION_CREATE_RESOURCE: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before creating a resource.";
        }
        const auto role = action.compose_resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE
                              ? resource_role_t::sample
                              : action.compose_resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM
                                    ? resource_role_t::bgm
                                    : action.compose_resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGA
                                          ? resource_role_t::bga
                                          : resource_role_t::music;
        const auto chart = std::ranges::find(history.document.charts, state.chart,
                                             &chart_document_t::id);
        const auto applied = apply_resource_creation(
            history, action.compose_input_document_revision,
            {.asset = asset_id_t{action.compose_resource_asset_id},
             .role = role,
             .allocate_bms_identifier =
                 chart != history.document.charts.end() &&
                 chart->source_family == format_family_t::bms});
        if (!applied.diagnostics.empty()) {
            return applied.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? "Resource created, but workspace rebinding failed."
                       : rebound.diagnostics.front().message;
        }
        return role == resource_role_t::bga
                   ? "BGA resource created from the selected image."
                   : "Resource created from the selected audio asset.";
    }
    case WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET:
        return dispatch_compose_import_resource_action(context);
    case WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT:
    case WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT:
        return dispatch_compose_copy_resource_action(context);
    case WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP:
        return dispatch_compose_export_output_group_action(context);
    case WORM_UI_COMPOSE_ACTION_PLACE_RESOURCE:
        return dispatch_compose_place_resource_action(context);
    default:
        return std::nullopt;
    }
    return compose_result_status(result, status);
}

std::optional<std::string> dispatch_compose_bmson_action(compose_action_context_t& context) {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    auto& workspace = context.workspace;
    auto& state = context.state;
    auto& application_state = context.application_state;
    editor_interaction_result_t result;
    std::string status;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_BGM_TO_ACTIVE_LANE:
    case WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_NOTE_TO_BGM: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before assigning BMSON sound.";
        }
        const auto event = object_id_t{action.compose_bmson_bgm_event_id};
        const auto to_lane =
            action.compose_action == WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_BGM_TO_ACTIVE_LANE;
        const auto selected_kind = to_lane ? editor_selection_object_kind_t::bgm_event
                                           : editor_selection_object_kind_t::note;
        if (state.selected_objects.size() != 1 ||
            state.selected_objects.front().kind != selected_kind ||
            state.selected_objects.front().id != event) {
            return "Select one linked BMSON tap or BGM marker before assigning sound.";
        }
        const auto assigned = assign_bmson_sound_event(
            history, action.compose_input_document_revision, state.chart, event,
            to_lane ? std::optional<lane_id_t>{state.active_lane} : std::nullopt);
        if (!assigned.diagnostics.empty()) {
            return assigned.diagnostics.front().message;
        }
        if (!assigned.applied) {
            return "BMSON sound is already assigned to that destination.";
        }
        state.selected_objects = {{to_lane ? editor_selection_object_kind_t::note
                                           : editor_selection_object_kind_t::bgm_event,
                                   event}};
        state.selected_notes =
            to_lane ? std::vector<object_id_t>{event} : std::vector<object_id_t>{};
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? "BMSON sound assigned, but workspace rebinding failed."
                       : rebound.diagnostics.front().message;
        }
        return to_lane ? "BMSON BGM assigned to the active playable lane."
                       : "BMSON tap assigned to BGM.";
    }
    case WORM_UI_COMPOSE_ACTION_MOVE_BMSON_BGM_TO_CURSOR: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before moving a BMSON marker.";
        }
        const object_id_t event{action.compose_bmson_bgm_event_id};
        if (state.selected_objects.size() != 1 ||
            state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
            state.selected_objects.front().id != event) {
            return "Select one BMSON BGM marker before moving it.";
        }
        const auto moved = move_bmson_bgm_marker(
            history, action.compose_input_document_revision, state.chart, event, state.cursor);
        if (!moved.diagnostics.empty()) {
            return moved.diagnostics.front().message;
        }
        if (!moved.applied) {
            return "BMSON BGM marker is already at the cursor.";
        }
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? "BMSON marker moved, but workspace rebinding failed."
                       : rebound.diagnostics.front().message;
        }
        return "BMSON BGM marker moved to the edit cursor.";
    }
    case WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_CONTINUE_AT_CURSOR:
    case WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_RESTART_AT_CURSOR: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before inserting a BMSON marker.";
        }
        const object_id_t event{action.compose_bmson_bgm_event_id};
        if (state.selected_objects.size() != 1 ||
            state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
            state.selected_objects.front().id != event) {
            return "Select one BMSON BGM marker to choose its sound channel.";
        }
        const auto inserted = insert_bmson_bgm_marker(
            history, action.compose_input_document_revision, state.chart, event, state.cursor,
            action.compose_action == WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_RESTART_AT_CURSOR);
        if (!inserted.diagnostics.empty()) {
            return inserted.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? "BMSON marker inserted, but workspace rebinding failed."
                       : rebound.diagnostics.front().message;
        }
        return "BMSON BGM marker inserted at the edit cursor.";
    }
    case WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_CONTINUE_AT_CURSOR:
    case WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_RESTART_AT_CURSOR: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before staging a BMSON marker.";
        }
        return stage_bmson_bgm_marker(
            history, state, application_state,
            object_id_t{action.compose_bmson_bgm_event_id}, state.cursor,
            action.compose_action == WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_RESTART_AT_CURSOR);
    }
    case WORM_UI_COMPOSE_ACTION_COMMIT_STAGED_BMSON_BGM: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before committing BMSON markers.";
        }
        auto& draft = application_state.bmson_marker_draft;
        if (!draft || draft->markers.empty() || draft->chart != state.chart ||
            draft->revision != history.document.revision) {
            draft.reset();
            return "The BMSON marker batch is no longer available for this chart revision.";
        }
        const auto inserted = insert_bmson_sound_markers(history, draft->revision, draft->chart,
                                                         draft->reference, draft->markers);
        if (!inserted.diagnostics.empty()) {
            return inserted.diagnostics.front().message;
        }
        draft.reset();
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? "BMSON markers inserted, but workspace rebinding failed."
                       : rebound.diagnostics.front().message;
        }
        return "BMSON markers inserted in one undoable edit.";
    }
    case WORM_UI_COMPOSE_ACTION_CANCEL_STAGED_BMSON_BGM:
        application_state.bmson_marker_draft.reset();
        return "BMSON marker batch canceled.";
    case WORM_UI_COMPOSE_ACTION_DELETE_BMSON_BGM_MARKER: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before deleting a BMSON marker.";
        }
        const object_id_t event{action.compose_bmson_bgm_event_id};
        if (state.selected_objects.size() != 1 ||
            state.selected_objects.front().kind != editor_selection_object_kind_t::bgm_event ||
            state.selected_objects.front().id != event) {
            return "Select one BMSON BGM marker before deleting it.";
        }
        const auto deleted = delete_bmson_bgm_marker(
            history, action.compose_input_document_revision, state.chart, event);
        if (!deleted.diagnostics.empty()) {
            return deleted.diagnostics.front().message;
        }
        if (!deleted.applied) {
            return "BMSON marker deletion made no document change.";
        }
        state.selected_objects.clear();
        state.selected_notes.clear();
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? "BMSON marker deleted, but workspace rebinding failed."
                       : rebound.diagnostics.front().message;
        }
        return "BMSON BGM marker deleted.";
    }
    default:
        return std::nullopt;
    }
    return compose_result_status(result, status);
}

std::optional<std::string> dispatch_compose_beat_section_action(compose_action_context_t& context) {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    auto& workspace = context.workspace;
    auto& state = context.state;
    editor_interaction_result_t result;
    std::string status;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION:
    case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_END:
    case WORM_UI_COMPOSE_ACTION_DELETE_BEAT_SECTION: {
        if (state.virtual_bms_cursor_row &&
            (action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION ||
             action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN ||
             action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_END)) {
            return "Beat sections require a materialized cursor measure.";
        }
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before editing sections.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart) {
            return "Choose a chart before editing sections.";
        }
        marker_edit_action_t edit;
        bool section_added_from_selection = false;
        if (action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION) {
            const auto built = build_measure_index(*chart);
            const auto* measures = std::get_if<measure_index_t>(&built);
            if (!measures) {
                return "The chart measure geometry cannot define a section span.";
            }
            std::optional<position_span_t> span;
            if (state.retained_selection_range) {
                const auto begin = absolute_row(*measures, state.retained_selection_range->begin);
                const auto end = absolute_row(*measures, state.retained_selection_range->end);
                if (!begin || !end || *begin >= *end) {
                    return "The retained area/Space interval is no longer valid; select it again.";
                }
                span = state.retained_selection_range;
                section_added_from_selection = true;
            } else {
                const auto begin = absolute_row(*measures, state.cursor);
                if (!begin || state.active_snap_denominator == 0 ||
                    chart->divisions_per_standard_measure % state.active_snap_denominator != 0) {
                    return "The active Compose grid cannot define a section span.";
                }
                const auto step =
                    chart->divisions_per_standard_measure / state.active_snap_denominator;
                if (step == 0 || *begin >= measures->total_rows ||
                    step >= measures->total_rows - *begin) {
                    return "Move the cursor earlier to create a one-snap section.";
                }
                const auto end = timing_position_at_row(*measures, *begin + step);
                if (!end) {
                    return "The next active-snap row is outside the chart.";
                }
                span = position_span_t{state.cursor, *end};
            }
            edit = insert_section_action_t{
                .chart = state.chart,
                .label = action.compose_marker_label,
                .anchor = marker_anchor_t::beat,
                .beat_span = *span,
                .audio_millisecond_span = std::nullopt,
            };
        } else {
            if (action.compose_section_id == 0) {
                return "Choose a beat-anchored section before editing it.";
            }
            const auto section = std::ranges::find(
                chart->sections, object_id_t{action.compose_section_id}, &section_t::id);
            if (section == chart->sections.end() || section->anchor != marker_anchor_t::beat ||
                !section->beat_span) {
                return "That beat-anchored section is no longer available.";
            }
            if (action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_BEAT_SECTION) {
                edit = delete_section_action_t{.chart = state.chart, .section = section->id};
            } else {
                auto span = *section->beat_span;
                auto label = section->label;
                if (action.compose_action == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL) {
                    label = action.compose_marker_label;
                } else if (action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN) {
                    span.begin = state.cursor;
                } else {
                    span.end = state.cursor;
                }
                edit = update_section_action_t{
                    .chart = state.chart,
                    .section = section->id,
                    .label = std::move(label),
                    .anchor = marker_anchor_t::beat,
                    .beat_span = span,
                    .audio_millisecond_span = std::nullopt,
                };
            }
        }
        const auto applied =
            apply_marker_edit(history, action.compose_input_document_revision, std::move(edit));
        if (!applied.applied) {
            if (!applied.diagnostics.empty()) {
                return applied.diagnostics.front().message;
            }
            return "Section edit made no changes.";
        }
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty() ? "Section changed, but workspace rebinding failed."
                                               : rebound.diagnostics.front().message;
        }
        switch (action.compose_action) {
        case WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION:
            return section_added_from_selection
                       ? "Section added from the retained area/Space interval."
                       : "One-snap section added at the Compose cursor.";
        case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL:
            return "Section renamed.";
        case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN:
            return "Section start moved to the Compose cursor.";
        case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_END:
            return "Section end moved to the Compose cursor.";
        default:
            return "Section deleted.";
        }
    }
    default:
        return std::nullopt;
    }
    return compose_result_status(result, status);
}

std::optional<std::string> dispatch_compose_audio_bookmark_action(compose_action_context_t& context) {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    auto& workspace = context.workspace;
    auto& state = context.state;
    editor_interaction_result_t result;
    std::string status;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before editing bookmarks.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart) {
            return "Choose a chart before editing audio-time bookmarks.";
        }
        marker_edit_action_t edit;
        const auto deleting = action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK;
        std::optional<std::int64_t> milliseconds;
        if (!deleting) {
            milliseconds = parse_timing_signed(action.compose_marker_audio_begin);
            if (!milliseconds) {
                return "Enter the audio bookmark position as exact signed milliseconds.";
            }
        }
        if (action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK) {
            edit = insert_bookmark_action_t{
                .chart = state.chart,
                .label = action.compose_marker_label,
                .anchor = marker_anchor_t::audio_time,
                .position = std::nullopt,
                .audio_milliseconds = *milliseconds,
            };
        } else {
            if (action.compose_bookmark_id == 0) {
                return "Choose an audio-time bookmark before editing it.";
            }
            const auto bookmark = std::ranges::find(
                chart->bookmarks, object_id_t{action.compose_bookmark_id}, &bookmark_t::id);
            if (bookmark == chart->bookmarks.end() ||
                bookmark->anchor != marker_anchor_t::audio_time || !bookmark->audio_milliseconds ||
                bookmark->position) {
                return "That audio-time bookmark is no longer available.";
            }
            if (deleting) {
                edit = delete_bookmark_action_t{.chart = state.chart, .bookmark = bookmark->id};
            } else {
                edit = update_bookmark_action_t{
                    .chart = state.chart,
                    .bookmark = bookmark->id,
                    .label = action.compose_marker_label,
                    .anchor = marker_anchor_t::audio_time,
                    .position = std::nullopt,
                    .audio_milliseconds = *milliseconds,
                };
            }
        }
        const auto applied =
            apply_marker_edit(history, action.compose_input_document_revision, std::move(edit));
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Audio bookmark edit made no changes."
                                               : applied.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? "Audio bookmark changed, but workspace rebinding failed."
                       : rebound.diagnostics.front().message;
        }
        if (action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK) {
            return "Audio-time bookmark added.";
        }
        return deleting ? "Audio-time bookmark deleted." : "Audio-time bookmark updated.";
    }
    default:
        return std::nullopt;
    }
    return compose_result_status(result, status);
}

std::optional<std::string> dispatch_compose_audio_section_action(compose_action_context_t& context) {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    auto& workspace = context.workspace;
    auto& state = context.state;
    editor_interaction_result_t result;
    std::string status;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION:
    case WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_SECTION:
    case WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before editing sections.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart) {
            return "Choose a chart before editing audio-time sections.";
        }
        marker_edit_action_t edit;
        const auto deleting = action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION;
        std::optional<std::int64_t> begin;
        std::optional<std::int64_t> end;
        if (!deleting) {
            begin = parse_timing_signed(action.compose_marker_audio_begin);
            end = parse_timing_signed(action.compose_marker_audio_end);
            if (!begin || !end) {
                return "Enter both audio section bounds as exact signed milliseconds.";
            }
        }
        if (action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION) {
            edit = insert_section_action_t{
                .chart = state.chart,
                .label = action.compose_marker_label,
                .anchor = marker_anchor_t::audio_time,
                .beat_span = std::nullopt,
                .audio_millisecond_span = std::pair{*begin, *end},
            };
        } else {
            if (action.compose_section_id == 0) {
                return "Choose an audio-time section before editing it.";
            }
            const auto section = std::ranges::find(
                chart->sections, object_id_t{action.compose_section_id}, &section_t::id);
            if (section == chart->sections.end() ||
                section->anchor != marker_anchor_t::audio_time ||
                !section->audio_millisecond_span || section->beat_span) {
                return "That audio-time section is no longer available.";
            }
            if (deleting) {
                edit = delete_section_action_t{.chart = state.chart, .section = section->id};
            } else {
                edit = update_section_action_t{
                    .chart = state.chart,
                    .section = section->id,
                    .label = action.compose_marker_label,
                    .anchor = marker_anchor_t::audio_time,
                    .beat_span = std::nullopt,
                    .audio_millisecond_span = std::pair{*begin, *end},
                };
            }
        }
        const auto applied =
            apply_marker_edit(history, action.compose_input_document_revision, std::move(edit));
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Audio section edit made no changes."
                                               : applied.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? "Audio section changed, but workspace rebinding failed."
                       : rebound.diagnostics.front().message;
        }
        if (action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION) {
            return "Audio-time section added.";
        }
        return deleting ? "Audio-time section deleted." : "Audio-time section updated.";
    }
    default:
        return std::nullopt;
    }
    return compose_result_status(result, status);
}

std::optional<std::string> dispatch_compose_beat_bookmark_action(compose_action_context_t& context) {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    auto& workspace = context.workspace;
    auto& state = context.state;
    editor_interaction_result_t result;
    std::string status;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION:
    case WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK: {
        if (state.virtual_bms_cursor_row &&
            (action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK ||
             action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION)) {
            return "Beat bookmarks require a materialized cursor measure.";
        }
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before editing bookmarks.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart) {
            return "Choose a chart before editing bookmarks.";
        }
        marker_edit_action_t edit;
        if (action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK) {
            edit = insert_bookmark_action_t{
                .chart = state.chart,
                .label = action.compose_marker_label,
                .anchor = marker_anchor_t::beat,
                .position = state.cursor,
                .audio_milliseconds = std::nullopt,
            };
        } else {
            if (action.compose_bookmark_id == 0) {
                return "Choose a beat-anchored bookmark before editing it.";
            }
            const auto bookmark = std::ranges::find(
                chart->bookmarks, object_id_t{action.compose_bookmark_id}, &bookmark_t::id);
            if (bookmark == chart->bookmarks.end() || bookmark->anchor != marker_anchor_t::beat ||
                !bookmark->position) {
                return "That beat-anchored bookmark is no longer available.";
            }
            if (action.compose_action == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK ||
                action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION) {
                edit = update_bookmark_action_t{
                    .chart = state.chart,
                    .bookmark = bookmark->id,
                    .label = action.compose_action == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK
                                 ? action.compose_marker_label
                                 : bookmark->label,
                    .anchor = marker_anchor_t::beat,
                    .position =
                        action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION
                            ? std::optional{state.cursor}
                            : bookmark->position,
                    .audio_milliseconds = std::nullopt,
                };
            } else {
                edit = delete_bookmark_action_t{.chart = state.chart, .bookmark = bookmark->id};
            }
        }
        const auto applied =
            apply_marker_edit(history, action.compose_input_document_revision, std::move(edit));
        if (!applied.applied) {
            if (!applied.diagnostics.empty()) {
                return applied.diagnostics.front().message;
            }
            return action.compose_action == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK
                       ? "Bookmark label was unchanged."
                       : "Bookmark edit made no changes.";
        }
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty() ? "Bookmark changed, but workspace rebinding failed."
                                               : rebound.diagnostics.front().message;
        }
        if (action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK) {
            return "Bookmark added at the Compose cursor.";
        }
        if (action.compose_action == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK) {
            return "Bookmark renamed.";
        }
        return action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION
                   ? "Bookmark moved to the Compose cursor."
                   : "Bookmark deleted.";
    }
    default:
        return std::nullopt;
    }
    return compose_result_status(result, status);
}

std::optional<std::string> dispatch_compose_marker_action(compose_action_context_t& context) {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    auto& workspace = context.workspace;
    auto& state = context.state;
    auto& application_state = context.application_state;
    editor_interaction_result_t result;
    std::string status;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL:
    case WORM_UI_COMPOSE_ACTION_CLEAR_NOTE_LABEL: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before editing note labels.";
        }
        if (state.selected_notes.empty()) {
            return "Select one or more notes before editing their label.";
        }
        const auto label = action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL
                               ? std::optional<std::string>{action.compose_note_label}
                               : std::optional<std::string>{};
        const auto applied = apply_note_label_edit(
            history, action.compose_input_document_revision,
            {.chart = state.chart, .notes = state.selected_notes, .label = label});
        if (!applied.diagnostics.empty()) {
            return applied.diagnostics.front().message;
        }
        if (!applied.applied) {
            return label ? "Selected notes already have that label."
                         : "Selected notes already have no label.";
        }
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? "Note label changed, but workspace rebinding failed."
                       : rebound.diagnostics.front().message;
        }
        return label ? "Note label applied." : "Note labels cleared.";
    }
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
    case WORM_UI_COMPOSE_ACTION_NAVIGATE_NEXT_AUDIO_MARKER: {
        if (!application_state.transport_audio_milliseconds ||
            !application_state.transport_seek_audio_milliseconds) {
            return "Start chart preview once before navigating audio-time markers.";
        }
        const auto cursor = application_state.transport_audio_milliseconds();
        if (!cursor) {
            return "Start chart preview once to compile its seekable timeline.";
        }
        const auto direction =
            action.compose_action == WORM_UI_COMPOSE_ACTION_NAVIGATE_PREVIOUS_AUDIO_MARKER
                ? marker_navigation_direction_t::previous
                : marker_navigation_direction_t::next;
        const auto marker = navigate_markers(history.document, history.document.revision,
                                             state.chart, *cursor, direction);
        if (marker.status == marker_navigation_status_t::no_result) {
            return direction == marker_navigation_direction_t::previous
                       ? "No previous audio-time bookmark or section boundary."
                       : "No next audio-time bookmark or section boundary.";
        }
        if (!marker || !marker.item->audio_milliseconds) {
            return marker.diagnostics.empty()
                       ? "Audio-time marker navigation could not resolve an exact millisecond."
                       : marker.diagnostics.front().message;
        }
        return application_state.transport_seek_audio_milliseconds(
            *marker.item->audio_milliseconds);
    }
    case WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION:
    case WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_SECTION:
    case WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION:
        return dispatch_compose_audio_section_action(context);
    default:
        return std::nullopt;
    }
    return compose_result_status(result, status);
}

std::optional<std::string> dispatch_compose_semantic_layer_catalog_action(compose_action_context_t& context) {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    auto& workspace = context.workspace;
    auto& state = context.state;
    auto& application_state = context.application_state;
    editor_interaction_result_t result;
    std::string status;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER: {
        const auto* chart = selected_chart(history, workspace);
        if (!chart || action.compose_semantic_layer_id == 0) {
            return "Choose a visible semantic layer from the current Compose palette.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, layer_id_t{action.compose_semantic_layer_id});
        if (!layer) {
            return "That semantic-layer palette entry is no longer visible.";
        }
        application_state.semantic_layer_target = layer->id;
        return "Semantic-layer target: " + layer->name;
    }
    case WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE:
    case WORM_UI_COMPOSE_ACTION_NEXT_SEMANTIC_LAYER_PAGE: {
        const auto* chart = selected_chart(history, workspace);
        if (!chart) {
            return "Choose a chart before navigating semantic-layer pages.";
        }
        if (application_state.semantic_layer_palette_chart != chart->id) {
            application_state.semantic_layer_palette_chart = chart->id;
            application_state.semantic_layer_palette_page = 0;
        }
        const auto palette =
            make_semantic_layer_palette_window(*chart, application_state.semantic_layer_target,
                                               application_state.semantic_layer_palette_page, false,
                                               WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY);
        if (!palette) {
            return "Semantic-layer palette navigation exceeded its bounded limit.";
        }
        application_state.semantic_layer_palette_page = palette->page;
        if (action.compose_action == WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE) {
            if (application_state.semantic_layer_palette_page == 0) {
                return "Semantic-layer palette is already on its first page.";
            }
            --application_state.semantic_layer_palette_page;
        } else {
            if (palette->page_count == 0 ||
                application_state.semantic_layer_palette_page + 1 >= palette->page_count) {
                return "Semantic-layer palette is already on its last page.";
            }
            ++application_state.semantic_layer_palette_page;
        }
        return "Semantic-layer palette page " +
               std::to_string(application_state.semantic_layer_palette_page + 1) + " of " +
               std::to_string(palette->page_count) + ".";
    }
    case WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before adding a semantic layer.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart) {
            return "Choose a chart before adding a semantic layer.";
        }
        std::string name = "Layer";
        for (std::size_t suffix = 2;; ++suffix) {
            const auto duplicate = std::ranges::any_of(
                chart->semantic_layers, [&](const auto& layer) { return layer.name == name; });
            if (!duplicate) {
                break;
            }
            name = "Layer " + std::to_string(suffix);
        }
        constexpr std::array<semantic_layer_rgba_t, 6> colors{{
            {0.91, 0.35, 0.31, 1.0},
            {0.95, 0.67, 0.24, 1.0},
            {0.36, 0.74, 0.36, 1.0},
            {0.25, 0.62, 0.91, 1.0},
            {0.58, 0.42, 0.84, 1.0},
            {0.87, 0.38, 0.68, 1.0},
        }};
        const auto applied = apply_semantic_layer_edit(
            history, action.compose_input_document_revision,
            insert_semantic_layer_action_t{state.chart, std::move(name),
                                           colors[chart->semantic_layers.size() % colors.size()]});
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Semantic layer was not added."
                                               : applied.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(workspace, history);
        return rebound ? "Semantic layer added."
                       : (rebound.diagnostics.empty()
                              ? "Semantic layer added, but workspace rebinding failed."
                              : rebound.diagnostics.front().message);
    }
    case WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before renaming a semantic layer.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a visible semantic layer before renaming it.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
        if (!layer) {
            return "The selected semantic layer is no longer visible.";
        }
        const auto applied = apply_semantic_layer_edit(
            history, action.compose_input_document_revision,
            update_semantic_layer_action_t{
                .chart = state.chart,
                .layer = layer->id,
                .name = action.compose_semantic_layer_name,
                .color = {.red = static_cast<double>(layer->color.red) / 255.0,
                          .green = static_cast<double>(layer->color.green) / 255.0,
                          .blue = static_cast<double>(layer->color.blue) / 255.0,
                          .alpha = static_cast<double>(layer->color.alpha) / 255.0},
                .visible = layer->visible,
                .locked = layer->locked});
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Semantic-layer name was unchanged."
                                               : applied.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(workspace, history);
        return rebound ? "Semantic layer renamed."
                       : (rebound.diagnostics.empty()
                              ? "Semantic layer renamed, but workspace rebinding failed."
                              : rebound.diagnostics.front().message);
    }
    case WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before recoloring a semantic "
                   "layer.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a visible semantic layer before recoloring it.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
        if (!layer) {
            return "The selected semantic layer is no longer visible.";
        }
        const auto color = semantic_layer_rgba_t{
            .red = static_cast<double>(action.compose_semantic_layer_red) / 255.0,
            .green = static_cast<double>(action.compose_semantic_layer_green) / 255.0,
            .blue = static_cast<double>(action.compose_semantic_layer_blue) / 255.0,
            .alpha = static_cast<double>(action.compose_semantic_layer_alpha) / 255.0,
        };
        const auto applied = apply_semantic_layer_edit(
            history, action.compose_input_document_revision,
            update_semantic_layer_action_t{state.chart, layer->id, layer->name, color,
                                           layer->visible, layer->locked});
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Semantic-layer color was unchanged."
                                               : applied.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(workspace, history);
        return rebound ? "Semantic-layer color changed."
                       : (rebound.diagnostics.empty()
                              ? "Semantic-layer color changed, but workspace rebinding failed."
                              : rebound.diagnostics.front().message);
    }
    case WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before deleting a semantic layer.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a visible semantic layer before deleting it.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
        if (!layer) {
            return "The selected semantic layer is no longer visible.";
        }
        const auto layer_id = layer->id;
        const auto applied = apply_semantic_layer_edit(
            history, action.compose_input_document_revision,
            delete_semantic_layer_action_t{
                state.chart, layer_id,
                action.compose_action ==
                    WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS});
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Semantic layer was not deleted."
                                               : applied.diagnostics.front().message;
        }
        application_state.required_semantic_layer_filters.erase(
            std::remove(application_state.required_semantic_layer_filters.begin(),
                        application_state.required_semantic_layer_filters.end(), layer_id),
            application_state.required_semantic_layer_filters.end());
        application_state.excluded_semantic_layer_filters.erase(
            std::remove(application_state.excluded_semantic_layer_filters.begin(),
                        application_state.excluded_semantic_layer_filters.end(), layer_id),
            application_state.excluded_semantic_layer_filters.end());
        application_state.semantic_layer_target.reset();
        application_state.semantic_layer_paint_mode_active = false;
        application_state.semantic_layer_solo_active = false;
        const auto rebound = rebind_workspace(workspace, history);
        return rebound ? "Semantic layer deleted."
                       : (rebound.diagnostics.empty()
                              ? "Semantic layer deleted, but workspace rebinding failed."
                              : rebound.diagnostics.front().message);
    }
    case WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP:
    case WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_DOWN: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before reordering semantic layers.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a visible semantic layer before reordering it.";
        }
        const auto found =
            std::ranges::find(chart->semantic_layers, *application_state.semantic_layer_target,
                              &semantic_layer_t::id);
        if (found == chart->semantic_layers.end()) {
            return "The selected semantic layer is no longer available.";
        }
        const auto index =
            static_cast<std::size_t>(std::distance(chart->semantic_layers.begin(), found));
        const auto moving_up =
            action.compose_action == WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP;
        if ((moving_up && index == 0) ||
            (!moving_up && index + 1 == chart->semantic_layers.size())) {
            return "Semantic layer is already at that palette boundary.";
        }
        std::vector<layer_id_t> ordered;
        ordered.reserve(chart->semantic_layers.size());
        for (const auto& layer : chart->semantic_layers)
            ordered.push_back(layer.id);
        std::swap(ordered[index], ordered[moving_up ? index - 1 : index + 1]);
        const auto applied = apply_semantic_layer_edit(
            history, action.compose_input_document_revision,
            reorder_semantic_layers_action_t{state.chart, std::move(ordered)});
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Semantic-layer order was unchanged."
                                               : applied.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(workspace, history);
        return rebound ? "Semantic-layer order updated."
                       : (rebound.diagnostics.empty()
                              ? "Semantic-layer order updated, but workspace rebinding failed."
                              : rebound.diagnostics.front().message);
    }
    case WORM_UI_COMPOSE_ACTION_REMOVE_SEMANTIC_LAYER:
    default:
        return std::nullopt;
    }
    return compose_result_status(result, status);
}

std::optional<std::string> dispatch_compose_semantic_layer_state_action(compose_action_context_t& context) {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.history;
    auto& workspace = context.workspace;
    auto& state = context.state;
    auto& application_state = context.application_state;
    editor_interaction_result_t result;
    std::string status;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before changing layer visibility.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a semantic layer before changing its visibility.";
        }
        const auto layer =
            std::ranges::find(chart->semantic_layers, *application_state.semantic_layer_target,
                              &semantic_layer_t::id);
        if (layer == chart->semantic_layers.end()) {
            application_state.semantic_layer_target.reset();
            return "The selected semantic layer is no longer available.";
        }
        const auto was_visible = layer->visible;
        const auto applied = apply_semantic_layer_edit(
            history, action.compose_input_document_revision,
            update_semantic_layer_action_t{
                state.chart,
                layer->id,
                layer->name,
                {.red = static_cast<double>(layer->color.red) / 255.0,
                 .green = static_cast<double>(layer->color.green) / 255.0,
                 .blue = static_cast<double>(layer->color.blue) / 255.0,
                 .alpha = static_cast<double>(layer->color.alpha) / 255.0},
                !was_visible,
                layer->locked});
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Semantic-layer visibility was unchanged."
                                               : applied.diagnostics.front().message;
        }
        if (was_visible) {
            application_state.semantic_layer_paint_mode_active = false;
            application_state.semantic_layer_solo_active = false;
        }
        const auto rebound = rebind_workspace(workspace, history);
        return rebound ? (was_visible ? "Semantic layer hidden." : "Semantic layer shown.")
                       : (rebound.diagnostics.empty()
                              ? "Semantic-layer visibility changed, but workspace rebinding failed."
                              : rebound.diagnostics.front().message);
    }
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER:
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_EXCLUDE_FILTER: {
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target ||
            !visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target)) {
            return "Choose a visible semantic layer before filtering by it.";
        }
        if (application_state.semantic_layer_filter_chart != chart->id) {
            application_state.semantic_layer_filter_chart = chart->id;
            application_state.required_semantic_layer_filters.clear();
            application_state.excluded_semantic_layer_filters.clear();
        }
        const auto require =
            action.compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER;
        auto& enabled = require ? application_state.required_semantic_layer_filters
                                : application_state.excluded_semantic_layer_filters;
        auto& opposite = require ? application_state.excluded_semantic_layer_filters
                                 : application_state.required_semantic_layer_filters;
        const auto target = *application_state.semantic_layer_target;
        if (const auto found = std::ranges::find(enabled, target); found != enabled.end()) {
            enabled.erase(found);
            return require ? "Required semantic-layer filter removed."
                           : "Excluded semantic-layer filter removed.";
        }
        opposite.erase(std::remove(opposite.begin(), opposite.end(), target), opposite.end());
        enabled.push_back(target);
        return require ? "Semantic layer required in the view filter."
                       : "Semantic layer excluded from the view filter.";
    }
    case WORM_UI_COMPOSE_ACTION_CLEAR_SEMANTIC_LAYER:
        application_state.semantic_layer_target.reset();
        application_state.semantic_layer_paint_mode_active = false;
        application_state.semantic_layer_solo_active = false;
        return "Semantic-layer target cleared.";
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_PAINT_MODE: {
        if (editor_interaction_has_pending_gesture(state)) {
            result = dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
            if (!result.accepted) {
                return compose_result_status(result, "Compose gesture canceled.");
            }
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a visible semantic layer before painting note membership.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
        if (!layer || layer->locked) {
            application_state.semantic_layer_paint_mode_active = false;
            return "Choose an unlocked visible semantic layer before painting note membership.";
        }
        application_state.semantic_layer_paint_mode_active =
            !application_state.semantic_layer_paint_mode_active;
        if (application_state.semantic_layer_paint_mode_active) {
            application_state.note_paint_mode_active = false;
        }
        return application_state.semantic_layer_paint_mode_active
                   ? "Semantic-layer paint mode enabled."
                   : "Semantic-layer paint mode disabled.";
    }
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_SOLO: {
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a visible semantic layer before soloing it.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
        if (!layer) {
            application_state.semantic_layer_solo_active = false;
            return "The selected semantic layer is no longer visible.";
        }
        application_state.semantic_layer_solo_active =
            !application_state.semantic_layer_solo_active;
        return application_state.semantic_layer_solo_active ? "Semantic-layer solo enabled."
                                                            : "Semantic-layer solo disabled.";
    }
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before changing layer lock state.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a visible semantic layer before changing its lock state.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
        if (!layer) {
            application_state.semantic_layer_target.reset();
            application_state.semantic_layer_paint_mode_active = false;
            application_state.semantic_layer_solo_active = false;
            return "The selected semantic layer is no longer visible.";
        }
        const auto was_locked = layer->locked;
        const auto applied = apply_semantic_layer_edit(
            history, action.compose_input_document_revision,
            update_semantic_layer_action_t{
                .chart = state.chart,
                .layer = layer->id,
                .name = layer->name,
                .color = {.red = static_cast<double>(layer->color.red) / 255.0,
                          .green = static_cast<double>(layer->color.green) / 255.0,
                          .blue = static_cast<double>(layer->color.blue) / 255.0,
                          .alpha = static_cast<double>(layer->color.alpha) / 255.0},
                .visible = layer->visible,
                .locked = !was_locked});
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Semantic-layer lock state was unchanged."
                                               : applied.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? "Semantic-layer lock changed, but workspace rebinding failed."
                       : rebound.diagnostics.front().message;
        }
        if (!was_locked) {
            application_state.semantic_layer_paint_mode_active = false;
        }
        return was_locked ? "Semantic layer unlocked." : "Semantic layer locked.";
    }
    case WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER_NOTES: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before selecting by layer.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target) {
            return "Choose a visible semantic layer before selecting its notes.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
        if (!layer) {
            application_state.semantic_layer_target.reset();
            application_state.semantic_layer_paint_mode_active = false;
            application_state.semantic_layer_solo_active = false;
            return "The selected semantic layer is no longer visible.";
        }
        std::vector<object_id_t> notes;
        for (const auto& measure : chart->measures) {
            for (const auto& note : measure.notes) {
                if (std::ranges::find(note.semantic_layers, layer->id) !=
                    note.semantic_layers.end()) {
                    notes.push_back(note.id);
                }
            }
        }
        result =
            dispatch_editor_action(history, state, select_editor_notes_action_t{std::move(notes)});
        return compose_result_status(result, "Semantic-layer notes selected.");
    }
    case WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER:
    case WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER: {
        if (editor_interaction_has_pending_gesture(state)) {
            return "Finish or cancel the active Compose gesture before editing semantic layers.";
        }
        const auto* chart = selected_chart(history, workspace);
        if (!chart || !application_state.semantic_layer_target || state.selected_notes.empty()) {
            return "Choose a visible semantic layer and select playable notes first.";
        }
        const auto* layer =
            visible_semantic_layer_by_id(*chart, *application_state.semantic_layer_target);
        if (!layer) {
            application_state.semantic_layer_target.reset();
            application_state.semantic_layer_paint_mode_active = false;
            application_state.semantic_layer_solo_active = false;
            return "The selected semantic layer is no longer visible.";
        }
        if (layer->locked) {
            return "Unlock the selected semantic layer before changing note membership.";
        }
        semantic_layer_edit_action_t edit;
        if (action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER) {
            edit = assign_notes_to_semantic_layer_action_t{state.chart, layer->id,
                                                           state.selected_notes};
        } else if (action.compose_action == WORM_UI_COMPOSE_ACTION_REMOVE_SEMANTIC_LAYER) {
            edit = remove_notes_from_semantic_layer_action_t{state.chart, layer->id,
                                                             state.selected_notes};
        } else {
            edit = toggle_semantic_layer_membership_action_t{state.chart, layer->id,
                                                             state.selected_notes};
        }
        const auto applied = apply_semantic_layer_edit(
            history, action.compose_input_document_revision, std::move(edit));
        if (!applied.applied) {
            return applied.diagnostics.empty() ? "Semantic-layer membership was unchanged."
                                               : applied.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? "Semantic-layer membership changed, but workspace rebinding failed."
                       : rebound.diagnostics.front().message;
        }
        return "Semantic-layer membership updated.";
    }
    default:
        return std::nullopt;
    }
    return compose_result_status(result, status);
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
    if (auto response = dispatch_compose_view_action_group(context)) return *response;
    if (auto response = dispatch_compose_resource_action(context)) return *response;
    if (auto response = dispatch_compose_bmson_action(context)) return *response;
    if (auto response = dispatch_compose_marker_action(context)) return *response;
    if (auto response = dispatch_compose_semantic_layer_catalog_action(context)) return *response;
    if (auto response = dispatch_compose_semantic_layer_state_action(context)) return *response;
    return compose_result_status(result, status);
}



} // namespace wormedit::sys
