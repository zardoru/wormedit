#pragma once
#include <wormedit/core/proposal_apply_result.h>

#include <wormedit/core/proposal.h>
#include <wormedit/core/document.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wormedit {

struct channel_map_proposal_payload_t;

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
    auto operator==(const channel_map_replacement_t&) const -> bool = default;
};

struct channel_map_conflict_t {
    std::string channel;
    std::string first_lane_name;
    std::string second_lane_name;
    auto operator==(const channel_map_conflict_t&) const -> bool = default;
};

struct channel_map_report_t {
    std::vector<channel_map_conflict_t> conflicts;
    std::size_t conflict_count{};
    std::size_t affected_note_count{};
    auto operator==(const channel_map_report_t&) const -> bool = default;
};

// A failed attempt retains display/identity metadata, never applicable replacements or a seal.
struct channel_map_diagnostic_t : diagnostic_t {
    channel_map_issue_t issue{channel_map_issue_t::none};
    chart_stamp_t stamp;
    channel_map_report_t report;
    auto operator==(const channel_map_diagnostic_t&) const -> bool = default;

    [[nodiscard]] static auto from(channel_map_issue_t issue, std::string message,
                                   chart_stamp_t stamp, channel_map_report_t report)
        -> channel_map_diagnostic_t {
        return {error(diagnostic_code_t::invalid_binding, std::move(message)), issue, stamp,
                std::move(report)};
    }
};

struct channel_map_proposal_t;
using channel_map_diagnostics_t = std::vector<channel_map_diagnostic_t>;
using channel_map_proposal_outcome_t =
    proposal_outcome_t<channel_map_proposal_t, chart_stamp_t, channel_map_diagnostics_t>;

struct channel_map_proposal_t {
    static constexpr proposal_capture_policy_t apply_policy{.stale_label = "Channel-map edit"};
    static constexpr auto apply_stale_issue = channel_map_issue_t::stale_revision;
    static constexpr auto apply_commit_failure_issue = channel_map_issue_t::invalid_replacement;
    static constexpr std::string_view apply_commit_failure_message = "The channel-map edit failed validation.";
    [[nodiscard]] auto validate_apply(const chart_stamp_t&) const
        -> proposal_apply_payload_t<channel_map_issue_t>;
    [[nodiscard]] auto apply(proposal_edit_scope_t<chart_stamp_t>&) const
        -> diagnosed_t<std::optional<std::string>, proposal_apply_payload_t<channel_map_issue_t>>;
    [[nodiscard]] static auto propose(const song_document_t& song, std::uint64_t expected_revision,
        chart_id_t chart, object_id_t layout_segment,
        std::span<const channel_map_replacement_t> replacements) -> channel_map_proposal_outcome_t;
    [[nodiscard]] auto revise(const chart_stamp_t& stamp, const song_document_t& song,
        std::span<const channel_map_replacement_t> replacements) const -> channel_map_proposal_outcome_t;

    object_id_t layout_segment;
    std::vector<channel_map_replacement_t> replacements;
    channel_map_report_t report;
    auto operator==(const channel_map_proposal_t&) const -> bool = default;

  private:
    [[nodiscard]] static auto prepare(const song_document_t&, const chart_stamp_t&, object_id_t,
        std::span<const channel_map_replacement_t>) -> channel_map_proposal_outcome_t;
    std::shared_ptr<const channel_map_proposal_payload_t> canonical_;
};

static_assert(staged_proposal<channel_map_proposal_t,
    std::tuple<const song_document_t&, std::uint64_t, chart_id_t, object_id_t,
               std::span<const channel_map_replacement_t>>,
    std::tuple<const chart_stamp_t&, const song_document_t&,
               std::span<const channel_map_replacement_t>>,
    chart_stamp_t, channel_map_diagnostics_t>);

using channel_map_preview_view_t =
    proposal_preview_view_t<channel_map_proposal_t, chart_stamp_t, channel_map_diagnostics_t>;

} // namespace wormedit
