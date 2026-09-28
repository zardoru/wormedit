#include "sys/resource_job_state.h"

#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/resource_assignment.h>

#include <algorithm>
#include <utility>

namespace wormedit::sys {

auto runtime::resource_copy_t::start(pending_project_resource_copy_t&& pending) -> bool {
    if (busy())
        return false;
    pending_ = std::move(pending);
    return true;
}

auto runtime::resource_copy_t::poll(wormedit::document_history_t& history,
                                  wormedit::workspace_controller_t& workspace)
    -> std::optional<std::string> {
    using namespace wormedit;
    if (!pending_)
        return std::nullopt;

    const auto completed = pending_->graph.result(pending_->task);
    if (!completed)
        return std::nullopt;

    const auto request = std::move(*pending_);
    pending_.reset();
    const auto copied = task_value_cast<project_asset_copy_result_t>(*completed);
    if (!completed->succeeded() || !copied) {
        return completed->error ? completed->error->message
                                : "Resource copy returned no result.";
    }
    if (!*copied)
        return copied->message.empty() ? "Resource copy failed." : copied->message;
    if (request.export_copy) {
        const auto reference = export_asset_reference_path(request.output_path,
                                                           copied->relative_path);
        return !reference
                   ? "Export copy finished, but its chart-relative path is invalid."
               : copied->status == project_asset_copy_status_t::reused
                   ? "Identical export resource is available; export will verify it."
                   : "Resource copied into the export folder; export will verify it.";
    }
    if (history.document.id != request.song || history.document.revision != request.revision)
        return "Resource copy finished, but the document changed; source was not relinked.";

    const auto audio = std::ranges::find(history.document.audio_assets, request.asset,
                                         &audio_asset_t::id);
    const auto visual = std::ranges::find(history.document.visual_assets, request.asset,
                                          &visual_asset_t::id);
    const auto current_path = audio != history.document.audio_assets.end()     ? audio->source_path
                              : visual != history.document.visual_assets.end() ? visual->source_path
                                                                               : std::filesystem::path{};
    if (current_path != request.source_path)
        return "Resource copy finished, but its source changed; it was not relinked.";

    const auto relinked = apply_resource_asset_relink(
        history, request.revision,
        {.asset = request.asset, .source_path = copied->relative_path});
    if (!relinked.diagnostics.empty())
        return relinked.diagnostics.front().message;

    const auto rebound = rebind_workspace(workspace, history);
    if (!rebound)
        return "Resource copied, but workspace rebinding failed.";
    return copied->status == project_asset_copy_status_t::reused
               ? "Existing project copy linked to the resource."
               : "Resource copied into the project and linked.";
}

void runtime::resource_copy_t::cancel() {
    if (!pending_)
        return;
    pending_->graph.cancel();
    pending_.reset();
}

auto runtime::output_group_export_t::start(pending_output_group_export_t&& pending) -> bool {
    if (busy())
        return false;
    this->pending = std::move(pending);
    return true;
}

auto runtime::output_group_export_t::poll(const wormedit::document_history_t& history)
    -> std::optional<std::string> {
    if (!pending)
        return std::nullopt;
    const auto completed = pending->graph.result(pending->task);
    if (!completed)
        return std::nullopt;

    const auto exported_song = pending->song;
    const auto exported_revision = pending->revision;
    pending.reset();
    const auto exported =
        wormedit::task_value_cast<output_group_export_task_result_t>(*completed);
    auto status = !completed->succeeded() || !exported
                      ? completed->error ? completed->error->message
                                         : "Output group export returned no result."
                      : exported->message;
    if (exported && exported->published &&
        (history.document.id != exported_song ||
         history.document.revision != exported_revision))
        status += " The editor session changed after this export started.";
    return status;
}

void runtime::output_group_export_t::cancel() {
    if (!pending)
        return;
    pending->graph.cancel();
    pending.reset();
}

void runtime::output_group_export_t::shutdown() {
    cancel();
    scheduler.shutdown();
}

} // namespace wormedit::sys
