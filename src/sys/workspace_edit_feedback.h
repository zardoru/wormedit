#include "sys/projection_notification_feedback.h"
#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/workspace.h>
#include <optional>

namespace wormedit::sys {

class workspace_edit_feedback_t {
    struct stamp_t {
        song_id_t song;
        std::uint64_t revision{};
        std::size_t undo{}, redo{};
        bool dirty{};
        auto operator==(const stamp_t&) const -> bool = default;
    };
    std::optional<stamp_t> failed_;

public:
    void rebind(workspace_controller_t& workspace, const document_history_t& history,
                notification_sink_t notifications) {
        const stamp_t stamp{history.document.id, history.document.revision,
                            history.undo.size(), history.redo.size(), is_document_dirty(history)};
        const bool needs_rebind = workspace.song == stamp.song &&
            (workspace.document_revision != stamp.revision || workspace.history.undo_depth != stamp.undo ||
             workspace.history.redo_depth != stamp.redo || workspace.history.dirty != stamp.dirty);
        if (!needs_rebind) { failed_.reset(); return; }
        // Retrying is authoritative; suppression affects only publication.
        const auto rebound = rebind_workspace(workspace, history);
        if (rebound) { failed_.reset(); return; }
        if (failed_ == stamp) return;
        failed_ = stamp;
        if (rebound.diagnostics.empty())
            notifications.post(notification_severity_t::error, notification_source_t::document,
                               "Workspace could not follow the latest document edit.");
        else
            notifications.post(to_notification_severity(rebound.diagnostics.front().severity),
                               notification_source_t::document,
                               rebound.diagnostics.front().message);
    }
};

} // namespace wormedit::sys
