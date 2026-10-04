#include "sys/resource_job_state.h"

#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/resource_assignment.h>

#include <algorithm>
#include <utility>

namespace wormedit::sys {

namespace {
auto copy_failure_severity(wormedit::project_asset_copy_status_t status)
    -> wormedit::notification_severity_t {
    using namespace wormedit;
    if (status == project_asset_copy_status_t::cancelled)
        return notification_severity_t::info;
    if (status == project_asset_copy_status_t::destination_collision)
        return notification_severity_t::warning;
    return notification_severity_t::error;
}

auto diagnostic_notification_severity(wormedit::diagnostic_severity_t severity)
    -> wormedit::notification_severity_t {
    using namespace wormedit;
    switch (severity) {
    case diagnostic_severity_t::info: return notification_severity_t::info;
    case diagnostic_severity_t::warning: return notification_severity_t::warning;
    case diagnostic_severity_t::error: return notification_severity_t::error;
    }
    return notification_severity_t::error;
}

struct resource_copy_feedback_t {
    std::string message;
    wormedit::notification_severity_t severity{wormedit::notification_severity_t::error};
    bool publish{true};
    std::optional<wormedit::notification_source_t> source;
};

auto copy_task_failure(const wormedit::task_result_t& completed,
                       const std::shared_ptr<const wormedit::project_asset_copy_result_t>& copied)
    -> std::optional<resource_copy_feedback_t> {
    using namespace wormedit;
    if (!completed.succeeded() || !copied) {
        const auto cancelled = completed.error &&
                               completed.error->code == task_error_code_t::cancelled;
        return resource_copy_feedback_t{
            .message = completed.error ? completed.error->message
                                       : "Resource copy returned no result.",
            .severity = cancelled ? notification_severity_t::info
                                  : notification_severity_t::error};
    }
    if (*copied)
        return std::nullopt;
    return resource_copy_feedback_t{
        .message = copied->message.empty() ? "Resource copy failed." : copied->message,
        .severity = copy_failure_severity(copied->status)};
}

auto complete_project_resource_copy(const pending_project_resource_copy_t& request,
                                    const wormedit::project_asset_copy_result_t& copied,
                                    wormedit::document_history_t& history,
                                    wormedit::workspace_controller_t& workspace)
    -> resource_copy_feedback_t {
    using namespace wormedit;
    if (history.document.id != request.song || history.document.revision != request.revision)
        return {"Resource copy finished, but the document changed; source was not relinked.",
                notification_severity_t::warning};
    const auto audio = std::ranges::find(history.document.audio_assets, request.asset,
                                         &audio_asset_t::id);
    const auto visual = std::ranges::find(history.document.visual_assets, request.asset,
                                          &visual_asset_t::id);
    const auto current_path = audio != history.document.audio_assets.end()
        ? audio->source_path
        : visual != history.document.visual_assets.end() ? visual->source_path
                                                         : std::filesystem::path{};
    if (current_path != request.source_path)
        return {"Resource copy finished, but its source changed; it was not relinked.",
                notification_severity_t::warning};
    const auto relinked = apply_resource_asset_relink(
        history, request.revision,
        {.asset = request.asset, .source_path = copied.relative_path});
    if (!relinked.diagnostics.empty())
        return {relinked.diagnostics.front().message,
                diagnostic_notification_severity(relinked.diagnostics.front().severity)};
    if (!rebind_workspace(workspace, history))
        return {"Resource copied, but workspace rebinding failed.", notification_severity_t::error,
                true, notification_source_t::document};
    return {copied.status == project_asset_copy_status_t::reused
                ? "Existing project copy linked to the resource."
                : "Resource copied into the project and linked.",
            notification_severity_t::success};
}

auto complete_resource_copy(const pending_project_resource_copy_t& request,
                            const wormedit::project_asset_copy_result_t& copied,
                            wormedit::document_history_t& history,
                            wormedit::workspace_controller_t& workspace)
    -> resource_copy_feedback_t {
    using namespace wormedit;
    if (!request.export_copy)
        return complete_project_resource_copy(request, copied, history, workspace);
    const auto reference = export_asset_reference_path(request.output_path, copied.relative_path);
    if (!reference)
        return {"Export copy finished, but its chart-relative path is invalid.",
                notification_severity_t::warning};
    return {copied.status == project_asset_copy_status_t::reused
                ? "Identical export resource is available; export will verify it."
                : "Resource copied into the export folder; export will verify it.",
            notification_severity_t::success};
}
} // namespace

runtime::resource_copy_t::resource_copy_t(
    std::optional<wormedit::notification_sink_t> notifications)
    : notifications_(std::move(notifications)) {}

auto runtime::resource_copy_t::start(pending_project_resource_copy_t&& pending) -> bool {
    if (busy())
        return false;
    const auto source = pending.export_copy ? wormedit::notification_source_t::export_
                                            : wormedit::notification_source_t::import_;
    pending_ = std::move(pending);
    if (notifications_)
        notifications_->post(wormedit::notification_severity_t::info, source,
                             pending_->export_copy ? "Export copy started in the background."
                                                   : "Resource copy started in the background.");
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
    const auto source = request.export_copy ? notification_source_t::export_
                                            : notification_source_t::import_;
    const auto finish = [&](std::string message, notification_severity_t severity,
                             bool publish = true,
                             std::optional<notification_source_t> source_override = {})
        -> std::optional<std::string> {
        if (publish && notifications_ && !message.empty())
            notifications_->post(severity, source_override.value_or(source), message);
        return message;
    };
    const auto copied = task_value_cast<project_asset_copy_result_t>(*completed);
    auto feedback = copy_task_failure(*completed, copied);
    if (!feedback)
        feedback = complete_resource_copy(request, *copied, history, workspace);
    return finish(std::move(feedback->message), feedback->severity, feedback->publish,
                  feedback->source);
}

void runtime::resource_copy_t::cancel() {
    if (!pending_)
        return;
    pending_->graph.cancel();
    pending_.reset();
}

runtime::output_group_export_t::output_group_export_t(
    std::optional<wormedit::notification_sink_t> notifications)
    : notifications_(std::move(notifications)) {}

runtime::output_group_export_t::~output_group_export_t() noexcept {
    try {
        shutdown();
    } catch (...) {
    }
}

auto runtime::output_group_export_t::start(pending_output_group_export_t&& pending) -> bool {
    if (busy())
        return false;
    this->pending = std::move(pending);
    if (notifications_)
        notifications_->post(wormedit::notification_severity_t::info,
                             wormedit::notification_source_t::export_,
                             "Output group conversion started in the background.");
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
    auto severity = !completed->succeeded() || !exported
                        ? completed->error && completed->error->code ==
                                                  wormedit::task_error_code_t::cancelled
                              ? wormedit::notification_severity_t::info
                              : wormedit::notification_severity_t::error
                        : exported->severity;
    if (exported && exported->published &&
        (history.document.id != exported_song || history.document.revision != exported_revision)) {
        status += " The editor session changed after this export started.";
        severity = wormedit::notification_severity_t::warning;
    }
    if (notifications_ && !status.empty())
        notifications_->post(severity, wormedit::notification_source_t::export_, status);
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
