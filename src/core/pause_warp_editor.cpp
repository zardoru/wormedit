#include "timing_event_internal.h"
#include "position_validation.h"
#include "chart_object_ids.h"
#include <wormedit/core/pause_warp_editor.h>

#include <algorithm>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace wormedit {
namespace {

auto is_valid_pause_kind(pause_kind_t kind) -> bool {
    switch (kind) {
    case pause_kind_t::stop:
    case pause_kind_t::delay:
        return true;
    }
    return false;
}



auto validate_pause_values(pause_kind_t kind, signed_rational_t duration, pause_unit_t unit,
                           document_command_result_t& result) -> bool {
    if (!is_valid_pause_kind(kind)) {
        reject(result, diagnostic_code_t::invalid_resolution, "pause event has an unknown kind");
        return false;
    }
    if (!is_valid_pause_unit(unit) ||
        (unit == pause_unit_t::bms_stop_ticks && kind != pause_kind_t::stop)) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "BMS STOP ticks are valid only for stop events; pause kind and unit must agree");
        return false;
    }
    if (duration.denominator == 0 || duration.numerator < 0) {
        reject(result, diagnostic_code_t::invalid_rational,
               "pause duration must be a nonnegative exact rational");
        return false;
    }
    return true;
}



auto validate_position(const chart_document_t& chart, local_position_t position,
                       document_command_result_t& result, std::string_view event_name) -> bool {
    return validate_chart_position(chart, position, result.diagnostics, event_name);
}

auto validate_span(const chart_document_t& chart, position_span_t span,
                   document_command_result_t& result) -> bool {
    const auto measures = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(measures)) {
        result.diagnostics = std::get<diagnostic_list_t>(measures);
        return false;
    }
    const auto& index = std::get<measure_index_t>(measures);
    const auto begin = absolute_row(index, span.begin);
    const auto end = absolute_row(index, span.end);
    if (!begin.has_value() || !end.has_value()) {
        reject(result, diagnostic_code_t::row_out_of_range,
               "warp span is outside the selected chart's measure geometry");
        return false;
    }
    if (*end <= *begin) {
        reject(result, diagnostic_code_t::invalid_hold,
               "warp end must be strictly after its start");
        return false;
    }
    return true;
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

auto matching_pause_projection(chart_document_t& chart, const pause_event_t& event,
                               document_command_result_t& result) -> bms_pause_projection_t* {
    const auto found =
        std::ranges::find(chart.bms_pause_projections, event.id, &bms_pause_projection_t::event);
    if (found == chart.bms_pause_projections.end()) {
        return nullptr;
    }
    if (std::ranges::count(chart.bms_pause_projections, event.id, &bms_pause_projection_t::event) !=
        1) {
        reject(result, diagnostic_code_t::duplicate_id,
               "BMS pause projection is ambiguous because it appears more than once");
        return nullptr;
    }
    if (found->source_order != event.provenance.source_order) {
        reject(result, diagnostic_code_t::invalid_id,
               "BMS pause projection source identity does not match its event");
        return nullptr;
    }
    return &*found;
}

struct pause_warp_edit_context_t {
    song_document_t& proposed;
    document_command_result_t& result;
    pause_warp_edit_limits_t limits;
    std::string command_label;
    bool changed{};

    template <typename Edit> void operator()(const Edit& edit);

};

auto target_chart(pause_warp_edit_context_t& context, chart_id_t id) -> chart_document_t* {
    const auto found = context.proposed.find_chart(id);
    if (!id || found == nullptr) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "pause/warp edit refers to an unknown chart");
        return nullptr;
    }
    return &*found;
}

auto pause_event_cap(pause_warp_edit_context_t& context, const chart_document_t& chart)
    -> bool {
    if (chart.pauses.size() <= context.limits.maximum_pause_events) return true;
    reject(context.result, diagnostic_code_t::invalid_resolution,
           "selected chart exceeds the pause event cap");
    return false;
}

auto warp_event_cap(pause_warp_edit_context_t& context, const chart_document_t& chart)
    -> bool {
    if (chart.warps.size() <= context.limits.maximum_warp_events) return true;
    reject(context.result, diagnostic_code_t::invalid_resolution,
           "selected chart exceeds the warp event cap");
    return false;
}

