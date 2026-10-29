#pragma once

#include <wormedit/core/document_command_types.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wormedit {

struct lane_transform_limits_t {
    std::size_t maximum_selected_notes{50'000};
};

struct lane_permutation_entry_t {
    lane_id_t from;
    lane_id_t to;
};

// Reverses the lane order of the selected playable notes inside each stage of
// the layout active at their position. Scratch, free-zone, pedal and other
// non-playable lanes stay where they are.
struct mirror_lanes_action_t {
    chart_id_t chart;
    std::vector<object_id_t> notes;
};

// Applies an explicit lane permutation to the selected notes. `permutation`
// must be a bijection over the lanes it names; selected notes in other lanes
// keep their lane.
struct remap_lanes_action_t {
    chart_id_t chart;
    std::vector<object_id_t> notes;
    std::vector<lane_permutation_entry_t> permutation;
};

// Both commands change only `note.lane`: rows, durations, sounds, labels and
// layers are untouched. They are all-or-nothing: a note whose target lane is
// missing from the layout at its head or tail, or any resulting overlap on a
// lane, rejects the whole command. A transform that moves no note creates no
// history entry.
[[nodiscard]] auto apply_lane_mirror(document_history_t& history,
                                     const mirror_lanes_action_t& action,
                                     lane_transform_limits_t limits = {})
    -> document_command_result_t;

[[nodiscard]] auto apply_lane_remap(document_history_t& history,
                                    const remap_lanes_action_t& action,
                                    lane_transform_limits_t limits = {})
    -> document_command_result_t;

} // namespace wormedit
