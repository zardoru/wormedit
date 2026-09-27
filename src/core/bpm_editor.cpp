#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/measure.h>
#include <wormedit/core/timing.h>

#include <algorithm>
#include <charconv>
#include <limits>
#include <ranges>
#include <string>
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

auto parse_bpm_text(std::string_view text) -> std::optional<signed_rational_t> {
    const auto parsed = parse_measure_length(text);
    if (!parsed || parsed.value->numerator >
                       static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return signed_rational_t{static_cast<std::int64_t>(parsed.value->numerator),
                             parsed.value->denominator};
}

auto format_bpm_text(signed_rational_t bpm, std::size_t maximum_fraction_digits)
    -> std::optional<bpm_text_format_t> {
    if (bpm.numerator <= 0 || bpm.denominator == 0 || maximum_fraction_digits > 64)
        return std::nullopt;
    bpm_text_format_t result;
    result.fraction = std::to_string(bpm.numerator) + "/" + std::to_string(bpm.denominator);
    const auto numerator = static_cast<std::uint64_t>(bpm.numerator);
    result.decimal = std::to_string(numerator / bpm.denominator);
    auto remainder = numerator % bpm.denominator;
    if (remainder == 0) {
        result.decimal_exact = true;
        return result;
    }
    result.decimal.push_back('.');
    for (std::size_t digit = 0; digit < maximum_fraction_digits && remainder != 0; ++digit) {
        if (remainder > std::numeric_limits<std::uint64_t>::max() / 10)
            break;
        remainder *= 10;
        result.decimal.push_back(static_cast<char>('0' + remainder / bpm.denominator));
        remainder %= bpm.denominator;
    }
    if (remainder == 0) {
        result.decimal_exact = true;
        return result;
    }
    char approximate[64]{};
    const auto value = static_cast<double>(bpm.numerator) / static_cast<double>(bpm.denominator);
    const auto converted = std::to_chars(approximate, approximate + sizeof(approximate), value,
                                         std::chars_format::general, 12);
    if (converted.ec != std::errc{})
        return std::nullopt;
    result.decimal.assign(approximate, converted.ptr);
    result.decimal_exact = false;
    return result;
}

auto step_bpm_by_one(signed_rational_t bpm, bpm_step_direction_t direction)
    -> std::optional<signed_rational_t> {
    if (bpm.numerator <= 0 || bpm.denominator == 0 ||
        bpm.denominator > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    const auto denominator = static_cast<std::int64_t>(bpm.denominator);
    if (direction == bpm_step_direction_t::increment) {
        if (bpm.numerator > std::numeric_limits<std::int64_t>::max() - denominator)
            return std::nullopt;
        return signed_rational_t{bpm.numerator + denominator, bpm.denominator};
    }
    if (bpm.numerator <= denominator)
        return std::nullopt;
    return signed_rational_t{bpm.numerator - denominator, bpm.denominator};
}

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

auto begin_beat_interval_drag(const song_document_t& document, chart_id_t chart,
                              object_id_t bpm_event, local_position_t beat,
                              std::uint32_t sample_rate, bool timing_locked)
    -> beat_interval_drag_result_t {
    beat_interval_drag_result_t result;
    if (timing_locked || sample_rate == 0) {
        result.diagnostics.push_back(error(timing_locked ? diagnostic_code_t::unsupported_feature
                                                         : diagnostic_code_t::invalid_rational,
                                           timing_locked
                                               ? "beat fitting is disabled while timing is locked"
                                               : "beat fitting requires a sample rate"));
        return result;
    }
    const auto selected = std::ranges::find(document.charts, chart, &chart_document_t::id);
    if (!chart || selected == document.charts.end()) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "beat fit refers to an unknown chart"));
        return result;
    }
    const auto bpm = std::ranges::find(selected->bpms, bpm_event, &bpm_event_t::id);
    if (!bpm_event || bpm == selected->bpms.end() ||
        std::ranges::count(selected->bpms, bpm_event, &bpm_event_t::id) != 1) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "beat fit requires one selected BPM event"));
        return result;
    }
    const auto timing = build_timing_index(*selected);
    const auto measures = build_measure_index(*selected);
    if (!timing || !std::holds_alternative<measure_index_t>(measures)) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_resolution, "beat fit requires valid chart timing"));
        return result;
    }
    const auto& index = std::get<measure_index_t>(measures);
    const auto anchor_row = absolute_row(index, bpm->position);
    const auto beat_row = absolute_row(index, beat);
    if (!anchor_row || !beat_row || *beat_row <= *anchor_row) {
        result.diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
                                           "fitted beat must follow the selected BPM event"));
        return result;
    }
    const auto at = timing_at(*timing.index, *beat_row);
    const auto frame =
        at ? sample_frame_at(*at.seconds, sample_rate, sample_rounding_t::nearest_ties_to_even)
           : sample_frame_result_t{};
    if (!frame) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_rational,
                                           "fitted beat is outside the source frame range"));
        return result;
    }
    result.drag = beat_interval_drag_t{.chart = chart,
                                       .document_revision = document.revision,
                                       .bpm_event = bpm_event,
                                       .original_bpm_position = bpm->position,
                                       .beat = beat,
                                       .sample_rate = sample_rate,
                                       .original_sample_frame = *frame.frame,
                                       .proposed_sample_frame = *frame.frame,
                                       .original_bpm = bpm->beats_per_minute,
                                       .proposed_bpm = bpm->beats_per_minute};
    return result;
}

