#pragma once

#include <wormedit/core/command.h>
#include <wormedit/formats/bms.h>

namespace wormedit {

struct bms_branch_statement_command_result_t {
    document_command_result_t command;
    std::vector<bms_branch_statement_edit_diagnostic_t> format_diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return command.applied;
    }
};

// Applies a source-verified inactive branch statement edit through the shared
// document history. The expected revision rejects stale editor actions before
// inspecting their retained-source identities.
[[nodiscard]] auto apply_bms_branch_statement_edit_command(document_history_t& history,
                                                           std::uint64_t expected_revision,
                                                           bms_branch_statement_edit_t edit)
    -> bms_branch_statement_command_result_t;

[[nodiscard]] auto apply_bms_branch_statement_insert_command(
    document_history_t& history, std::uint64_t expected_revision,
    bms_branch_statement_insert_t insertion) -> bms_branch_statement_command_result_t;

[[nodiscard]] auto apply_bms_branch_statement_delete_command(document_history_t& history,
                                                             std::uint64_t expected_revision,
                                                             bms_branch_statement_delete_t deletion)
    -> bms_branch_statement_command_result_t;

} // namespace wormedit
