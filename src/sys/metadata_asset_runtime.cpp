#include "sys/metadata_asset_runtime.h"
#include "sys/thread_priority.h"

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
    wormedit::project_asset_copy_result_t copy;
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

struct pending_metadata_copy_preview_t {
    wormedit::task_graph_handle_t graph;
    wormedit::task_id_t task;
};

struct metadata_copy_preview_t {
    wormedit::task_scheduler_t scheduler{1, 1, wormedit::background_scheduler_options()};
    std::optional<pending_metadata_copy_preview_t> pending;
    std::optional<metadata_copy_preview_key_t> key;
    std::string message;
};

auto link_imported_metadata_audio(
    const pending_metadata_asset_import_t& request,
    const metadata_asset_import_task_result_t& imported,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    std::string& status) -> bool {
    auto name = request.source_path.filename().generic_string();
    if (name.size() > 255)
        name.clear();
    const auto applied = wormedit::apply_primary_music_set(
        history, request.revision,
        {.source_path = request.source_path,
         .copied_project_relative_path =
             request.export_copy ? std::optional<std::filesystem::path>{}
                                 : std::optional{imported.copy.relative_path},
         .expected_new_asset_id = request.asset,
         .name = std::move(name),
         .allocate_bms_identifier = request.allocate_bms_identifier,
         .audio_stream = imported.stream,
         .image_geometry = std::nullopt});
    if (!applied.diagnostics.empty()) {
        status = applied.diagnostics.front().message;
        return false;
    }
    if (!applied.applied) {
        status = "Copied audio could not be registered.";
        return false;
    }
    const auto rebound = wormedit::rebind_workspace(workspace, history);
    if (!rebound) {
        status = rebound.diagnostics.empty()
                     ? "Audio imported, but workspace rebinding failed."
                     : rebound.diagnostics.front().message;
    } else {
        const auto& resources = history.document.resources;
        const auto music = std::ranges::find_if(resources, [](const auto& resource) {
            return std::ranges::find(resource.roles, wormedit::resource_role_t::music) !=
                   resource.roles.end();
        });
        (void)wormedit::apply_workspace_action(
            workspace, history.document, history.document.revision,
            wormedit::workspace_select_audio_asset_action_t{
                music != resources.end() ? music->asset : request.asset});
        status = request.export_copy
                     ? "Audio staged in export assets and set as the song audio."
                     : "Audio copied into project assets and set as the song audio.";
    }
    return true;
}

auto process_pending_metadata_asset_import(
    std::optional<pending_metadata_asset_import_t>& pending_import,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    const std::filesystem::path& asset_base) -> std::optional<std::string> {
    if (!pending_import)
        return std::nullopt;
    const auto completed = pending_import->graph.result(pending_import->task);
    if (!completed)
        return std::nullopt;
    const auto request = std::move(*pending_import);
    pending_import.reset();
    std::string status;
    const auto imported =
        wormedit::task_value_cast<metadata_asset_import_task_result_t>(*completed);
    bool import_linked = false;
    if (!completed->succeeded() || !imported) {
        status = completed->error ? completed->error->message
                                  : "Project import-copy returned no result.";
    } else if (!imported->copy) {
        status = imported->decode_error.empty() ? imported->copy.message
                                                : imported->decode_error;
    } else if (!imported->stream) {
        status = imported->decode_error.empty() ? "Copied audio could not be decoded."
                                                : imported->decode_error;
    } else if (history.document.id != request.song ||
               history.document.revision != request.revision ||
               !workspace.selected_chart ||
               *workspace.selected_chart != request.chart) {
        status = "Document changed before copied audio could be linked.";
    } else {
        import_linked = link_imported_metadata_audio(request, *imported, history, workspace,
                                                     status);
    }
    if (imported && imported->copy.status ==
                        wormedit::project_asset_copy_status_t::copied &&
        !import_linked) {
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
                                         imported->copy)
                                   : wormedit::discard_unlinked_project_asset_copy(
                                         copy_request, imported->copy);
        if (!discarded)
            status += " Published copy remains; review it before removal.";
    }
    return status;
}

