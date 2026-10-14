#include "timing_event_internal.h"
#include "position_validation.h"
#include "chart_object_ids.h"
#include <wormedit/core/scroll_speed_editor.h>

#include <algorithm>
#include <limits>
#include <ranges>
#include <string_view>
#include <utility>
#include <variant>

namespace wormedit {
namespace {

auto validate_scroll_value(signed_rational_t multiplier, document_command_result_t& result)
    -> bool {
    if (multiplier.denominator == 0) {
        reject(result, diagnostic_code_t::invalid_rational,
               "scroll multiplier requires a nonzero denominator");
        return false;
    }
    return true;
}

auto validate_speed_values(signed_rational_t multiplier, rational_t transition_beats,
                           document_command_result_t& result) -> bool {
    if (multiplier.numerator < 0 || multiplier.denominator == 0 ||
        transition_beats.denominator == 0) {
        reject(result, diagnostic_code_t::invalid_rational,
               "speed multiplier and transition duration must be valid nonnegative rationals");
        return false;
    }
    return true;
}

auto validate_position(const chart_document_t& chart, local_position_t position,
                       document_command_result_t& result) -> bool {
    return validate_chart_position(chart, position, result.diagnostics, "scroll/speed event");
}



template <typename event_t>
auto unique_event(const chart_document_t& chart, std::vector<event_t>& events, object_id_t id,
                  std::string_view name, document_command_result_t& result) -> event_t* {
    const auto found = std::ranges::find(events, id, &event_t::id);
    if (found == events.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               std::string(name) + " command refers to an unknown event");
        return nullptr;
    }
    if (chart_object_id_count(chart, id) != 1) {
        reject(result, diagnostic_code_t::duplicate_id,
               std::string(name) + " event ID is ambiguous because it appears more than once");
        return nullptr;
    }
    return &*found;
}

struct scroll_speed_edit_context_t {
    song_document_t& proposed;
    document_command_result_t& result;
    scroll_speed_edit_limits_t limits;
    std::string command_label;
    bool changed{};
};

auto target_chart(scroll_speed_edit_context_t& context, chart_id_t id) -> chart_document_t* {
    const auto found = context.proposed.find_chart(id);
    if (!id || found == nullptr) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "scroll/speed edit refers to an unknown chart");
        return nullptr;
    }
    return &*found;
}

auto check_scroll_cap(scroll_speed_edit_context_t& context, const chart_document_t& chart,
                      bool inserting) -> bool {
    if (chart.scrolls.size() > context.limits.maximum_scroll_events) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "selected chart exceeds the scroll event cap");
        return false;
    }
    if (inserting && chart.scrolls.size() >= context.limits.maximum_scroll_events) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "scroll insertion would exceed the configured event cap");
        return false;
    }
    return true;
}

auto check_speed_cap(scroll_speed_edit_context_t& context, const chart_document_t& chart,
                     bool inserting) -> bool {
    if (chart.speeds.size() > context.limits.maximum_speed_events) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "selected chart exceeds the speed event cap");
        return false;
    }
    if (inserting && chart.speeds.size() >= context.limits.maximum_speed_events) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "speed insertion would exceed the configured event cap");
        return false;
    }
    return true;
}

auto allocate_event_id(scroll_speed_edit_context_t& context, const chart_document_t& chart)
    -> std::optional<object_id_t> {
    const auto allocation = next_chart_object_id(chart, context.limits.maximum_chart_objects);
    if (allocation.id) return allocation.id;
    reject(context.result, diagnostic_code_t::invalid_resolution,
           allocation.over_limit ? "selected chart exceeds the object ID allocation cap"
                                 : "chart object ID sequence is exhausted");
    return std::nullopt;
}

auto insert_scroll(scroll_speed_edit_context_t& context, chart_document_t& chart,
                   const insert_scroll_event_action_t& edit) -> void {
    if (!check_scroll_cap(context, chart, true)) return;
    const auto valid = validate_scroll_value(edit.multiplier, context.result);
    if (!validate_position(chart, edit.position, context.result) || !valid) return;
    const auto id = allocate_event_id(context, chart);
    if (!id) return;
    const auto order = next_source_order(chart.scrolls);
    if (!order) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "scroll source-order sequence is exhausted");
        return;
    }
    chart.scrolls.push_back({.id = *id, .position = edit.position,
        .multiplier = edit.multiplier, .provenance = {.source_order = *order}});
    context.command_label = "Insert scroll event";
    context.changed = true;
}

auto insert_speed(scroll_speed_edit_context_t& context, chart_document_t& chart,
                  const insert_speed_event_action_t& edit) -> void {
    if (!check_speed_cap(context, chart, true)) return;
    const auto valid = validate_speed_values(edit.multiplier, edit.transition_beats,
                                              context.result);
    if (!validate_position(chart, edit.position, context.result) || !valid) return;
    const auto id = allocate_event_id(context, chart);
    if (!id) return;
    const auto order = next_source_order(chart.speeds);
    if (!order) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "speed source-order sequence is exhausted");
        return;
    }
    chart.speeds.push_back({.id = *id, .position = edit.position,
        .multiplier = edit.multiplier, .transition_beats = edit.transition_beats,
        .transition_is_seconds = edit.transition_is_seconds,
        .provenance = {.source_order = *order}});
    context.command_label = "Insert speed event";
    context.changed = true;
}

