#include <wormedit/core/bpm_editor.h>

#include <algorithm>
#include <limits>
#include <ranges>
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

auto validate_bpm_value(signed_rational_t bpm, document_command_result_t& result) -> bool {
    if (bpm.numerator <= 0 || bpm.denominator == 0) {
        reject(result, diagnostic_code_t::invalid_rational,
               "BPM event requires a positive exact rational value");
        return false;
    }
    return true;
}

auto validate_bpm_position(const chart_document_t& chart, local_position_t position,
                           document_command_result_t& result) -> bool {
    const auto measures = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(measures)) {
        result.diagnostics = std::get<diagnostic_list_t>(measures);
        return false;
    }
    if (!absolute_row(std::get<measure_index_t>(measures), position).has_value()) {
        reject(result, diagnostic_code_t::row_out_of_range,
               "BPM event position is outside the selected chart's measure geometry");
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

auto next_bpm_source_order(const chart_document_t& chart) -> std::optional<std::size_t> {
    if (chart.bpms.empty()) {
        return 0;
    }
    auto maximum = std::size_t{};
    for (const auto& bpm : chart.bpms) {
        maximum = std::max(maximum, bpm.provenance.source_order);
    }
    if (maximum == std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }
    return maximum + 1;
}

auto unique_bpm_event(chart_document_t& chart, object_id_t id, document_command_result_t& result)
    -> bpm_event_t* {
    const auto found = std::ranges::find(chart.bpms, id, &bpm_event_t::id);
    if (found == chart.bpms.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "BPM event command refers to an unknown event");
        return nullptr;
    }
    if (std::ranges::count(chart.bpms, id, &bpm_event_t::id) != 1) {
        reject(result, diagnostic_code_t::duplicate_id,
               "BPM event ID is ambiguous because it appears more than once");
        return nullptr;
    }
    return &*found;
}

} // namespace

auto apply_bpm_edit(document_history_t& history, std::uint64_t expected_revision,
                    const bpm_edit_action_t& action, bpm_edit_limits_t limits)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id, "BPM editor document revision is stale");
        return result;
    }
    if (limits.maximum_bpm_events == 0) {
        reject(result, diagnostic_code_t::invalid_resolution, "BPM event cap must be positive");
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
                       "BPM edit refers to an unknown chart");
                return;
            }
            if (chart->bpms.size() > limits.maximum_bpm_events) {
                reject(result, diagnostic_code_t::invalid_resolution,
                       "selected chart exceeds the BPM event cap");
                return;
            }

            if constexpr (std::is_same_v<edit_t, insert_bpm_event_action_t>) {
                if (chart->bpms.size() >= limits.maximum_bpm_events) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           "BPM insertion would exceed the configured event cap");
                    return;
                }
                if (!validate_bpm_position(*chart, edit.position, result) ||
                    !validate_bpm_value(edit.beats_per_minute, result)) {
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
                const auto source_order = next_bpm_source_order(*chart);
                if (!source_order.has_value()) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           "BPM source-order sequence is exhausted");
                    return;
                }
                chart->bpms.push_back({
                    .id = *allocation.id,
                    .position = edit.position,
                    .beats_per_minute = edit.beats_per_minute,
                    .provenance = {.source_order = *source_order},
                });
                command_label = "Insert BPM event";
                changed = true;
            } else if constexpr (std::is_same_v<edit_t, update_bpm_event_action_t>) {
                if (!edit.event) {
                    reject(result, diagnostic_code_t::invalid_id,
                           "BPM update uses the reserved zero event ID");
                    return;
                }
                auto* event = unique_bpm_event(*chart, edit.event, result);
                if (event == nullptr || !validate_bpm_position(*chart, edit.position, result) ||
                    !validate_bpm_value(edit.beats_per_minute, result)) {
                    return;
                }
                changed = event->position != edit.position ||
                          event->beats_per_minute != edit.beats_per_minute;
                if (!changed) {
                    return;
                }
                event->position = edit.position;
                event->beats_per_minute = edit.beats_per_minute;
                command_label = "Update BPM event";
            } else {
                if (!edit.event) {
                    reject(result, diagnostic_code_t::invalid_id,
                           "BPM deletion uses the reserved zero event ID");
                    return;
                }
                auto* event = unique_bpm_event(*chart, edit.event, result);
                if (event == nullptr) {
                    return;
                }
                const auto event_id = event->id;
                std::erase_if(chart->bms_bpm_projections, [event_id](const auto& projection) {
                    return projection.event == event_id;
                });
                std::erase_if(chart->bpms,
                              [event_id](const auto& item) { return item.id == event_id; });
                command_label = "Delete BPM event";
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
