#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/notifications.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace wormedit::sys {

struct bookmark_rename_outcome_t {
    bool accepted{};
    bool document_changed{};
    std::string status;
    diagnostic_list_t diagnostics;
    notification_severity_t severity{notification_severity_t::warning};
};

// Renames the uniquely identified bookmark without changing its anchor or position.
[[nodiscard]] auto rename_bookmark(document_history_t& history,
                                   std::uint64_t expected_revision, chart_id_t chart,
                                   object_id_t bookmark, std::string_view label)
    -> bookmark_rename_outcome_t;

} // namespace wormedit::sys
