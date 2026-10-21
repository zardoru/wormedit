#include <wormedit/core/timing.h>
#include "sys/notifications/projection_notification_feedback.h"
#include "sys/compose_label_search.h"

#include <wormedit/core/grid.h>
#include <wormedit/core/note_label_editor.h>
#include <wormedit/core/interaction_dispatch.h>

#include <algorithm>
#include <variant>

namespace wormedit::sys {
namespace {

struct located_match_t {
    object_id_t id;
    local_position_t position;
    std::uint64_t row{};
};

auto note_position(const chart_document_t& chart, object_id_t id) -> std::optional<local_position_t> {
    const auto* note = chart.find_note(id);
    return note ? std::optional{note->position} : std::nullopt;
}

auto locate_matches(const chart_document_t& chart, const measure_index_t& measures,
                    const std::vector<object_id_t>& ids) -> std::vector<located_match_t> {
    std::vector<located_match_t> located;
    for (const auto id : ids) {
        const auto position = note_position(chart, id);
        const auto row = position ? (measures).row_of(*position) : std::nullopt;
        if (row) located.push_back({id, *position, *row});
    }
    return located;
}

auto pick_target(const std::vector<located_match_t>& located, std::uint64_t cursor_row,
                 label_search_step_t step) -> const located_match_t& {
    if (step == label_search_step_t::first) return located.front();
    if (step == label_search_step_t::next) {
        const auto after = std::ranges::find_if(
            located, [&](const auto& match) { return match.row > cursor_row; });
        return after == located.end() ? located.front() : *after;
    }
    const auto before = std::ranges::find_if(located.rbegin(), located.rend(), [&](const auto& match) {
        return match.row < cursor_row;
    });
    return before == located.rend() ? located.back() : *before;
}

} // namespace

auto find_label_search_hit(const chart_document_t& chart, const measure_index_t& measures,
                           std::string_view text,
                           local_position_t cursor, label_search_step_t step)
    -> std::optional<label_search_hit_t> {
    const auto ids = find_notes_by_label(chart, text);
    if (ids.empty()) return std::nullopt;
    auto located = locate_matches(chart, measures, ids);
    if (located.empty()) return std::nullopt;
    std::ranges::stable_sort(located, {}, &located_match_t::row);
    const auto cursor_row = measures.row_of(cursor).value_or(0);
    return label_search_hit_t{.matches = ids, .target = pick_target(located, cursor_row, step).position};
}

auto run_label_search(document_history_t& history, editor_interaction_state_t& state,
                      std::string_view text, label_search_step_t step,
                      notification_severity_t* severity) -> std::string {
    set_feedback_severity(severity, notification_severity_t::warning);
    const auto chart = history.document.find_chart(state.chart);
    if (chart == nullptr) return "The selected difficulty is no longer available.";
    if (text.empty()) return "Type a label to search for.";
    const auto& measures = history.indices(chart->id)->measures;
    const auto hit = measures ? find_label_search_hit(*chart, *measures, text, state.cursor, step)
                              : std::nullopt;
    if (!hit) {
        set_feedback_severity(severity, notification_severity_t::hint);
        return "No notes have a label containing that text.";
    }
    if (step == label_search_step_t::first) {
        const auto selected = dispatch_editor_action(history, state, select_editor_notes_action_t{hit->matches});
        if (!selected.accepted) {
            if (!selected.diagnostics.empty())
                set_feedback_severity(
                    severity, to_notification_severity(selected.diagnostics.front().severity));
            return "The matching notes could not be selected.";
        }
    }
    const auto jumped = dispatch_editor_action(
        history, state, jump_edit_cursor_action_t{hit->target, history.document.revision});
    if (!jumped.accepted) {
        if (!jumped.diagnostics.empty())
            set_feedback_severity(severity,
                                  to_notification_severity(jumped.diagnostics.front().severity));
        return "The cursor could not move to the matching note.";
    }
    set_feedback_severity(severity, notification_severity_t::hint);
    return step == label_search_step_t::first
               ? "Selected " + std::to_string(hit->matches.size()) + " note(s) with that label."
               : "Moved to the matching note.";
}

} // namespace wormedit::sys
