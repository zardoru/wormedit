#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/lane_profiles.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace wormedit {

// Profiles a layout with `lane_count` playable lanes can switch to: osu!mania keys and, when the
// count matches a named StepMania step type, its arrow layout. Never includes lane scratch roles.
[[nodiscard]] auto lane_profile_choices(std::size_t lane_count) -> std::vector<lane_input_profile_t>;

// Only all-key layouts can use the same-count key-profile switcher. Scratch and auxiliary
// roles need an explicit layout edit, rather than being relabeled as ordinary keys.
[[nodiscard]] auto can_use_lane_key_profiles(const lane_layout_t& layout) -> bool;
// Compatibility for the coordinated sys/UI migration.
inline auto lane_layout_supports_key_profiles(const lane_layout_t& layout) -> bool { return can_use_lane_key_profiles(layout); }

// Built-in profiles offered when a chart has no playable layout yet.
[[nodiscard]] auto lane_profile_choices() -> std::vector<lane_input_profile_t>;

// The layout segment in effect at `position`: the last segment starting at or before it.
[[nodiscard]] auto active_lane_layout_segment(const chart_document_t& chart, local_position_t position)
    -> const lane_layout_segment_t*;

// Keeps the current lane IDs, order and stages and adopts the target's profile id, display name,
// lane names, glyphs and key colors. SpecialStyle switches also adopt the target stage split.
// Nullopt unless both layouts have the same lane count and corresponding lane roles.
[[nodiscard]] auto retarget_lane_layout(const lane_layout_t& current, const lane_layout_t& target)
    -> std::optional<lane_layout_t>;

// Switches the layout segment active at `position` to `target` as one undoable command. Notes
// never move; lane IDs are unchanged.
[[nodiscard]] auto apply_lane_profile_switch(document_history_t& history, chart_id_t chart,
                                             local_position_t position,
                                             const lane_layout_t& target)
    -> document_command_result_t;

} // namespace wormedit
