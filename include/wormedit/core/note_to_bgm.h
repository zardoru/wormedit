#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/interaction.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wormedit {

struct note_to_bgm_policy_t {
    bool allow_new_lanes{true};
    std::size_t max_lanes{1'000'000};
};

struct note_to_bgm_assignment_t {
    object_id_t note;
    object_id_t lane;
    auto operator==(const note_to_bgm_assignment_t&) const -> bool = default;
};

struct note_to_bgm_new_lane_t {
    object_id_t id;
    std::string name;
    std::size_t source_order{};
};

struct note_to_bgm_plan_t {
    std::vector<note_to_bgm_assignment_t> assignments;
    std::vector<note_to_bgm_new_lane_t> new_lanes;
    std::vector<object_id_t> retained_event_ids;
    diagnostic_list_t diagnostics;

    [[nodiscard]] auto accepted() const -> bool { return diagnostics.empty(); }
};

// Pure ordinary-note allocator. Retained linked notes are rejected so callers route BMSON stem
// reassignment through the existing command path. A supplied lane requests manual right/left
// placement; nullopt requests canonical automatic group placement.
[[nodiscard]] auto plan_notes_to_bgm(const chart_document_t& chart,
                                     std::span<const resource_entry_t> resources,
                                     std::span<const object_id_t> notes,
                                     std::optional<object_id_t> preferred_lane,
                                     note_to_bgm_policy_t policy = {}) -> note_to_bgm_plan_t;

// Moves playable notes to the BGM area in one undo step. A null preferred_lane uses automatic
// group-aware placement; an explicit lane keeps the compatible manual right-then-left route.
// A BMSON tap that stands for a stem point returns to its stem's BGM lane and keeps its slice
// point (the retained event just stops being playable). Ordinary notes become BGM events at the
// same row. Explicit placement uses `preferred_lane`, then the next free lane to the right and
// left, then creates a lane; automatic placement uses the group-aware plan. Hold tails are dropped
// in this phase until the approved BGM tail schema phase lands. Unknown notes, a stale
// revision, or a stem that cannot be reassigned reject the whole edit.
[[nodiscard]] auto move_notes_to_bgm(document_history_t& history, std::uint64_t expected_revision,
                                     chart_id_t chart, std::span<const object_id_t> notes,
                                     std::optional<object_id_t> preferred_lane,
                                     note_to_bgm_policy_t policy = {})
    -> document_command_result_t;

// The reverse move, in one undo step: BGM events become playable taps in `lane`, keeping their row.
// A BMSON marker that stands for a stem point becomes the stem's playable tap, keeping its slice
// point; any other event becomes a plain tap with its sound, layers and branch. A lane that does
// not exist at the row, or already holds a note there, rejects the whole edit.
[[nodiscard]] auto move_bgm_events_to_lane(document_history_t& history, std::uint64_t expected_revision,
                                           chart_id_t chart, std::span<const object_id_t> events,
                                           lane_id_t lane) -> document_command_result_t;

// Assigns a mixed selected note/BGM set into a lane as one all-or-nothing undo command.
[[nodiscard]] auto move_selected_objects_to_lane(
    document_history_t& history, std::uint64_t expected_revision,
    const editor_interaction_state_t& state, std::span<const object_id_t> notes,
    std::span<const object_id_t> events, lane_id_t lane) -> document_command_result_t;

} // namespace wormedit
