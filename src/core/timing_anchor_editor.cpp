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

constexpr target_query_policy_t anchor_target_policy{
    .messages = {"timing anchor command refers to an unknown anchor",
                 "timing anchor ID is ambiguous because it appears more than once",
                 "timing anchor command uses the reserved zero object ID"},
    .scope = target_uniqueness_scope_t::chart_objects,
    .order = target_query_order_t::identity_first};

struct anchor_edit_result_t {
    bool changed{};
    const char* command_label{};
};

struct anchor_operation_visitor_t : chart_edit_context_t<timing_anchor_edit_limits_t> {
    static constexpr std::string_view invalid_limits_message = "timing anchor cap must be positive";
    static constexpr std::string_view unknown_chart_message =
        "timing anchor edit refers to an unknown chart";
    static auto has_valid_limits(const timing_anchor_edit_limits_t& limits) -> bool {
        return limits.maximum_timing_anchors != 0;
    }
    template <class Edit> auto event_caps(const chart_document_t& chart, const Edit&) const {
        return std::array{chart_event_cap_t{chart.timing_anchors.size(),
                                            limits.maximum_timing_anchors,
                                            "selected chart exceeds the timing anchor cap"}};
    }

    auto operator()(chart_document_t& chart,
                    const insert_edit_t<timing_anchor_edit_value_t>& edit) const
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
        chart.timing_anchors.push_back(
            {.id = *allocation.id,
             .position = edit.value.position,
             .source_sample_frame = edit.value.settings.source_sample_frame,
             .locked = edit.value.settings.locked});
        return {.changed = true, .command_label = "Insert timing anchor"};
    }

    auto operator()(chart_document_t& chart,
                    const update_edit_t<timing_anchor_edit_value_t>& edit) const
        -> anchor_edit_result_t {
        auto* anchor = chart.unique_target(&chart_document_t::timing_anchors, edit.id,
                                           result.diagnostics, anchor_target_policy);
        if (anchor == nullptr || !validate_position(chart, edit.value.position, result)) {
            return {};
        }
        const bool changed =
            anchor->position != edit.value.position ||
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

    auto operator()(chart_document_t& chart,
                    const delete_edit_t<timing_anchor_edit_value_t>& edit) const
        -> anchor_edit_result_t {
        auto* anchor = chart.unique_target(&chart_document_t::timing_anchors, edit.id,
                                           result.diagnostics, anchor_target_policy);
        if (anchor == nullptr) {
            return {};
        }
        const auto anchor_id = anchor->id;
        std::erase_if(chart.timing_anchors,
                      [anchor_id](const auto& item) { return item.id == anchor_id; });
        return {.changed = true, .command_label = "Delete timing anchor"};
    }
};

} // namespace

auto apply_timing_anchor_edit(document_history_t& history,
    const timing_anchor_edit_action_t& action, timing_anchor_edit_limits_t limits) -> document_command_result_t {
    return document_command_transaction_t(history).execute<anchor_operation_visitor_t>(action, limits);
}

auto apply_timing_anchor_edit(document_command_transaction_t& transaction,
                              const timing_anchor_edit_action_t& action,
                              timing_anchor_edit_limits_t limits) -> document_command_result_t {
    return transaction.execute<anchor_operation_visitor_t>(action, limits);
}

} // namespace wormedit
