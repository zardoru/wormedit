#include "sys/resources/resource_job_state.h"
#include "sys/commands/document_edit_feedback.h"
#include "sys/resources/image_import_probe.h"

#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/resource_assignment.h>

#include <algorithm>
#include <utility>

namespace wormedit::sys {

namespace {
auto current_export_target(const output_group_export_task_result_t* result,
    const document_history_t& history, song_id_t project, std::uint64_t revision) -> notification_target_t {
    if (!result || history.document.id != project || history.document.revision != revision) return {};
    return result->target;
}

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
    const auto& copied = completed->payload->copied;
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
    auto feedback = rebind_document_edit(
        workspace, history,
        {.success = copied.status == project_asset_copy_phase_t::reused
                        ? "Existing project copy linked to the resource."
                        : "Resource copied into the project and linked.",
         .rebind_failure = "Resource copied, but workspace rebinding failed.",
         .is_message_diagnostic = false,
         .source = notification_source_t::import_,
         .failure_source = notification_source_t::document});
    return {.message = std::string{feedback.message_text()},
            .severity = feedback.severity,
            .source = feedback.source};
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

storyboard_import_receipt_t::~storyboard_import_receipt_t() {
    if (copy_ && !linked_) (void)discard_unlinked_project_asset_copy(request_, *copy_);
}
auto storyboard_import_receipt_t::create_copy(std::stop_token stop) -> project_asset_copy_outcome_t {
    if (copy_) return *copy_;
    auto plan = plan_project_asset_copies(std::span{&request_, 1}, request_.relative_directory, stop);
    if (!plan) return std::unexpected(plan.error());
    request_ = std::move(plan->front());
    auto copied = create_project_asset_copy(request_, stop);
    if (copied) {
        copy_ = *copied;
        validation_identity_ = project_asset_copy_receipt_t::published_identity_t::from(copied->absolute_path);
        if (copied->published_identity && validation_identity_ != copied->published_identity)
            return std::unexpected(error_t<failure_code_t>{failure_code_t::conflict, "Image copy changed after publication."});
    }
    return copied;
}

auto storyboard_import_receipt_t::validate_link() const -> result_t<void, error_t<failure_code_t>> {
    if (!copy_ || !validation_identity_)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::conflict, "Image copy identity is unavailable."});
    const auto source = inspect_project_path_source({.source_directory = request_.project_directory,
        .relative_path = copy_->relative_path, .maximum_bytes = request_.maximum_bytes, .expected_source = validation_identity_});
    if (!source) return std::unexpected(source.error());
    return {};
}

runtime::resource_copy_t::resource_copy_t(wormedit::task_scheduler_t& scheduler,
                                          wormedit::notification_sink_t notifications)
    : notifications_(std::move(notifications)),
      job_(scheduler, 1, "Resource copy could not start.",
           "scheduler outstanding-task capacity exceeded", "Resource copy returned no result.",
           wormedit::async_job_destruction_policy_t::release),
      save_job_(scheduler, 1, "Project save could not start.",
                "scheduler outstanding-task capacity exceeded", "Project save returned no result.",
                async_job_destruction_policy_t::release) {}

static auto save_project_files(const project_save_request_t& request, std::stop_token stop)
    -> project_save_job_t::work_outcome_t {
    return create_project_save(request, stop);
}

auto runtime::resource_copy_t::start_project_save(const song_document_t& document,
    const std::filesystem::path& source_base, const std::filesystem::path& destination)
    -> async_job_start_outcome_t {
    if (is_busy())
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request,
            "Wait for the current resource copy or save to finish."});
    return save_job_.request([&] {
        return project_save_request_t{.document = document, .source_base = source_base,
                                       .destination = destination};
    }, document_stamp_t::from(document), save_project_files);
}

auto runtime::resource_copy_t::poll_project_save() -> std::optional<project_save_job_t::completion_t> {
    return save_job_.poll();
}

