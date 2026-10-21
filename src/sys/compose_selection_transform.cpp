#include "sys/notifications/projection_notification_feedback.h"
#include "sys/compose_selection_transform.h"

#include <wormedit/core/lane_profile_switch.h>
#include <wormedit/core/time_shift.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/interaction_dispatch.h>

#include <algorithm>
#include <map>

namespace wormedit::sys {
namespace {

auto command_status(const document_command_result_t& result, const char* applied,
                    const char* unchanged, notification_severity_t* severity) -> std::string {
    if (!result.diagnostics.empty() && !result.applied) {
        set_feedback_severity(severity,
                              to_notification_severity(result.diagnostics.front().severity));
        return result.diagnostics.front().message;
    }
    set_feedback_severity(severity, result.applied ? notification_severity_t::success
                                                 : notification_severity_t::info);
    return result.applied ? applied : unchanged;
}

// The selection moves as one block, by one step of the measure its earliest object moves within.
auto selection_shift_step(const document_history_t& history, const chart_document_t& chart,
                          const shift_selection_action_t& action, snap_grid_t grid, bool later)
    -> std::optional<std::uint64_t> {
    const auto* indices = history.indices(chart.id);
    if (!indices || !indices->measures) return std::nullopt;
    const auto& index = *indices->measures;
    std::optional<std::uint64_t> earliest;
    const auto consider = [&](std::optional<std::uint64_t> row) {
        if (row && (!earliest || *row < *earliest)) earliest = row;
    };
    for (const auto id : action.notes)
        if (const auto* note = chart.find_note(id)) consider(index.row_of(note->position));
    for (const auto id : action.bar_lines) {
        const auto entry = std::ranges::find(index.entries, id, &measure_index_entry_t::id);
        if (entry != index.entries.end()) consider(entry->absolute_begin_row);
    }
    if (!earliest) return snap_step(chart, grid, 0);
    return adjacent_snap_step(chart, index, *earliest, grid, later);
}

auto shift_selection(document_history_t& history, const editor_interaction_state_t& state,
                     const chart_document_t& chart, bool later,
                     notification_severity_t* severity) -> std::string {
    auto action = editor_shift_selection(state, 0);
    if (action.notes.empty() && action.bar_lines.empty())
        return "Select notes or bar lines before shifting them.";
    const auto step = selection_shift_step(history, chart, action, active_snap_grid(state), later);
    if (!step)
        return "The active snap does not divide the chart's rows.";
    action.rows = later ? static_cast<std::int64_t>(*step) : -static_cast<std::int64_t>(*step);
    const auto result = apply_shift_selection(history, action);
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
    const auto* chart = history.document.find_chart(state.chart);
    if (!chart) return "The selected difficulty is no longer available.";
    if (transform == selection_transform_t::shift_earlier || transform == selection_transform_t::shift_later)
        return shift_selection(history, state, *chart, transform == selection_transform_t::shift_later, severity);
    const auto notes = editor_quantize_selection(state).notes;
    if (notes.empty()) return "Select notes before transforming them.";
    switch (transform) {
    case selection_transform_t::mirror:
        return command_status(apply_lane_mirror(history, {.chart = state.chart, .notes = notes}),
                              "Selection mirrored.", "Mirroring moved no notes.", severity);
    case selection_transform_t::rotate_left:
    case selection_transform_t::rotate_right: {
        const auto* segment = active_lane_layout_segment(*chart, state.cursor);
        if (!segment) return "The cursor has no lane layout.";
        auto permutation = rotate_lane_permutation(segment->layout,
                                                   transform == selection_transform_t::rotate_left);
        if (permutation.size() < 2) return "The layout has no lanes to rotate.";
        return command_status(apply_lane_remap(history, {.chart = state.chart,
                                                         .notes = notes,
                                                         .permutation = std::move(permutation)}),
                              "Selection rotated.", "Rotation moved no notes.", severity);
    }
    case selection_transform_t::shift_earlier:
    case selection_transform_t::shift_later:
        break;
    }
    return "Unknown transform.";
}

} // namespace wormedit::sys
