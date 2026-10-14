#pragma once
#include <wormedit/core/proposal_apply_result.h>

#include <wormedit/core/command.h>
#include <wormedit/core/lane_profiles.h>

#include <cstdint>
#include <cstddef>
#include <optional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit {

struct lane_migration_proposal_payload_t;
class lane_migration_proposal_access_t;

enum class lane_migration_issue_t {
    none,
    chart_not_found,
    stale_revision,
    invalid_target_profile,
    missing_layout,
    invalid_mapping,
    ambiguous_default_mapping,
    unsupported_branch_identity,
    drop_confirmation_required,
    lane_collision,
};

struct lane_migration_mapping_t {
    lane_id_t source_lane;
    std::optional<lane_id_t> target_lane;
    auto operator==(const lane_migration_mapping_t&) const -> bool = default;
};

struct lane_migration_collision_t {
    object_id_t first_note;
    object_id_t second_note;
    lane_id_t target_lane;
    bool first_is_sound_channel_event{};
    bool second_is_sound_channel_event{};
    auto operator==(const lane_migration_collision_t&) const -> bool = default;
};

struct lane_migration_proposal_t {
    lane_migration_issue_t issue{lane_migration_issue_t::none};
    chart_stamp_t stamp;
    diagnostic_list_t diagnostics;
    lane_input_profile_t target;
    std::vector<lane_migration_mapping_t> mapping;
    std::vector<object_id_t> dropped_notes;
    std::vector<object_id_t> dropped_sound_channel_events;
    // The example list is capped at 64. collision_count counts incoming
    // objects with at least one conflict, not every colliding pair. Touching
    // hold endpoints count as overlap.
    std::vector<lane_migration_collision_t> collisions;
    std::size_t collision_count{};
    std::size_t mapped_note_count{};
    std::size_t remapped_note_count{};
    std::size_t notes_in_merged_lanes{};
    // Number of old bindings removed plus new target bindings added.
    std::size_t changed_bindings{};

    [[nodiscard]] auto message() const noexcept -> std::string_view {
        return diagnostics.empty() ? std::string_view{} : std::string_view{diagnostics.front().message};
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return issue == lane_migration_issue_t::none && static_cast<bool>(canonical_);
    }

  private:
    std::shared_ptr<const lane_migration_proposal_payload_t> canonical_;
    friend class lane_migration_proposal_access_t;
};

using lane_migration_apply_result_t = proposal_apply_result_t<lane_migration_issue_t>;

// An empty mapping requests the display-order default. A supplied mapping must
// name every source lane; null targets explicitly drop that lane's notes.
[[nodiscard]] auto propose_lane_migration(
    const song_document_t& song, std::uint64_t expected_revision, chart_id_t chart,
    lane_input_profile_t target,
    std::span<const lane_migration_mapping_t> mapping = {}) -> lane_migration_proposal_t;

// The proposal is revision-bound and applies all chart layout segments,
// notes, bindings, and retained playable sound-channel events atomically.
[[nodiscard]] auto apply_lane_migration(document_history_t& history,
                                        const lane_migration_proposal_t& proposal,
                                        bool confirm_drop) -> lane_migration_apply_result_t;

} // namespace wormedit
