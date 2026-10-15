#include "sys/recovery_notification_feedback.h"
#include <algorithm>
#include <utility>
namespace wormedit::sys {
namespace {
auto post_recovery_feedback(const wormedit::notification_sink_t& notifications,
    wormedit::notification_severity_t severity, std::string message,
    wormedit::notification_source_t source = wormedit::notification_source_t::recovery) -> std::string {
    notifications.post(severity, source, message);
    return message;
}
auto recovery_diagnostic_severity(
    const wormedit::native_project_recovery_controller_dispatch_outcome_t& result,
    wormedit::notification_severity_t fallback) -> wormedit::notification_severity_t {
    const auto* diagnostics = &result.diagnostics;
    if (diagnostics->empty() && result.value.action_result) diagnostics = &result.value.action_result->diagnostics;
    if (diagnostics->empty()) return fallback;
    return wormedit::to_notification_severity(diagnostics->front().severity, fallback);
}
auto first_recovery_diagnostic(
    const wormedit::native_project_recovery_controller_dispatch_outcome_t& result) -> std::string {
    if (!result.diagnostics.empty()) {
        const auto& diagnostic = result.diagnostics.front();
        return diagnostic.path.empty()
                   ? diagnostic.message
                   : diagnostic.message + " (" + diagnostic.path.generic_string() + ")";
    }
    if (result.value.action_result && !result.value.action_result->diagnostics.empty()) {
        const auto& diagnostic = result.value.action_result->diagnostics.front();
        return diagnostic.path.empty()
                   ? diagnostic.message
                   : diagnostic.message + " (" + diagnostic.path.generic_string() + ")";
    }
    return {};
}

auto recovery_action_request(
    const WormUiWorkspaceAction& action, const wormedit::document_history_t& history,
    const std::optional<wormedit::native_project_document_version_t>& scanned_primary_version,
    const wormedit::native_project_recovery_action_target_t& target,
    std::string& error) -> wormedit::native_project_recovery_controller_action_t {
    using namespace wormedit;
    switch (action.kind) {
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT:
        return native_project_recovery_target_action_t{
            .target = target, .kind = wormedit::native_project_recovery_target_kind_t::inspect};
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER:
        if (!history.undo.empty() || !history.redo.empty()) {
            error = "The current workspace changed after recovery discovery; save or restart "
                    "before replacing it with a recovery candidate.";
            return native_project_recovery_target_action_t{
                .target = target, .kind = wormedit::native_project_recovery_target_kind_t::inspect};
        }
        return recover_native_project_recovery_action_t{
            .target = target,
            .expected_session = {
                .is_set = true,
                .version = scanned_primary_version
                    ? std::optional<native_project_document_version_t>{
                          native_project_document_version_t{.project_id = history.document.id,
                                                            .revision = history.document.revision}}
                    : std::nullopt,
            },
        };
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS: {
        const auto end = std::find(std::begin(action.recovery_save_as_destination),
                                   std::end(action.recovery_save_as_destination), '\0');
        const std::string destination(action.recovery_save_as_destination, end);
        if (destination.empty()) {
            error = "Choose a new project path before using Save as.";
            return native_project_recovery_target_action_t{
                .target = target, .kind = wormedit::native_project_recovery_target_kind_t::inspect};
        }
        return save_as_native_project_recovery_action_t{
            .target = target, .destination = std::filesystem::path{destination}};
    }
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_DISCARD:
        return native_project_recovery_target_action_t{
            .target = target, .kind = wormedit::native_project_recovery_target_kind_t::discard};
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE:
        return native_project_recovery_target_action_t{
            .target = target, .kind = wormedit::native_project_recovery_target_kind_t::quarantine};
    default:
        error = "Invalid recovery action.";
        return native_project_recovery_target_action_t{
            .target = target, .kind = wormedit::native_project_recovery_target_kind_t::inspect};
    }
}

auto load_recovered_document(
    const wormedit::native_project_recovery_action_result_t& result,
    const WormUiWorkspaceAction& action, wormedit::native_project_recovery_controller_t& controller,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    wormedit::native_project_autosave_loop_t& autosave_loop, bool& recovered,
    const wormedit::notification_sink_t& notifications) -> std::string {
    using namespace wormedit;
    if (!result.document)
        return post_recovery_feedback(notifications, notification_severity_t::error,
            "Recovery action completed without a replacement document.");
    auto replacement_history = document_history_t::from(*result.document, 128, false);
    auto replacement_binding = workspace_controller_t::from(replacement_history);
    if (!replacement_binding) {
        return post_recovery_feedback(notifications, notification_severity_t::error,
            "Recovered data validated, but the workspace could not be rebound; "
            "the current workspace was kept.", notification_source_t::document);
    }
    const auto& recovery_view = controller.view();
    std::optional<native_project_document_version_t> recovered_save_baseline;
    auto recovered_sequence = std::uint64_t{};
    if (action.recovery_row_index < recovery_view.rows.size()) {
        const auto& row = recovery_view.rows[action.recovery_row_index];
        if (row.last_explicit_save_revision) {
            recovered_save_baseline = native_project_document_version_t{
                .project_id = result.document->id,
                .revision = *row.last_explicit_save_revision};
        }
        if (row.recovery_session_sequence)
            recovered_sequence = *row.recovery_session_sequence;
    }
    if (!autosave_loop.reset_session(native_project_autosave_loop_t::clock_t::now(),
                                     recovered_save_baseline, recovered_sequence)) {
        return post_recovery_feedback(notifications, notification_severity_t::warning,
            "Wait for the current autosave to finish before recovering a session.");
    }
    history = std::move(replacement_history);
    workspace = std::move(*replacement_binding.workspace);
    recovered = true;
    (void)controller.replace_scan({});
    return post_recovery_feedback(notifications, notification_severity_t::success,
        "Recovery loaded into the current session. Review it and save explicitly.");
}

} // namespace

auto dispatch_recovery_ui_action(
    const WormUiWorkspaceAction& action, wormedit::native_project_recovery_controller_t& controller,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::native_project_document_version_t> scanned_primary_version,
    wormedit::native_project_autosave_loop_t& autosave_loop, bool& recovered,
    const wormedit::notification_sink_t& notifications) -> std::string {
    using namespace wormedit;
    const native_project_recovery_action_target_t target{
        .scan_generation = action.recovery_scan_generation,
        .row_index = action.recovery_row_index,
        .candidate_index = action.recovery_candidate_index,
    };
    std::string request_error;
    const auto request = recovery_action_request(action, history, scanned_primary_version,
                                                 target, request_error);
    if (!request_error.empty())
        return post_recovery_feedback(notifications, notification_severity_t::warning, request_error);
    const auto dispatched = controller.dispatch_action(request);
    const auto diagnostic = first_recovery_diagnostic(dispatched);
    if (!dispatched.value.accepted) {
        return post_recovery_feedback(notifications,
            recovery_diagnostic_severity(dispatched, notification_severity_t::warning),
            diagnostic.empty() ? "Recovery action was rejected." : diagnostic);
    }
    if (!dispatched.value.action_result) {
        return post_recovery_feedback(notifications,
            recovery_diagnostic_severity(dispatched, notification_severity_t::error),
            diagnostic.empty() ? "Recovery action did not produce a result." : diagnostic);
    }
    const auto& result = *dispatched.value.action_result;
    if (result.has_errors() || !result.applied) {
        return post_recovery_feedback(notifications,
            recovery_diagnostic_severity(dispatched, notification_severity_t::error),
            diagnostic.empty() ? "Recovery action did not complete." : diagnostic);
    }

    switch (action.kind) {
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT:
        if (result.document) {
            return post_recovery_feedback(notifications, notification_severity_t::info,
                "Inspected recovery candidate read-only: " + result.document->metadata.title +
                " (revision " + std::to_string(result.document->revision) + ").");
        }
        return post_recovery_feedback(notifications, notification_severity_t::info, "Recovery candidate inspected read-only.");
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER:
        return load_recovered_document(result, action, controller, history, workspace,
                                       autosave_loop, recovered, notifications);
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS:
        return post_recovery_feedback(notifications, notification_severity_t::success, "Recovery candidate saved as a new project; the current workspace was unchanged.");
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_DISCARD:
        return post_recovery_feedback(notifications, notification_severity_t::success, "Recovery artifact moved to its recoverable discarded location.");
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE:
        return post_recovery_feedback(notifications, notification_severity_t::success,
            result.quarantined_path
                ? "Recovery artifact quarantined at " + result.quarantined_path->generic_string()
                : "Recovery artifact quarantined.");
    default:
        return post_recovery_feedback(notifications, notification_severity_t::warning, "Invalid recovery action.");
    }
}

void recovery_scan_notification_publisher_t::publish(
    const native_project_recovery_controller_view_t& view, const notification_sink_t& notifications) {
    if (generation_ == view.scan_generation) return;
    generation_ = view.scan_generation;
    for (const auto& diagnostic : view.diagnostics) {
        const auto severity = to_notification_severity(diagnostic.severity);
        notifications.post(severity, notification_source_t::recovery,
            diagnostic.path.empty() ? diagnostic.message
                : diagnostic.message + " (" + diagnostic.path.generic_string() + ")");
    }
}
} // namespace wormedit::sys
