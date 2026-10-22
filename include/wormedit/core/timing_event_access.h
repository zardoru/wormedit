#pragma once

#include <wormedit/core/timing_lane_controller.h>

#include <algorithm>
#include <concepts>
#include <type_traits>

namespace wormedit {

// Group occupancy historically classifies every non-stop pause as delay. Lookup and
// insertion require an exact pause kind, even when inspecting malformed input.
enum class timing_pause_match_policy_t { exact_kind, stop_or_other };

template<auto Storage, typename Value, typename Edit, timing_lane_kind_t Lane>
struct timing_event_storage_t {
    using value_type = Value;
    using edit_type = Edit;

    static auto events(const chart_document_t& chart) -> const auto& { return chart.*Storage; }
    template<typename Event>
    static auto position(const Event& event) -> local_position_t {
        if constexpr (std::same_as<Event, warp_event_t>) return event.span.begin;
        else return event.position;
    }
    template<typename Event>
    static auto kind(const Event& event) -> timing_lane_kind_t {
        if constexpr (std::same_as<Event, pause_event_t>)
            return event.kind == pause_kind_t::stop ? timing_lane_kind_t::stop : timing_lane_kind_t::delay;
        else return Lane;
    }
    template<typename Event>
    static auto is_kind(const Event& event, timing_lane_kind_t requested,
                        timing_pause_match_policy_t policy) -> bool {
        if constexpr (std::same_as<Event, pause_event_t>) {
            if (policy == timing_pause_match_policy_t::stop_or_other) return kind(event) == requested;
            const auto pause_kind = requested == timing_lane_kind_t::stop ? pause_kind_t::stop : pause_kind_t::delay;
            return event.kind == pause_kind;
        } else return true;
    }
    static auto find(const chart_document_t& chart, timing_lane_kind_t requested, object_id_t id) {
        const auto& stored = events(chart);
        // Keep ID lookup before kind checking: duplicate IDs in malformed input do
        // not allow a later pause with a different kind to become the target.
        using event_type = typename std::remove_cvref_t<decltype(stored)>::value_type;
        const auto found = std::ranges::find(stored, id, &event_type::id);
        return found != stored.end() && is_kind(*found, requested, timing_pause_match_policy_t::exact_kind)
            ? &*found : nullptr;
    }
};

template<typename Event> struct timing_event_access_t;

template<> struct timing_event_access_t<bpm_event_t>
    : timing_event_storage_t<&chart_document_t::bpms, timing_bpm_value_t, bpm_edit_value_t, timing_lane_kind_t::bpm> {
    template<typename Action>
    static auto apply(document_command_transaction_t& transaction, const Action& action) -> document_command_result_t {
        return apply_bpm_edit(transaction, action);
    }
    template<typename Action>
    static auto apply(document_history_t& history, const Action& action) -> document_command_result_t {
        return apply_bpm_edit(history, action);
    }
    template<typename Action>
    static auto stage(document_command_transaction_t& transaction, const Action& action) -> attempt_t<document_edit_effect_t> {
        return stage_bpm_edit(transaction, action);
    }
};

template<> struct timing_event_access_t<scroll_event_t>
    : timing_event_storage_t<&chart_document_t::scrolls, timing_scroll_value_t, scroll_edit_value_t, timing_lane_kind_t::scroll> {
    template<typename Action>
    static auto apply(document_command_transaction_t& transaction, const Action& action) -> document_command_result_t {
        return apply_scroll_speed_edit(transaction, action);
    }
    template<typename Action>
    static auto apply(document_history_t& history, const Action& action) -> document_command_result_t {
        return apply_scroll_speed_edit(history, action);
    }
};

template<> struct timing_event_access_t<speed_event_t>
    : timing_event_storage_t<&chart_document_t::speeds, timing_speed_value_t, speed_edit_value_t, timing_lane_kind_t::speed> {
    template<typename Action>
    static auto apply(document_command_transaction_t& transaction, const Action& action) -> document_command_result_t {
        return apply_scroll_speed_edit(transaction, action);
    }
    template<typename Action>
    static auto apply(document_history_t& history, const Action& action) -> document_command_result_t {
        return apply_scroll_speed_edit(history, action);
    }
};

template<> struct timing_event_access_t<pause_event_t>
    : timing_event_storage_t<&chart_document_t::pauses, timing_pause_value_t, pause_edit_value_t, timing_lane_kind_t::stop> {
    template<typename Action>
    static auto apply(document_command_transaction_t& transaction, const Action& action) -> document_command_result_t {
        return apply_pause_warp_edit(transaction, action);
    }
    template<typename Action>
    static auto apply(document_history_t& history, const Action& action) -> document_command_result_t {
        return apply_pause_warp_edit(history, action);
    }
    template<typename Action>
    static auto stage(document_command_transaction_t& transaction, const Action& action) -> attempt_t<document_edit_effect_t> {
        return stage_pause_warp_edit(transaction, action);
    }
};

template<> struct timing_event_access_t<warp_event_t>
    : timing_event_storage_t<&chart_document_t::warps, timing_warp_value_t, warp_edit_value_t, timing_lane_kind_t::warp> {
    template<typename Action>
    static auto apply(document_command_transaction_t& transaction, const Action& action) -> document_command_result_t {
        return apply_pause_warp_edit(transaction, action);
    }
    template<typename Action>
    static auto apply(document_history_t& history, const Action& action) -> document_command_result_t {
        return apply_pause_warp_edit(history, action);
    }
    template<typename Action>
    static auto stage(document_command_transaction_t& transaction, const Action& action) -> attempt_t<document_edit_effect_t> {
        return stage_pause_warp_edit(transaction, action);
    }
};

template<> struct timing_event_access_t<kiai_event_t>
    : timing_event_storage_t<&chart_document_t::kiai, timing_kiai_value_t, kiai_edit_value_t, timing_lane_kind_t::kiai> {
    template<typename Action>
    static auto apply(document_command_transaction_t& transaction, const Action& action) -> document_command_result_t {
        return apply_marker_edit(transaction, action);
    }
    template<typename Action>
    static auto apply(document_history_t& history, const Action& action) -> document_command_result_t {
        return apply_marker_edit(history, action);
    }
};

template<> struct timing_event_access_t<timing_anchor_t>
    : timing_event_storage_t<&chart_document_t::timing_anchors, timing_anchor_value_t, timing_anchor_edit_value_t, timing_lane_kind_t::anchor> {
    template<typename Action>
    static auto apply(document_command_transaction_t& transaction, const Action& action) -> document_command_result_t {
        return apply_timing_anchor_edit(transaction, action);
    }
    template<typename Action>
    static auto apply(document_history_t& history, const Action& action) -> document_command_result_t {
        return apply_timing_anchor_edit(history, action);
    }
};

// One exhaustive lane dispatch. The caller supplies its own invalid-kind result;
// lookup/value checks fail closed, while occupancy conservatively reports occupied.
template<typename Operation, typename Result>
auto visit_timing_events(timing_lane_kind_t kind, Operation operation, Result invalid) -> Result {
    switch (kind) {
    case timing_lane_kind_t::bpm:
        return operation(timing_event_access_t<bpm_event_t>{});
    case timing_lane_kind_t::scroll:
        return operation(timing_event_access_t<scroll_event_t>{});
    case timing_lane_kind_t::speed:
        return operation(timing_event_access_t<speed_event_t>{});
    case timing_lane_kind_t::stop:
    case timing_lane_kind_t::delay:
        return operation(timing_event_access_t<pause_event_t>{});
    case timing_lane_kind_t::warp:
        return operation(timing_event_access_t<warp_event_t>{});
    case timing_lane_kind_t::kiai:
        return operation(timing_event_access_t<kiai_event_t>{});
    case timing_lane_kind_t::anchor:
        return operation(timing_event_access_t<timing_anchor_t>{});
    }
    return invalid;
}

} // namespace wormedit
