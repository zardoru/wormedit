#include <wormedit/core/scroll_speed_editor.h>

#include <algorithm>
#include <limits>
#include <ranges>
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

auto validate_scroll_value(signed_rational_t multiplier, document_command_result_t& result)
    -> bool {
    if (multiplier.denominator == 0) {
        reject(result, diagnostic_code_t::invalid_rational,
               "scroll multiplier requires a nonzero denominator");
        return false;
    }
    return true;
}

auto validate_speed_values(signed_rational_t multiplier, rational_t transition_beats,
                           document_command_result_t& result) -> bool {
    if (multiplier.numerator < 0 || multiplier.denominator == 0 ||
        transition_beats.denominator == 0) {
        reject(result, diagnostic_code_t::invalid_rational,
               "speed multiplier and transition duration must be valid nonnegative rationals");
        return false;
    }
    return true;
}

auto validate_position(const chart_document_t& chart, local_position_t position,
                       document_command_result_t& result) -> bool {
    const auto measures = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(measures)) {
        result.diagnostics = std::get<diagnostic_list_t>(measures);
        return false;
    }
    if (!absolute_row(std::get<measure_index_t>(measures), position).has_value()) {
        reject(result, diagnostic_code_t::row_out_of_range,
               "scroll/speed event position is outside the selected chart's measure geometry");
        return false;
    }
    return true;
}

struct object_id_allocation_t {
    std::optional<object_id_t> id;
    bool over_limit{};
    bool exhausted{};
};

auto next_chart_object_id(const chart_document_t& chart, std::size_t maximum_chart_objects)
    -> object_id_allocation_t {
    std::size_t object_count{};
    std::uint64_t maximum_id{};
    bool over_limit{};
    const auto record = [&](object_id_t id) {
        if (object_count >= maximum_chart_objects) {
            over_limit = true;
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
        return {.exhausted = true};
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

} // namespace

auto apply_scroll_speed_edit(document_history_t& history, std::uint64_t expected_revision,
                             const scroll_speed_edit_action_t& action,
                             scroll_speed_edit_limits_t limits) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id,
               "scroll/speed editor document revision is stale");
        return result;
    }
    if (limits.maximum_scroll_events == 0 || limits.maximum_speed_events == 0) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "scroll and speed event caps must be positive");
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
                       "scroll/speed edit refers to an unknown chart");
                return;
            }

            constexpr bool is_scroll = std::is_same_v<edit_t, insert_scroll_event_action_t> ||
                                       std::is_same_v<edit_t, update_scroll_event_action_t> ||
                                       std::is_same_v<edit_t, delete_scroll_event_action_t>;
            auto& events = [&]() -> auto& {
                if constexpr (is_scroll) {
                    return chart->scrolls;
                } else {
                    return chart->speeds;
                }
            }();
            const auto event_cap = [&]() {
                if constexpr (is_scroll) {
                    return limits.maximum_scroll_events;
                } else {
                    return limits.maximum_speed_events;
                }
            }();
            const auto event_name =
                is_scroll ? std::string_view{"scroll"} : std::string_view{"speed"};
            if (events.size() > event_cap) {
                reject(result, diagnostic_code_t::invalid_resolution,
                       "selected chart exceeds the " + std::string(event_name) + " event cap");
                return;
            }

            if constexpr (std::is_same_v<edit_t, insert_scroll_event_action_t> ||
                          std::is_same_v<edit_t, insert_speed_event_action_t>) {
                if (events.size() >= event_cap) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           std::string(event_name) +
                               " insertion would exceed the configured event cap");
                    return;
                }
                const auto valid = [&]() {
                    if constexpr (is_scroll) {
                        return validate_scroll_value(edit.multiplier, result);
                    } else {
                        return validate_speed_values(edit.multiplier, edit.transition_beats,
                                                     result);
                    }
                }();
                if (!validate_position(*chart, edit.position, result) || !valid) {
                    return;
                }
                const auto allocation = next_chart_object_id(*chart, limits.maximum_chart_objects);
                if (!allocation.id.has_value()) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           allocation.over_limit
                               ? "selected chart exceeds the object ID allocation cap"
                               : "chart object ID sequence is exhausted");
                    return;
                }
                const auto source_order = next_source_order(events);
                if (!source_order.has_value()) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           std::string(event_name) + " source-order sequence is exhausted");
                    return;
                }
                if constexpr (is_scroll) {
                    chart->scrolls.push_back({.id = *allocation.id,
                                              .position = edit.position,
                                              .multiplier = edit.multiplier,
                                              .provenance = {.source_order = *source_order}});
                    command_label = "Insert scroll event";
                } else {
                    chart->speeds.push_back({.id = *allocation.id,
                                             .position = edit.position,
                                             .multiplier = edit.multiplier,
                                             .transition_beats = edit.transition_beats,
                                             .transition_is_seconds = edit.transition_is_seconds,
                                             .provenance = {.source_order = *source_order}});
                    command_label = "Insert speed event";
                }
                changed = true;
            } else if constexpr (std::is_same_v<edit_t, update_scroll_event_action_t> ||
                                 std::is_same_v<edit_t, update_speed_event_action_t>) {
                if (!edit.event) {
                    reject(result, diagnostic_code_t::invalid_id,
                           std::string(event_name) + " update uses the reserved zero event ID");
                    return;
                }
                auto* event = unique_event(*chart, events, edit.event, event_name, result);
                const auto valid = [&]() {
                    if constexpr (is_scroll) {
                        return validate_scroll_value(edit.multiplier, result);
                    } else {
                        return validate_speed_values(edit.multiplier, edit.transition_beats,
                                                     result);
                    }
                }();
                if (event == nullptr || !validate_position(*chart, edit.position, result) ||
                    !valid) {
                    return;
                }
                if constexpr (is_scroll) {
                    changed =
                        event->position != edit.position || event->multiplier != edit.multiplier;
                    if (!changed) {
                        return;
                    }
                    event->position = edit.position;
                    event->multiplier = edit.multiplier;
                    command_label = "Update scroll event";
                } else {
                    changed = event->position != edit.position ||
                              event->multiplier != edit.multiplier ||
                              event->transition_beats != edit.transition_beats ||
                              event->transition_is_seconds != edit.transition_is_seconds;
                    if (!changed) {
                        return;
                    }
                    event->position = edit.position;
                    event->multiplier = edit.multiplier;
                    event->transition_beats = edit.transition_beats;
                    event->transition_is_seconds = edit.transition_is_seconds;
                    command_label = "Update speed event";
                }
            } else {
                if (!edit.event) {
                    reject(result, diagnostic_code_t::invalid_id,
                           std::string(event_name) + " deletion uses the reserved zero event ID");
                    return;
                }
                auto* event = unique_event(*chart, events, edit.event, event_name, result);
                if (event == nullptr) {
                    return;
                }
                const auto event_id = event->id;
                std::erase_if(events, [event_id](const auto& item) { return item.id == event_id; });
                command_label = is_scroll ? "Delete scroll event" : "Delete speed event";
                changed = true;
            }
        },
        action);

    if (!result.diagnostics.empty() || !changed) {
        return result;
    }
    return commit_document_command(history, std::move(proposed), std::move(command_label));
}

} // namespace wormedit