namespace {
auto copy_storyboard_image(const resource_copy_request_t& request, std::stop_token stop)
    -> resource_copy_job_t::work_outcome_t {
    const auto source_geometry = probe_resource_image(request.target.source_path, stop);
    if (!source_geometry) return std::unexpected(source_geometry.error());
    auto copied = request.storyboard_receipt->create_copy(stop);
    if (!copied) return std::unexpected(copied.error());
    const auto geometry = probe_resource_image(copied->absolute_path, stop);
    if (!geometry) return std::unexpected(geometry.error());
    osbx_place_image_t operation;
    const auto& target = *request.target.storyboard;
    if (target.resource) {
        auto resource = *target.resource;
        resource.file = copied->relative_path.generic_string(); operation.source = std::move(resource);
    } else {
        operation.source = osbx_import_image_t{.file = {.source_path = request.target.source_path,
            .copied_project_relative_path = copied->relative_path, .expected_new_asset_id = request.target.asset,
            .role = resource_role_t::bga, .name = request.target.source_path.stem().string(), .image_geometry = *geometry}};
    }
    if (target.capture.assets.kind == ui::storyboard_assets_action_kind_t::add_sprite)
        operation.sprite = osbx_sprite_placement_t{.layer = target.capture.assets.layer,
            .x = target.capture.assets.position.x, .y = target.capture.assets.position.y};
    return resource_copy_result_t{.copied = std::move(copied), .storyboard = std::move(operation),
        .receipt = request.storyboard_receipt};
}
}
auto runtime::resource_copy_t::copy_file(const resource_copy_request_t& request,
                                         std::stop_token stop)
    -> resource_copy_job_t::work_outcome_t {
    using namespace wormedit;
    if (request.target.storyboard) return copy_storyboard_image(request, stop);
    auto copied =
        request.target.export_copy
            ? create_export_asset_copy({.project_directory = request.project_directory,
                                        .source_path = request.target.source_path,
                                        .relative_output_path = request.target.output_path,
                                        .asset = request.target.asset},
                                       stop)
            : create_project_asset_copy({.project_directory = request.project_directory,
                                         .source_path = request.target.source_path,
                                         .asset = request.target.asset,
                                         .relative_directory = request.relative_directory},
                                        stop);
    return resource_copy_result_t{.copied = std::move(copied)};
}

auto runtime::resource_copy_t::start(resource_copy_request_t request,
                                     resource_copy_job_t::work_function_t work)
    -> wormedit::async_job_start_outcome_t {
    using namespace wormedit;
    if (is_busy())
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::invalid_request,
            "Wait for the current resource copy, import, or export to finish."});
    if (request.target.storyboard) {
        request.storyboard_receipt = std::make_shared<storyboard_import_receipt_t>(project_asset_copy_request_t{
            .project_directory = request.project_directory, .source_path = request.target.source_path,
            .asset = request.target.asset,
            .planned_filename = std::to_string(request.target.asset.value) + "-" + request.target.source_path.filename().string(),
            .validate_temporary = [](const std::filesystem::path& file) { return probe_resource_image(file).has_value(); },
            .relative_directory = request.relative_directory});
    }
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
                                  wormedit::workspace_controller_t& workspace, storyboard_import_completion_t storyboard)
    -> std::optional<std::string> {
    using namespace wormedit;
    const auto completed = job_.poll();
    if (!completed)
        return std::nullopt;
    const auto& request =
        completed->has_value() ? completed->value().identity : completed->error().identity;
    if (request.storyboard) {
        if (const auto failure = copy_task_failure(*completed)) {
            notifications_.post(failure->severity, notification_source_t::import_, failure->message);
            return failure->message;
        }
        const auto& result = *completed->value().payload;
        if (!storyboard || !result.storyboard || !result.receipt) return "Storyboard import no longer has an active editor.";
        if (const auto valid = result.receipt->validate_link(); !valid) {
            notifications_.post(notification_severity_t::warning, notification_source_t::import_, valid.error().message);
            return valid.error().message;
        }
        const bool applied = storyboard(request.storyboard->capture, *result.storyboard);
        if (applied && result.receipt) result.receipt->mark_linked();
        return applied ? "Storyboard image imported." : "Storyboard image import was refused.";
    }
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
            complete_resource_copy(request, *completed->value().payload->copied, history, workspace);
    return finish(std::move(feedback->message), feedback->severity, feedback->publish,
                  feedback->source);
}

void runtime::resource_copy_t::cancel() {
    job_.cancel();
    save_job_.cancel();
}

runtime::output_group_export_t::output_group_export_t(wormedit::notification_sink_t notifications)
    : notifications_(std::move(notifications)) {}

runtime::output_group_export_t::~output_group_export_t() noexcept {
    try {
        shutdown();
    } catch (...) {
    }
}

void runtime::output_group_export_t::bind_prepared_storyboards(
    std::shared_ptr<const std::vector<osbx_lua_prepared_scene_t>> prepared, std::uint64_t session) {
    if (prepared_session_ && *prepared_session_ != session) {
        cancel_confirmation();
        cancel();
    }
    prepared_session_ = session;
    prepared_storyboards_ = std::move(prepared);
}

