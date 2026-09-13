#include <wormedit/core/pause_warp_editor.h>

#include <algorithm>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace wormedit {
namespace {

auto error(diagnostic_code_t code, std::string message) -> diagnostic_t {
    return {.severity = diagnostic_severity_t::error, .code = code, .message = std::move(message)};
}

auto reject(document_command_result_t& result, diagnostic_code_t code, std::string message)
    -> void {
    result.diagnostics.push_back(error(code, std::move(message)));
}

auto valid_pause_kind(pause_kind_t kind) -> bool {
    switch (kind) {
    case pause_kind_t::stop:
    case pause_kind_t::delay:
        return true;
    }
    return false;
}

auto valid_pause_unit(pause_unit_t unit) -> bool {
    switch (unit) {
    case pause_unit_t::seconds:
    case pause_unit_t::milliseconds:
    case pause_unit_t::bms_stop_ticks:
    case pause_unit_t::beats:
        return true;
    }
    return false;
}

auto validate_pause_values(pause_kind_t kind, signed_rational_t duration, pause_unit_t unit,
                           document_command_result_t& result) -> bool {
    if (!valid_pause_kind(kind)) {
        reject(result, diagnostic_code_t::invalid_resolution, "pause event has an unknown kind");
        return false;
    }
    if (!valid_pause_unit(unit) ||
        (unit == pause_unit_t::bms_stop_ticks && kind != pause_kind_t::stop)) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "BMS STOP ticks are valid only for stop events; pause kind and unit must agree");
        return false;
    }
    if (duration.denominator == 0 || duration.numerator < 0) {
        reject(result, diagnostic_code_t::invalid_rational,
               "pause duration must be a nonnegative exact rational");
        return false;
    }
    return true;
}

auto valid_warp_encoding(warp_provenance_t encoding) -> bool {
    switch (encoding) {
    case warp_provenance_t::native:
    case warp_provenance_t::negative_bpm:
    case warp_provenance_t::negative_stop:
        return true;
    }
    return false;
}

auto validate_position(const chart_document_t& chart, local_position_t position,
                       document_command_result_t& result, std::string_view event_name) -> bool {
    const auto measures = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(measures)) {
        result.diagnostics = std::get<diagnostic_list_t>(measures);
        return false;
    }
    if (!absolute_row(std::get<measure_index_t>(measures), position).has_value()) {
        reject(result, diagnostic_code_t::row_out_of_range,
               std::string(event_name) +
                   " position is outside the selected chart's measure geometry");
        return false;
    }
    return true;
}

auto validate_span(const chart_document_t& chart, position_span_t span,
                   document_command_result_t& result) -> bool {
    const auto measures = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(measures)) {
        result.diagnostics = std::get<diagnostic_list_t>(measures);
        return false;
    }
    const auto& index = std::get<measure_index_t>(measures);
    const auto begin = absolute_row(index, span.begin);
    const auto end = absolute_row(index, span.end);
    if (!begin.has_value() || !end.has_value()) {
        reject(result, diagnostic_code_t::row_out_of_range,
               "warp span is outside the selected chart's measure geometry");
        return false;
    }
    if (*end <= *begin) {
        reject(result, diagnostic_code_t::invalid_hold,
               "warp end must be strictly after its start");
        return false;
    }
    return true;
}

struct object_id_allocation_t {
    std::optional<object_id_t> id;
    bool over_limit{};
};

