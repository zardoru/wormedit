#include "sys/bookmark_navigation.h"

#include <wormedit/core/marker_editor.h>

#include <algorithm>
#include <utility>

namespace wormedit::sys {
namespace {
auto rename_feedback_severity(const diagnostic_list_t& diagnostics) -> notification_severity_t {
    if (diagnostics.empty()) return notification_severity_t::info;
    switch (diagnostics.front().severity) {
    case diagnostic_severity_t::info: return notification_severity_t::info;
    case diagnostic_severity_t::warning: return notification_severity_t::warning;
    case diagnostic_severity_t::error: return notification_severity_t::error;
    }
    return notification_severity_t::error;
}

auto reject(std::string message) -> bookmark_rename_outcome_t {
    return {.status = std::move(message)};
}

auto find_bookmark(document_history_t& history, chart_id_t chart_id, object_id_t bookmark_id)
    -> bookmark_t* {
    const auto chart = std::ranges::find(history.document.charts, chart_id,
                                         &chart_document_t::id);
    if (chart == history.document.charts.end()) return nullptr;
    const auto bookmark = std::ranges::find(chart->bookmarks, bookmark_id, &bookmark_t::id);
    return bookmark == chart->bookmarks.end() ? nullptr : &*bookmark;
}

} // namespace

auto bookmark_navigation_t::rename_bookmark(document_history_t& history, std::uint64_t expected_revision,
                     chart_id_t chart_id, object_id_t bookmark_id, std::string_view label)
    -> bookmark_rename_outcome_t {
    if (history.document.revision != expected_revision)
        return reject("The chart changed before the bookmark rename was applied.");
    if (!chart_id || !bookmark_id) return reject("Choose a bookmark to rename.");
    const auto* bookmark = find_bookmark(history, chart_id, bookmark_id);
    if (!bookmark) return reject("That bookmark is no longer available.");
    const marker_edit_action_t action{update_bookmark_action_t{
        chart_id, bookmark_id, std::string(label), bookmark->anchor, bookmark->position,
        bookmark->audio_milliseconds}};
    const auto applied = apply_marker_edit(history, expected_revision, action);
    if (!applied.applied) {
        return {.status = applied.diagnostics.empty()
                              ? "Bookmark label was unchanged."
                              : applied.diagnostics.front().message,
                .diagnostics = applied.diagnostics,
                .severity = rename_feedback_severity(applied.diagnostics)};
    }
    return {.accepted = true, .document_changed = true, .status = "Bookmark renamed.",
            .severity = notification_severity_t::success};
}

} // namespace wormedit::sys
