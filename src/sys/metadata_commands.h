#pragma once

#include <wormedit/core/metadata_editor.h>

#include <optional>
#include <utility>

namespace wormedit::sys {
namespace metadata_command_detail {

// Metadata payload parsing precedes this boundary; preserve that rejection order.
inline auto stale_rejection(const document_history_t& history, std::uint64_t revision)
    -> std::optional<document_command_result_t> {
    const document_stamp_t stamp{.project = history.document.id, .revision = revision};
    if (stamp.is_current(history.document))
        return std::nullopt;
    return document_command_transaction_t::rejection(history.document.revision,
                                                     diagnostic_code_t::invalid_id,
                                                     stale_action_message("Metadata edit"));
}

} // namespace metadata_command_detail

inline auto apply_metadata_edit(document_history_t& history, std::uint64_t revision,
                                const metadata_edit_action_t& action) -> document_command_result_t {
    if (auto rejected = metadata_command_detail::stale_rejection(history, revision))
        return std::move(*rejected);
    return wormedit::apply_metadata_edit(history, action);
}

inline auto apply_batch_chart_metadata_edit(document_history_t& history, std::uint64_t revision,
                                            const batch_chart_metadata_action_t& action)
    -> document_command_result_t {
    if (auto rejected = metadata_command_detail::stale_rejection(history, revision))
        return std::move(*rejected);
    return wormedit::apply_batch_chart_metadata_edit(history, action);
}

} // namespace wormedit::sys
