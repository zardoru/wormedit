#include "chart_object_ids.h"
#include "position_validation.h"
#include "timing_event_internal.h"
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

constexpr target_query_policy_t scroll_target_policy{
    .messages = target_query_messages_t::from_event("scroll"),
    .scope = target_uniqueness_scope_t::chart_objects};
constexpr target_query_policy_t speed_target_policy{
    .messages = target_query_messages_t::from_event("speed"),
    .scope = target_uniqueness_scope_t::chart_objects};

struct scroll_speed_edit_context_t : chart_edit_context_t<scroll_speed_edit_limits_t> {
    static constexpr auto storage_support = chart_edit_storage_support_t::objects;
    static constexpr std::string_view invalid_limits_message =
        "scroll and speed event caps must be positive";
    static auto has_valid_limits(const scroll_speed_edit_limits_t& limits) -> bool {
        return limits.maximum_scroll_events != 0 && limits.maximum_speed_events != 0;
    }

    static constexpr std::string_view unknown_chart_message =
        "scroll/speed edit refers to an unknown chart";
    template <template <class> class Edit>
    auto event_caps(const chart_document_t& chart, const Edit<scroll_edit_value_t>&) const {
        return std::array{chart_event_cap_t{chart.scrolls.size(), limits.maximum_scroll_events,
                                            "selected chart exceeds the scroll event cap"}};
    }
    template <template <class> class Edit>
    auto event_caps(const chart_document_t& chart, const Edit<speed_edit_value_t>&) const {
        return std::array{chart_event_cap_t{chart.speeds.size(), limits.maximum_speed_events,
                                            "selected chart exceeds the speed event cap"}};
    }
    void operator()(chart_document_t& chart,
                    const wormedit::insert_edit_t<wormedit::scroll_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::update_edit_t<wormedit::scroll_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::delete_edit_t<wormedit::scroll_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::insert_edit_t<wormedit::speed_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::update_edit_t<wormedit::speed_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::delete_edit_t<wormedit::speed_edit_value_t>& edit);
};



auto allocate_event_id(scroll_speed_edit_context_t& context, const chart_document_t& chart)
    -> std::optional<object_id_t> {
    const auto allocation = next_chart_object_id(chart, context.limits.maximum_chart_objects);
    if (allocation.id)
        return allocation.id;
    reject(context.result, diagnostic_code_t::invalid_resolution,
           allocation.over_limit ? "selected chart exceeds the object ID allocation cap"
                                 : "chart object ID sequence is exhausted");
    return std::nullopt;
}

