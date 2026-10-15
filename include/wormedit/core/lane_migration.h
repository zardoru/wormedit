#pragma once
#include <wormedit/core/proposal_apply_result.h>

#include <wormedit/core/proposal.h>
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

struct lane_migration_report_t {
    std::vector<object_id_t> dropped_notes;
    std::vector<object_id_t> dropped_sound_channel_events;
    // Counts incoming conflicts (closed hold endpoints); examples remain capped at64.
    std::vector<lane_migration_collision_t> collisions;
    std::size_t collision_count{};
    std::size_t mapped_note_count{};
    std::size_t remapped_note_count{};
    std::size_t notes_in_merged_lanes{};
    std::size_t changed_bindings{};
    auto operator==(const lane_migration_report_t&) const -> bool = default;
};

struct lane_migration_diagnostic_t : diagnostic_t {
    lane_migration_issue_t issue{lane_migration_issue_t::none};
    chart_stamp_t stamp;
    lane_migration_report_t report;
    auto operator==(const lane_migration_diagnostic_t&) const -> bool = default;

    [[nodiscard]] static auto from(lane_migration_issue_t issue, std::string message,
        chart_stamp_t stamp, lane_migration_report_t report) -> lane_migration_diagnostic_t {
        return {error(diagnostic_code_t::invalid_binding, std::move(message)), issue, stamp,
                std::move(report)};
    }
};

struct lane_migration_proposal_t;
using lane_migration_diagnostics_t = std::vector<lane_migration_diagnostic_t>;
using lane_migration_proposal_outcome_t =
    proposal_outcome_t<lane_migration_proposal_t, chart_stamp_t, lane_migration_diagnostics_t>;

struct lane_migration_proposal_t {
    static constexpr proposal_capture_policy_t apply_policy{.stale_label = "Lane migration"};
    static constexpr auto apply_stale_issue = lane_migration_issue_t::stale_revision;
    static constexpr auto apply_commit_failure_issue = lane_migration_issue_t::invalid_mapping;
    static constexpr std::string_view apply_commit_failure_message = "The migrated chart failed document validation.";
    [[nodiscard]] auto validate_apply(const chart_stamp_t&, bool confirm_drop) const
        -> proposal_apply_payload_t<lane_migration_issue_t>;
    // Applies all layout segments, notes, bindings and retained playable events atomically.
    [[nodiscard]] auto apply(proposal_edit_scope_t<chart_stamp_t>&, bool confirm_drop) const
        -> diagnosed_t<std::optional<std::string>, proposal_apply_payload_t<lane_migration_issue_t>>;
    [[nodiscard]] static auto propose(const song_document_t& song, std::uint64_t expected_revision,
        chart_id_t chart, lane_input_profile_t target,
        std::span<const lane_migration_mapping_t> mapping = {}) -> lane_migration_proposal_outcome_t;
    [[nodiscard]] auto revise(const chart_stamp_t& stamp, const song_document_t& song,
        lane_input_profile_t target, std::span<const lane_migration_mapping_t> mapping = {}) const
        -> lane_migration_proposal_outcome_t;

    lane_input_profile_t target;
    std::vector<lane_migration_mapping_t> mapping;
    lane_migration_report_t report;
    auto operator==(const lane_migration_proposal_t&) const -> bool = default;

  private:
    [[nodiscard]] static auto prepare(const song_document_t&, const chart_stamp_t&, lane_input_profile_t,
        std::span<const lane_migration_mapping_t>) -> lane_migration_proposal_outcome_t;
    std::shared_ptr<const lane_migration_proposal_payload_t> canonical_;
};

static_assert(staged_proposal<lane_migration_proposal_t,
    std::tuple<const song_document_t&, std::uint64_t, chart_id_t, lane_input_profile_t,
               std::span<const lane_migration_mapping_t>>,
    std::tuple<const chart_stamp_t&, const song_document_t&, lane_input_profile_t,
               std::span<const lane_migration_mapping_t>>,
    chart_stamp_t, lane_migration_diagnostics_t>);

using lane_migration_preview_view_t =
    proposal_preview_view_t<lane_migration_proposal_t, chart_stamp_t, lane_migration_diagnostics_t>;

} // namespace wormedit
