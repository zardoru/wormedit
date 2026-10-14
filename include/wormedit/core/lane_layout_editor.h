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

struct lane_layout_edit_value_t {
    // Position and initial input bindings are insertion data; updates replace only the layout.
    local_position_t position;
    lane_layout_t layout;
    std::optional<input_profile_t> input;
    auto operator==(const lane_layout_edit_value_t&) const -> bool = default;
};

using lane_layout_action_t =
    std::variant<wormedit::insert_edit_t<wormedit::lane_layout_edit_value_t>,
                 wormedit::update_edit_t<wormedit::lane_layout_edit_value_t>,
                 wormedit::delete_edit_t<wormedit::lane_layout_edit_value_t>>;

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
