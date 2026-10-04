#include "sys/compose_measure_commands.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace wormedit::sys {
namespace {

void set_feedback_severity(notification_severity_t* output, notification_severity_t value) {
    if (output) *output = value;
}
auto diagnostic_feedback_severity(diagnostic_severity_t value) -> notification_severity_t {
    switch (value) {
    case diagnostic_severity_t::info: return notification_severity_t::info;
    case diagnostic_severity_t::warning: return notification_severity_t::warning;
    case diagnostic_severity_t::error: return notification_severity_t::error;
    }
    return notification_severity_t::error;
}

auto rejection(const document_command_result_t& result, const char* fallback, notification_severity_t* severity) -> std::string {
    if (!result.diagnostics.empty())
        set_feedback_severity(severity, diagnostic_feedback_severity(result.diagnostics.front().severity));
    return result.diagnostics.empty() ? fallback : result.diagnostics.front().message;
}

auto insert_measure(document_history_t& history, const chart_document_t& chart, std::size_t cursor_index,
                    bool after, notification_severity_t* severity) -> std::string {
    const auto result = apply_measure_edit(
        history, history.document.revision,
        insert_measure_action_t{.chart = chart.id,
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
    const auto result = apply_measure_edit(history, history.document.revision,
                                           delete_measure_action_t{.chart = chart.id, .measure = removed});
    if (!result) return rejection(result, "The measure could not be removed.", severity);
    place_cursor_at(state, target);
    set_feedback_severity(severity, notification_severity_t::success);
    return "Measure removed.";
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

auto apply_cursor_measure_meter(document_history_t& history, editor_interaction_state_t& state,
                                std::string_view numerator, std::string_view denominator,
                                notification_severity_t* severity)
    -> std::string {
    set_feedback_severity(severity, notification_severity_t::warning);
    const auto top = parse_meter_part(numerator);
    const auto bottom = parse_meter_part(denominator);
    if (!top || !bottom) return "A meter needs a non-zero numerator and denominator.";
    const auto found = std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    if (found == history.document.charts.end()) return "The selected difficulty is no longer available.";
    const auto measure = std::ranges::find(found->measures, state.cursor.measure, &measure_chunk_t::id);
    if (measure == found->measures.end()) return "The cursor is not inside a materialized measure.";
    const auto result = apply_measure_edit(
        history, history.document.revision,
        update_measure_definition_action_t{.chart = found->id,
                                           .measure = measure->id,
                                           .definition = {.length = {*top, *bottom},
                                                          .meter = meter_t{*top, *bottom}}});
    if (result) {
        set_feedback_severity(severity, notification_severity_t::success);
        return "Meter updated.";
    }
    set_feedback_severity(severity, result.diagnostics.empty() ? notification_severity_t::info
        : diagnostic_feedback_severity(result.diagnostics.front().severity));
    return result.diagnostics.empty() ? "Meter was unchanged." : result.diagnostics.front().message;
}

auto apply_cursor_measure_command(document_history_t& history, editor_interaction_state_t& state,
                                  cursor_measure_command_t command, notification_severity_t* severity) -> std::string {
    set_feedback_severity(severity, notification_severity_t::warning);
    const auto found = std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    if (found == history.document.charts.end()) return "The selected difficulty is no longer available.";
    const auto measure = std::ranges::find(found->measures, state.cursor.measure, &measure_chunk_t::id);
    if (measure == found->measures.end()) return "The cursor is not inside a materialized measure.";
    const auto index = static_cast<std::size_t>(measure - found->measures.begin());
    // Copy: the history edit replaces the chart storage this reference points into.
    const auto chart = *found;
    switch (command) {
    case cursor_measure_command_t::insert_before: return insert_measure(history, chart, index, false, severity);
    case cursor_measure_command_t::insert_after: return insert_measure(history, chart, index, true, severity);
    case cursor_measure_command_t::remove_empty: return remove_measure(history, state, chart, index, severity);
    }
    return "Unknown measure command.";
}

} // namespace wormedit::sys
