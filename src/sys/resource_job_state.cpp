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

struct resource_copy_feedback_t {
    std::string message;
    wormedit::notification_severity_t severity{wormedit::notification_severity_t::error};
    bool publish{true};
    std::optional<wormedit::notification_source_t> source;
};

auto copy_task_failure(const resource_copy_job_t::completion_t& completed)
    -> std::optional<resource_copy_feedback_t> {
    using namespace wormedit;
    if (!completed) {
        return resource_copy_feedback_t{.message = completed.error().cause.message,
                                        .severity = completed.error().cause.code ==
                                                            task_error_code_t::cancelled
                                                        ? notification_severity_t::info
                                                        : notification_severity_t::error};
    }
    const auto& copied = *completed->payload;
    if (copied)
        return std::nullopt;
    return resource_copy_feedback_t{.message = copied.error().message.empty()
                                                   ? "Resource copy failed."
                                                   : copied.error().message,
                                    .severity = copy_failure_severity(copied.error().code)};
}

auto complete_project_resource_copy(const resource_copy_identity_t& request,
                                    const wormedit::project_asset_copy_receipt_t& copied,
                                    wormedit::document_history_t& history,
                                    wormedit::workspace_controller_t& workspace)
    -> resource_copy_feedback_t {
    using namespace wormedit;
    if (!request.document.is_current(history.document))
        return {stale_action_message("Resource copy relink"),
                notification_severity_t::warning};
    const auto audio = history.document.find_audio_asset(request.asset);
    const auto visual = std::ranges::find(history.document.visual_assets, request.asset,
                                          &visual_asset_t::id);
    const auto current_path = audio != nullptr
        ? audio->source_path
        : visual != history.document.visual_assets.end() ? visual->source_path
                                                         : std::filesystem::path{};
    if (current_path != request.source_path)
        return {"Resource copy finished, but its source changed; it was not relinked.",
                notification_severity_t::warning};
    const auto relinked = apply_resource_asset_relink(
        history,
        {.asset = request.asset, .source_path = copied.relative_path});
    if (!relinked.diagnostics.empty())
        return {relinked.diagnostics.front().message,
                to_notification_severity(relinked.diagnostics.front().severity)};
    if (const auto rebound = rebind_workspace(workspace, history);
        !rebound.is_accepted(diagnostic_acceptance_policy_t::require_empty))
        return {"Resource copied, but workspace rebinding failed.", notification_severity_t::error,
                true, notification_source_t::document};
    return {copied.status == project_asset_copy_status_t::reused
                ? "Existing project copy linked to the resource."
                : "Resource copied into the project and linked.",
            notification_severity_t::success};
}

auto complete_resource_copy(const resource_copy_identity_t& request,
                            const wormedit::project_asset_copy_receipt_t& copied,
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

runtime::resource_copy_t::resource_copy_t(wormedit::task_scheduler_t& scheduler,
                                          wormedit::notification_sink_t notifications)
    : notifications_(std::move(notifications)),
      job_(scheduler, 1, "Resource copy could not start.",
           "scheduler outstanding-task capacity exceeded", "Resource copy returned no result.",
           wormedit::async_job_destruction_policy_t::release) {}

auto runtime::resource_copy_t::copy_file(const resource_copy_request_t& request,
                                         std::stop_token stop)
    -> resource_copy_job_t::work_outcome_t {
    using namespace wormedit;
    auto copied =
        request.target.export_copy
            ? create_export_asset_copy({.project_directory = request.project_directory,
                                        .source_path = request.target.source_path,
                                        .relative_output_path = request.target.output_path,
                                        .asset = request.target.asset},
                                       stop)
            : create_project_asset_copy({.project_directory = request.project_directory,
                                         .source_path = request.target.source_path,
                                         .asset = request.target.asset},
                                        stop);
    return resource_copy_job_t::work_outcome_t{std::in_place, std::move(copied)};
}

auto runtime::resource_copy_t::start(resource_copy_request_t request,
                                     resource_copy_job_t::work_function_t work)
    -> wormedit::async_job_start_outcome_t {
    using namespace wormedit;
    if (is_busy())
        return std::unexpected(error_t<async_job_start_status_t>{
            async_job_start_status_t::invalid_request,
            "Wait for the current resource copy, import, or export to finish."});
    const auto source = request.target.export_copy ? notification_source_t::export_
                                                   : notification_source_t::import_;
    const auto exporting = request.target.export_copy;
    auto started = job_.request([&request] { return std::move(request); }, request.target, work);
    if (started)
        notifications_.post(notification_severity_t::info, source,
                            exporting ? "Export copy started in the background."
                                      : "Resource copy started in the background.");
    return started;
}

auto runtime::resource_copy_t::poll(wormedit::document_history_t& history,
                                  wormedit::workspace_controller_t& workspace)
    -> std::optional<std::string> {
    using namespace wormedit;
    const auto completed = job_.poll();
    if (!completed)
        return std::nullopt;
    const auto& request =
        completed->has_value() ? completed->value().identity : completed->error().identity;
    const auto source = request.export_copy ? notification_source_t::export_
                                            : notification_source_t::import_;
    const auto finish = [&](std::string message, notification_severity_t severity,
                            bool publish = true,
                            std::optional<notification_source_t> source_override = {})
        -> std::optional<std::string> {
        if (publish && !message.empty())
            notifications_.post(severity, source_override.value_or(source), message);
        return message;
    };
    auto feedback = copy_task_failure(*completed);
    if (!feedback)
        feedback =
            complete_resource_copy(request, **completed->value().payload, history, workspace);
    return finish(std::move(feedback->message), feedback->severity, feedback->publish,
                  feedback->source);
}

void runtime::resource_copy_t::cancel() {
    job_.cancel();
}

runtime::output_group_export_t::output_group_export_t(wormedit::notification_sink_t notifications)
    : notifications_(std::move(notifications)) {}

runtime::output_group_export_t::~output_group_export_t() noexcept {
    try {
        shutdown();
    } catch (...) {
    }
}

auto runtime::output_group_export_t::start(pending_output_group_export_t&& pending) -> bool {
    if (is_busy())
        return false;
    this->pending = std::move(pending);
    notifications_.post(wormedit::notification_severity_t::info,
                        wormedit::notification_source_t::export_,
                        "Output group conversion started in the background.");
    return true;
}

auto runtime::output_group_export_t::poll(const wormedit::document_history_t& history)
    -> std::optional<std::string> {
    if (!pending)
        return std::nullopt;
    const auto completed = pending->graph.outcome(pending->task);
    if (!completed)
        return std::nullopt;

    const auto exported_song = pending->song;
    const auto exported_revision = pending->revision;
    pending.reset();
    const auto exported =
        wormedit::task_value_cast<output_group_export_task_result_t>(*completed);
    auto status = !completed->has_value() || !exported
                      ? !completed->has_value() ? completed->error().cause.message
                                         : "Output group export returned no result."
                      : exported->message;
    auto severity = !completed->has_value() || !exported
                        ? !completed->has_value() && completed->error().cause.code ==
                                                  wormedit::task_error_code_t::cancelled
                              ? wormedit::notification_severity_t::info
                              : wormedit::notification_severity_t::error
                        : exported->severity;
    if (exported && exported->published &&
        (history.document.id != exported_song || history.document.revision != exported_revision)) {
        status += " The editor session changed after this export started.";
        severity = wormedit::notification_severity_t::warning;
    }
    if (!status.empty())
        notifications_.post({.severity = severity,
                             .source = wormedit::notification_source_t::export_,
                             .message = status,
                             .detail = exported ? exported->detail : std::string{}});
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
