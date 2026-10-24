#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/proposal.h>
#include <wormedit/core/bms_program.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/bms_branch_selection.h>

namespace wormedit {

// Structural authoring targets are independent of the visible preview/edit Chart.
using bms_program_insert_value_t = std::variant<bms_random_scope_t, bms_if_scope_t,
    bms_else_if_scope_t, bms_else_scope_t, bms_switch_scope_t, bms_case_scope_t,
    bms_set_random_t, bms_set_switch_t, bms_chart_content_t, bms_text_t>;
struct bms_program_insert_command_t {
    chart_stamp_t stamp;
    branch_id_t parent;
    // Null inserts at the beginning. Otherwise insert immediately after this child.
    std::optional<branch_id_t> after;
    bms_program_insert_value_t value;
    bms_program_limits_t limits;
    auto operator==(const bms_program_insert_command_t&) const -> bool = default;
};
struct bms_program_text_command_t {
    chart_stamp_t stamp;
    branch_id_t node;
    bms_text_t text;
    bms_program_limits_t limits;
    auto operator==(const bms_program_text_command_t&) const -> bool = default;
};
struct bms_program_random_range_command_t {
    chart_stamp_t stamp;
    branch_id_t node;
    std::int64_t range{1};
    bms_program_limits_t limits;
    auto operator==(const bms_program_random_range_command_t&) const -> bool = default;
};
struct bms_program_parameter_command_t {
    chart_stamp_t stamp;
    branch_id_t node;
    std::optional<std::int64_t> value;
    bms_program_limits_t limits;
    auto operator==(const bms_program_parameter_command_t&) const -> bool = default;
};
struct bms_program_preview_command_t {
    chart_stamp_t stamp;
    branch_id_t node;
    std::optional<std::int64_t> value;
    auto operator==(const bms_program_preview_command_t&) const -> bool = default;
};
[[nodiscard]] auto apply_bms_program_parameter_command(document_history_t& history,
    bms_program_parameter_command_t request) -> document_command_result_t;
[[nodiscard]] auto apply_bms_program_preview_command(document_history_t& history,
    bms_program_preview_command_t request) -> document_command_result_t;
struct bms_program_skip_command_t {
    chart_stamp_t stamp;
    branch_id_t case_node;
    bms_skip_point_t skip;
    bms_program_limits_t limits;
    auto operator==(const bms_program_skip_command_t&) const -> bool = default;
};
[[nodiscard]] auto apply_bms_program_insert_command(document_history_t& history,
    bms_program_insert_command_t request) -> document_command_result_t;
[[nodiscard]] auto apply_bms_program_text_command(document_history_t& history,
    bms_program_text_command_t request) -> document_command_result_t;
[[nodiscard]] auto apply_bms_program_random_range_command(document_history_t& history,
    bms_program_random_range_command_t request) -> document_command_result_t;
[[nodiscard]] auto apply_bms_program_skip_command(document_history_t& history,
    bms_program_skip_command_t request) -> document_command_result_t;

struct bms_program_move_report_t {
    [[nodiscard]] static auto from(const bms_program_document_t& owner, branch_id_t subtree)
        -> std::optional<bms_program_move_report_t>;
    branch_id_t source_parent;
    std::vector<branch_id_t> subtree;
    // Conservative impact envelope: declarations can change later bindings anywhere.
    std::vector<branch_id_t> potentially_affected_charts;
    std::vector<object_id_t> potentially_affected_definitions;
    std::vector<branch_id_t> before_active_charts;
    std::vector<branch_id_t> after_active_charts;
    auto operator==(const bms_program_move_report_t&) const -> bool = default;
};
struct bms_program_move_edit_t {
    static constexpr proposal_capture_policy_t apply_policy{.stale_label = "Move BMS subtree"};
    bms_program_move_t move;
    bms_program_limits_t limits;
    bms_program_move_report_t report;
    [[nodiscard]] static auto propose(const song_document_t& song, chart_id_t difficulty,
        bms_program_move_t move, bms_program_limits_t limits = {})
        -> attempt_t<proposal_t<bms_program_move_edit_t>>;
    [[nodiscard]] auto apply(proposal_edit_scope_t<chart_stamp_t>& scope) const -> attempt_t<std::string>;
    auto operator==(const bms_program_move_edit_t&) const -> bool = default;
};
using bms_program_move_proposal_t = proposal_t<bms_program_move_edit_t>;

enum class bms_program_delete_decision_t { cancel, confirm };
class bms_program_delete_edit_t {
  public:
    static constexpr proposal_capture_policy_t apply_policy{.stale_label = "Delete BMS subtree"};
    [[nodiscard]] static auto propose(const song_document_t& song, chart_id_t difficulty,
        branch_id_t node, bms_program_limits_t limits = {})
        -> attempt_t<proposal_t<bms_program_delete_edit_t>>;
    [[nodiscard]] auto summary() const -> const bms_program_subtree_t& { return summary_; }
    [[nodiscard]] auto validate_capture(const chart_stamp_t& stamp) const -> diagnostic_list_t;
    [[nodiscard]] auto apply(proposal_edit_scope_t<chart_stamp_t>& scope,
        bms_program_delete_decision_t decision) const -> attempt_t<std::string>;
    auto operator==(const bms_program_delete_edit_t&) const -> bool = default;
  private:
    bms_program_delete_edit_t(chart_stamp_t stamp, bms_program_subtree_t summary, bms_program_limits_t limits)
        : stamp_(stamp), summary_(std::move(summary)), limits_(limits) {}
    chart_stamp_t stamp_;
    bms_program_subtree_t summary_;
    bms_program_limits_t limits_;
};
using bms_program_delete_proposal_t = proposal_t<bms_program_delete_edit_t>;

struct bms_program_unwrap_report_t {
    [[nodiscard]] static auto from(const bms_program_document_t& owner, branch_id_t node)
        -> std::optional<bms_program_unwrap_report_t>;
    bms_program_subtree_t contents;
    bms_program_move_report_t effects;
    std::vector<branch_id_t> unconditional_branches;
    std::size_t removed_skips{};
    auto operator==(const bms_program_unwrap_report_t&) const -> bool = default;
};
enum class bms_program_unwrap_decision_t { cancel, confirm_promotion, confirm_unconditional_conversion };
class bms_program_unwrap_edit_t {
  public:
    static constexpr proposal_capture_policy_t apply_policy{.stale_label = "Unwrap BMS scope"};
    [[nodiscard]] static auto propose(const song_document_t& song, chart_id_t difficulty,
        branch_id_t node, bms_program_limits_t limits = {})
        -> attempt_t<proposal_t<bms_program_unwrap_edit_t>>;
    [[nodiscard]] auto report() const -> const bms_program_unwrap_report_t& { return report_; }
    [[nodiscard]] auto validate_capture(const chart_stamp_t& stamp) const -> diagnostic_list_t;
    [[nodiscard]] auto apply(proposal_edit_scope_t<chart_stamp_t>& scope,
        bms_program_unwrap_decision_t decision) const -> attempt_t<std::string>;
    auto operator==(const bms_program_unwrap_edit_t&) const -> bool = default;
  private:
    bms_program_unwrap_edit_t(chart_stamp_t stamp, bms_program_unwrap_report_t report, bms_program_limits_t limits)
        : stamp_(stamp), report_(std::move(report)), limits_(limits) {}
    chart_stamp_t stamp_;
    bms_program_unwrap_report_t report_;
    bms_program_limits_t limits_;
};
using bms_program_unwrap_proposal_t = proposal_t<bms_program_unwrap_edit_t>;

struct bms_branch_statement_command_payload_t {
    std::vector<bms_branch_statement_edit_diagnostic_t> format_diagnostics;
};

using bms_branch_statement_command_result_t = command_result_with_t<bms_branch_statement_command_payload_t>;

// Applies a source-verified inactive branch statement edit through the shared
// document history. The expected revision rejects stale editor actions before
// inspecting their retained-source identities.
[[nodiscard]] auto apply_bms_branch_statement_edit_command(document_history_t& history,
                                                           bms_branch_statement_edit_t edit)
    -> bms_branch_statement_command_result_t;

[[nodiscard]] auto apply_bms_branch_statement_insert_command(
    document_history_t& history,
    bms_branch_statement_insert_t insertion) -> bms_branch_statement_command_result_t;

[[nodiscard]] auto apply_bms_branch_statement_delete_command(document_history_t& history,
                                                             bms_branch_statement_delete_t deletion)
    -> bms_branch_statement_command_result_t;

struct bms_branch_selection_command_payload_t {
    bms_branch_preview_view_t preview;
    std::vector<bms_branch_selection_diagnostic_t> format_diagnostics;
    std::vector<bms_write_diagnostic_t> write_diagnostics;
    std::vector<bms_parse_diagnostic_t> parse_diagnostics;
};

using bms_branch_selection_command_result_t = command_result_with_t<bms_branch_selection_command_payload_t>;

// Revision-checks, rematerializes, validates, and publishes one complete BMS
// branch choice as a single undoable document command.
[[nodiscard]] auto apply_bms_branch_selection_command(document_history_t& history,
                                                      bms_branch_selection_request_t request)
    -> bms_branch_selection_command_result_t;

struct bms_branch_choice_command_t {
    chart_id_t chart;
    branch_id_t branch;
    // Null removes the explicit selection; a value replaces or inserts it.
    std::optional<std::int64_t> selected_value;
};

// Scalar choice boundary for frontends. The complete proposal is derived from
// the current revision before using the same atomic rematerialization command.
[[nodiscard]] auto apply_bms_branch_choice_command(document_history_t& history,
                                                   bms_branch_choice_command_t choice)
    -> bms_branch_selection_command_result_t;

struct bms_branch_block_create_command_t {
    chart_id_t chart;
    branch_id_t first_statement;
    std::size_t first_source_order{};
    bms_source_span_t first_source_span;
    std::string first_original_text;
    branch_id_t last_statement;
    std::size_t last_source_order{};
    bms_source_span_t last_source_span;
    std::string last_original_text;
    std::size_t leaf_count{2};
};

struct bms_branch_block_remove_command_t {
    chart_id_t chart;
    branch_id_t random_block;
    std::size_t expected_source_order{};
    bms_source_span_t expected_source_span;
    std::string expected_original_text;
};

// These commands only accept one exact contiguous range of root-level raw
// statements. Creation places that range in leaf 1; remaining leaves are empty.
[[nodiscard]] auto apply_bms_branch_block_create_command(
    document_history_t& history,
    bms_branch_block_create_command_t request) -> document_command_result_t;

// Only an empty RANDOM/IF block can be removed. The complete edit is reparsed
// and published as one history command, preserving unrelated retained source.
[[nodiscard]] auto apply_bms_branch_block_remove_command(
    document_history_t& history,
    bms_branch_block_remove_command_t request) -> document_command_result_t;

enum class bms_branch_leaf_move_direction_t { up, down };

struct bms_branch_leaf_move_command_t {
    chart_id_t chart;
    branch_id_t random_block;
    branch_id_t leaf;
    // Identity of the moved leaf header, so a stale outline row rejects.
    std::size_t expected_leaf_source_order{};
    std::string expected_leaf_original_text;
    bms_branch_leaf_move_direction_t direction{bms_branch_leaf_move_direction_t::up};
};

// Swaps the bodies of two adjacent #IF/#ELSEIF leaves of one RANDOM block, so the leaf
// content moves while the #IF/#ELSEIF values stay in order. The block's active choice
// follows the moved content. Leaves with nested control flow, #ELSE leaves and edges
// reject atomically. One undoable history command.
[[nodiscard]] auto apply_bms_branch_leaf_move_command(
    document_history_t& history,
    bms_branch_leaf_move_command_t request) -> document_command_result_t;

} // namespace wormedit
