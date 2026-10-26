#include "sys/timing/timing_edit_runtime.h"

#include "sys/app/application_edit_inputs.h"
#include "sys/app/application_timing_stage.h"
#include "sys/compose/compose_timing_target.h"

#include <wormedit/core/timing_lane_catalog.h>

#include <utility>

namespace wormedit::sys {
namespace runtime {

void timing_edit_t::dispatch(const ui::workspace_action_t& action,
                             const document_edit_inputs_t& inputs, bool timing_locked,
                             std::uint64_t active_snap_denominator) {
    switch (action.timing_stage_action) {
    case ui::timing_stage_action_t::none:
        return;
    case ui::timing_stage_action_t::begin:
        begin(action, inputs, timing_locked, active_snap_denominator);
        return;
    case ui::timing_stage_action_t::quick_insert:
        quick_insert(action, inputs, timing_locked, active_snap_denominator);
        return;
    case ui::timing_stage_action_t::cancel:
        if (stage) {
            cancel();
            inputs.notifications.post_transient_hint(notification_source_t::edit,
                                                     "Timing event staging cancelled.");
        }
        return;
    case ui::timing_stage_action_t::update:
    case ui::timing_stage_action_t::move:
    case ui::timing_stage_action_t::confirm:
    case ui::timing_stage_action_t::remove:
        if (stage)
            dispatch_active(action, inputs, timing_locked, active_snap_denominator);
        return;
    }
}

void timing_edit_t::begin(const ui::workspace_action_t& action,
                          const document_edit_inputs_t& inputs, bool timing_locked,
                          std::uint64_t active_snap_denominator) {
    if (stage)
        return;
    auto candidate = begin_timing_stage(action, inputs.history, inputs.workspace, timing_locked,
                                        active_snap_denominator);
    if (candidate.value) {
        stage = std::move(candidate);
        inputs.notifications.post_transient_hint(
            notification_source_t::edit, stage->report()->existing_event.has_value()
                                             ? "Timing event loaded; apply to update it."
                                             : "Timing event staged; confirm to insert it.");
    } else {
        inputs.notifications.post_rejection(notification_source_t::edit, candidate.diagnostics,
                                             "Timing event could not be staged at that row.");
    }
}

// Timing focus: begin a new event with its default value and confirm it at once.
void timing_edit_t::quick_insert(const ui::workspace_action_t& action,
                                 const document_edit_inputs_t& inputs, bool timing_locked,
                                 std::uint64_t active_snap_denominator) {
    if (stage) {
        inputs.notifications.post(
            notification_severity_t::warning, notification_source_t::edit,
            "Confirm or cancel the open timing edit before inserting another event.");
        return;
    }
    auto candidate = begin_timing_stage(action, inputs.history, inputs.workspace, timing_locked,
                                        active_snap_denominator);
    if (!candidate.value) {
        inputs.notifications.post_rejection(notification_source_t::edit, candidate.diagnostics,
                                             "Timing event could not be inserted at that row.");
        return;
    }
    const auto result = apply_proposal(inputs.history, candidate, timing_locked);
    if (result)
        inputs.notifications.post(notification_severity_t::success, notification_source_t::edit,
                                  "Timing event inserted.");
    else
        inputs.notifications.post_rejection(notification_source_t::edit, result.diagnostics,
                                             "Timing event insertion was rejected.");
}

void timing_edit_t::cancel() {
    cancel_proposal(*stage);
    stage.reset();
}

void timing_edit_t::dispatch_active(const ui::workspace_action_t& action,
                                    const document_edit_inputs_t& inputs, bool timing_locked,
                                    std::uint64_t active_snap_denominator) {
    if (auto failure =
            validate_timing_stage_binding(action, inputs.history, inputs.workspace, *stage)) {
        cancel();
        inputs.notifications.post(notification_severity_t::warning, notification_source_t::edit,
                                  *failure);
        return;
    }
    const auto* chart = inputs.history.read_chart(stage->captured_stamp().chart);
    if (action.timing_stage_action == ui::timing_stage_action_t::remove) {
        remove(action, inputs, timing_locked);
    } else if (action.timing_stage_action == ui::timing_stage_action_t::move) {
        move(action, inputs, timing_locked, active_snap_denominator, chart);
    } else {
        revise_value(action, inputs, timing_locked, active_snap_denominator, chart);
    }
}

void timing_edit_t::revise_value(const ui::workspace_action_t& action,
                                 const document_edit_inputs_t& inputs, bool timing_locked,
                                 std::uint64_t active_snap_denominator,
                                 const chart_document_t* chart) {
    if (action.timing_anchoring) {
        *stage = timing_lane_pseudonote_preview_t::revise(std::move(*stage), inputs.history,
                                                          *action.timing_anchoring, timing_locked);
        return;
    }
    const auto value = chart == nullptr ? std::optional<timing_lane_value_t>{}
                                        : timing_stage_value(*stage->report(), *chart,
                                                             *inputs.history.indices(chart->id),
                                                             action, active_snap_denominator);
    if (!value) {
        stage->diagnostics = {{
            .severity = diagnostic_severity_t::error,
            .code = diagnostic_code_t::invalid_rational,
            .message = "Enter a bounded exact rational for this timing value.",
        }};
        stage->complete(false);
        inputs.notifications.post(notification_severity_t::warning, notification_source_t::edit,
                                  "Timing value is outside its supported bounds.");
        return;
    }
    *stage = timing_lane_pseudonote_preview_t::revise(std::move(*stage), inputs.history, *value,
                                                      timing_locked);
    if (action.timing_stage_action == ui::timing_stage_action_t::confirm)
        confirm(inputs, timing_locked);
}

void timing_edit_t::confirm(const document_edit_inputs_t& inputs, bool timing_locked) {
    if (!stage->value) {
        inputs.notifications.post_rejection(notification_source_t::edit, stage->diagnostics,
                                             "Timing event values are invalid.");
        return;
    }
    const auto result = apply_proposal(inputs.history, *stage, timing_locked);
    const bool edited_existing = stage->report()->existing_event.has_value();
    if (result)
        inputs.notifications.post(notification_severity_t::success, notification_source_t::edit,
                                  edited_existing ? "Timing event updated."
                                                  : "Timing event inserted.");
    else if (result.diagnostics.empty() && edited_existing)
        inputs.notifications.post(notification_severity_t::info, notification_source_t::edit,
                                  "Timing event was unchanged.");
    else
        inputs.notifications.post_rejection(notification_source_t::edit, result.diagnostics,
                                             "Timing event insertion was rejected.");
    stage.reset();
}

void timing_edit_t::move(const ui::workspace_action_t& action, const document_edit_inputs_t& inputs,
                         bool timing_locked, std::uint64_t active_snap_denominator,
                         const chart_document_t* chart) {
    const auto target =
        chart == nullptr || !stage->report()->existing_event || !action.timing_has_target_y
            ? std::optional<local_position_t>{}
            : timing_position_from_fraction(*chart, *inputs.history.indices(chart->id),
                                            timing_target_fraction(action.timing_target),
                                            active_snap_denominator);
    if (!target || !(*stage = timing_lane_pseudonote_preview_t::revise(
                         std::move(*stage), inputs.history, *target, timing_locked))
                        .value.has_value()) {
        inputs.notifications.post_rejection(notification_source_t::edit, stage->diagnostics,
                                             "The timing marker cannot move to that row.");
    } else {
        inputs.notifications.post_transient_hint(
            notification_source_t::edit,
            "Timing marker moved in the staged preview; apply to commit.");
    }
}

void timing_edit_t::remove(const ui::workspace_action_t& action,
                           const document_edit_inputs_t& inputs, bool timing_locked) {
    const auto* kind = find_catalog_entry(timing_lanes, action.timing_lane);
    if (!stage->report()->existing_event || !kind || kind->kind != stage->report()->kind) {
        inputs.notifications.post(notification_severity_t::warning, notification_source_t::edit,
                                  "The selected timing event cannot be deleted.");
    } else {
        const auto result = delete_existing_timing_lane_event(
            inputs.history, stage->captured_stamp().chart, kind->kind,
            *stage->report()->existing_event, timing_locked, stage->report()->anchoring);
        if (result)
            inputs.notifications.post(notification_severity_t::success, notification_source_t::edit,
                                      "Timing event deleted.");
        else
            inputs.notifications.post_rejection(notification_source_t::edit, result.diagnostics,
                                                 "Timing event deletion was rejected.");
    }
    cancel();
}

void timing_edit_t::cancel_if_stale(const document_history_t& history,
                                    const workspace_controller_t& workspace,
                                    const application_command_state_t&) {
    if (stage && (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
                  !stage->captured_stamp().is_current(history.document, *workspace.selected_chart,
                                                      document_identity_policy_t::revision_only))) {
        cancel();
    }
}

} // namespace runtime
} // namespace wormedit::sys
