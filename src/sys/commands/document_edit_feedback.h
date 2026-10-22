#pragma once

#include <wormedit/core/notifications.h>
#include <wormedit/core/workspace.h>

#include <optional>
#include <string_view>

namespace wormedit::sys {

struct document_edit_description_t {
    std::string_view success;
    std::string_view rebind_failure;
    bool is_message_diagnostic{true};
    bool is_severity_diagnostic{true};
    notification_severity_t unknown_severity{notification_severity_t::error};
    std::optional<notification_severity_t> failure_severity;
    notification_source_t source{notification_source_t::document};
    std::optional<notification_source_t> failure_source;
    auto operator==(const document_edit_description_t&) const -> bool = default;
};

// Mapping only: notification delivery and ordering stay with the caller.
struct document_edit_feedback_t {
    bool accepted{};
    notification_severity_t severity{notification_severity_t::success};
    // Static caller text is borrowed; diagnostic text stays in diagnostics.
    std::string_view message;
    bool is_message_diagnostic{true};
    notification_source_t source{notification_source_t::document};
    diagnostic_list_t diagnostics;
    auto operator==(const document_edit_feedback_t&) const -> bool = default;

    [[nodiscard]] auto message_text() const -> std::string_view {
        return is_message_diagnostic && !diagnostics.empty() ? diagnostics.front().message
                                                             : message;
    }

    [[nodiscard]] static auto from(dispatch_outcome_t rebound,
                                   const document_edit_description_t& description)
        -> document_edit_feedback_t;
};

[[nodiscard]] auto rebind_document_edit(workspace_controller_t& workspace,
                                        const document_history_t& history,
                                        const document_edit_description_t& description)
    -> document_edit_feedback_t;

} // namespace wormedit::sys
