#include "interaction_edit_commit.h"
#include "interaction_internal.h"
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/bms_program_edit.h>

namespace wormedit {
auto pending_resource_paint_gesture_t::begin(editor_edit_context_t& context,
    const begin_resource_paint_gesture_action_t& request)
    -> diagnosed_t<std::optional<pending_resource_paint_gesture_t>> {
    if (!request.pointer_captured)
        return {.diagnostics = {error(diagnostic_code_t::invalid_hold,
            "pointer capture is required to begin keysound painting")}};
    if ((!request.resource && !request.hitsample) ||
        (request.hitsample && !request.hitsample->is_valid()))
        return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Keysound paint requires a valid sample or hitsound palette")}};
    document_command_result_t eligibility;
    if (request.resource) {
        const auto* program = context.history.document.find_bms_program(context.state.chart);
        const auto projection = program ? program->build_projection()
            : attempt_t<bms_program_projection_t, bms_program_projection_diagnostics_t>{};
        if (program && !projection.value) return {.diagnostics = {error(diagnostic_code_t::invalid_id,
            "Scoped keysound paint cannot evaluate its sample catalog.")}};
        const auto& catalog = projection.value ? projection.value->song() : context.history.document;
        if (!validate_resource_role(catalog, *request.resource, resource_role_t::sample, eligibility))
            return {.diagnostics = std::move(eligibility.diagnostics)};
    }
    pending_resource_paint_gesture_t pending{.resource = request.resource, .hitsample = request.hitsample,
        .targets = std::make_shared<std::vector<editor_selection_object_t>>()};
    auto diagnostics = pending.revise(context, request.note);
    if (!diagnostics.empty()) return {.diagnostics = std::move(diagnostics)};
    return {.value = std::move(pending)};
}

auto pending_resource_paint_gesture_t::revise(editor_edit_context_t& context, object_id_t id)
    -> diagnostic_list_t {
    if (!id || visited.contains(id.value)) return {};
    const auto* note = context.scope.chart.find_note(id);
    if (!note) return {error(diagnostic_code_t::invalid_id, "Keysound paint target no longer exists", id)};
    if (!is_keyboard_entry_editable(context.scope.chart, *note)) return {};
    if (targets->size() >= maximum_note_paint_targets)
        return {error(diagnostic_code_t::invalid_resolution, "Keysound paint exceeds the per-gesture target cap")};
    auto expanded = std::make_shared<std::vector<editor_selection_object_t>>(*targets);
    expanded->push_back({editor_selection_object_kind_t::note, id});
    targets = std::move(expanded);
    visited.insert(id.value);
    return {};
}

auto pending_resource_paint_gesture_t::apply(editor_edit_context_t& context) const
    -> editor_interaction_result_t {
    if (!targets || targets->size() > maximum_note_paint_targets)
        return {.diagnostics = {error(diagnostic_code_t::invalid_hold, "Keysound paint capture is malformed")}};
    if (targets->empty()) return {.accepted = true};
    auto transaction = document_command_transaction_t(context.history);
    if (resource) {
        const auto assigned = stage_resource_assignment(transaction,
            {.chart = context.state.chart, .resource = *resource, .objects = *targets});
        if (!assigned.value) return {.diagnostics = assigned.diagnostics};
    }
    if (hitsample) {
        const auto edit = [&](chart_document_t& chart) -> attempt_t<document_edit_effect_t> {
                for (const auto& target : *targets) {
                    auto* note = chart.find_note(target.id);
                    if (!note) return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                        "Keysound paint target no longer exists", target.id)}};
                    note->osu_hitsample = *hitsample;
                }
                return {.value = document_edit_effect_t{.changed = true, .command_label = "Paint keysounds"}};
            };
        std::vector<object_id_t> ids;
        for (const auto& target : *targets) ids.push_back(target.id);
        const auto painted = transaction.has_bms_program(context.state.chart)
            ? transaction.stage_bms_native_objects(context.state.chart, ids, bms_existing_edit_kind_t::update, edit)
            : transaction.stage(context.state.chart, "Keysound paint chart no longer exists",
                [&](chart_document_t& chart, song_document_t&) { return edit(chart); });
        if (!painted.value) return {.diagnostics = painted.diagnostics};
    }
    return commit_editor_edit(context, transaction,
        {.label = "Paint keysounds", .noop_policy = command_noop_policy_t::accept_without_diagnostics});
}
} // namespace wormedit
