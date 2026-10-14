#include "sys/projection_notification_feedback.h"
#include "sys/compose_measure_commands.h"

#include <algorithm>
#include <wormedit/core/snap.h>
#include <cstdint>
#include <limits>
#include <optional>

namespace wormedit::sys {
namespace {

auto rejection(const document_command_result_t& result, const char* fallback, notification_severity_t* severity) -> std::string {
    if (!result.diagnostics.empty())
        set_feedback_severity(severity, projection_diagnostic_severity(result.diagnostics.front().severity));
    return result.diagnostics.empty() ? fallback : result.diagnostics.front().message;
}

auto insert_measure(document_history_t& history, const chart_document_t& chart, std::size_t cursor_index,
                    bool after, notification_severity_t* severity) -> std::string {
    const auto result = apply_measure_edit(
        history, insert_measure_action_t{.chart = chart.id,
                                         .before_index = cursor_index + (after ? 1 : 0),
                                         .definition = chart.measures[cursor_index].definition});
    if (result) {
        set_feedback_severity(severity, notification_severity_t::success);
        return "Measure inserted.";
    }
    return rejection(result, "The measure could not be inserted.", severity);
}

auto remove_measure(document_history_t& history, editor_interaction_state_t& state,
                    const chart_document_t& chart, std::size_t cursor_index,
                    notification_severity_t* severity) -> std::string {
    const auto neighbor = cursor_index > 0 ? cursor_index - 1 : cursor_index + 1;
    if (neighbor >= chart.measures.size()) return "A chart needs at least one measure.";
    const auto target = local_position_t{chart.measures[neighbor].id, 0};
    const auto removed = chart.measures[cursor_index].id;
    const auto result =
        apply_measure_edit(history, delete_measure_action_t{.chart = chart.id, .measure = removed});
    if (!result) return rejection(result, "The measure could not be removed.", severity);
    place_cursor_at(state, target);
    set_feedback_severity(severity, notification_severity_t::success);
    return "Measure removed.";
}

// The removed measure took its notes, selected objects and retained spans with it; keeping
// them would make the next field scene reject the view (selection larger than the chart).
void clear_selection_after_removal(editor_interaction_state_t& state) {
    state.selected_notes.clear();
    state.selected_objects.clear();
    state.retained_selection_range.reset();
    state.retained_canvas_selection_range.reset();
    state.selected_canvas_measure.reset();
}

auto remove_measure_with_contents(document_history_t& history, editor_interaction_state_t& state,
                                  const chart_document_t& chart, std::size_t cursor_index,
                                  notification_severity_t* severity) -> std::string {
    if (chart.measures.size() < 2) return "A chart needs at least one measure.";
    // The following measure receives the timing events; the last measure hands them back.
    const auto neighbor = cursor_index + 1 < chart.measures.size() ? cursor_index + 1 : cursor_index - 1;
    const auto target = local_position_t{chart.measures[neighbor].id, 0};
    const auto result = apply_measure_edit(
        history, remove_measure_with_contents_action_t{.chart = chart.id,
                                                       .measure = chart.measures[cursor_index].id});
    if (!result) return rejection(result, "The measure could not be removed.", severity);
    place_cursor_at(state, target);
    clear_selection_after_removal(state);
    set_feedback_severity(severity, notification_severity_t::success);
    return "Measure removed with its contents.";
}

// Digits only, non-zero, 32 bits: the signature fields are typed text.
auto parse_meter_part(std::string_view text) -> std::optional<std::uint32_t> {
    if (text.empty() || text.size() > 10) return std::nullopt;
    std::uint64_t value = 0;
    for (const auto character : text) {
        if (character < '0' || character > '9') return std::nullopt;
        value = value * 10 + static_cast<std::uint64_t>(character - '0');
    }
    if (value == 0 || value > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    return static_cast<std::uint32_t>(value);
}

} // namespace

auto apply_cursor_measure_split(document_history_t& history, editor_interaction_state_t& state,
                                 notification_severity_t* severity) -> std::string {
    set_feedback_severity(severity, notification_severity_t::warning);
    if (state.virtual_bms_cursor_row) return "Materialize the cursor measure before splitting it.";
    if (state.cursor.row == 0) {
        set_feedback_severity(severity, notification_severity_t::hint);
        return "The cursor is already on a measure head.";
    }
    const auto chart = history.document.find_chart(state.chart);
    if (chart == nullptr) return "The selected chart is unavailable.";
    const auto index = build_measure_index(*chart);
    const auto* before = std::get_if<measure_index_t>(&index);
    const auto row = before ? (*before).row_of(state.cursor) : std::nullopt;
    if (!row) return "The cursor is outside the authored chart.";
    const auto result =
        apply_measure_edit(history, split_measure_action_t{state.chart, state.cursor});
    if (!result) return rejection(result, "The measure could not be split.", severity);
    const auto current = history.document.find_chart(state.chart);
    const auto after = build_measure_index(*current);
    if (const auto* measures = std::get_if<measure_index_t>(&after))
        if (const auto position = position_at_absolute_row(*measures, *row)) place_cursor_at(state, *position);
    state.retained_selection_range.reset();
    state.retained_canvas_selection_range.reset();
    state.selected_canvas_measure.reset();
    set_feedback_severity(severity, notification_severity_t::success);
    return "Measure split at cursor.";
}

auto apply_cursor_measure_meter(document_history_t& history, editor_interaction_state_t& state,
                                std::string_view numerator, std::string_view denominator,
                                notification_severity_t* severity)
    -> std::string {
    set_feedback_severity(severity, notification_severity_t::warning);
    const auto top = parse_meter_part(numerator);
    const auto bottom = parse_meter_part(denominator);
    if (!top || !bottom) return "A meter needs a non-zero numerator and denominator.";
    const auto found = history.document.find_chart(state.chart);
    if (found == nullptr) return "The selected difficulty is no longer available.";
    const auto measure = found->find_measure(state.cursor.measure);
    if (measure == nullptr) return "The cursor is not inside a materialized measure.";
    const auto result = apply_measure_edit(
        history,
        resize_measure_keep_rows_action_t{
            .chart = found->id,
            .measure_indices = {static_cast<std::size_t>(measure - found->measures.data())},
            .definition = {.length = {*top, *bottom}, .meter = meter_t{*top, *bottom}}});
    if (result) {
        set_feedback_severity(severity, notification_severity_t::success);
        return "Meter updated.";
    }
    set_feedback_severity(severity, result.diagnostics.empty() ? notification_severity_t::info
        : projection_diagnostic_severity(result.diagnostics.front().severity));
    return result.diagnostics.empty() ? "Meter was unchanged." : result.diagnostics.front().message;
}

auto apply_measures_meter(document_history_t& history, editor_interaction_state_t& state,
                          std::span<const std::uint64_t> measure_ids, std::string_view numerator,
                          std::string_view denominator, notification_severity_t* severity)
    -> std::string {
    set_feedback_severity(severity, notification_severity_t::warning);
    const auto top = parse_meter_part(numerator);
    const auto bottom = parse_meter_part(denominator);
    if (!top || !bottom) return "A meter needs a non-zero numerator and denominator.";
    if (measure_ids.empty()) return "Select at least one measure first.";
    // The IDs are chart positions; objects keep their rows from the start of the song.
    resize_measure_keep_rows_action_t action{
        .chart = state.chart,
        .definition = {.length = {*top, *bottom}, .meter = meter_t{*top, *bottom}}};
    for (const auto index : measure_ids) action.measure_indices.push_back(static_cast<std::size_t>(index));
    const auto result = apply_measure_edit(history, action);
    if (result) {
        set_feedback_severity(severity, notification_severity_t::success);
        return measure_ids.size() == 1 ? "Meter updated."
                                       : "Meter updated on " + std::to_string(measure_ids.size()) + " measures.";
    }
    set_feedback_severity(severity, result.diagnostics.empty() ? notification_severity_t::info
        : projection_diagnostic_severity(result.diagnostics.front().severity));
    return result.diagnostics.empty() ? "Meter was unchanged." : result.diagnostics.front().message;
}

auto apply_bar_line_move(document_history_t& history, editor_interaction_state_t& state,
                        std::uint64_t measure_index, meter_t first, meter_t second,
                        notification_severity_t* severity) -> std::string {
    set_feedback_severity(severity, notification_severity_t::warning);
    if (first.numerator == 0 || first.denominator == 0 || second.numerator == 0 || second.denominator == 0)
        return "A meter needs a non-zero numerator and denominator.";
    const auto result = apply_measure_edit(
        history, move_bar_line_action_t{
                     .chart = state.chart,
                     .measure_index = static_cast<std::size_t>(measure_index),
                     .before = {.length = {first.numerator, first.denominator}, .meter = first},
                     .after = {.length = {second.numerator, second.denominator}, .meter = second}});
    if (result) {
        set_feedback_severity(severity, notification_severity_t::success);
        return "Bar line moved.";
    }
    set_feedback_severity(severity, result.diagnostics.empty() ? notification_severity_t::info
        : projection_diagnostic_severity(result.diagnostics.front().severity));
    return result.diagnostics.empty() ? "Bar line was unchanged." : result.diagnostics.front().message;
}

auto apply_cursor_measure_command(document_history_t& history, editor_interaction_state_t& state,
                                  cursor_measure_command_t command, notification_severity_t* severity) -> std::string {
    set_feedback_severity(severity, notification_severity_t::warning);
    const auto found = history.document.find_chart(state.chart);
    if (found == nullptr) return "The selected difficulty is no longer available.";
    const auto measure = found->find_measure(state.cursor.measure);
    if (measure == nullptr) return "The cursor is not inside a materialized measure.";
    const auto index = static_cast<std::size_t>(measure - found->measures.data());
    // Copy: the history edit replaces the chart storage this reference points into.
    const auto chart = *found;
    switch (command) {
    case cursor_measure_command_t::insert_before: return insert_measure(history, chart, index, false, severity);
    case cursor_measure_command_t::insert_after: return insert_measure(history, chart, index, true, severity);
    case cursor_measure_command_t::remove_empty: return remove_measure(history, state, chart, index, severity);
    case cursor_measure_command_t::remove_with_contents:
        return remove_measure_with_contents(history, state, chart, index, severity);
    }
    return "Unknown measure command.";
}

} // namespace wormedit::sys
