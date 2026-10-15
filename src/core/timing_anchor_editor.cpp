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
    if (!chart.query_object_id(id).validate(result.diagnostics, {
            "timing anchor command refers to an unknown anchor",
            "timing anchor ID is ambiguous because it appears more than once",
            "timing anchor command uses the reserved zero object ID"})) return nullptr;
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

auto apply_anchor_edit(chart_document_t& chart,
                       const wormedit::insert_edit_t<wormedit::timing_anchor_edit_value_t>& edit,
                       timing_anchor_edit_limits_t limits, document_command_result_t& result)
    -> anchor_edit_result_t {
    if (chart.timing_anchors.size() >= limits.maximum_timing_anchors) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "timing anchor insertion would exceed the configured anchor cap");
        return {};
    }
    if (!validate_position(chart, edit.value.position, result)) {
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
                                    .position = edit.value.position,
                                    .source_sample_frame = edit.value.settings.source_sample_frame,
                                    .locked = edit.value.settings.locked});
    return {.changed = true, .command_label = "Insert timing anchor"};
}

auto apply_anchor_edit(chart_document_t& chart,
                       const wormedit::update_edit_t<wormedit::timing_anchor_edit_value_t>& edit,
                       timing_anchor_edit_limits_t, document_command_result_t& result)
    -> anchor_edit_result_t {
    auto* anchor = unique_anchor(chart, edit.id, result);
    if (anchor == nullptr || !validate_position(chart, edit.value.position, result)) {
        return {};
    }
    const bool changed = anchor->position != edit.value.position ||
                         anchor->source_sample_frame != edit.value.settings.source_sample_frame ||
                         anchor->locked != edit.value.settings.locked;
    if (!changed) {
        return {};
    }
    anchor->position = edit.value.position;
    anchor->source_sample_frame = edit.value.settings.source_sample_frame;
    anchor->locked = edit.value.settings.locked;
    return {.changed = true, .command_label = "Update timing anchor"};
}

auto apply_anchor_edit(chart_document_t& chart,
                       const wormedit::delete_edit_t<wormedit::timing_anchor_edit_value_t>& edit,
                       timing_anchor_edit_limits_t, document_command_result_t& result)
    -> anchor_edit_result_t {
    auto* anchor = unique_anchor(chart, edit.id, result);
    if (anchor == nullptr) {
        return {};
    }
    const auto anchor_id = anchor->id;
    std::erase_if(chart.timing_anchors,
                  [anchor_id](const auto& item) { return item.id == anchor_id; });
    return {.changed = true, .command_label = "Delete timing anchor"};
}

struct anchor_operation_visitor_t {
    static constexpr std::string_view invalid_limits_message = "timing anchor cap must be positive";
    static auto has_valid_limits(const timing_anchor_edit_limits_t& limits) -> bool {
        return limits.maximum_timing_anchors != 0;
    }

    song_document_t& proposed;
    document_command_result_t& result;
    timing_anchor_edit_limits_t limits;

    template <typename Edit> auto operator()(const Edit& edit) const -> anchor_edit_result_t {
        const auto chart = proposed.find_chart(edit.chart);
        if (!edit.chart || chart == nullptr) {
            reject(result, diagnostic_code_t::invalid_id,
                   "timing anchor edit refers to an unknown chart");
            return {};
        }
        if (chart->timing_anchors.size() > limits.maximum_timing_anchors) {
            reject(result, diagnostic_code_t::invalid_resolution,
                   "selected chart exceeds the timing anchor cap");
            return {};
        }
        return apply_anchor_edit(*chart, edit, limits, result);
    }
};

} // namespace

auto apply_timing_anchor_edit(document_history_t& history,
    const timing_anchor_edit_action_t& action, timing_anchor_edit_limits_t limits) -> document_command_result_t {
    return document_command_transaction_t::from(history).execute<anchor_operation_visitor_t>(action, limits);
}

auto apply_timing_anchor_edit(document_command_transaction_t& transaction,
                              const timing_anchor_edit_action_t& action,
                              timing_anchor_edit_limits_t limits) -> document_command_result_t {
    return transaction.execute<anchor_operation_visitor_t>(action, limits);
}

} // namespace wormedit
