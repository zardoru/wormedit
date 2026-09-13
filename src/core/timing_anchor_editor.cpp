#include <wormedit/core/timing_anchor_editor.h>

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

auto validate_position(const chart_document_t& chart, local_position_t position,
                       document_command_result_t& result) -> bool {
    const auto measures = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(measures)) {
        result.diagnostics = std::get<diagnostic_list_t>(measures);
        return false;
    }
    if (!absolute_row(std::get<measure_index_t>(measures), position).has_value()) {
        reject(result, diagnostic_code_t::row_out_of_range,
               "timing anchor position is outside the selected chart's measure geometry");
        return false;
    }
    return true;
}

template <typename callback_t>
auto visit_chart_object_ids(const chart_document_t& chart, callback_t&& callback) -> bool {
    const auto visit = [&callback](const auto& values) {
        for (const auto& value : values) {
            if (!callback(value.id)) {
                return false;
            }
        }
        return true;
    };
    if (!visit(chart.bgm_source_lanes) || !visit(chart.lane_layouts) || !visit(chart.bpms) ||
        !visit(chart.pauses) || !visit(chart.warps) || !visit(chart.scrolls) ||
        !visit(chart.speeds) || !visit(chart.kiai) || !visit(chart.timing_anchors) ||
        !visit(chart.bookmarks) || !visit(chart.sections) || !visit(chart.bga_events) ||
        !visit(chart.sound_channels)) {
        return false;
    }
    for (const auto& measure : chart.measures) {
        if (!visit(measure.notes) || !visit(measure.bgm_events)) {
            return false;
        }
    }
    for (const auto& channel : chart.sound_channels) {
        if (!visit(channel.events)) {
            return false;
        }
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
    const auto within_limit = visit_chart_object_ids(chart, [&](object_id_t id) {
        if (object_count >= maximum_chart_objects) {
            return false;
        }
        ++object_count;
        maximum_id = std::max(maximum_id, id.value);
        return true;
    });
    if (!within_limit || object_count >= maximum_chart_objects) {
        return {.over_limit = true};
    }
    if (maximum_id == std::numeric_limits<std::uint64_t>::max()) {
        return {};
    }
    return {.id = object_id_t{maximum_id + 1}};
}

auto chart_object_id_count(const chart_document_t& chart, object_id_t sought) -> std::size_t {
    std::size_t count{};
    (void)visit_chart_object_ids(chart, [&](object_id_t id) {
        if (id == sought) {
            ++count;
        }
        return count < 2;
    });
    return count;
}

auto unique_anchor(chart_document_t& chart, object_id_t id, document_command_result_t& result)
    -> timing_anchor_t* {
    if (!id) {
        reject(result, diagnostic_code_t::invalid_id,
               "timing anchor command uses the reserved zero object ID");
        return nullptr;
    }
    const auto id_count = chart_object_id_count(chart, id);
    if (id_count == 0) {
        reject(result, diagnostic_code_t::invalid_id,
               "timing anchor command refers to an unknown anchor");
        return nullptr;
    }
    if (id_count != 1) {
        reject(result, diagnostic_code_t::duplicate_id,
               "timing anchor ID is ambiguous because it appears more than once");
        return nullptr;
    }
    const auto found = std::ranges::find(chart.timing_anchors, id, &timing_anchor_t::id);
    if (found == chart.timing_anchors.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "timing anchor command refers to an unknown anchor");
        return nullptr;
    }
    return &*found;
}

} // namespace

auto apply_timing_anchor_edit(document_history_t& history, std::uint64_t expected_revision,
                              const timing_anchor_edit_action_t& action,
                              timing_anchor_edit_limits_t limits) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id,
               "timing anchor editor document revision is stale");
        return result;
    }
    if (limits.maximum_timing_anchors == 0) {
        reject(result, diagnostic_code_t::invalid_resolution, "timing anchor cap must be positive");
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
                       "timing anchor edit refers to an unknown chart");
                return;
            }
            if (chart->timing_anchors.size() > limits.maximum_timing_anchors) {
                reject(result, diagnostic_code_t::invalid_resolution,
                       "selected chart exceeds the timing anchor cap");
                return;
            }

            if constexpr (std::is_same_v<edit_t, insert_timing_anchor_action_t>) {
                if (chart->timing_anchors.size() >= limits.maximum_timing_anchors) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           "timing anchor insertion would exceed the configured anchor cap");
                    return;
                }
                if (!validate_position(*chart, edit.position, result)) {
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
                chart->timing_anchors.push_back({.id = *allocation.id,
                                                 .position = edit.position,
                                                 .source_sample_frame = edit.source_sample_frame,
                                                 .locked = edit.locked});
                command_label = "Insert timing anchor";
                changed = true;
            } else if constexpr (std::is_same_v<edit_t, update_timing_anchor_action_t>) {
                auto* anchor = unique_anchor(*chart, edit.anchor, result);
                if (anchor == nullptr || !validate_position(*chart, edit.position, result)) {
                    return;
                }
                changed = anchor->position != edit.position ||
                          anchor->source_sample_frame != edit.source_sample_frame ||
                          anchor->locked != edit.locked;
                if (!changed) {
                    return;
                }
                anchor->position = edit.position;
                anchor->source_sample_frame = edit.source_sample_frame;
                anchor->locked = edit.locked;
                command_label = "Update timing anchor";
            } else {
                auto* anchor = unique_anchor(*chart, edit.anchor, result);
                if (anchor == nullptr) {
                    return;
                }
                const auto anchor_id = anchor->id;
                std::erase_if(chart->timing_anchors,
                              [anchor_id](const auto& item) { return item.id == anchor_id; });
                command_label = "Delete timing anchor";
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
