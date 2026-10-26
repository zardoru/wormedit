#include "sys/bookmarks/bookmark_navigation.h"
#include <limits>
#include "sys/commands/native_chart_edit_action.h"

#include <wormedit/core/marker_editor.h>

#include <algorithm>
#include <utility>

namespace wormedit::sys {
namespace {

auto find_bookmark(document_history_t& history, chart_id_t chart_id, object_id_t bookmark_id)
    -> const bookmark_t* {
    const auto chart = history.read_chart(chart_id);
    if (chart == nullptr)
        return nullptr;
    const auto bookmark = std::ranges::find(chart->bookmarks, bookmark_id, &bookmark_t::id);
    return bookmark == chart->bookmarks.end() ? nullptr : &*bookmark;
}

} // namespace

auto bookmark_navigation_t::rename_bookmark(document_history_t& history,
                                            std::uint64_t expected_revision, chart_id_t chart_id,
                                            object_id_t bookmark_id, std::string_view label)
    -> compose_action_outcome_t {
    if (!document_stamp_t{.project = history.document.id, .revision = expected_revision}.is_current(
            history.document))
        return compose_action_outcome_t::blocked(stale_action_message("Bookmark rename"));
    if (!chart_id || !bookmark_id)
        return compose_action_outcome_t::blocked("Choose a bookmark to rename.");
    const auto* bookmark = find_bookmark(history, chart_id, bookmark_id);
    if (!bookmark)
        return compose_action_outcome_t::blocked("That bookmark is no longer available.");
    const marker_edit_action_t action{wormedit::update_edit_t<wormedit::bookmark_edit_value_t>{
        .chart = chart_id,
        .id = bookmark_id,
        .value = {.label = std::string(label),
                  .anchor = bookmark->anchor,
                  .position = bookmark->position,
                  .audio_milliseconds = bookmark->audio_milliseconds}}};

    return run_native_chart_edit<marker_edit_action_t>(
        history, action,
        compose_action_description_t{.success = "Bookmark renamed.",
                                     .unchanged = "Bookmark label was unchanged.",
                                     .rebind_failure =
                                         "Bookmark renamed, but workspace rebinding failed.",
                                     .is_unchanged_accepted = false});
}

auto bookmark_navigation_t::reorder_bookmark(document_history_t& history, std::uint64_t revision,
    chart_id_t chart, const bookmark_reorder_t& request) -> compose_action_outcome_t {
    if (revision != history.document.revision)
        return compose_action_outcome_t::blocked(stale_action_message("Bookmark reorder"));
    const auto page = bookmark_chooser_view_t::from(history.document, chart, 0,
        std::numeric_limits<std::size_t>::max(), {}, {}, request.sort, request.descending);
    if (!page.is_success()) return compose_action_outcome_t::blocked(page.status);
    std::vector<object_id_t> order;
    for (const auto& row : page.rows) order.push_back(row.bookmark_id);
    if (request.bookmark == request.target || !std::ranges::contains(order, request.bookmark) ||
        !std::ranges::contains(order, request.target))
        return compose_action_outcome_t::blocked("Choose two different available bookmarks.");
    std::erase(order, request.bookmark);
    auto destination = std::ranges::find(order, request.target);
    if (request.place_after) ++destination;
    order.insert(destination, request.bookmark);
    return compose_action_outcome_t::from(apply_bookmark_order_edit(history, chart, order),
        compose_action_description_t{.success = "Bookmarks rearranged.",
            .unchanged = "Bookmark order is unchanged.",
            .rebind_failure = "Bookmark order changed, but workspace rebinding failed."});
}

} // namespace wormedit::sys