void scroll_speed_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::insert_edit_t<wormedit::scroll_edit_value_t>& edit) {
    auto& context = *this;
    if (chart.scrolls.size() >= limits.maximum_scroll_events) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "scroll insertion would exceed the configured event cap");
        return;
    }
    const auto valid = validate_scroll_value(edit.value.settings.multiplier, context.result);
    if (!validate_position(chart, edit.value.position, context.result) || !valid)
        return;
    const auto id = allocate_event_id(context, chart);
    if (!id)
        return;
    const auto order = next_source_order(chart.scrolls);
    if (!order) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "scroll source-order sequence is exhausted");
        return;
    }
    chart.scrolls.push_back({.id = *id,
                             .position = edit.value.position,
                             .multiplier = edit.value.settings.multiplier,
                             .provenance = {.source_order = *order}});
    context.command_label = "Insert scroll event";
    context.changed = true;
}
void scroll_speed_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::update_edit_t<wormedit::scroll_edit_value_t>& edit) {
    auto& context = *this;
    if (!edit.id) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "scroll update uses the reserved zero event ID");
        return;
    }
    auto* event = chart.unique_target(&chart_document_t::scrolls, edit.id,
                                      context.result.diagnostics, scroll_target_policy);
    const auto valid = validate_scroll_value(edit.value.settings.multiplier, context.result);
    if (!event || !validate_position(chart, edit.value.position, context.result) || !valid)
        return;
    context.changed = event->position != edit.value.position ||
                      event->multiplier != edit.value.settings.multiplier;
    if (!context.changed)
        return;
    event->position = edit.value.position;
    event->multiplier = edit.value.settings.multiplier;
    context.command_label = "Update scroll event";
}
void scroll_speed_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::delete_edit_t<wormedit::scroll_edit_value_t>& edit) {
    auto& context = *this;
    if (!edit.id) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "scroll deletion uses the reserved zero event ID");
        return;
    }
    if (!chart.unique_target(&chart_document_t::scrolls, edit.id, context.result.diagnostics,
                             scroll_target_policy))
        return;
    std::erase_if(chart.scrolls, [&](const auto& item) { return item.id == edit.id; });
    context.command_label = "Delete scroll event";
    context.changed = true;
}
void scroll_speed_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::insert_edit_t<wormedit::speed_edit_value_t>& edit) {
    auto& context = *this;
    if (chart.speeds.size() >= limits.maximum_speed_events) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "speed insertion would exceed the configured event cap");
        return;
    }
    const auto valid = validate_speed_values(edit.value.settings.multiplier,
                                             edit.value.settings.transition_beats, context.result);
    if (!validate_position(chart, edit.value.position, context.result) || !valid)
        return;
    const auto id = allocate_event_id(context, chart);
    if (!id)
        return;
    const auto order = next_source_order(chart.speeds);
    if (!order) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "speed source-order sequence is exhausted");
        return;
    }
    chart.speeds.push_back({.id = *id,
                            .position = edit.value.position,
                            .multiplier = edit.value.settings.multiplier,
                            .transition_beats = edit.value.settings.transition_beats,
                            .transition_is_seconds = edit.value.settings.transition_is_seconds,
                            .provenance = {.source_order = *order}});
    context.command_label = "Insert speed event";
    context.changed = true;
}
void scroll_speed_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::update_edit_t<wormedit::speed_edit_value_t>& edit) {
    auto& context = *this;
    if (!edit.id) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "speed update uses the reserved zero event ID");
        return;
    }
    auto* event = chart.unique_target(&chart_document_t::speeds, edit.id,
                                      context.result.diagnostics, speed_target_policy);
    const auto valid = validate_speed_values(edit.value.settings.multiplier,
                                             edit.value.settings.transition_beats, context.result);
    if (!event || !validate_position(chart, edit.value.position, context.result) || !valid)
        return;
    context.changed = event->position != edit.value.position ||
                      event->multiplier != edit.value.settings.multiplier ||
                      event->transition_beats != edit.value.settings.transition_beats ||
                      event->transition_is_seconds != edit.value.settings.transition_is_seconds;
    if (!context.changed)
        return;
    event->position = edit.value.position;
    event->multiplier = edit.value.settings.multiplier;
    event->transition_beats = edit.value.settings.transition_beats;
    event->transition_is_seconds = edit.value.settings.transition_is_seconds;
    context.command_label = "Update speed event";
}
void scroll_speed_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::delete_edit_t<wormedit::speed_edit_value_t>& edit) {
    auto& context = *this;
    if (!edit.id) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "speed deletion uses the reserved zero event ID");
        return;
    }
    if (!chart.unique_target(&chart_document_t::speeds, edit.id, context.result.diagnostics,
                             speed_target_policy))
        return;
    std::erase_if(chart.speeds, [&](const auto& item) { return item.id == edit.id; });
    context.command_label = "Delete speed event";
    context.changed = true;
}

} // namespace

auto apply_scroll_speed_edit(document_history_t& history, const scroll_speed_edit_action_t& action,
                             scroll_speed_edit_limits_t limits) -> document_command_result_t {
    return document_command_transaction_t(history).execute<scroll_speed_edit_context_t>(
        action, limits);
}

auto apply_scroll_speed_edit(document_command_transaction_t& transaction,
                             const scroll_speed_edit_action_t& action,
                             scroll_speed_edit_limits_t limits) -> document_command_result_t {
    return transaction.execute<scroll_speed_edit_context_t>(action, limits);
}

} // namespace wormedit
