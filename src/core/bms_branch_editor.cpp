#include <wormedit/core/bms_branch_editor.h>

#include <utility>

namespace wormedit {
namespace {

auto stale_revision_result(const document_history_t& history)
    -> bms_branch_statement_command_result_t {
    bms_branch_statement_command_result_t result;
    result.command.revision = history.document.revision;
    result.command.diagnostics.push_back({
        .severity = diagnostic_severity_t::error,
        .code = diagnostic_code_t::invalid_id,
        .message = "BMS branch editor document revision is stale",
    });
    return result;
}

auto publish_branch_edit(document_history_t& history, bms_branch_statement_edit_result_t edit,
                         std::string label) -> bms_branch_statement_command_result_t {
    bms_branch_statement_command_result_t result;
    result.command.revision = history.document.revision;
    result.format_diagnostics = std::move(edit.diagnostics);
    if (!edit.song || edit.has_errors()) {
        return result;
    }

    result.command = commit_document_command(history, std::move(*edit.song), std::move(label));
    return result;
}

} // namespace

auto apply_bms_branch_statement_edit_command(document_history_t& history,
                                             std::uint64_t expected_revision,
                                             bms_branch_statement_edit_t edit)
    -> bms_branch_statement_command_result_t {
    if (expected_revision != history.document.revision) {
        return stale_revision_result(history);
    }
    return publish_branch_edit(
        history, edit_bms_inactive_branch_statement(history.document, std::move(edit)),
        "Edit inactive BMS statement");
}

auto apply_bms_branch_statement_insert_command(document_history_t& history,
                                               std::uint64_t expected_revision,
                                               bms_branch_statement_insert_t insertion)
    -> bms_branch_statement_command_result_t {
    if (expected_revision != history.document.revision) {
        return stale_revision_result(history);
    }
    return publish_branch_edit(
        history, insert_bms_inactive_branch_statement(history.document, std::move(insertion)),
        "Insert inactive BMS statement");
}

auto apply_bms_branch_statement_delete_command(document_history_t& history,
                                               std::uint64_t expected_revision,
                                               bms_branch_statement_delete_t deletion)
    -> bms_branch_statement_command_result_t {
    if (expected_revision != history.document.revision) {
        return stale_revision_result(history);
    }
    return publish_branch_edit(
        history, delete_bms_inactive_branch_statement(history.document, std::move(deletion)),
        "Delete inactive BMS statement");
}

auto apply_bms_branch_selection_command(document_history_t& history,
                                        std::uint64_t expected_revision,
                                        bms_branch_selection_request_t request)
    -> bms_branch_selection_command_result_t {
    bms_branch_selection_command_result_t result;
    result.command.revision = history.document.revision;
    if (expected_revision != history.document.revision) {
        result.command.diagnostics.push_back({
            .severity = diagnostic_severity_t::error,
            .code = diagnostic_code_t::invalid_id,
            .message = "BMS branch selection document revision is stale",
            .object = std::nullopt,
        });
        return result;
    }

    auto rematerialized = rematerialize_bms_branch_selection(history.document, request);
    const auto can_publish = rematerialized.song.has_value() && !rematerialized.has_errors();
    result.preview = std::move(rematerialized.preview);
    result.format_diagnostics = std::move(rematerialized.diagnostics);
    result.write_diagnostics = std::move(rematerialized.write_diagnostics);
    result.parse_diagnostics = std::move(rematerialized.parse_diagnostics);
    if (!can_publish) {
        return result;
    }
    result.command = commit_document_command(history, std::move(*rematerialized.song),
                                             "Change active BMS branch");
    return result;
}

} // namespace wormedit