auto allocate_event_id(pause_warp_edit_context_t& context, const chart_document_t& chart)
    -> std::optional<object_id_t> {
    const auto allocation = next_chart_object_id(chart, context.limits.maximum_chart_objects);
    if (allocation.id) return allocation.id;
    reject(context.result, diagnostic_code_t::invalid_resolution,
           allocation.over_limit ? "selected chart exceeds the object ID allocation cap"
                                 : "chart object ID sequence is exhausted");
    return std::nullopt;
}

auto insert_pause(pause_warp_edit_context_t& context, chart_document_t& chart,
                  const insert_pause_event_action_t& edit) -> void {
    if (!pause_event_cap(context, chart)) return;
    if (chart.pauses.size() >= context.limits.maximum_pause_events) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "pause insertion would exceed the configured event cap");
        return;
    }
    if (!validate_position(chart, edit.position, context.result, "pause event") ||
        !validate_pause_values(edit.kind, edit.duration, edit.source_unit, context.result)) return;
    const auto id = allocate_event_id(context, chart);
    if (!id) return;
    const auto source_order = next_source_order(chart.pauses);
    if (!source_order) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "pause source-order sequence is exhausted");
        return;
    }
    chart.pauses.push_back({.id = *id, .position = edit.position, .kind = edit.kind,
        .duration = edit.duration, .source_unit = edit.source_unit,
        .provenance = {.source_order = *source_order}});
    context.command_label = "Insert pause event";
    context.changed = true;
}

struct pause_update_target_t {
    pause_event_t* event{};
    bms_pause_projection_t* projection{};
};

auto validate_pause_update(pause_warp_edit_context_t& context, chart_document_t& chart,
                           const update_pause_event_action_t& edit)
    -> std::optional<pause_update_target_t> {
    if (!edit.id) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "pause update uses the reserved zero event ID");
        return std::nullopt;
    }
    auto* event = unique_event(chart, chart.pauses, edit.id, "pause", context.result);
    if (!event || !validate_position(chart, edit.position, context.result, "pause event") ||
        !validate_pause_values(edit.kind, edit.duration, edit.source_unit, context.result))
        return std::nullopt;
    auto* projection = matching_pause_projection(chart, *event, context.result);
    if (!context.result.diagnostics.empty()) return std::nullopt;
    if (event->provenance.family == format_family_t::bms && !projection) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "source-backed BMS pause has no matching import projection");
        return std::nullopt;
    }
    if (projection && (projection->imported_kind != edit.kind ||
                       projection->imported_source_unit != edit.source_unit)) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "pause update would make its BMS source projection incoherent");
        return std::nullopt;
    }
    return pause_update_target_t{event, projection};
}

auto update_pause(pause_warp_edit_context_t& context, chart_document_t& chart,
                  const update_pause_event_action_t& edit) -> void {
    if (!pause_event_cap(context, chart)) return;
    const auto target = validate_pause_update(context, chart, edit);
    if (!target) return;
    auto& event = *target->event;
    context.changed = event.position != edit.position || event.kind != edit.kind ||
                      event.duration != edit.duration || event.source_unit != edit.source_unit;
    if (!context.changed) return;
    event.position = edit.position;
    event.kind = edit.kind;
    event.duration = edit.duration;
    event.source_unit = edit.source_unit;
    context.command_label = "Update pause event";
}

auto delete_pause(pause_warp_edit_context_t& context, chart_document_t& chart,
                  const delete_pause_event_action_t& edit) -> void {
    if (!pause_event_cap(context, chart)) return;
    if (!edit.id) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "pause deletion uses the reserved zero event ID");
        return;
    }
    auto* event = unique_event(chart, chart.pauses, edit.id, "pause", context.result);
    if (!event) return;
    const auto id = event->id;
    std::erase_if(chart.bms_pause_projections,
                  [id](const auto& projection) { return projection.event == id; });
    std::erase_if(chart.pauses, [id](const auto& item) { return item.id == id; });
    context.command_label = "Delete pause event";
    context.changed = true;
}

auto insert_warp(pause_warp_edit_context_t& context, chart_document_t& chart,
                 const insert_warp_event_action_t& edit) -> void {
    if (!warp_event_cap(context, chart)) return;
    if (chart.warps.size() >= context.limits.maximum_warp_events) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "warp insertion would exceed the configured event cap");
        return;
    }
    if (!validate_span(chart, edit.span, context.result)) return;
    if (!is_valid_warp_encoding(edit.source_encoding)) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "warp event has an unknown source encoding");
        return;
    }
    const auto id = allocate_event_id(context, chart);
    if (!id) return;
    const auto source_order = next_source_order(chart.warps);
    if (!source_order) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "warp source-order sequence is exhausted");
        return;
    }
    chart.warps.push_back({.id = *id, .span = edit.span,
        .source_encoding = edit.source_encoding,
        .provenance = {.source_order = *source_order}});
    context.command_label = "Insert warp event";
    context.changed = true;
}

