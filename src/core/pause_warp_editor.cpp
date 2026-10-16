#include "chart_object_ids.h"
#include "position_validation.h"
#include "timing_event_internal.h"
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
    const auto measures = measure_index_t::from(chart);
    if (!measures) {
        result.diagnostics = measures.error();
        return false;
    }
    const auto& index = *measures;
    const auto begin = index.row_of(span.begin);
    const auto end = index.row_of(span.end);
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

constexpr target_query_policy_t pause_target_policy{
    .messages = target_query_messages_t::from_event("pause"),
    .scope = target_uniqueness_scope_t::chart_objects};
constexpr target_query_policy_t warp_target_policy{
    .messages = target_query_messages_t::from_event("warp"),
    .scope = target_uniqueness_scope_t::chart_objects};

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

struct pause_warp_edit_context_t : chart_edit_context_t<pause_warp_edit_limits_t> {
    static constexpr std::string_view invalid_limits_message =
        "pause and warp event caps must be positive";
    static auto has_valid_limits(const pause_warp_edit_limits_t& limits) -> bool {
        return limits.maximum_pause_events != 0 && limits.maximum_warp_events != 0;
    }

    static constexpr std::string_view unknown_chart_message =
        "pause/warp edit refers to an unknown chart";
    template <template <class> class Edit>
    auto event_caps(const chart_document_t& chart, const Edit<pause_edit_value_t>&) const {
        return std::array{chart_event_cap_t{chart.pauses.size(), limits.maximum_pause_events,
                                            "selected chart exceeds the pause event cap"}};
    }
    template <template <class> class Edit>
    auto event_caps(const chart_document_t& chart, const Edit<warp_edit_value_t>&) const {
        return std::array{chart_event_cap_t{chart.warps.size(), limits.maximum_warp_events,
                                            "selected chart exceeds the warp event cap"}};
    }
    void operator()(chart_document_t& chart,
                    const wormedit::insert_edit_t<wormedit::pause_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::update_edit_t<wormedit::pause_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::delete_edit_t<wormedit::pause_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::insert_edit_t<wormedit::warp_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::update_edit_t<wormedit::warp_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::delete_edit_t<wormedit::warp_edit_value_t>& edit);
};

auto allocate_event_id(pause_warp_edit_context_t& context, const chart_document_t& chart)
    -> std::optional<object_id_t> {
    const auto allocation = next_chart_object_id(chart, context.limits.maximum_chart_objects);
    if (allocation.id)
        return allocation.id;
    reject(context.result, diagnostic_code_t::invalid_resolution,
           allocation.over_limit ? "selected chart exceeds the object ID allocation cap"
                                 : "chart object ID sequence is exhausted");
    return std::nullopt;
}

struct pause_update_target_t {
    pause_event_t* event{};
    bms_pause_projection_t* projection{};
};

auto validate_pause_update(pause_warp_edit_context_t& context, chart_document_t& chart,
                           const wormedit::update_edit_t<wormedit::pause_edit_value_t>& edit)
    -> std::optional<pause_update_target_t> {
    if (!edit.id) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "pause update uses the reserved zero event ID");
        return std::nullopt;
    }
    auto* event = chart.unique_target(&chart_document_t::pauses, edit.id,
                                      context.result.diagnostics, pause_target_policy);
    if (!event || !validate_position(chart, edit.value.position, context.result, "pause event") ||
        !validate_pause_values(edit.value.settings.kind, edit.value.settings.duration,
                               edit.value.settings.source_unit, context.result))
        return std::nullopt;
    auto* projection = matching_pause_projection(chart, *event, context.result);
    if (!context.result.diagnostics.empty())
        return std::nullopt;
    if (event->provenance.family == format_family_t::bms && !projection) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "source-backed BMS pause has no matching import projection");
        return std::nullopt;
    }
    if (projection && (projection->imported_kind != edit.value.settings.kind ||
                       projection->imported_source_unit != edit.value.settings.source_unit)) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "pause update would make its BMS source projection incoherent");
        return std::nullopt;
    }
    return pause_update_target_t{event, projection};
}

