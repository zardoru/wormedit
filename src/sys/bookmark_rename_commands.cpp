#include "sys/bookmark_navigation.h"

#include <wormedit/core/marker_editor.h>

#include <algorithm>
#include <utility>

namespace wormedit::sys {
namespace {

auto reject(std::string message) -> bookmark_rename_outcome_t {
    return {.status = std::move(message)};
}

auto find_bookmark(document_history_t& history, chart_id_t chart_id, object_id_t bookmark_id)
    -> bookmark_t* {
    const auto chart = history.document.find_chart(chart_id);
    if (chart == nullptr) return nullptr;
    const auto bookmark = std::ranges::find(chart->bookmarks, bookmark_id, &bookmark_t::id);
    return bookmark == chart->bookmarks.end() ? nullptr : &*bookmark;
}

} // namespace

auto bookmark_navigation_t::rename_bookmark(document_history_t& history, std::uint64_t expected_revision,
                     chart_id_t chart_id, object_id_t bookmark_id, std::string_view label)
    -> bookmark_rename_outcome_t {
    if (!document_stamp_t{.project = history.document.id, .revision = expected_revision}
             .is_current(history.document))
        return reject(stale_action_message("Bookmark rename"));
    if (!chart_id || !bookmark_id) return reject("Choose a bookmark to rename.");
    const auto* bookmark = find_bookmark(history, chart_id, bookmark_id);
    if (!bookmark) return reject("That bookmark is no longer available.");
    const marker_edit_action_t action{update_bookmark_action_t{
        chart_id, bookmark_id, std::string(label), bookmark->anchor, bookmark->position,
        bookmark->audio_milliseconds}};
    const auto applied = apply_marker_edit(history, action);
    if (!applied.applied) {
        return {.status = applied.diagnostics.empty() ? "Bookmark label was unchanged."
                                                      : applied.diagnostics.front().message,
                .diagnostics = applied.diagnostics,
                .severity =
                    to_notification_severity(applied.diagnostics, notification_severity_t::info)};
    }
    return {.accepted = true, .document_changed = true, .status = "Bookmark renamed.",
            .severity = notification_severity_t::success};
}

} // namespace wormedit::sys
