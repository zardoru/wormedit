#include "sys/compose_label_search.h"

#include <wormedit/core/grid.h>
#include <wormedit/core/note_label_editor.h>

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
    for (const auto& measure : chart.measures) {
        const auto found = std::ranges::find(measure.notes, id, &note_t::id);
        if (found != measure.notes.end()) return found->position;
    }
    return std::nullopt;
}

auto locate_matches(const chart_document_t& chart, const measure_index_t& measures,
                    const std::vector<object_id_t>& ids) -> std::vector<located_match_t> {
    std::vector<located_match_t> located;
    for (const auto id : ids) {
        const auto position = note_position(chart, id);
        const auto row = position ? absolute_row(measures, *position) : std::nullopt;
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

auto find_label_search_hit(const chart_document_t& chart, std::string_view text,
                           local_position_t cursor, label_search_step_t step)
    -> std::optional<label_search_hit_t> {
    const auto ids = find_notes_by_label(chart, text);
    if (ids.empty()) return std::nullopt;
    const auto built = build_measure_index(chart);
    const auto* measures = std::get_if<measure_index_t>(&built);
    if (!measures) return std::nullopt;
    auto located = locate_matches(chart, *measures, ids);
    if (located.empty()) return std::nullopt;
    std::ranges::stable_sort(located, {}, &located_match_t::row);
    const auto cursor_row = absolute_row(*measures, cursor).value_or(0);
    return label_search_hit_t{.matches = ids, .target = pick_target(located, cursor_row, step).position};
}

auto run_label_search(document_history_t& history, editor_interaction_state_t& state,
                      std::string_view text, label_search_step_t step) -> std::string {
    const auto chart = std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    if (chart == history.document.charts.end()) return "The selected difficulty is no longer available.";
    if (text.empty()) return "Type a label to search for.";
    const auto hit = find_label_search_hit(*chart, text, state.cursor, step);
    if (!hit) return "No notes have a label containing that text.";
    if (step == label_search_step_t::first) {
        const auto selected = dispatch_editor_action(history, state, select_editor_notes_action_t{hit->matches});
        if (!selected.accepted) return "The matching notes could not be selected.";
    }
    const auto jumped = dispatch_editor_action(
        history, state, jump_edit_cursor_action_t{hit->target, history.document.revision});
    if (!jumped.accepted) return "The cursor could not move to the matching note.";
    return step == label_search_step_t::first
               ? "Selected " + std::to_string(hit->matches.size()) + " note(s) with that label."
               : "Moved to the matching note.";
}

} // namespace wormedit::sys
