#include <wormedit/core/sample_control_editor.h>
#include "chart_object_ids.h"
#include "position_validation.h"
#include "timing_event_internal.h"
#include <set>

namespace wormedit {
namespace {
template<timing_lane_kind_t Lane>
struct sample_control_edit_t : chart_edit_context_t<sample_control_edit_limits_t> {
    static constexpr auto storage_support = chart_edit_storage_support_t::legacy_only;
    static constexpr auto storage = sample_control_storage_t<sample_control_edit_value_t<Lane>>::member;
    static constexpr std::string_view invalid_limits_message = "Sample-control event cap must be positive.";
    static constexpr std::string_view unknown_chart_message = "Sample-control edit refers to an unknown chart.";
    static auto has_valid_limits(const sample_control_edit_limits_t& limits) -> bool {
        return limits.maximum_events > 0;
    }
    template<class Edit> auto event_caps(const chart_document_t& chart, const Edit&) const {
        return std::array{chart_event_cap_t{(chart.*storage).size(), limits.maximum_events,
            "Sample-control event cap exceeded."}};
    }
    auto validate_value(const chart_document_t& chart, const sample_control_edit_value_t<Lane>& value,
                        object_id_t except = {}) -> bool {
        if (!validate_chart_position(chart, value.position, result.diagnostics, "sample control")) return false;
        if (!sample_control_event_t<Lane>{.value = value.settings.value}.is_valid()) {
            reject(result, diagnostic_code_t::invalid_resolution, "Invalid sample-control value.");
            return false;
        }
        if (std::ranges::any_of(chart.*storage, [&](const auto& event) {
                return event.id != except && event.position == value.position;
            })) {
            reject(result, diagnostic_code_t::collision, "Sample-control lane already has a point on this row.");
            return false;
        }
        return true;
    }
    void operator()(chart_document_t& chart, const insert_edit_t<sample_control_edit_value_t<Lane>>& edit) {
        if (!validate_value(chart, edit.value)) return;
        const auto id = next_chart_object_id(chart, limits.maximum_chart_objects);
        const auto order = next_source_order(chart.*storage);
        if (!id.id || !order || (chart.*storage).size() >= limits.maximum_events) {
            reject(result, diagnostic_code_t::invalid_resolution, "Sample-control insertion exceeds its limits.");
            return;
        }
        (chart.*storage).push_back({.id = *id.id, .position = edit.value.position,
            .value = edit.value.settings.value, .provenance = {.source_order = *order}});
        changed = true;
        command_label = "Insert sample control";
    }
    void operator()(chart_document_t& chart, const update_edit_t<sample_control_edit_value_t<Lane>>& edit) {
        auto* event = chart.unique_target(storage, edit.id, result.diagnostics,
            {.messages = target_query_messages_t::from_event("sample control"),
             .scope = target_uniqueness_scope_t::chart_objects});
        if (!event || !validate_value(chart, edit.value, edit.id)) return;
        changed = event->position != edit.value.position || event->value != edit.value.settings.value;
        event->position = edit.value.position;
        event->value = edit.value.settings.value;
        command_label = "Update sample control";
    }
    void operator()(chart_document_t& chart, const delete_edit_t<sample_control_edit_value_t<Lane>>& edit) {
        if (!chart.unique_target(storage, edit.id, result.diagnostics,
                {.messages = target_query_messages_t::from_event("sample control"),
                 .scope = target_uniqueness_scope_t::chart_objects})) return;
        std::erase_if(chart.*storage, [&](const auto& event) { return event.id == edit.id; });
        changed = true;
        command_label = "Delete sample control";
    }
};

template<class Event>
void validate_control_lane(const std::vector<Event>& events, diagnostic_list_t& diagnostics) {
    std::set<std::pair<measure_id_t, std::uint64_t>> rows;
    for (const auto& event : events) {
        if (!event.is_valid()) diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
            "Invalid sample-control value.", event.id));
        if (!rows.emplace(event.position.measure, event.position.row).second)
            diagnostics.push_back(error(diagnostic_code_t::collision,
                "Sample-control lane has multiple points on the same row.", event.id));
    }
}
}

template<timing_lane_kind_t Lane>
auto apply_sample_control_edit(document_command_transaction_t& transaction,
    const sample_control_edit_action_t<Lane>& action) -> document_command_result_t {
    return transaction.execute<sample_control_edit_t<Lane>>(action, sample_control_edit_limits_t{});
}
template auto apply_sample_control_edit<timing_lane_kind_t::volume>(document_command_transaction_t&,
    const sample_control_edit_action_t<timing_lane_kind_t::volume>&) -> document_command_result_t;
template auto apply_sample_control_edit<timing_lane_kind_t::sample_set>(document_command_transaction_t&,
    const sample_control_edit_action_t<timing_lane_kind_t::sample_set>&) -> document_command_result_t;

auto validate_sample_controls(const chart_document_t& chart) -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    validate_control_lane(chart.volumes, diagnostics);
    validate_control_lane(chart.sample_sets, diagnostics);
    return diagnostics;
}
} // namespace wormedit