auto update_beat_interval_drag(const song_document_t& document, const beat_interval_drag_t& drag,
                               std::uint64_t target_sample_frame, bool timing_locked)
    -> beat_interval_drag_result_t {
    beat_interval_drag_result_t result;
    if (timing_locked || document.revision != drag.document_revision) {
        result.diagnostics.push_back(error(
            timing_locked ? diagnostic_code_t::unsupported_feature : diagnostic_code_t::invalid_id,
            timing_locked ? "beat fitting is disabled while timing is locked"
                          : "beat fit expired after the document changed"));
        return result;
    }
    const auto chart = std::ranges::find(document.charts, drag.chart, &chart_document_t::id);
    if (chart == document.charts.end()) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "beat fit chart is unavailable"));
        return result;
    }
    const auto bpm = std::ranges::find(chart->bpms, drag.bpm_event, &bpm_event_t::id);
    if (bpm == chart->bpms.end() || bpm->beats_per_minute != drag.original_bpm ||
        bpm->position != drag.original_bpm_position) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "beat fit BPM event changed during drag"));
        return result;
    }
    auto updated = drag;
    updated.proposed_sample_frame = target_sample_frame;
    if (target_sample_frame == drag.original_sample_frame) {
        updated.proposed_bpm = drag.original_bpm;
    } else {
        const auto fitted = fit_beat_interval(*chart, drag.bpm_event, drag.beat,
                                              target_sample_frame, drag.sample_rate);
        if (!fitted) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_rational,
                      fitted.error ? fitted.error->message : "beat fit has no valid BPM"));
            return result;
        }
        updated.proposed_bpm = *fitted.beats_per_minute;
    }
    result.drag = updated;
    return result;
}

auto commit_beat_interval_drag(document_history_t& history, const beat_interval_drag_t& drag,
                               bool timing_locked) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    const auto updated = update_beat_interval_drag(history.document, drag,
                                                   drag.proposed_sample_frame, timing_locked);
    if (!updated || updated.drag->proposed_bpm != drag.proposed_bpm) {
        result.diagnostics = updated.diagnostics;
        if (result.diagnostics.empty()) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_id, "beat fit proposal changed before commit"));
        }
        return result;
    }
    return apply_bpm_edit(history, drag.document_revision,
                          update_bpm_event_action_t{drag.chart, drag.bpm_event,
                                                    drag.original_bpm_position, drag.proposed_bpm});
}

} // namespace wormedit
