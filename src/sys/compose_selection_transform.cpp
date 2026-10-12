#include "sys/compose_selection_transform.h"

#include <wormedit/core/lane_profile_switch.h>
#include <wormedit/core/time_shift.h>

#include <algorithm>
#include <map>

namespace wormedit::sys {
namespace {

void set_feedback_severity(notification_severity_t* output, notification_severity_t value) {
    if (output) *output = value;
}
auto diagnostic_feedback_severity(diagnostic_severity_t value) -> notification_severity_t {
    switch (value) {
    case diagnostic_severity_t::info: return notification_severity_t::info;
    case diagnostic_severity_t::warning: return notification_severity_t::warning;
    case diagnostic_severity_t::error: return notification_severity_t::error;
    }
    return notification_severity_t::error;
}

auto command_status(const document_command_result_t& result, const char* applied,
                    const char* unchanged, notification_severity_t* severity) -> std::string {
    if (!result.diagnostics.empty() && !result.applied) {
        set_feedback_severity(severity, diagnostic_feedback_severity(result.diagnostics.front().severity));
        return result.diagnostics.front().message;
    }
    set_feedback_severity(severity, result.applied ? notification_severity_t::success
                                                 : notification_severity_t::info);
    return result.applied ? applied : unchanged;
}

auto chart_for(const document_history_t& history, const editor_interaction_state_t& state)
    -> const chart_document_t* {
    const auto found = std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    return found == history.document.charts.end() ? nullptr : &*found;
}

auto shift_selection(document_history_t& history, const editor_interaction_state_t& state,
                     const chart_document_t& chart, const std::vector<object_id_t>& notes,
                     bool later, notification_severity_t* severity) -> std::string {
    if (state.active_snap_denominator == 0 ||
        chart.divisions_per_standard_measure % state.active_snap_denominator != 0)
        return "The active snap does not divide the chart's rows.";
    const auto step = static_cast<std::int64_t>(chart.divisions_per_standard_measure /
                                                state.active_snap_denominator);
    const auto result = apply_shift_selection(
        history, history.document.revision,
        {.chart = state.chart, .notes = notes, .rows = later ? step : -step});
    return command_status(result.command, "Selection shifted by one snap step.",
                          "Selection did not move.", severity);
}

} // namespace

auto rotate_lane_permutation(const lane_layout_t& layout, bool left)
    -> std::vector<lane_permutation_entry_t> {
    std::map<stage_group_t, std::vector<const lane_definition_t*>> stages;
    for (const auto& lane : layout.lanes)
        if (lane.role == lane_role_t::playable) stages[lane.stage].push_back(&lane);
    std::vector<lane_permutation_entry_t> permutation;
    for (auto& [stage, lanes] : stages) {
        std::ranges::stable_sort(lanes, {}, &lane_definition_t::display_order);
        for (std::size_t index = 0; index < lanes.size(); ++index) {
            const auto target = left ? (index + lanes.size() - 1) % lanes.size()
                                     : (index + 1) % lanes.size();
            permutation.push_back({lanes[index]->id, lanes[target]->id});
        }
    }
    return permutation;
}

auto apply_selection_transform(document_history_t& history, const editor_interaction_state_t& state,
                               selection_transform_t transform, notification_severity_t* severity) -> std::string {
    set_feedback_severity(severity, notification_severity_t::warning);
    const auto notes = editor_quantize_selection(state).notes;
    if (notes.empty()) return "Select notes before transforming them.";
    const auto* chart = chart_for(history, state);
    if (!chart) return "The selected difficulty is no longer available.";
    const auto revision = history.document.revision;
    switch (transform) {
    case selection_transform_t::mirror:
        return command_status(
            apply_lane_mirror(history, revision, {.chart = state.chart, .notes = notes}),
            "Selection mirrored.", "Mirroring moved no notes.", severity);
    case selection_transform_t::rotate_left:
    case selection_transform_t::rotate_right: {
        const auto* segment = active_lane_layout_segment(*chart, state.cursor);
        if (!segment) return "The cursor has no lane layout.";
        auto permutation = rotate_lane_permutation(segment->layout,
                                                   transform == selection_transform_t::rotate_left);
        if (permutation.size() < 2) return "The layout has no lanes to rotate.";
        return command_status(
            apply_lane_remap(history, revision,
                             {.chart = state.chart, .notes = notes,
                              .permutation = std::move(permutation)}),
            "Selection rotated.", "Rotation moved no notes.", severity);
    }
    case selection_transform_t::shift_earlier:
    case selection_transform_t::shift_later:
        return shift_selection(history, state, *chart, notes, transform == selection_transform_t::shift_later, severity);
    }
    return "Unknown transform.";
}

} // namespace wormedit::sys
