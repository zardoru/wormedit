#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/lane_profiles.h>

namespace wormedit {

struct duplicate_chart_payload_t {
    std::optional<chart_id_t> chart;
};

using duplicate_chart_result_t = command_result_with_t<duplicate_chart_payload_t>;

// Creates one empty standard measure with a 120 BPM origin and fresh identities.
// No chart content or output-group membership is copied from an existing chart.
[[nodiscard]] auto apply_new_chart_command(document_history_t& history,
    std::uint64_t expected_revision, lane_input_profile_t profile = make_bms_7k_profile(),
    format_family_t source_family = format_family_t::native_project) -> duplicate_chart_result_t;

// Templates only the initial lane profile, resolution and exact audio offset.
// Creates one empty standard measure with fresh identities; source content is never cloned.
[[nodiscard]] auto apply_add_chart_command(document_history_t& history,
    std::uint64_t expected_revision, chart_id_t template_chart) -> duplicate_chart_result_t;

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

// nullopt creates a group; an index replaces it atomically. Empty membership is a draft.
[[nodiscard]] auto apply_set_output_group_command(document_history_t& history,
    std::uint64_t expected_revision, std::optional<std::size_t> index, output_group_t group)
    -> document_command_result_t;
[[nodiscard]] auto apply_delete_output_group_command(document_history_t& history,
    std::uint64_t expected_revision, std::size_t index) -> document_command_result_t;

} // namespace wormedit
