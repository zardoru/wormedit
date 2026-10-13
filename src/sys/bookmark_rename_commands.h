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

} // namespace wormedit::sys
