#include "sys/compose_quantize.h"

#include <utility>
#include <wormedit/core/command.h>
#include <wormedit/core/interaction_dispatch.h>

namespace wormedit::sys {
namespace {

void normalize_compose_quantize_request(quantize_request_t& request,
                                        const editor_interaction_state_t& state) {
    request.collisions = quantization_collision_t::reject;
    request.allow_resolution_promotion = false;
    request.objects.clear();
    request.selection = editor_quantize_selection(state);
}

} // namespace

auto is_compose_quantize_request_valid(const quantize_request_t& request) -> bool {
    return request.divisions != 0 && request.space <= quantization_space_t::authored_measure &&
           request.tie <= quantization_tie_t::reject &&
           request.holds <= quantization_hold_t::both_endpoints;
}

auto compose_quantize_editor_for(document_history_t& history, editor_interaction_state_t& state,
                                 application_command_state_t& application)
    -> application_command_state_t::quantize_editor_t& {
    auto& editor = application.project.quantize.quantize_editor;
    if (!editor || !editor->stamp.is_identity_current(history.document, state.chart)) {
        editor = application_command_state_t::quantize_editor_t{
            .stamp = chart_stamp_t::from(history.document, state.chart),
            .request = {.divisions = state.active_snap_denominator,
                        .space = state.active_snap_space}};
    }
    return *editor;
}

auto can_apply_compose_quantize(const application_command_state_t& application,
                                const editor_interaction_state_t& state) -> bool {
    return !application.project.compose_view.compose_timing_focus &&
           !application.project.clipboard.paste_session && !state.is_pending();
}

auto toggle_compose_quantize_preview(document_history_t& history,
                                     editor_interaction_state_t& state,
                                     application_command_state_t& application) -> bool {
    auto& editor = compose_quantize_editor_for(history, state, application);
    editor.preview_enabled = !editor.preview_enabled;
    return editor.preview_enabled;
}

auto apply_compose_quantize_selection(document_history_t& history,
                                      editor_interaction_state_t& state,
                                      application_command_state_t& application,
                                      quantize_request_t request) -> compose_action_outcome_t {
    if (!is_compose_quantize_request_valid(request))
        return compose_action_outcome_t::blocked("Selection quantization has invalid settings.");
    normalize_compose_quantize_request(request, state);
    const auto* chart = history.document.find_chart(state.chart);
    if (!chart)
        return compose_action_outcome_t::blocked("The chart is no longer available.");
    const auto& geometry = history.indices(chart->id)->measures;
    if (!geometry)
        return compose_action_outcome_t::blocked("The selected area has invalid measure geometry.");
    auto report = preview_quantize_selection(*chart, request.selection->notes, request, &*geometry);
    auto result = apply_quantize_command(history, state.chart, request);
    if (!result.command.applied)
        return result.command.diagnostics.empty()
                   ? compose_action_outcome_t::blocked("Selection quantization rejected.")
                   : compose_action_outcome_t::from(std::move(result.command.diagnostics),
                                                    notification_source_t::edit,
                                                    compose_feedback_delivery_t::first_diagnostic);
    application.project.quantize.quantize_applied_report =
        application_command_state_t::quantize_applied_report_t{
            .song = history.document.id,
            .chart = state.chart,
            .rows = std::move(report.report_rows),
            .total_rows = report.report_total_rows,
            .truncated = report.report_truncated};
    return compose_action_outcome_t::from(
        std::move(result.command),
        compose_action_description_t{
            .success = "Selected objects quantized.",
            .unchanged = {},
            .rebind_failure = "Selected objects quantized, but workspace rebinding failed.",
            .source = notification_source_t::edit,
            .diagnostic_policy = compose_diagnostic_feedback_policy_t::rejected_only});
}

} // namespace wormedit::sys
