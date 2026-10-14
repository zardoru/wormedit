#pragma once

#include <wormedit/core/command.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/bms_branch_selection.h>

namespace wormedit {

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
    bms_branch_projection_preview_t preview;
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