void pause_warp_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::insert_edit_t<wormedit::pause_edit_value_t>& edit) {
    auto& context = *this;
    if (chart.pauses.size() >= context.limits.maximum_pause_events) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "pause insertion would exceed the configured event cap");
        return;
    }
    if (!validate_position(chart, edit.value.position, context.result, "pause event") ||
        !validate_pause_values(edit.value.settings.kind, edit.value.settings.duration,
                               edit.value.settings.source_unit, context.result))
        return;
    const auto id = allocate_event_id(context, chart);
    if (!id)
        return;
    const auto source_order = next_source_order(chart.pauses);
    if (!source_order) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "pause source-order sequence is exhausted");
        return;
    }
    chart.pauses.push_back({.id = *id,
                            .position = edit.value.position,
                            .kind = edit.value.settings.kind,
                            .duration = edit.value.settings.duration,
                            .source_unit = edit.value.settings.source_unit,
                            .provenance = {.source_order = *source_order}});
    context.command_label = "Insert pause event";
    context.changed = true;
}
void pause_warp_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::update_edit_t<wormedit::pause_edit_value_t>& edit) {
    auto& context = *this;
    const auto target = validate_pause_update(context, chart, edit);
    if (!target)
        return;
    auto& event = *target->event;
    context.changed = event.position != edit.value.position ||
                      event.kind != edit.value.settings.kind ||
                      event.duration != edit.value.settings.duration ||
                      event.source_unit != edit.value.settings.source_unit;
    if (!context.changed)
        return;
    event.position = edit.value.position;
    event.kind = edit.value.settings.kind;
    event.duration = edit.value.settings.duration;
    event.source_unit = edit.value.settings.source_unit;
    context.command_label = "Update pause event";
}
void pause_warp_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::delete_edit_t<wormedit::pause_edit_value_t>& edit) {
    auto& context = *this;
    if (!edit.id) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "pause deletion uses the reserved zero event ID");
        return;
    }
    auto* event = chart.unique_target(&chart_document_t::pauses, edit.id,
                                      context.result.diagnostics, pause_target_policy);
    if (!event)
        return;
    const auto id = event->id;
    std::erase_if(chart.bms_pause_projections,
                  [id](const auto& projection) { return projection.event == id; });
    std::erase_if(chart.pauses, [id](const auto& item) { return item.id == id; });
    context.command_label = "Delete pause event";
    context.changed = true;
}
void pause_warp_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::insert_edit_t<wormedit::warp_edit_value_t>& edit) {
    auto& context = *this;
    if (chart.warps.size() >= context.limits.maximum_warp_events) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "warp insertion would exceed the configured event cap");
        return;
    }
    if (!validate_span(chart, edit.value.span(), context.result))
        return;
    if (!is_valid_warp_encoding(edit.value.settings.source_encoding)) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "warp event has an unknown source encoding");
        return;
    }
    const auto id = allocate_event_id(context, chart);
    if (!id)
        return;
    const auto source_order = next_source_order(chart.warps);
    if (!source_order) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "warp source-order sequence is exhausted");
        return;
    }
    chart.warps.push_back({.id = *id,
                           .span = edit.value.span(),
                           .source_encoding = edit.value.settings.source_encoding,
                           .provenance = {.source_order = *source_order}});
    context.command_label = "Insert warp event";
    context.changed = true;
}
void pause_warp_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::update_edit_t<wormedit::warp_edit_value_t>& edit) {
    auto& context = *this;
    if (!edit.id) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "warp update uses the reserved zero event ID");
        return;
    }
    auto* event = chart.unique_target(&chart_document_t::warps, edit.id, context.result.diagnostics,
                                      warp_target_policy);
    if (!event || !validate_span(chart, edit.value.span(), context.result))
        return;
    if (edit.value.source_encoding() && !is_valid_warp_encoding(*edit.value.source_encoding())) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "warp event has an unknown source encoding");
        return;
    }
    const auto encoding = edit.value.source_encoding().value_or(event->source_encoding);
    context.changed = event->span.begin != edit.value.span().begin ||
                      event->span.end != edit.value.span().end ||
                      event->source_encoding != encoding;
    if (!context.changed)
        return;
    event->span = edit.value.span();
    event->source_encoding = encoding;
    context.command_label = "Update warp event";
}
void pause_warp_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::delete_edit_t<wormedit::warp_edit_value_t>& edit) {
    auto& context = *this;
    if (!edit.id) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "warp deletion uses the reserved zero event ID");
        return;
    }
    if (!chart.unique_target(&chart_document_t::warps, edit.id, context.result.diagnostics,
                             warp_target_policy))
        return;
    std::erase_if(chart.warps, [&](const auto& item) { return item.id == edit.id; });
    context.command_label = "Delete warp event";
    context.changed = true;
}

} // namespace

auto apply_pause_warp_edit(document_history_t& history, const pause_warp_edit_action_t& action,
                           pause_warp_edit_limits_t limits) -> document_command_result_t {
    return document_command_transaction_t(history).execute<pause_warp_edit_context_t>(action,
                                                                                            limits);
}

auto apply_pause_warp_edit(document_command_transaction_t& transaction,
                           const pause_warp_edit_action_t& action, pause_warp_edit_limits_t limits)
    -> document_command_result_t {
    return transaction.execute<pause_warp_edit_context_t>(action, limits);
}

} // namespace wormedit
