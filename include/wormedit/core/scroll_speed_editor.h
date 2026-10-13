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

struct insert_scroll_event_action_t {
    chart_id_t chart;
    local_position_t position;
    signed_rational_t multiplier;
};

struct update_scroll_event_action_t {
    chart_id_t chart;
    object_id_t event;
    local_position_t position;
    signed_rational_t multiplier;
};

struct delete_scroll_event_action_t {
    chart_id_t chart;
    object_id_t event;
};

struct insert_speed_event_action_t {
    chart_id_t chart;
    local_position_t position;
    signed_rational_t multiplier;
    rational_t transition_beats{};
    bool transition_is_seconds{};
};

struct update_speed_event_action_t {
    chart_id_t chart;
    object_id_t event;
    local_position_t position;
    signed_rational_t multiplier;
    rational_t transition_beats{};
    bool transition_is_seconds{};
};

struct delete_speed_event_action_t {
    chart_id_t chart;
    object_id_t event;
};

using scroll_speed_edit_action_t =
    std::variant<insert_scroll_event_action_t, update_scroll_event_action_t,
                 delete_scroll_event_action_t, insert_speed_event_action_t,
                 update_speed_event_action_t, delete_speed_event_action_t>;

// Mutates one scroll or speed event through shared document history. Exact
// positions and rationals are preserved; scroll multipliers may be signed,
// while speed multipliers and transition durations must be nonnegative.
// Same-row events remain legal and timing evaluation resolves them by source
// order and stable insertion/ID ordering. Inserts allocate chart-wide object
// IDs and source orders independently for each event family.
[[nodiscard]] auto apply_scroll_speed_edit(document_history_t& history,
                                           std::uint64_t expected_revision,
                                           const scroll_speed_edit_action_t& action,
                                           scroll_speed_edit_limits_t limits = {})
    -> document_command_result_t;

} // namespace wormedit
