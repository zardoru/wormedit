#include "sys/compose_measure_commands.h"

#include <algorithm>

namespace wormedit::sys {
namespace {

auto rejection(const document_command_result_t& result, const char* fallback) -> std::string {
    return result.diagnostics.empty() ? fallback : result.diagnostics.front().message;
}

auto insert_measure(document_history_t& history, const chart_document_t& chart, std::size_t cursor_index,
                    bool after) -> std::string {
    const auto result = apply_measure_edit(
        history, history.document.revision,
        insert_measure_action_t{.chart = chart.id,
                                .before_index = cursor_index + (after ? 1 : 0),
                                .definition = chart.measures[cursor_index].definition});
    return result ? "Measure inserted." : rejection(result, "The measure could not be inserted.");
}

auto remove_measure(document_history_t& history, editor_interaction_state_t& state,
                    const chart_document_t& chart, std::size_t cursor_index) -> std::string {
    const auto neighbor = cursor_index > 0 ? cursor_index - 1 : cursor_index + 1;
    if (neighbor >= chart.measures.size()) return "A chart needs at least one measure.";
    const auto target = local_position_t{chart.measures[neighbor].id, 0};
    const auto removed = chart.measures[cursor_index].id;
    const auto result = apply_measure_edit(history, history.document.revision,
                                           delete_measure_action_t{.chart = chart.id, .measure = removed});
    if (!result) return rejection(result, "The measure could not be removed.");
    place_cursor_at(state, target);
    return "Measure removed.";
}

} // namespace

auto apply_cursor_measure_command(document_history_t& history, editor_interaction_state_t& state,
                                  cursor_measure_command_t command) -> std::string {
    const auto found = std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    if (found == history.document.charts.end()) return "The selected difficulty is no longer available.";
    const auto measure = std::ranges::find(found->measures, state.cursor.measure, &measure_chunk_t::id);
    if (measure == found->measures.end()) return "The cursor is not inside a materialized measure.";
    const auto index = static_cast<std::size_t>(measure - found->measures.begin());
    // Copy: the history edit replaces the chart storage this reference points into.
    const auto chart = *found;
    switch (command) {
    case cursor_measure_command_t::insert_before: return insert_measure(history, chart, index, false);
    case cursor_measure_command_t::insert_after: return insert_measure(history, chart, index, true);
    case cursor_measure_command_t::remove_empty: return remove_measure(history, state, chart, index);
    }
    return "Unknown measure command.";
}

} // namespace wormedit::sys
