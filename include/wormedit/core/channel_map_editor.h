#pragma once
#include <wormedit/core/proposal_apply_result.h>

#include <wormedit/core/command.h>
#include <wormedit/core/document.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit {

struct channel_map_proposal_payload_t;
class channel_map_proposal_access_t;

enum class channel_map_issue_t {
    none,
    stale_revision,
    chart_not_found,
    layout_segment_not_found,
    unsupported_format,
    unsupported_branch_channel,
    invalid_replacement,
    invalid_channel,
    missing_export_channel,
    duplicate_channel,
};

struct channel_map_replacement_t {
    lane_id_t lane;
    bms_channel_map_t channels;
    auto operator==(const channel_map_replacement_t& other) const -> bool {
        return lane == other.lane && channels.visible == other.channels.visible &&
               channels.invisible == other.channels.invisible &&
               channels.long_note == other.channels.long_note &&
               channels.mine == other.channels.mine;
    }
};

struct channel_map_conflict_t {
    std::string channel;
    std::string first_lane_name;
    std::string second_lane_name;
    auto operator==(const channel_map_conflict_t&) const -> bool = default;
};

struct channel_map_proposal_t {
    channel_map_issue_t issue{channel_map_issue_t::none};
    chart_stamp_t stamp;
    diagnostic_list_t diagnostics;
    object_id_t layout_segment;
    std::vector<channel_map_replacement_t> replacements;
    std::vector<channel_map_conflict_t> conflicts;
    std::size_t conflict_count{};
    std::size_t affected_note_count{};

    [[nodiscard]] auto message() const noexcept -> std::string_view {
        return diagnostics.empty() ? std::string_view{} : std::string_view{diagnostics.front().message};
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return issue == channel_map_issue_t::none && static_cast<bool>(canonical_);
    }

  private:
    std::shared_ptr<const channel_map_proposal_payload_t> canonical_;
    friend class channel_map_proposal_access_t;
};

using channel_map_apply_result_t = proposal_apply_result_t<channel_map_issue_t>;

// Each replacement names one lane in the selected layout segment. The final map,
// including all replacements, is validated as a whole so simultaneous swaps work.
[[nodiscard]] auto propose_bms_channel_map_edit(
    const song_document_t& song, std::uint64_t expected_revision, chart_id_t chart,
    object_id_t layout_segment, std::span<const channel_map_replacement_t> replacements)
    -> channel_map_proposal_t;

// Applies the sealed preview as one revision-checked undoable command.
[[nodiscard]] auto apply_bms_channel_map_edit(
    document_history_t& history, const channel_map_proposal_t& proposal)
    -> channel_map_apply_result_t;

} // namespace wormedit