auto next_chart_object_id(const chart_document_t& chart, std::size_t maximum_chart_objects)
    -> object_id_allocation_t {
    std::size_t object_count{};
    std::uint64_t maximum_id{};
    const auto record = [&](object_id_t id) {
        if (object_count >= maximum_chart_objects) {
            return false;
        }
        ++object_count;
        maximum_id = std::max(maximum_id, id.value);
        return true;
    };
    const auto scan = [&record](const auto& values) {
        for (const auto& value : values) {
            if (!record(value.id)) {
                return false;
            }
        }
        return true;
    };

    auto within_limit =
        scan(chart.bgm_source_lanes) && scan(chart.lane_layouts) && scan(chart.bpms) &&
        scan(chart.pauses) && scan(chart.warps) && scan(chart.scrolls) && scan(chart.speeds) &&
        scan(chart.kiai) && scan(chart.timing_anchors) && scan(chart.bookmarks) &&
        scan(chart.sections) && scan(chart.bga_events) && scan(chart.sound_channels);
    if (within_limit) {
        for (const auto& measure : chart.measures) {
            if (!scan(measure.notes) || !scan(measure.bgm_events)) {
                within_limit = false;
                break;
            }
        }
    }
    if (within_limit) {
        for (const auto& channel : chart.sound_channels) {
            if (!scan(channel.events)) {
                within_limit = false;
                break;
            }
        }
    }
    if (!within_limit || object_count >= maximum_chart_objects) {
        return {.over_limit = true};
    }
    if (maximum_id == std::numeric_limits<std::uint64_t>::max()) {
        return {};
    }
    return {.id = object_id_t{maximum_id + 1}};
}

auto chart_object_id_count(const chart_document_t& chart, object_id_t sought) -> std::size_t {
    auto count = std::size_t{};
    const auto scan = [&](const auto& values) {
        for (const auto& value : values) {
            if (value.id == sought) {
                ++count;
            }
        }
    };
    scan(chart.bgm_source_lanes);
    scan(chart.lane_layouts);
    scan(chart.bpms);
    scan(chart.pauses);
    scan(chart.warps);
    scan(chart.scrolls);
    scan(chart.speeds);
    scan(chart.kiai);
    scan(chart.timing_anchors);
    scan(chart.bookmarks);
    scan(chart.sections);
    scan(chart.bga_events);
    scan(chart.sound_channels);
    for (const auto& measure : chart.measures) {
        scan(measure.notes);
        scan(measure.bgm_events);
    }
    for (const auto& channel : chart.sound_channels) {
        scan(channel.events);
    }
    return count;
}

template <typename event_t>
auto next_source_order(const std::vector<event_t>& events) -> std::optional<std::size_t> {
    if (events.empty()) {
        return 0;
    }
    auto maximum = std::size_t{};
    for (const auto& event : events) {
        maximum = std::max(maximum, event.provenance.source_order);
    }
    if (maximum == std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }
    return maximum + 1;
}

template <typename event_t>
auto unique_event(const chart_document_t& chart, std::vector<event_t>& events, object_id_t id,
                  std::string_view name, document_command_result_t& result) -> event_t* {
    const auto found = std::ranges::find(events, id, &event_t::id);
    if (found == events.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               std::string(name) + " command refers to an unknown event");
        return nullptr;
    }
    if (chart_object_id_count(chart, id) != 1) {
        reject(result, diagnostic_code_t::duplicate_id,
               std::string(name) + " event ID is ambiguous because it appears more than once");
        return nullptr;
    }
    return &*found;
}

auto matching_pause_projection(chart_document_t& chart, const pause_event_t& event,
                               document_command_result_t& result) -> bms_pause_projection_t* {
    const auto found =
        std::ranges::find(chart.bms_pause_projections, event.id, &bms_pause_projection_t::event);
    if (found == chart.bms_pause_projections.end()) {
        return nullptr;
    }
    if (std::ranges::count(chart.bms_pause_projections, event.id, &bms_pause_projection_t::event) !=
        1) {
        reject(result, diagnostic_code_t::duplicate_id,
               "BMS pause projection is ambiguous because it appears more than once");
        return nullptr;
    }
    if (found->source_order != event.provenance.source_order) {
        reject(result, diagnostic_code_t::invalid_id,
               "BMS pause projection source identity does not match its event");
        return nullptr;
    }
    return &*found;
}

} // namespace

