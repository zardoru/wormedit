#pragma once

#include <wormedit/core/command.h>

namespace wormedit {

template<timing_lane_kind_t Lane> struct timing_sample_control_value_t {
    std::int64_t value{sample_control_event_t<Lane>{}.value};
    [[nodiscard]] static auto from(const sample_control_event_t<Lane>& event)
        -> timing_sample_control_value_t { return {.value = event.value}; }
    [[nodiscard]] static auto with_defaults(chart_edit_value_default_policy_t)
        -> timing_sample_control_value_t { return {}; }
    [[nodiscard]] auto is_valid() const -> bool { return sample_control_event_t<Lane>{.value = value}.is_valid(); }
    auto operator==(const timing_sample_control_value_t&) const -> bool = default;
};
template<timing_lane_kind_t Lane>
using sample_control_edit_value_t = positioned_edit_value_t<timing_sample_control_value_t<Lane>>;

// These chart-local controls have no scoped BMS semantic adapter.
template<class Value> struct sample_control_storage_t;

template<> struct sample_control_storage_t<sample_control_edit_value_t<timing_lane_kind_t::volume>> {
    static constexpr auto member = &chart_document_t::volumes;
};
template<> struct sample_control_storage_t<sample_control_edit_value_t<timing_lane_kind_t::sample_set>> {
    static constexpr auto member = &chart_document_t::sample_sets;
};

template<timing_lane_kind_t Lane>
using sample_control_edit_action_t = chart_edit_t<sample_control_edit_value_t<Lane>>;

struct sample_control_edit_limits_t {
    std::size_t maximum_events{100'000};
    std::size_t maximum_chart_objects{default_maximum_chart_objects};
};

template<timing_lane_kind_t Lane>
[[nodiscard]] auto apply_sample_control_edit(document_command_transaction_t&,
    const sample_control_edit_action_t<Lane>&) -> document_command_result_t;

// Shared by document validation and all edits, including clipboard publication.
[[nodiscard]] auto validate_sample_controls(const chart_document_t&) -> diagnostic_list_t;

} // namespace wormedit