auto update_warp(pause_warp_edit_context_t& context, chart_document_t& chart,
                 const update_warp_event_action_t& edit) -> void {
    if (!warp_event_cap(context, chart)) return;
    if (!edit.id) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "warp update uses the reserved zero event ID");
        return;
    }
    auto* event = unique_event(chart, chart.warps, edit.id, "warp", context.result);
    if (!event || !validate_span(chart, edit.span, context.result)) return;
    if (edit.source_encoding && !is_valid_warp_encoding(*edit.source_encoding)) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "warp event has an unknown source encoding");
        return;
    }
    const auto encoding = edit.source_encoding.value_or(event->source_encoding);
    context.changed = event->span.begin != edit.span.begin || event->span.end != edit.span.end ||
                      event->source_encoding != encoding;
    if (!context.changed) return;
    event->span = edit.span;
    event->source_encoding = encoding;
    context.command_label = "Update warp event";
}

auto delete_warp(pause_warp_edit_context_t& context, chart_document_t& chart,
                 const delete_warp_event_action_t& edit) -> void {
    if (!warp_event_cap(context, chart)) return;
    if (!edit.id) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "warp deletion uses the reserved zero event ID");
        return;
    }
    if (!unique_event(chart, chart.warps, edit.id, "warp", context.result))
        return;
    std::erase_if(chart.warps, [&](const auto& item) { return item.id == edit.id; });
    context.command_label = "Delete warp event";
    context.changed = true;
}

auto apply_pause_warp_action(pause_warp_edit_context_t& context, chart_document_t& chart,
                             const insert_pause_event_action_t& edit) -> void {
    insert_pause(context, chart, edit);
}
auto apply_pause_warp_action(pause_warp_edit_context_t& context, chart_document_t& chart,
                             const update_pause_event_action_t& edit) -> void {
    update_pause(context, chart, edit);
}
auto apply_pause_warp_action(pause_warp_edit_context_t& context, chart_document_t& chart,
                             const delete_pause_event_action_t& edit) -> void {
    delete_pause(context, chart, edit);
}
auto apply_pause_warp_action(pause_warp_edit_context_t& context, chart_document_t& chart,
                             const insert_warp_event_action_t& edit) -> void {
    insert_warp(context, chart, edit);
}
auto apply_pause_warp_action(pause_warp_edit_context_t& context, chart_document_t& chart,
                             const update_warp_event_action_t& edit) -> void {
    update_warp(context, chart, edit);
}
auto apply_pause_warp_action(pause_warp_edit_context_t& context, chart_document_t& chart,
                             const delete_warp_event_action_t& edit) -> void {
    delete_warp(context, chart, edit);
}

template <typename Edit>
auto apply_pause_warp_operation(pause_warp_edit_context_t& context, const Edit& edit) -> void {
    auto* chart = target_chart(context, edit.chart);
    if (chart) apply_pause_warp_action(context, *chart, edit);
}

template <typename Edit>
void pause_warp_edit_context_t::operator()(const Edit& edit) {
    apply_pause_warp_operation(*this, edit);
}


} // namespace

auto apply_pause_warp_edit(document_history_t& history,
    const pause_warp_edit_action_t& action, pause_warp_edit_limits_t limits) -> document_command_result_t {
    auto transaction = document_command_transaction_t::from(history);
    return apply_pause_warp_edit(transaction, action, limits);
}

auto apply_pause_warp_edit(document_command_transaction_t& transaction,
                           const pause_warp_edit_action_t& action, pause_warp_edit_limits_t limits)
    -> document_command_result_t {
    if (!transaction.is_ready()) return transaction.commit({});
    auto& result = transaction.outcome;
    if (limits.maximum_pause_events == 0 || limits.maximum_warp_events == 0) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "pause and warp event caps must be positive");
        return transaction.finish_without_commit();
    }

    auto& proposed = transaction.copy_document();
    pause_warp_edit_context_t context{.proposed = proposed, .result = result,
        .limits = limits, .command_label = {}, .changed = false};
    std::visit(context, action);

    if (!result.diagnostics.empty() || !context.changed) {
        return transaction.finish_without_commit();
    }
    return transaction.commit(std::move(context.command_label));
}

} // namespace wormedit