auto runtime::output_group_export_t::start(const song_document_t& document,
    output_group_export_plan_t plan, std::filesystem::path asset_base,
    work_function_t work) -> start_outcome_t {
    if (is_busy() || confirmation_)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request,
            "Wait for the current resource copy, import, or export to finish."});
    if (prepared_storyboards_) {
        const auto capture = [&](conversion_request_t& request) {
            if (!request.osbx_prepared.empty()) return;
            for (const auto& scene : *prepared_storyboards_)
                // The worker prepares the current revision and may need this
                // session's earlier success if the changed link fails.
                if (scene.stamp.project == document.id) request.osbx_prepared.push_back(scene);
        };
        capture(plan.request);
        for (auto& target : plan.additional_targets) capture(target.request);
    }
    auto receipt = std::make_shared<std::optional<output_group_export_task_result_t>>();
    auto progress = std::make_shared<job_progress_t>();
    auto request = std::make_shared<output_group_export_request_t>(
        output_group_export_request_t{.document = document,
                                      .plan = std::move(plan),
                                      .asset_base = std::move(asset_base),
                                      .progress = progress});
    task_graph_t graph{1};
    const auto added = graph.create_task(
        {}, [request, receipt, work](std::span<const task_outcome_t>, std::stop_token stop) {
            receipt->emplace(work(*request, stop));
            return task_value_t{};
        });
    if (!added)
        return std::unexpected(added.error());
    auto submitted = scheduler_.begin(std::move(graph));
    if (!submitted)
        return std::unexpected(submitted.error());
    progress_ = std::move(progress);
    cancelling_ = false;
    pending_ =
        pending_t{.graph = std::move(*submitted),
                  .task = *added,
                  .identity = {.project = document.id, .document_revision = document.revision},
                  .receipt = std::move(receipt),
                  .request = std::move(request),
                  .work = work, .session = prepared_session_};
    notifications_.post(notification_severity_t::info, notification_source_t::export_,
                        "Export target preparation started in the background.");
    return {};
}

auto runtime::output_group_export_t::poll(const wormedit::document_history_t& history)
    -> std::optional<std::string> {
    if (!pending_)
        return std::nullopt;
    const auto completed = pending_->graph.outcome(pending_->task);
    if (!completed)
        return std::nullopt;

    const bool is_current_session = pending_->session == prepared_session_;
    const auto exported_song = pending_->identity.project;
    const auto exported_revision = pending_->identity.document_revision;
    const auto receipt = pending_->receipt;
    const auto request = pending_->request;
    const auto work = pending_->work;
    pending_.reset();
    // A cancellation can replace the scheduler outcome after files were published.
    const auto* exported = receipt->has_value() ? &receipt->value() : nullptr;
    if (!is_current_session && (!exported || !exported->published)) {
        const std::string status = "Export cancelled after changing projects.";
        notifications_.post(notification_severity_t::info, notification_source_t::export_, status);
        return status;
    }
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
    if (exported && exported->confirmation_required) {
        confirmation_ = confirmation_t{.request = *request,
                                       .message = exported->message,
                                       .detail = exported->detail,
                                       .work = work};
        return exported->message;
    }
    if (exported && exported->published &&
        (history.document.id != exported_song || history.document.revision != exported_revision || !is_current_session)) {
        status += " The editor session changed after this export started.";
        severity = wormedit::notification_severity_t::warning;
    }
    if (!status.empty())
        notifications_.post({.severity = severity,
                             .source = wormedit::notification_source_t::export_,
                             .message = status,
                             .detail = exported ? exported->detail : std::string{},
                             .target = current_export_target(is_current_session ? exported : nullptr, history, exported_song, exported_revision)});
    return status;
}

auto runtime::output_group_export_t::confirm(const document_history_t& history) -> start_outcome_t {
    if (!confirmation_)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request,
                                                       "No export confirmation is pending."});
    if (document_stamp_t::from(confirmation_->request.document) !=
        document_stamp_t::from(history.document)) {
        confirmation_.reset();
        return std::unexpected(
            error_t<failure_code_t>{failure_code_t::stale_generation,
                                    "The project changed; export confirmation was cancelled."});
    }
    auto request = std::move(confirmation_->request);
    const auto work = confirmation_->work;
    confirmation_.reset();
    request.plan.replace_asked_files();
    return start(request.document, std::move(request.plan), std::move(request.asset_base), work);
}

void runtime::output_group_export_t::cancel_confirmation() {
    confirmation_.reset();
}

void runtime::output_group_export_t::cancel() {
    if (!pending_)
        return;
    cancelling_ = true;
    pending_->graph.cancel();
}

auto runtime::output_group_export_t::view() const -> ui::file_job_view_t {
    return {.active = is_busy(), .cancelling = cancelling_,
            .completed = progress_ ? progress_->completed.load() : 0,
            .total = progress_ ? progress_->total.load() : 0,
            .label = progress_ && progress_->phase.load() == job_phase_t::publishing
                         ? "Publishing artifacts" : "Converting and checking artifacts"};
}

auto runtime::output_group_export_t::confirmation_view() const
    -> output_export_confirmation_view_t {
    if (!confirmation_)
        return {};
    return {.required = true,
            .project_id = confirmation_->request.document.id.value,
            .document_revision = confirmation_->request.document.revision,
            .message = confirmation_->message,
            .detail = confirmation_->detail};
}

void runtime::output_group_export_t::shutdown() {
    cancel();
    scheduler_.shutdown();
    pending_.reset();
    confirmation_.reset();
}

} // namespace wormedit::sys
