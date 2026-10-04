#pragma once

#include <wormedit/core/notifications.h>
#include <span>
#include <string>

namespace wormedit::sys {

inline auto project_import_warning(std::span<const std::string> warnings) -> notification_post_t {
    notification_post_t post{.severity = notification_severity_t::warning,
                             .source = notification_source_t::import_, .message = {}, .detail = {}, .target = {}};
    if (warnings.empty()) return post;
    post.message = "Chart imported with warning: " + warnings.front();
    if (warnings.size() > 1)
        post.message += " (" + std::to_string(warnings.size()) + " resolution warnings total)";
    for (const auto& warning : warnings) {
        if (!post.detail.empty()) post.detail += '\n';
        post.detail += warning;
    }
    return post;
}

} // namespace wormedit::sys
