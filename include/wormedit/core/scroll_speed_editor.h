#pragma once

#include <wormedit/core/command.h>

#include <cstddef>
#include <cstdint>
#include <variant>

namespace wormedit {

struct scroll_speed_edit_limits_t {
    std::size_t maximum_scroll_events{100'000};
    std::size_t maximum_speed_events{100'000};
    std::size_t maximum_chart_objects{default_maximum_chart_objects};
};

struct timing_scroll_value_t {
    signed_rational_t multiplier{1, 1};

    [[nodiscard]] static auto with_defaults(chart_edit_value_default_policy_t policy)
        -> timing_scroll_value_t {
        timing_scroll_value_t value;
        if (policy == chart_edit_value_default_policy_t::command)
            value.multiplier = {};
        return value;
    }

    [[nodiscard]] static auto from(const scroll_event_t& event) -> timing_scroll_value_t {
        return {.multiplier = event.multiplier};
    }
    auto operator==(const timing_scroll_value_t&) const -> bool = default;
};

using scroll_edit_value_t = positioned_edit_value_t<timing_scroll_value_t>;

struct timing_speed_value_t {
    signed_rational_t multiplier{1, 1};
    rational_t transition_beats{};
    bool transition_is_seconds{};

    [[nodiscard]] static auto with_defaults(chart_edit_value_default_policy_t policy)
        -> timing_speed_value_t {
        timing_speed_value_t value;
        if (policy == chart_edit_value_default_policy_t::command)
            value.multiplier = {};
        return value;
    }

    [[nodiscard]] static auto from(const speed_event_t& event) -> timing_speed_value_t {
        return {.multiplier = event.multiplier,
                .transition_beats = event.transition_beats,
                .transition_is_seconds = event.transition_is_seconds};
    }
    auto operator==(const timing_speed_value_t&) const -> bool = default;
};

using speed_edit_value_t = positioned_edit_value_t<timing_speed_value_t>;

using scroll_speed_edit_action_t =
    std::variant<wormedit::insert_edit_t<wormedit::scroll_edit_value_t>,
                 wormedit::update_edit_t<wormedit::scroll_edit_value_t>,
                 wormedit::delete_edit_t<wormedit::scroll_edit_value_t>,
                 wormedit::insert_edit_t<wormedit::speed_edit_value_t>,
                 wormedit::update_edit_t<wormedit::speed_edit_value_t>,
                 wormedit::delete_edit_t<wormedit::speed_edit_value_t>>;

// Mutates one scroll or speed event through shared document history. Exact
// positions and rationals are preserved; scroll multipliers may be signed,
// while speed multipliers and transition durations must be nonnegative.
// Same-row events remain legal and timing evaluation resolves them by source
// order and stable insertion/ID ordering. Inserts allocate chart-wide object
// IDs and source orders independently for each event family.
[[nodiscard]] auto apply_scroll_speed_edit(document_history_t& history,
                                           const scroll_speed_edit_action_t& action,
                                           scroll_speed_edit_limits_t limits = {})
    -> document_command_result_t;

// Uses a ready command transaction supplied by a captured edit proposal.
[[nodiscard]] auto apply_scroll_speed_edit(document_command_transaction_t& transaction,
    const scroll_speed_edit_action_t& action, scroll_speed_edit_limits_t limits = {})
    -> document_command_result_t;

} // namespace wormedit
