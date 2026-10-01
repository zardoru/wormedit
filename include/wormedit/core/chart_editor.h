#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/lane_profiles.h>

namespace wormedit {

struct duplicate_chart_result_t {
    document_command_result_t command;
    std::optional<chart_id_t> chart;
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(command); }
};

// Creates one empty standard measure with a 120 BPM origin and fresh identities.
// No chart content or output-group membership is copied from an existing chart.
[[nodiscard]] auto apply_new_chart_command(document_history_t& history,
    std::uint64_t expected_revision, lane_input_profile_t profile = make_bms_7k_profile(),
    format_family_t source_family = format_family_t::native_project) -> duplicate_chart_result_t;

[[nodiscard]] auto apply_duplicate_chart_command(document_history_t& history,
    std::uint64_t expected_revision, chart_id_t chart) -> duplicate_chart_result_t;
[[nodiscard]] auto apply_delete_chart_command(document_history_t& history,
    std::uint64_t expected_revision, chart_id_t chart) -> document_command_result_t;
// destination is the chart's zero-based position in the resulting chart vector.
[[nodiscard]] auto apply_reorder_chart_command(document_history_t& history,
    std::uint64_t expected_revision, chart_id_t chart, std::size_t destination)
    -> document_command_result_t;
// Removes all memberships, then appends to the selected group. nullopt unassigns.
[[nodiscard]] auto apply_move_chart_group_command(document_history_t& history,
    std::uint64_t expected_revision, chart_id_t chart, std::optional<std::size_t> group)
    -> document_command_result_t;

} // namespace wormedit