void process_metadata_copy_preview(metadata_copy_preview_t& runtime) {
    if (!runtime.pending)
        return;
    const auto completed = runtime.pending->graph.result(runtime.pending->task);
    if (!completed)
        return;
    runtime.pending.reset();
    const auto inspected =
        wormedit::task_value_cast<wormedit::project_asset_copy_result_t>(*completed);
    if (!completed->succeeded() || !inspected) {
        runtime.message = completed->error ? completed->error->message
                                           : "Copy inspection returned no result.";
    } else {
        using copy_status_t = wormedit::project_asset_copy_status_t;
        runtime.message =
            inspected->status == copy_status_t::not_staged
                ? "Destination is free; copy will create it."
            : inspected->status == copy_status_t::reused
                ? "Identical file exists; copy will reuse it."
            : inspected->status == copy_status_t::destination_collision
                ? "Different file occupies destination; copy will preserve it."
            : inspected->message.empty() ? "Copy destination cannot be used."
                                         : inspected->message;
    }
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
            copy_resource_file_into_export({.project_directory = copy_request.project_directory,
                                            .source_path = copy_request.source_path,
                                            .relative_output_path = output_path,
                                            .asset = copy_request.asset,
                                            .maximum_bytes = copy_request.maximum_bytes,
                                            .validate_temporary = validate},
                                           stop);
    } else {
        auto request = copy_request;
        request.validate_temporary = validate;
        completed->copy = copy_resource_file_into_project(request, stop);
    }
    if (completed->copy && !stop.stop_requested()) {
        audio::decoder_t decoder;
        if (decoder.open(completed->copy.absolute_path, &completed->decode_error)) {
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
    if (stop.stop_requested() && completed->copy.status == project_asset_copy_status_t::copied) {
        if (export_copy)
            (void)discard_unlinked_export_asset_copy(
                {.project_directory = copy_request.project_directory,
                 .source_path = copy_request.source_path,
                 .relative_output_path = output_path,
                 .asset = copy_request.asset},
                completed->copy);
        else
            (void)discard_unlinked_project_asset_copy(copy_request, completed->copy);
    }
    return task_result_t::success(make_task_value(
        std::shared_ptr<const metadata_asset_import_task_result_t>{std::move(completed)}));
}
#endif

} // namespace

struct metadata_asset_t::impl_t {
    std::optional<pending_metadata_asset_import_t> pending_import;
    metadata_copy_preview_t copy_preview;
};

metadata_asset_t::metadata_asset_t() : impl_(std::make_unique<impl_t>()) {}
metadata_asset_t::~metadata_asset_t() = default;

auto metadata_asset_t::start_copy_preview(
    const WormUiWorkspaceAction& action, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace,
    const std::filesystem::path& asset_base) -> std::string {
    using namespace wormedit;
    auto& runtime = impl_->copy_preview;
    if (workspace.active_tab != workspace_tab_t::metadata || !workspace.selected_chart ||
        workspace.selected_chart->value != action.compose_input_chart_id ||
        history.document.revision != action.compose_input_document_revision)
        return "Metadata copy inspection expired after the chart or document changed.";
    if (asset_base.empty())
        return "Open a saved native project before inspecting asset copies.";
    if (runtime.pending)
        return "Wait for the current copy inspection to finish.";
    const bool import_copy = action.metadata_inspect_import;
    const asset_id_t asset{action.compose_resource_asset_id};
    std::filesystem::path source_path;
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
    } else {
        const auto audio =
            std::ranges::find(history.document.audio_assets, asset, &audio_asset_t::id);
        if (!asset || audio == history.document.audio_assets.end())
            return "Selected music asset is no longer available.";
        source_path = audio->source_path;
    }
    std::filesystem::path output_path;
    if (action.metadata_inspect_export) {
        if (auto error = resolve_export_copy_path(action, history, workspace, output_path)) {
            return *error;
        }
    }
    const project_asset_copy_request_t request{
        .project_directory = asset_base, .source_path = source_path, .asset = asset};
    task_graph_t graph{1};
    const auto added = graph.add_task({}, [request, export_copy = action.metadata_inspect_export,
                                           output_path](std::span<const task_result_t>,
                                                        std::stop_token stop) {
        auto inspected = std::make_shared<const project_asset_copy_result_t>(
            export_copy ? inspect_export_asset_copy({.project_directory = request.project_directory,
                                                     .source_path = request.source_path,
                                                     .relative_output_path = output_path,
                                                     .asset = request.asset,
                                                     .maximum_bytes = request.maximum_bytes},
                                                    stop)
                        : inspect_project_asset_copy(request, stop));
        return task_result_t::success(make_task_value(std::move(inspected)));
    });
    if (!added)
        return added.error ? added.error->message : "Copy inspection could not start.";
    auto submitted = runtime.scheduler.submit(std::move(graph));
    if (!submitted)
        return submitted.error ? submitted.error->message : "Copy inspection could not start.";
    runtime.pending =
        pending_metadata_copy_preview_t{.graph = std::move(submitted.graph), .task = *added.id};
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
    return "Copy destination inspection started.";
}

