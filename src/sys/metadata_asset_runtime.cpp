#include "sys/metadata_asset_runtime.h"
#include "sys/thread_priority.h"
#include <wormedit/core/async_job.h>

#include "ui/frontend.h"

#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/task_graph.h>

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/decoder.h>
#endif

#include <algorithm>
#include <memory>
#include <ranges>
#include <span>
#include <stop_token>
#include <utility>

namespace wormedit::sys {
namespace runtime {
namespace {

struct metadata_asset_import_task_result_t {
    wormedit::project_asset_copy_outcome_t copy{std::unexpected(
        wormedit::error_t<wormedit::project_asset_copy_status_t>{
            wormedit::project_asset_copy_status_t::invalid_request, {}})};
    std::optional<wormedit::audio_stream_info_t> stream;
    std::string decode_error;
};

struct pending_metadata_asset_import_t {
    wormedit::task_graph_handle_t graph;
    wormedit::task_id_t task;
    wormedit::song_id_t song;
    std::uint64_t revision{};
    wormedit::chart_id_t chart;
    wormedit::asset_id_t asset;
    std::filesystem::path source_path;
    std::filesystem::path output_path;
    bool export_copy{};
    bool allocate_bms_identifier{};
};

struct metadata_copy_inspection_request_t {
    wormedit::project_asset_copy_request_t copy;
    std::filesystem::path output_path;
    bool export_copy{};
};
using metadata_copy_inspection_job_t =
    wormedit::async_job_t<metadata_copy_inspection_request_t,
                          wormedit::project_asset_copy_outcome_t, wormedit::notification_source_t>;

struct metadata_copy_preview_t {
    wormedit::task_scheduler_t scheduler{1, 1, wormedit::background_scheduler_options()};
    metadata_copy_inspection_job_t job{scheduler,
                                       1,
                                       "Copy inspection could not start.",
                                       "scheduler outstanding-task capacity exceeded",
                                       "Copy inspection returned no result.",
                                       wormedit::async_job_destruction_policy_t::release};
    std::optional<metadata_copy_preview_key_t> key;
    std::string message;
};

auto inspect_copy(const metadata_copy_inspection_request_t& request, std::stop_token stop)
    -> metadata_copy_inspection_job_t::work_outcome_t {
    using namespace wormedit;
    auto inspected =
        request.export_copy
            ? inspect_export_asset_file({.project_directory = request.copy.project_directory,
                                         .source_path = request.copy.source_path,
                                         .relative_output_path = request.output_path,
                                         .asset = request.copy.asset,
                                         .maximum_bytes = request.copy.maximum_bytes},
                                        stop)
            : inspect_project_asset_file(request.copy, stop);
    return metadata_copy_inspection_job_t::work_outcome_t{std::in_place, std::move(inspected)};
}

struct metadata_import_feedback_t {
    std::string message;
    wormedit::notification_severity_t severity{wormedit::notification_severity_t::error};
    wormedit::notification_source_t source{wormedit::notification_source_t::import_};
    bool linked{};
};

auto post_runtime_message(const wormedit::notification_sink_t& notifications,
                          wormedit::notification_severity_t severity,
                          wormedit::notification_source_t source, std::string_view message)
    -> void {
    notifications.post(severity, source, message);
}

auto post_start_result(const wormedit::notification_sink_t& notifications,
                       wormedit::notification_severity_t severity,
                       wormedit::notification_source_t source, std::string message) -> std::string {
    post_runtime_message(notifications, severity, source, message);
    return message;
}

auto link_imported_metadata_audio(
    const pending_metadata_asset_import_t& request,
    const metadata_asset_import_task_result_t& imported,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    std::string& status, wormedit::notification_severity_t& severity,
    wormedit::notification_source_t& source) -> bool {
    auto name = request.source_path.filename().generic_string();
    if (name.size() > 255)
        name.clear();
    const auto applied = wormedit::apply_primary_music_set(
        history,
        {.source_path = request.source_path,
         .copied_project_relative_path =
             request.export_copy ? std::optional<std::filesystem::path>{}
                                 : std::optional{imported.copy->relative_path},
         .expected_new_asset_id = request.asset,
         .name = std::move(name),
         .allocate_bms_identifier = request.allocate_bms_identifier,
         .audio_stream = imported.stream,
         .image_geometry = std::nullopt});
    if (!applied.diagnostics.empty()) {
        source = wormedit::notification_source_t::document;
        status = applied.diagnostics.front().message;
        severity = to_notification_severity(applied.diagnostics.front().severity);
        return false;
    }
    if (!applied.applied) {
        source = wormedit::notification_source_t::document;
        status = "Copied audio could not be registered.";
        severity = wormedit::notification_severity_t::error;
        return false;
    }
    const auto rebound = wormedit::workspace_action_result_t::rebind(workspace, history);
    if (!(rebound.value && rebound.diagnostics.empty())) {
        source = wormedit::notification_source_t::document;
        severity = rebound.diagnostics.empty()
                       ? wormedit::notification_severity_t::error
                       : to_notification_severity(rebound.diagnostics.front().severity);
        status = rebound.diagnostics.empty()
                     ? "Audio imported, but workspace rebinding failed."
                     : rebound.diagnostics.front().message;
    } else {
        severity = wormedit::notification_severity_t::success;
        const auto& resources = history.document.resources;
        const auto music = std::ranges::find_if(resources, [](const auto& resource) {
            return std::ranges::find(resource.roles, wormedit::resource_role_t::music) !=
                   resource.roles.end();
        });
        (void)wormedit::workspace_action_result_t::from(
            workspace, history.document,
            wormedit::workspace_select_asset_action_t{
                .asset = music != resources.end() ? music->asset : request.asset,
                .slot = wormedit::workspace_asset_slot_t::audio});
        status = request.export_copy
                     ? "Audio staged in export assets and set as the song audio."
                     : "Audio copied into project assets and set as the song audio.";
    }
    return true;
}

auto failed_import_feedback(const wormedit::task_outcome_t& completed,
                            const std::shared_ptr<const metadata_asset_import_task_result_t>& imported)
    -> std::optional<metadata_import_feedback_t> {
    using namespace wormedit;
    if (!completed.has_value() || !imported) {
        const auto cancelled = !completed.has_value() &&
                               completed.error().cause.code == task_error_code_t::cancelled;
        return metadata_import_feedback_t{
            .message = !completed.has_value() ? completed.error().cause.message
                                       : "Project import-copy returned no result.",
            .severity = cancelled ? notification_severity_t::info
                                  : notification_severity_t::error};
    }
    if (!imported->copy) {
        const auto status = imported->copy.error().code;
        const auto severity = status == project_asset_copy_status_t::cancelled
                                  ? notification_severity_t::info
                              : status == project_asset_copy_status_t::destination_collision
                                  ? notification_severity_t::warning
                                  : notification_severity_t::error;
        return metadata_import_feedback_t{
            .message = imported->decode_error.empty() ? imported->copy.error().message
                                                      : imported->decode_error,
            .severity = severity};
    }
    if (!imported->stream)
        return metadata_import_feedback_t{
            .message = imported->decode_error.empty() ? "Copied audio could not be decoded."
                                                      : imported->decode_error};
    return std::nullopt;
}

auto complete_import_feedback(
    const pending_metadata_asset_import_t& request, const wormedit::task_outcome_t& completed,
    const std::shared_ptr<const metadata_asset_import_task_result_t>& imported,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace)
    -> metadata_import_feedback_t {
    using namespace wormedit;
    auto result = metadata_import_feedback_t{
        .source = request.export_copy ? notification_source_t::export_
                                     : notification_source_t::import_};
    if (auto failure = failed_import_feedback(completed, imported)) {
        failure->source = result.source;
        return *failure;
    }
    if (!workspace.selected_chart) {
        result.message = "Document changed before copied audio could be linked.";
        result.severity = notification_severity_t::warning;
        return result;
    }
    if (!chart_stamp_t{.document = {.project = request.song, .revision = request.revision},
                       .chart = request.chart}.is_current(history.document, *workspace.selected_chart)) {
        result.message = stale_action_message("Metadata audio copy linking");
        result.severity = notification_severity_t::warning;
        return result;
    }
    result.linked = link_imported_metadata_audio(request, *imported, history, workspace,
                                                result.message, result.severity, result.source);
    return result;
}

auto copy_preview_feedback(const metadata_copy_inspection_job_t::completion_t& completed)
    -> std::pair<std::string, wormedit::notification_severity_t> {
    using namespace wormedit;
    if (!completed) {
        return {completed.error().cause.message,
                completed.error().cause.code == task_error_code_t::cancelled
                    ? notification_severity_t::info
                    : notification_severity_t::error};
    }
    const auto& inspected = completed->payload;
    using copy_status_t = project_asset_copy_status_t;
    const auto status = inspected->has_value() ? (*inspected)->status : inspected->error().code;
    const auto severity = status == copy_status_t::destination_collision
                              ? notification_severity_t::warning
                          : status == copy_status_t::not_staged ||
                                    status == copy_status_t::reused ||
                                    status == copy_status_t::cancelled
                              ? notification_severity_t::info
                              : notification_severity_t::error;
    auto message = inspected->has_value() ? std::string{} : inspected->error().message;
    if (status == copy_status_t::not_staged)
        message = "Destination is free; copy will create it.";
    else if (status == copy_status_t::reused)
        message = "Identical file exists; copy will reuse it.";
    else if (status == copy_status_t::destination_collision)
        message = "Different file occupies destination; copy will preserve it.";
    else if (message.empty())
        message = "Copy destination cannot be used.";
    return {std::move(message), severity};
}

auto process_pending_metadata_asset_import(
    std::optional<pending_metadata_asset_import_t>& pending_import,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    const std::filesystem::path& asset_base, const wormedit::notification_sink_t& notifications)
    -> std::optional<std::string> {
    if (!pending_import)
        return std::nullopt;
    const auto completed = pending_import->graph.outcome(pending_import->task);
    if (!completed)
        return std::nullopt;
    const auto request = std::move(*pending_import);
    pending_import.reset();
    const auto imported =
        wormedit::task_value_cast<metadata_asset_import_task_result_t>(*completed);
    auto feedback = complete_import_feedback(request, *completed, imported, history, workspace);
    if (imported && imported->copy && imported->copy->status ==
                        wormedit::project_asset_copy_status_t::copied &&
        !feedback.linked) {
        const wormedit::project_asset_copy_request_t copy_request{
            .project_directory = asset_base,
            .source_path = request.source_path,
            .asset = request.asset};
        const bool discarded = request.export_copy
                                   ? wormedit::discard_unlinked_export_asset_copy(
                                         {.project_directory = asset_base,
                                          .source_path = request.source_path,
                                          .relative_output_path = request.output_path,
                                          .asset = request.asset},
                                         *imported->copy)
                                   : wormedit::discard_unlinked_project_asset_copy(
                                         copy_request, *imported->copy);
        if (!discarded)
            feedback.message += " Published copy remains; review it before removal.";
    }
    post_runtime_message(notifications, feedback.severity, feedback.source, feedback.message);
    return feedback.message;
}

void process_metadata_copy_preview(metadata_copy_preview_t& runtime,
                                   const wormedit::notification_sink_t& notifications) {
    const auto completed = runtime.job.poll();
    if (!completed)
        return;
    const auto source =
        completed->has_value() ? completed->value().identity : completed->error().identity;
    auto [message, severity] = copy_preview_feedback(*completed);
    runtime.message = std::move(message);
    post_runtime_message(notifications, severity, source, runtime.message);
}

auto resolve_export_copy_path(const WormUiWorkspaceAction& action,
                              const wormedit::document_history_t& history,
                              const wormedit::workspace_controller_t& workspace,
                              std::filesystem::path& output_path) -> std::optional<std::string> {
    if (action.compose_output_group_index >= history.document.output_groups.size()) {
        return "The selected output group is no longer available.";
    }
    const auto& group = history.document.output_groups[action.compose_output_group_index];
    if (std::ranges::find(group.charts, *workspace.selected_chart) == group.charts.end()) {
        return "The selected output group no longer includes this difficulty.";
    }
    output_path = group.relative_output_path;
    return std::nullopt;
}

auto resolve_copy_preview_source(const WormUiWorkspaceAction& action,
                                 const wormedit::document_history_t& history,
                                 wormedit::asset_id_t asset, bool import_copy,
                                 std::filesystem::path& source_path) -> std::optional<std::string> {
    using namespace wormedit;
    if (import_copy) {
        const auto reserved = next_resource_asset_id(history.document);
        source_path = std::filesystem::path{action.compose_import_path}.lexically_normal();
        if (!reserved || *reserved != asset || !source_path.is_absolute() ||
            source_path.filename().empty())
            return "External audio or its reserved asset ID changed before inspection.";
        if (std::ranges::any_of(history.document.audio_assets, [&](const auto& audio) {
                return audio.source_path.lexically_normal() == source_path;
            }))
            return "This external audio is already linked; inspect its existing asset.";
        return std::nullopt;
    }
    const auto audio = history.document.find_audio_asset(asset);
    if (!asset || audio == nullptr)
        return "Selected music asset is no longer available.";
    source_path = audio->source_path;
    return std::nullopt;
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
auto copy_and_decode_metadata_audio(const wormedit::project_asset_copy_request_t& copy_request,
                                    const std::filesystem::path& output_path, bool export_copy,
                                    std::stop_token stop) -> wormedit::task_result_t {
    using namespace wormedit;
    auto completed = std::make_shared<metadata_asset_import_task_result_t>();
    const auto validate = [&](const std::filesystem::path& temporary) {
        audio::decoder_t decoder;
        if (!decoder.open(temporary, &completed->decode_error))
            return false;
        const auto info = decoder.info();
        return info && info->source_sample_rate != 0 && info->source_channel_count != 0 &&
               info->source_channel_count <= 32;
    };
    if (export_copy) {
        completed->copy =
            create_export_asset_copy({.project_directory = copy_request.project_directory,
                                            .source_path = copy_request.source_path,
                                            .relative_output_path = output_path,
                                            .asset = copy_request.asset,
                                            .maximum_bytes = copy_request.maximum_bytes,
                                            .validate_temporary = validate},
                                           stop);
    } else {
        auto request = copy_request;
        request.validate_temporary = validate;
        completed->copy = create_project_asset_copy(request, stop);
    }
    if (completed->copy && !stop.stop_requested()) {
        audio::decoder_t decoder;
        if (decoder.open(completed->copy->absolute_path, &completed->decode_error)) {
            if (const auto info = decoder.info(); info && info->source_sample_rate != 0 &&
                                                  info->source_channel_count != 0 &&
                                                  info->source_channel_count <= 32) {
                completed->stream = audio_stream_info_t{.sample_rate = info->source_sample_rate,
                                                        .channels = info->source_channel_count,
                                                        .frame_count = info->source_frame_count,
                                                        .seekable = info->seekable};
            }
        }
        if (!completed->stream && completed->decode_error.empty())
            completed->decode_error = "Published audio has invalid stream geometry.";
    }
    if (stop.stop_requested() && completed->copy && completed->copy->status == project_asset_copy_status_t::copied) {
        if (export_copy)
            (void)discard_unlinked_export_asset_copy(
                {.project_directory = copy_request.project_directory,
                 .source_path = copy_request.source_path,
                 .relative_output_path = output_path,
                 .asset = copy_request.asset},
                *completed->copy);
        else
            (void)discard_unlinked_project_asset_copy(copy_request, *completed->copy);
    }
    return task_result_t::success(task_value_t::from(
        std::shared_ptr<const metadata_asset_import_task_result_t>{std::move(completed)}));
}
#endif

} // namespace

struct metadata_asset_t::impl_t {
    std::optional<pending_metadata_asset_import_t> pending_import;
    metadata_copy_preview_t copy_preview;
    notification_sink_t notifications{notification_sink_t::muted()};
};

metadata_asset_t::metadata_asset_t(notification_sink_t notifications)
    : impl_(std::make_unique<impl_t>()) {
    impl_->notifications = std::move(notifications);
}
metadata_asset_t::~metadata_asset_t() = default;

auto metadata_asset_t::start_copy_preview(
    const WormUiWorkspaceAction& action, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace,
    const std::filesystem::path& asset_base) -> std::string {
    using namespace wormedit;
    auto& runtime = impl_->copy_preview;
    const auto source = action.metadata_inspect_export ? notification_source_t::export_
                       : action.metadata_inspect_import ? notification_source_t::import_
                                                        : notification_source_t::document;
    const auto finish = [&](std::string message, notification_severity_t severity) {
        return post_start_result(impl_->notifications, severity, source, std::move(message));
    };
    if (!workspace.selected_chart)
        return finish("Metadata copy inspection expired after the chart or document changed.", notification_severity_t::warning);
    if (workspace.active_tab != workspace_tab_t::metadata ||
        !chart_stamp_t{.document = {.project = history.document.id,
                                   .revision = action.compose_input_document_revision},
                       .chart = chart_id_t{action.compose_input_chart_id}}
             .is_current(history.document, *workspace.selected_chart))
        return finish(stale_action_message("Metadata copy inspection"), notification_severity_t::warning);
    if (asset_base.empty())
        return finish("Open a saved native project before inspecting asset copies.",
                      notification_severity_t::warning);
    if (runtime.job.is_busy())
        return finish("Wait for the current copy inspection to finish.",
                      notification_severity_t::info);
    const bool import_copy = action.metadata_inspect_import;
    const asset_id_t asset{action.compose_resource_asset_id};
    std::filesystem::path source_path;
    if (auto error = resolve_copy_preview_source(action, history, asset, import_copy,
                                                 source_path))
        return finish(*error, notification_severity_t::warning);
    std::filesystem::path output_path;
    if (action.metadata_inspect_export) {
        if (auto error = resolve_export_copy_path(action, history, workspace, output_path)) {
            return finish(*error, notification_severity_t::warning);
        }
    }
    const project_asset_copy_request_t request{
        .project_directory = asset_base, .source_path = source_path, .asset = asset};
    const auto started = runtime.job.request(
        [&request, &output_path, &action] {
            return metadata_copy_inspection_request_t{.copy = request,
                                                      .output_path = output_path,
                                                      .export_copy =
                                                          action.metadata_inspect_export};
        },
        source, inspect_copy);
    if (!started)
        return finish(started.error().message, notification_severity_t::error);
    runtime.key =
        metadata_copy_preview_key_t{.song = history.document.id,
                                    .revision = history.document.revision,
                                    .chart = *workspace.selected_chart,
                                    .asset = asset,
                                    .source_path = source_path,
                                    .output_path = output_path,
                                    .output_group_index = action.compose_output_group_index,
                                    .export_copy = action.metadata_inspect_export,
                                    .import_copy = import_copy};
    runtime.message = "Inspecting destination bytes in the background...";
    return finish("Copy destination inspection started.", notification_severity_t::info);
}

auto metadata_asset_t::start_import_copy(
    const WormUiWorkspaceAction& action, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace,
    const std::filesystem::path& asset_base, wormedit::task_scheduler_t& scheduler,
    bool resource_copy_busy, bool export_busy) -> std::string {
    using namespace wormedit;
    const auto source = action.metadata_import_copy_export ? notification_source_t::export_
                                                           : notification_source_t::import_;
    const auto finish = [&](std::string message, notification_severity_t severity) {
        return post_start_result(impl_->notifications, severity, source, std::move(message));
    };
#if !defined(WORMEDIT_HAS_AUDIO_DECODER)
    (void)action;
    (void)history;
    (void)workspace;
    (void)asset_base;
    (void)scheduler;
    (void)resource_copy_busy;
    (void)export_busy;
    return finish("Audio import is unavailable in this build.", notification_severity_t::warning);
#else
    auto& pending_import = impl_->pending_import;
    if (!workspace.selected_chart)
        return finish("Metadata import-copy expired after the chart or document changed.", notification_severity_t::warning);
    if (workspace.active_tab != workspace_tab_t::metadata ||
        !chart_stamp_t{.document = {.project = history.document.id,
                                   .revision = action.compose_input_document_revision},
                       .chart = chart_id_t{action.compose_input_chart_id}}
             .is_current(history.document, *workspace.selected_chart))
        return finish(stale_action_message("Metadata import-copy"), notification_severity_t::warning);
    if (asset_base.empty())
        return finish("Open a saved native project before importing an asset copy.",
                      notification_severity_t::warning);
    const bool export_copy = action.metadata_import_copy_export;
    if (export_copy == action.metadata_import_copy_project)
        return finish("Choose exactly one project or export import-copy destination.",
                      notification_severity_t::warning);
    if (resource_copy_busy || pending_import || export_busy)
        return finish("Wait for the current resource copy, import, or export to finish.",
                      notification_severity_t::info);
    const auto reserved = next_resource_asset_id(history.document);
    if (!reserved || reserved->value != action.compose_resource_asset_id)
        return finish("The reserved asset ID changed before import-copy started.",
                      notification_severity_t::warning);
    const std::filesystem::path source_path{action.compose_import_path};
    const auto normalized_source = source_path.lexically_normal();
    if (!normalized_source.is_absolute() || normalized_source.filename().empty())
        return finish("Import-copy requires an absolute source file path.",
                      notification_severity_t::warning);
    if (std::ranges::any_of(history.document.audio_assets, [&](const auto& asset) {
            return asset.source_path.lexically_normal() == normalized_source;
        }))
        return finish("This audio is already linked; select its asset and copy it instead.",
                      notification_severity_t::warning);
    const auto chart = history.document.find_chart(*workspace.selected_chart);
    if (chart == nullptr)
        return finish("The selected difficulty is no longer available.",
                      notification_severity_t::warning);
    std::filesystem::path output_path;
    if (export_copy) {
        if (auto error = resolve_export_copy_path(action, history, workspace, output_path)) {
            return finish(*error, notification_severity_t::warning);
        }
    }
    task_graph_t graph{1};
    const project_asset_copy_request_t copy_request{
        .project_directory = asset_base, .source_path = normalized_source, .asset = *reserved};
    const auto added = graph.create_task({}, [copy_request, output_path,
                                           export_copy](std::span<const task_result_t>,
                                                        std::stop_token stop) {
        return copy_and_decode_metadata_audio(copy_request, output_path, export_copy, stop);
    });
    if (!added)
        return finish(!added.has_value() ? added.error().message : "Import-copy could not start.",
                      notification_severity_t::error);
    auto submitted = scheduler.begin(std::move(graph));
    if (!submitted)
        return finish(!submitted.has_value() ? submitted.error().message : "Import-copy could not start.",
                      notification_severity_t::error);
    pending_import = pending_metadata_asset_import_t{
        .graph = std::move(submitted.value()),
        .task = *added,
        .song = history.document.id,
        .revision = history.document.revision,
        .chart = *workspace.selected_chart,
        .asset = *reserved,
        .source_path = normalized_source,
        .output_path = output_path,
        .export_copy = export_copy,
        .allocate_bms_identifier = chart->source_family == format_family_t::bms};
    return finish(export_copy ? "Export audio copy and import started in the background."
                              : "Project audio import-copy started in the background.",
                   notification_severity_t::info);
#endif
}

auto metadata_asset_t::poll(
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    const std::filesystem::path& asset_base) -> std::optional<std::string> {
    process_metadata_copy_preview(impl_->copy_preview, impl_->notifications);
    return process_pending_metadata_asset_import(impl_->pending_import, history, workspace,
                                                 asset_base, impl_->notifications);
}

void metadata_asset_t::cancel() {
    if (impl_->pending_import)
        impl_->pending_import->graph.cancel();
    // Advisory feedback is still collected after explicit cancellation.
    impl_->copy_preview.job.cancel(wormedit::async_job_cancel_policy_t::retain_completion);
}

void metadata_asset_t::cleanup(const std::filesystem::path& asset_base) {
    if (impl_->pending_import) {
        if (const auto completed = impl_->pending_import->graph.outcome(
                impl_->pending_import->task);
            completed && completed->has_value()) {
            const auto imported =
                wormedit::task_value_cast<metadata_asset_import_task_result_t>(*completed);
            if (imported && imported->copy && imported->copy->status ==
                                wormedit::project_asset_copy_status_t::copied) {
                const auto& request = *impl_->pending_import;
                if (request.export_copy)
                    (void)wormedit::discard_unlinked_export_asset_copy(
                        {.project_directory = asset_base,
                         .source_path = request.source_path,
                         .relative_output_path = request.output_path,
                         .asset = request.asset},
                        *imported->copy);
                else
                    (void)wormedit::discard_unlinked_project_asset_copy(
                        {.project_directory = asset_base,
                         .source_path = request.source_path,
                         .asset = request.asset},
                        *imported->copy);
            }
        }
    }
    impl_->copy_preview.scheduler.shutdown();
}

auto metadata_asset_t::import_pending() const -> bool {
    return impl_->pending_import.has_value();
}

auto metadata_asset_t::copy_preview_pending() const -> bool {
    return impl_->copy_preview.job.is_busy();
}

auto metadata_asset_t::copy_preview_key() const
    -> const std::optional<metadata_copy_preview_key_t>& {
    return impl_->copy_preview.key;
}

auto metadata_asset_t::copy_preview_message() const -> const std::string& {
    return impl_->copy_preview.message;
}

void metadata_asset_t::clear_copy_preview_key() {
    impl_->copy_preview.key.reset();
}

} // namespace runtime
} // namespace wormedit::sys
