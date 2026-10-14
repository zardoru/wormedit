#pragma once

#include <wormedit/core/command.h>

#include <cstdint>
#include <optional>
#include <variant>

namespace wormedit {

// Where a lane layout segment may start. Open question 6 is still provisional,
// so the default keeps every change on a measure boundary.
enum class lane_layout_change_policy_t {
    measure_boundaries_only,
    any_row,
};

struct insert_lane_layout_action_t {
    chart_id_t chart;
    local_position_t position;
    lane_layout_t layout;
    // Key bindings for the new layout's lanes, taken only when the chart has none yet (an empty
    // BMS has no playable channel to detect a profile from), so the lanes can be played at once.
    std::optional<input_profile_t> input;
};

struct update_lane_layout_action_t {
    chart_id_t chart;
    object_id_t id;
    lane_layout_t layout;
};

struct delete_lane_layout_action_t {
    chart_id_t chart;
    object_id_t id;
};

using lane_layout_action_t = std::variant<insert_lane_layout_action_t, update_lane_layout_action_t,
                                          delete_lane_layout_action_t>;

// Inserts, replaces or removes one layout segment through shared history.
// Notes are never moved: when any note at or after the change would sit on a
// lane the active layout does not define, the whole command is rejected with
// the unmapped-note count. The segment at the chart start cannot be removed.
[[nodiscard]] auto apply_lane_layout_edit(
    document_history_t& history,
    const lane_layout_action_t& action,
    lane_layout_change_policy_t policy = lane_layout_change_policy_t::measure_boundaries_only)
    -> document_command_result_t;

} // namespace wormedit