auto metadata_asset_t::start_import_copy(
    const WormUiWorkspaceAction& action, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace,
    const std::filesystem::path& asset_base, wormedit::task_scheduler_t& scheduler,
    bool resource_copy_busy, bool export_busy) -> std::string {
    using namespace wormedit;
#if !defined(WORMEDIT_HAS_AUDIO_DECODER)
    (void)action;
    (void)history;
    (void)workspace;
    (void)asset_base;
    (void)scheduler;
    (void)resource_copy_busy;
    (void)export_busy;
    return "Audio import is unavailable in this build.";
#else
    auto& pending_import = impl_->pending_import;
    if (workspace.active_tab != workspace_tab_t::metadata || !workspace.selected_chart ||
        workspace.selected_chart->value != action.compose_input_chart_id ||
        history.document.revision != action.compose_input_document_revision)
        return "Metadata import-copy expired after the chart or document changed.";
    if (asset_base.empty())
        return "Open a saved native project before importing an asset copy.";
    const bool export_copy = action.metadata_import_copy_export;
    if (export_copy == action.metadata_import_copy_project)
        return "Choose exactly one project or export import-copy destination.";
    if (resource_copy_busy || pending_import || export_busy)
        return "Wait for the current resource copy, import, or export to finish.";
    const auto reserved = next_resource_asset_id(history.document);
    if (!reserved || reserved->value != action.compose_resource_asset_id)
        return "The reserved asset ID changed before import-copy started.";
    const std::filesystem::path source_path{action.compose_import_path};
    const auto source = source_path.lexically_normal();
    if (!source.is_absolute() || source.filename().empty())
        return "Import-copy requires an absolute source file path.";
    if (std::ranges::any_of(history.document.audio_assets, [&](const auto& asset) {
            return asset.source_path.lexically_normal() == source;
        }))
        return "This audio is already linked; select its asset and copy it instead.";
    const auto chart = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                         &chart_document_t::id);
    if (chart == history.document.charts.end())
        return "The selected difficulty is no longer available.";
    std::filesystem::path output_path;
    if (export_copy) {
        if (auto error = resolve_export_copy_path(action, history, workspace, output_path)) {
            return *error;
        }
    }
    task_graph_t graph{1};
    const project_asset_copy_request_t copy_request{
        .project_directory = asset_base, .source_path = source, .asset = *reserved};
    const auto added = graph.add_task({}, [copy_request, output_path,
                                           export_copy](std::span<const task_result_t>,
                                                        std::stop_token stop) {
        return copy_and_decode_metadata_audio(copy_request, output_path, export_copy, stop);
    });
    if (!added)
        return added.error ? added.error->message : "Import-copy could not start.";
    auto submitted = scheduler.submit(std::move(graph));
    if (!submitted)
        return submitted.error ? submitted.error->message : "Import-copy could not start.";
    pending_import = pending_metadata_asset_import_t{
        .graph = std::move(submitted.graph),
        .task = *added.id,
        .song = history.document.id,
        .revision = history.document.revision,
        .chart = *workspace.selected_chart,
        .asset = *reserved,
        .source_path = source,
        .output_path = output_path,
        .export_copy = export_copy,
        .allocate_bms_identifier = chart->source_family == format_family_t::bms};
    return export_copy ? "Export audio copy and import started in the background."
                       : "Project audio import-copy started in the background.";
#endif
}

auto metadata_asset_t::poll(
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    const std::filesystem::path& asset_base) -> std::optional<std::string> {
    process_metadata_copy_preview(impl_->copy_preview);
    return process_pending_metadata_asset_import(impl_->pending_import, history, workspace,
                                                 asset_base);
}

void metadata_asset_t::cancel() {
    if (impl_->pending_import)
        impl_->pending_import->graph.cancel();
    if (impl_->copy_preview.pending)
        impl_->copy_preview.pending->graph.cancel();
}

void metadata_asset_t::cleanup(const std::filesystem::path& asset_base) {
    if (impl_->pending_import) {
        if (const auto completed = impl_->pending_import->graph.result(
                impl_->pending_import->task);
            completed && completed->succeeded()) {
            const auto imported =
                wormedit::task_value_cast<metadata_asset_import_task_result_t>(*completed);
            if (imported && imported->copy.status ==
                                wormedit::project_asset_copy_status_t::copied) {
                const auto& request = *impl_->pending_import;
                if (request.export_copy)
                    (void)wormedit::discard_unlinked_export_asset_copy(
                        {.project_directory = asset_base,
                         .source_path = request.source_path,
                         .relative_output_path = request.output_path,
                         .asset = request.asset},
                        imported->copy);
                else
                    (void)wormedit::discard_unlinked_project_asset_copy(
                        {.project_directory = asset_base,
                         .source_path = request.source_path,
                         .asset = request.asset},
                        imported->copy);
            }
        }
    }
    impl_->copy_preview.scheduler.shutdown();
}

auto metadata_asset_t::import_pending() const -> bool {
    return impl_->pending_import.has_value();
}

auto metadata_asset_t::copy_preview_pending() const -> bool {
    return impl_->copy_preview.pending.has_value();
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
