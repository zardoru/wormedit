#include "position_validation.h"
#include "chart_object_ids.h"
#include <wormedit/core/timing_anchor_editor.h>

#include <algorithm>
#include <limits>
#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>

namespace wormedit {
namespace {

auto validate_position(const chart_document_t& chart, local_position_t position,
                       document_command_result_t& result) -> bool {
    return validate_chart_position(chart, position, result.diagnostics, "timing anchor");
}

auto unique_anchor(chart_document_t& chart, object_id_t id, document_command_result_t& result)
    -> timing_anchor_t* {
    if (!id) {
        reject(result, diagnostic_code_t::invalid_id,
               "timing anchor command uses the reserved zero object ID");
        return nullptr;
    }
    const auto id_count = chart_object_id_count(chart, id, 2);
    if (id_count == 0) {
        reject(result, diagnostic_code_t::invalid_id,
               "timing anchor command refers to an unknown anchor");
        return nullptr;
    }
    if (id_count != 1) {
        reject(result, diagnostic_code_t::duplicate_id,
               "timing anchor ID is ambiguous because it appears more than once");
        return nullptr;
    }
    const auto found = std::ranges::find(chart.timing_anchors, id, &timing_anchor_t::id);
    if (found == chart.timing_anchors.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "timing anchor command refers to an unknown anchor");
        return nullptr;
    }
    return &*found;
}

struct anchor_edit_result_t {
    bool changed{};
    const char* command_label{};
};

auto apply_anchor_edit(chart_document_t& chart, const insert_timing_anchor_action_t& edit,
                       timing_anchor_edit_limits_t limits, document_command_result_t& result)
    -> anchor_edit_result_t {
    if (chart.timing_anchors.size() >= limits.maximum_timing_anchors) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "timing anchor insertion would exceed the configured anchor cap");
        return {};
    }
    if (!validate_position(chart, edit.position, result)) {
        return {};
    }
    const auto allocation = next_chart_object_id(chart, limits.maximum_chart_objects);
    if (!allocation.id.has_value()) {
        reject(result, diagnostic_code_t::invalid_resolution,
               allocation.over_limit ? "selected chart exceeds the object ID allocation cap"
                                     : "chart object ID sequence is exhausted");
        return {};
    }
    chart.timing_anchors.push_back({.id = *allocation.id,
                                    .position = edit.position,
                                    .source_sample_frame = edit.source_sample_frame,
                                    .locked = edit.locked});
    return {.changed = true, .command_label = "Insert timing anchor"};
}

auto apply_anchor_edit(chart_document_t& chart, const update_timing_anchor_action_t& edit,
                       timing_anchor_edit_limits_t, document_command_result_t& result)
    -> anchor_edit_result_t {
    auto* anchor = unique_anchor(chart, edit.anchor, result);
    if (anchor == nullptr || !validate_position(chart, edit.position, result)) {
        return {};
    }
    const bool changed = anchor->position != edit.position ||
                         anchor->source_sample_frame != edit.source_sample_frame ||
                         anchor->locked != edit.locked;
    if (!changed) {
        return {};
    }
    anchor->position = edit.position;
    anchor->source_sample_frame = edit.source_sample_frame;
    anchor->locked = edit.locked;
    return {.changed = true, .command_label = "Update timing anchor"};
}

auto apply_anchor_edit(chart_document_t& chart, const delete_timing_anchor_action_t& edit,
                       timing_anchor_edit_limits_t, document_command_result_t& result)
    -> anchor_edit_result_t {
    auto* anchor = unique_anchor(chart, edit.anchor, result);
    if (anchor == nullptr) {
        return {};
    }
    const auto anchor_id = anchor->id;
    std::erase_if(chart.timing_anchors,
                  [anchor_id](const auto& item) { return item.id == anchor_id; });
    return {.changed = true, .command_label = "Delete timing anchor"};
}

} // namespace

auto apply_timing_anchor_edit(document_history_t& history, std::uint64_t expected_revision,
                              const timing_anchor_edit_action_t& action,
                              timing_anchor_edit_limits_t limits) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id,
               "timing anchor editor document revision is stale");
        return result;
    }
    if (limits.maximum_timing_anchors == 0) {
        reject(result, diagnostic_code_t::invalid_resolution, "timing anchor cap must be positive");
        return result;
    }

    auto proposed = history.document;
    anchor_edit_result_t edit_result;
    std::visit(
        [&](const auto& edit) {
            const auto chart =
                std::ranges::find(proposed.charts, edit.chart, &chart_document_t::id);
            if (!edit.chart || chart == proposed.charts.end()) {
                reject(result, diagnostic_code_t::invalid_id,
                       "timing anchor edit refers to an unknown chart");
                return;
            }
            if (chart->timing_anchors.size() > limits.maximum_timing_anchors) {
                reject(result, diagnostic_code_t::invalid_resolution,
                       "selected chart exceeds the timing anchor cap");
                return;
            }

            edit_result = apply_anchor_edit(*chart, edit, limits, result);
        },
        action);

    if (!result.diagnostics.empty() || !edit_result.changed) {
        return result;
    }
    return commit_document_command(history, std::move(proposed), edit_result.command_label);
}

} // namespace wormedit
