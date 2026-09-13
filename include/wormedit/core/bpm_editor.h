#pragma once

#include <wormedit/core/command.h>

#include <cstddef>
#include <cstdint>
#include <variant>

namespace wormedit {

struct bpm_edit_limits_t {
    std::size_t maximum_bpm_events{100'000};
    std::size_t maximum_chart_objects{1'000'000};
};

struct insert_bpm_event_action_t {
    chart_id_t chart;
    local_position_t position;
    signed_rational_t beats_per_minute;
};

struct update_bpm_event_action_t {
    chart_id_t chart;
    object_id_t event;
    local_position_t position;
    signed_rational_t beats_per_minute;
};

struct delete_bpm_event_action_t {
    chart_id_t chart;
    object_id_t event;
};

using bpm_edit_action_t =
    std::variant<insert_bpm_event_action_t, update_bpm_event_action_t, delete_bpm_event_action_t>;

// Mutates one chart BPM event through shared document history. Local positions
// and exact positive rationals are preserved verbatim; same-row events remain
// legal and timing order uses source order before insertion order and stable ID.
// Insert assigns a fresh chart-wide object ID and an order after existing BPMs.
[[nodiscard]] auto apply_bpm_edit(document_history_t& history, std::uint64_t expected_revision,
                                  const bpm_edit_action_t& action, bpm_edit_limits_t limits = {})
    -> document_command_result_t;

} // namespace wormedit