auto update_scroll(scroll_speed_edit_context_t& context, chart_document_t& chart,
                   const update_scroll_event_action_t& edit) -> void {
    if (!check_scroll_cap(context, chart, false)) return;
    if (!edit.event) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "scroll update uses the reserved zero event ID");
        return;
    }
    auto* event = unique_event(chart, chart.scrolls, edit.event, "scroll", context.result);
    const auto valid = validate_scroll_value(edit.multiplier, context.result);
    if (!event || !validate_position(chart, edit.position, context.result) || !valid) return;
    context.changed = event->position != edit.position || event->multiplier != edit.multiplier;
    if (!context.changed) return;
    event->position = edit.position;
    event->multiplier = edit.multiplier;
    context.command_label = "Update scroll event";
}

auto update_speed(scroll_speed_edit_context_t& context, chart_document_t& chart,
                  const update_speed_event_action_t& edit) -> void {
    if (!check_speed_cap(context, chart, false)) return;
    if (!edit.event) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "speed update uses the reserved zero event ID");
        return;
    }
    auto* event = unique_event(chart, chart.speeds, edit.event, "speed", context.result);
    const auto valid = validate_speed_values(edit.multiplier, edit.transition_beats,
                                              context.result);
    if (!event || !validate_position(chart, edit.position, context.result) || !valid) return;
    context.changed = event->position != edit.position || event->multiplier != edit.multiplier ||
        event->transition_beats != edit.transition_beats ||
        event->transition_is_seconds != edit.transition_is_seconds;
    if (!context.changed) return;
    event->position = edit.position;
    event->multiplier = edit.multiplier;
    event->transition_beats = edit.transition_beats;
    event->transition_is_seconds = edit.transition_is_seconds;
    context.command_label = "Update speed event";
}

auto delete_scroll(scroll_speed_edit_context_t& context, chart_document_t& chart,
                   const delete_scroll_event_action_t& edit) -> void {
    if (!check_scroll_cap(context, chart, false)) return;
    if (!edit.event) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "scroll deletion uses the reserved zero event ID");
        return;
    }
    if (!unique_event(chart, chart.scrolls, edit.event, "scroll", context.result)) return;
    std::erase_if(chart.scrolls, [&](const auto& item) { return item.id == edit.event; });
    context.command_label = "Delete scroll event";
    context.changed = true;
}

auto delete_speed(scroll_speed_edit_context_t& context, chart_document_t& chart,
                  const delete_speed_event_action_t& edit) -> void {
    if (!check_speed_cap(context, chart, false)) return;
    if (!edit.event) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "speed deletion uses the reserved zero event ID");
        return;
    }
    if (!unique_event(chart, chart.speeds, edit.event, "speed", context.result)) return;
    std::erase_if(chart.speeds, [&](const auto& item) { return item.id == edit.event; });
    context.command_label = "Delete speed event";
    context.changed = true;
}

auto apply_scroll_speed_action(scroll_speed_edit_context_t& context, chart_document_t& chart,
                               const insert_scroll_event_action_t& edit) -> void {
    insert_scroll(context, chart, edit);
}
auto apply_scroll_speed_action(scroll_speed_edit_context_t& context, chart_document_t& chart,
                               const update_scroll_event_action_t& edit) -> void {
    update_scroll(context, chart, edit);
}
auto apply_scroll_speed_action(scroll_speed_edit_context_t& context, chart_document_t& chart,
                               const delete_scroll_event_action_t& edit) -> void {
    delete_scroll(context, chart, edit);
}
auto apply_scroll_speed_action(scroll_speed_edit_context_t& context, chart_document_t& chart,
                               const insert_speed_event_action_t& edit) -> void {
    insert_speed(context, chart, edit);
}
auto apply_scroll_speed_action(scroll_speed_edit_context_t& context, chart_document_t& chart,
                               const update_speed_event_action_t& edit) -> void {
    update_speed(context, chart, edit);
}
auto apply_scroll_speed_action(scroll_speed_edit_context_t& context, chart_document_t& chart,
                               const delete_speed_event_action_t& edit) -> void {
    delete_speed(context, chart, edit);
}

template <typename Edit>
auto apply_scroll_speed_operation(scroll_speed_edit_context_t& context, const Edit& edit) -> void {
    auto* chart = target_chart(context, edit.chart);
    if (chart) apply_scroll_speed_action(context, *chart, edit);
}

} // namespace

auto apply_scroll_speed_edit(document_history_t& history,
                             const scroll_speed_edit_action_t& action,
                             scroll_speed_edit_limits_t limits) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (limits.maximum_scroll_events == 0 || limits.maximum_speed_events == 0) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "scroll and speed event caps must be positive");
        return result;
    }

    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    scroll_speed_edit_context_t context{.proposed = proposed, .result = result,
        .limits = limits, .command_label = {}, .changed = false};
    std::visit([&](const auto& edit) { apply_scroll_speed_operation(context, edit); }, action);

    if (!result.diagnostics.empty() || !context.changed) {
        return result;
    }
    return transaction.commit(std::move(context.command_label));
}

} // namespace wormedit
