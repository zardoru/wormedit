#include "sys/resource_job_state.h"

#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/resource_assignment.h>

#include <algorithm>
#include <utility>

namespace wormedit::sys {

namespace {
auto copy_failure_severity(wormedit::failure_code_t status)
    -> wormedit::notification_severity_t {
    using namespace wormedit;
    if (status == failure_code_t::cancelled)
        return notification_severity_t::info;
    if (status == failure_code_t::conflict)
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
                                                            failure_code_t::cancelled
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
        return {.message = stale_action_message("Resource copy relink"),
                .severity = notification_severity_t::warning};
    const auto audio = history.document.find_audio_asset(request.asset);
    const auto visual = std::ranges::find(history.document.visual_assets, request.asset,
                                          &visual_asset_t::id);
    const auto current_path = audio != nullptr
        ? audio->source_path
        : visual != history.document.visual_assets.end() ? visual->source_path
                                                         : std::filesystem::path{};
    if (current_path != request.source_path)
        return {.message = "Resource copy finished, but its source changed; it was not relinked.",
                .severity = notification_severity_t::warning};
    const auto relinked = apply_resource_asset_relink(
        history,
        {.asset = request.asset, .source_path = copied.relative_path});
    if (!relinked.diagnostics.empty())
        return {.message = relinked.diagnostics.front().message,
                .severity = to_notification_severity(relinked.diagnostics.front().severity)};
    if (const auto rebound = rebind_workspace(workspace, history);
        !rebound.is_accepted(diagnostic_acceptance_policy_t::require_empty))
        return {.message = "Resource copied, but workspace rebinding failed.",
                .severity = notification_severity_t::error, .publish = true,
                .source = notification_source_t::document};
    return {.message = copied.status == project_asset_copy_phase_t::reused
                ? "Existing project copy linked to the resource."
                : "Resource copied into the project and linked.",
            .severity = notification_severity_t::success};
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
        return {.message = "Export copy finished, but its chart-relative path is invalid.",
                .severity = notification_severity_t::warning};
    return {.message = copied.status == project_asset_copy_phase_t::reused
                ? "Identical export resource is available; export will verify it."
                : "Resource copied into the export folder; export will verify it.",
            .severity = notification_severity_t::success};
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
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::invalid_request,
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

auto runtime::output_group_export_t::start(const song_document_t& document,
    output_group_export_plan_t plan, std::filesystem::path asset_base,
    work_function_t work) -> start_outcome_t {
    if (is_busy())
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request,
            "Wait for the current resource copy, import, or export to finish."});
    auto receipt = std::make_shared<std::optional<output_group_export_task_result_t>>();
    task_graph_t graph{1};
    const auto added = graph.create_task({},
        [request = output_group_export_request_t{.document = document, .plan = std::move(plan),
                                                 .asset_base = std::move(asset_base)},
         receipt, work](std::span<const task_outcome_t>, std::stop_token stop) {
            receipt->emplace(work(request, stop));
            return task_value_t{};
        });
    if (!added)
        return std::unexpected(added.error());
    auto submitted = scheduler_.begin(std::move(graph));
    if (!submitted)
        return std::unexpected(submitted.error());
    pending_ = pending_t{.graph = std::move(*submitted), .task = *added,
                        .identity = {.project = document.id,
                                     .document_revision = document.revision},
                        .receipt = std::move(receipt)};
    notifications_.post(notification_severity_t::info, notification_source_t::export_,
                        "Output group conversion started in the background.");
    return {};
}

auto runtime::output_group_export_t::poll(const wormedit::document_history_t& history)
    -> std::optional<std::string> {
    if (!pending_)
        return std::nullopt;
    const auto completed = pending_->graph.outcome(pending_->task);
    if (!completed)
        return std::nullopt;

    const auto exported_song = pending_->identity.project;
    const auto exported_revision = pending_->identity.document_revision;
    const auto receipt = pending_->receipt;
    pending_.reset();
    // A cancellation can replace the scheduler outcome after files were published.
    const auto* exported = receipt->has_value() ? &receipt->value() : nullptr;
    auto status = !exported
                      ? !completed->has_value() ? completed->error().cause.message
                                         : "Output group export returned no result."
                      : exported->message;
    auto severity = !exported
                        ? !completed->has_value() && completed->error().cause.code ==
                                                  wormedit::failure_code_t::cancelled
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
    if (!pending_)
        return;
    pending_->graph.cancel();
}

void runtime::output_group_export_t::shutdown() {
    cancel();
    scheduler_.shutdown();
    pending_.reset();
}

} // namespace wormedit::sys
