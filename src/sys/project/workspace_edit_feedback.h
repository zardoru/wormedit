#include "sys/notifications/projection_notification_feedback.h"
#pragma once

#include "sys/application_edit_inputs.h"
#include "sys/compose/actions/compose_action_outcome.h"

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
  void rebind(workspace_controller_t& workspace, document_history_t& history,
              notification_sink_t notifications) {
      const stamp_t stamp{history.document.id, history.document.revision, history.undo.size(),
                          history.redo.size(), is_document_dirty(history)};
      const bool needs_rebind =
          workspace.song == stamp.song &&
          (workspace.document_revision != stamp.revision ||
           workspace.history.undo_depth != stamp.undo ||
           workspace.history.redo_depth != stamp.redo || workspace.history.dirty != stamp.dirty);
      if (!needs_rebind) {
          failed_.reset();
          return;
      }
      // Retrying is authoritative; suppression affects only publication.
      auto outcome = compose_action_outcome_t::from({}, notification_severity_t::success,
                                                    notification_source_t::document,
                                                    compose_feedback_delivery_t::none);
      outcome.is_workspace_refresh_required = true;
      outcome.rebind_failure = "Workspace could not follow the latest document edit.";
      outcome.rebind_feedback_policy = compose_rebind_feedback_policy_t::document_only;
      const auto finished = finish_compose_action(
          {history, workspace, notifications},
          failed_ == stamp ? notification_sink_t::muted() : notifications, std::move(outcome));
      if (finished.accepted)
          failed_.reset();
      else
          failed_ = stamp;
  }
};

} // namespace wormedit::sys