auto apply_pause_warp_edit(document_history_t& history, std::uint64_t expected_revision,
                           const pause_warp_edit_action_t& action, pause_warp_edit_limits_t limits)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id,
               "pause/warp editor document revision is stale");
        return result;
    }
    if (limits.maximum_pause_events == 0 || limits.maximum_warp_events == 0) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "pause and warp event caps must be positive");
        return result;
    }

    auto proposed = history.document;
    std::string command_label;
    bool changed{};
    std::visit(
        [&](const auto& edit) {
            using edit_t = std::remove_cvref_t<decltype(edit)>;
            const auto chart =
                std::ranges::find(proposed.charts, edit.chart, &chart_document_t::id);
            if (!edit.chart || chart == proposed.charts.end()) {
                reject(result, diagnostic_code_t::invalid_id,
                       "pause/warp edit refers to an unknown chart");
                return;
            }

            if constexpr (std::is_same_v<edit_t, insert_pause_event_action_t> ||
                          std::is_same_v<edit_t, update_pause_event_action_t> ||
                          std::is_same_v<edit_t, delete_pause_event_action_t>) {
                if (chart->pauses.size() > limits.maximum_pause_events) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           "selected chart exceeds the pause event cap");
                    return;
                }
                if constexpr (std::is_same_v<edit_t, insert_pause_event_action_t>) {
                    if (chart->pauses.size() >= limits.maximum_pause_events) {
                        reject(result, diagnostic_code_t::invalid_resolution,
                               "pause insertion would exceed the configured event cap");
                        return;
                    }
                    if (!validate_position(*chart, edit.position, result, "pause event") ||
                        !validate_pause_values(edit.kind, edit.duration, edit.source_unit,
                                               result)) {
                        return;
                    }
                    const auto allocation =
                        next_chart_object_id(*chart, limits.maximum_chart_objects);
                    if (!allocation.id.has_value()) {
                        reject(result, diagnostic_code_t::invalid_resolution,
                               allocation.over_limit
                                   ? "selected chart exceeds the object ID allocation cap"
                                   : "chart object ID sequence is exhausted");
                        return;
                    }
                    const auto source_order = next_source_order(chart->pauses);
                    if (!source_order.has_value()) {
                        reject(result, diagnostic_code_t::invalid_resolution,
                               "pause source-order sequence is exhausted");
                        return;
                    }
                    chart->pauses.push_back({.id = *allocation.id,
                                             .position = edit.position,
                                             .kind = edit.kind,
                                             .duration = edit.duration,
                                             .source_unit = edit.source_unit,
                                             .provenance = {.source_order = *source_order}});
                    command_label = "Insert pause event";
                    changed = true;
                } else if constexpr (std::is_same_v<edit_t, update_pause_event_action_t>) {
                    if (!edit.event) {
                        reject(result, diagnostic_code_t::invalid_id,
                               "pause update uses the reserved zero event ID");
                        return;
                    }
                    auto* event = unique_event(*chart, chart->pauses, edit.event, "pause", result);
                    if (event == nullptr ||
                        !validate_position(*chart, edit.position, result, "pause event") ||
                        !validate_pause_values(edit.kind, edit.duration, edit.source_unit,
                                               result)) {
                        return;
                    }
                    auto* projection = matching_pause_projection(*chart, *event, result);
                    if (!result.diagnostics.empty()) {
                        return;
                    }
                    if (event->provenance.family == format_family_t::bms && projection == nullptr) {
                        reject(result, diagnostic_code_t::invalid_id,
                               "source-backed BMS pause has no matching import projection");
                        return;
                    }
                    if (projection != nullptr &&
                        (projection->imported_kind != edit.kind ||
                         projection->imported_source_unit != edit.source_unit)) {
                        reject(result, diagnostic_code_t::invalid_resolution,
                               "pause update would make its BMS source projection incoherent");
                        return;
                    }
                    changed = event->position != edit.position || event->kind != edit.kind ||
                              event->duration != edit.duration ||
                              event->source_unit != edit.source_unit;
                    if (!changed) {
                        return;
                    }
                    event->position = edit.position;
                    event->kind = edit.kind;
                    event->duration = edit.duration;
                    event->source_unit = edit.source_unit;
                    command_label = "Update pause event";
                } else {
                    if (!edit.event) {
                        reject(result, diagnostic_code_t::invalid_id,
                               "pause deletion uses the reserved zero event ID");
                        return;
                    }
                    auto* event = unique_event(*chart, chart->pauses, edit.event, "pause", result);
                    if (event == nullptr) {
                        return;
                    }
                    const auto event_id = event->id;
                    std::erase_if(chart->bms_pause_projections, [event_id](const auto& projection) {
                        return projection.event == event_id;
                    });
                    std::erase_if(chart->pauses,
                                  [event_id](const auto& item) { return item.id == event_id; });
                    command_label = "Delete pause event";
                    changed = true;
                }
            } else {
                if (chart->warps.size() > limits.maximum_warp_events) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           "selected chart exceeds the warp event cap");
                    return;
                }
                if constexpr (std::is_same_v<edit_t, insert_warp_event_action_t>) {
                    if (chart->warps.size() >= limits.maximum_warp_events) {
                        reject(result, diagnostic_code_t::invalid_resolution,
                               "warp insertion would exceed the configured event cap");
                        return;
                    }
                    if (!validate_span(*chart, edit.span, result)) {
                        return;
                    }
                    if (!valid_warp_encoding(edit.source_encoding)) {
                        reject(result, diagnostic_code_t::invalid_resolution,
                               "warp event has an unknown source encoding");
                        return;
                    }
                    const auto allocation =
                        next_chart_object_id(*chart, limits.maximum_chart_objects);
                    if (!allocation.id.has_value()) {
                        reject(result, diagnostic_code_t::invalid_resolution,
                               allocation.over_limit
                                   ? "selected chart exceeds the object ID allocation cap"
                                   : "chart object ID sequence is exhausted");
                        return;
                    }
                    const auto source_order = next_source_order(chart->warps);
                    if (!source_order.has_value()) {
                        reject(result, diagnostic_code_t::invalid_resolution,
                               "warp source-order sequence is exhausted");
                        return;
                    }
                    chart->warps.push_back({.id = *allocation.id,
                                            .span = edit.span,
                                            .source_encoding = edit.source_encoding,
                                            .provenance = {.source_order = *source_order}});
                    command_label = "Insert warp event";
                    changed = true;
                } else if constexpr (std::is_same_v<edit_t, update_warp_event_action_t>) {
                    if (!edit.event) {
                        reject(result, diagnostic_code_t::invalid_id,
                               "warp update uses the reserved zero event ID");
                        return;
                    }
                    auto* event = unique_event(*chart, chart->warps, edit.event, "warp", result);
                    if (event == nullptr || !validate_span(*chart, edit.span, result)) {
                        return;
                    }
                    if (edit.source_encoding.has_value() &&
                        !valid_warp_encoding(*edit.source_encoding)) {
                        reject(result, diagnostic_code_t::invalid_resolution,
                               "warp event has an unknown source encoding");
                        return;
                    }
                    const auto source_encoding =
                        edit.source_encoding.value_or(event->source_encoding);
                    changed = event->span.begin != edit.span.begin ||
                              event->span.end != edit.span.end ||
                              event->source_encoding != source_encoding;
                    if (!changed) {
                        return;
                    }
                    event->span = edit.span;
                    event->source_encoding = source_encoding;
                    command_label = "Update warp event";
                } else {
                    if (!edit.event) {
                        reject(result, diagnostic_code_t::invalid_id,
                               "warp deletion uses the reserved zero event ID");
                        return;
                    }
                    if (unique_event(*chart, chart->warps, edit.event, "warp", result) == nullptr) {
                        return;
                    }
                    std::erase_if(chart->warps,
                                  [&edit](const auto& item) { return item.id == edit.event; });
                    command_label = "Delete warp event";
                    changed = true;
                }
            }
        },
        action);

    if (!result.diagnostics.empty() || !changed) {
        return result;
    }
    return commit_document_command(history, std::move(proposed), std::move(command_label));
}

} // namespace wormedit
