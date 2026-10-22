#pragma once

#include "sys/app/platform_recovery.h"
#include <wormedit/core/notifications.h>

namespace wormedit::sys {

inline auto make_initial_application_feedback(bool loaded_primary,
    const std::filesystem::path& requested_primary_path,
    const result_t<std::filesystem::path, std::string>& recovery) -> notification_post_t {
    notification_post_t post{.severity = notification_severity_t::info,
                             .source = notification_source_t::recovery, .message = {}, .detail = {}, .target = {}};
    if (loaded_primary) {
        post.severity = notification_severity_t::success;
        post.source = notification_source_t::document;
        post.message = "Opened native project: " + requested_primary_path.generic_string();
    } else if (!requested_primary_path.empty()) {
        post.severity = notification_severity_t::error;
        post.source = notification_source_t::document;
        post.message = "Primary project was not loaded; review the completed recovery scan.";
    } else {
        post.message = recovery && !recovery->empty() ? "No project loaded; recovery scan complete."
                                : "No project loaded; recovery discovery unavailable.";
    }
    // The platform producer owns its failure; never append it to this context.
    return post;
}

} // namespace wormedit::sys
