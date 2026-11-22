#include "timing_event_internal.h"
#include "timing_arithmetic.h"
#include "text_util.h"
#include <wormedit/core/rational.h>
#include "position_validation.h"
#include "chart_object_ids.h"
#include "chart_position_storage.h"
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/timing_reposition.h>
#include <wormedit/core/measure.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/numeric_text.h>
#include <wormedit/core/timing.h>

#include "wide_math.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <numeric>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace wormedit {
namespace {

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
    return validate_chart_position(chart, position, result.diagnostics, "BPM event");
}



constexpr target_query_policy_t bpm_target_policy{
    .messages = {"BPM event command refers to an unknown event",
                 "BPM event ID is ambiguous because it appears more than once"},
    .scope = target_uniqueness_scope_t::chart_objects};


struct bpm_operation_visitor_t : chart_edit_context_t<bpm_edit_limits_t> {
    static constexpr auto storage_support = chart_edit_storage_support_t::objects;
    static constexpr std::string_view invalid_limits_message = "BPM event cap must be positive";
    static constexpr std::string_view unknown_chart_message = "BPM edit refers to an unknown chart";
    static auto has_valid_limits(const bpm_edit_limits_t& limits) -> bool {
        return limits.maximum_bpm_events != 0;
    }
    template <class Edit> auto event_caps(const chart_document_t& chart, const Edit&) const {
        return std::array{chart_event_cap_t{chart.bpms.size(), limits.maximum_bpm_events,
                                            "selected chart exceeds the BPM event cap"}};
    }

    auto operator()(chart_document_t& chart, const insert_edit_t<bpm_edit_value_t>& edit) const
        -> document_edit_effect_t {
        if (chart.bpms.size() >= limits.maximum_bpm_events) {
            reject(result, diagnostic_code_t::invalid_resolution,
                   "BPM insertion would exceed the configured event cap");
            return {};
        }
        if (!validate_bpm_position(chart, edit.value.position, result) ||
            !validate_bpm_value(edit.value.settings.beats_per_minute, result)) {
            return {};
        }
        const auto allocation = next_chart_object_id(chart, limits.maximum_chart_objects);
        if (!allocation.id) {
            reject(result, diagnostic_code_t::invalid_resolution,
                   allocation.over_limit ? "selected chart exceeds the object ID allocation cap"
                                         : "chart object ID sequence is exhausted");
            return {};
        }
        const auto source_order = next_source_order(chart.bpms);
        if (!source_order) {
            reject(result, diagnostic_code_t::invalid_resolution,
                   "BPM source-order sequence is exhausted");
            return {};
        }
        chart.bpms.push_back({
            .id = *allocation.id,
            .position = edit.value.position,
            .beats_per_minute = edit.value.settings.beats_per_minute,
            .provenance = {.source_order = *source_order},
        });
        return document_edit_effect_t{.changed = true, .command_label = "Insert BPM event"};
    }

    auto operator()(chart_document_t& chart, const update_edit_t<bpm_edit_value_t>& edit) const
        -> document_edit_effect_t {
        if (!edit.id) {
            reject(result, diagnostic_code_t::invalid_id,
                   "BPM update uses the reserved zero event ID");
            return {};
        }
        auto* event = chart.unique_target(&chart_document_t::bpms, edit.id, result.diagnostics,
                                          bpm_target_policy);
        if (event == nullptr || !validate_bpm_position(chart, edit.value.position, result) ||
            !validate_bpm_value(edit.value.settings.beats_per_minute, result)) {
            return {};
        }
        if (event->position == edit.value.position &&
            event->beats_per_minute == edit.value.settings.beats_per_minute) {
            return document_edit_effect_t{};
        }
        event->position = edit.value.position;
        event->beats_per_minute = edit.value.settings.beats_per_minute;
        return document_edit_effect_t{.changed = true, .command_label = "Update BPM event"};
    }

    auto operator()(chart_document_t& chart, const delete_edit_t<bpm_edit_value_t>& edit) const
        -> document_edit_effect_t {
        if (!edit.id) {
            reject(result, diagnostic_code_t::invalid_id,
                   "BPM deletion uses the reserved zero event ID");
            return {};
        }
        auto* event = chart.unique_target(&chart_document_t::bpms, edit.id, result.diagnostics,
                                          bpm_target_policy);
        if (event == nullptr) {
            return {};
        }
        const auto id = event->id;
        std::erase_if(chart.bms_bpm_projections,
                      [id](const auto& projection) { return projection.event == id; });
        std::erase_if(chart.bpms, [id](const auto& item) { return item.id == id; });
        return document_edit_effect_t{.changed = true, .command_label = "Delete BPM event"};
    }

    auto postvalidate(chart_document_t&, song_document_t& proposed) -> bool {
        if (extend_native_charts_to_audio_end(proposed))
            return true;
        reject(result, diagnostic_code_t::invalid_measure,
               "BPM edit would exceed the audio chart measure limits");
        return false;
    }
};


} // namespace

auto parse_bpm_text(std::string_view text) -> std::optional<signed_rational_t> {
    text = text_util::trim(text, " \t\r\n");
    if (text.empty()) return std::nullopt;
    if (text.find('/') == std::string_view::npos) {
        const auto parsed = parse_exact_decimal(text);
        return parsed && parsed->numerator > 0 ? parsed : std::nullopt;
    }
    const auto parsed = parse_measure_length(text);
    if (!parsed || parsed->numerator >
                       static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return signed_rational_t{static_cast<std::int64_t>(parsed->numerator),
                             parsed->denominator};
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
    const auto value = to_double(bpm);
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

auto apply_bpm_edit(document_history_t& history,
    const bpm_edit_action_t& action, bpm_edit_limits_t limits, timing_anchoring_t anchoring) -> document_command_result_t {
    auto transaction = document_command_transaction_t(history);
    return transaction.finish_execution(stage_bpm_edit(transaction, action, limits, anchoring));
}

auto stage_bpm_edit(document_command_transaction_t& transaction, const bpm_edit_action_t& action,
                    bpm_edit_limits_t limits, timing_anchoring_t anchoring) -> attempt_t<document_edit_effect_t> {
    auto staged = transaction.stage<bpm_operation_visitor_t>(action, limits);
    const auto chart = std::visit([](const auto& edit) { return edit.chart; }, action);
    return stage_timing_reposition(transaction, chart, anchoring, std::move(staged));
}

namespace {

struct bpm_range_warp_plan_t {
    chart_id_t chart;
    position_span_t range;
    signed_rational_t multiplier;
    std::uint64_t first{};
    std::uint64_t last{};
    bool has_first_event{};
    bool has_last_event{};
    std::vector<bpm_edit_action_t> edits;

    auto add(local_position_t position, signed_rational_t bpm, object_id_t id = {}) -> bool {
        const auto scaled = position == range.end ? std::optional{bpm}
                                                  : timing_arithmetic::multiply(bpm, multiplier);
        if (!scaled || scaled->numerator <= 0) return false;
        const auto value = bpm_edit_value_t::from(position, timing_bpm_value_t{*scaled});
        if (id.value) edits.emplace_back(update_edit_t<bpm_edit_value_t>{chart, id, value});
        else edits.emplace_back(insert_edit_t<bpm_edit_value_t>{chart, value});
        return true;
    }

    auto collect(const chart_document_t& source, const measure_index_t& index) -> bool {
        for (const auto& bpm : source.bpms) {
            const auto row = index.row_of(bpm.position);
            if (!row || *row < first || *row > last) continue;
            if (*row == last) {
                has_last_event = true;
                continue;
            }
            has_first_event |= *row == first;
            if (!add(bpm.position, bpm.beats_per_minute, bpm.id)) return false;
        }
        return true;
    }

    auto add_boundaries(const timing_index_t& timing) -> bool {
        const auto at = [&](std::uint64_t row) {
            return std::prev(std::ranges::upper_bound(timing.rows, row, {},
                &timing_row_state_t::absolute_row))->effective_bpm;
        };
        return (has_first_event || add(range.begin, at(first))) &&
               (has_last_event || add(range.end, at(last)));
    }
};

struct bpm_warp_geometry_t {
    measure_id_t measure;
    std::uint64_t first, last;
    rational_t ratio;

    auto scale_rows(std::uint64_t rows) const -> std::optional<std::uint64_t> {
        if (rows % ratio.denominator != 0) return std::nullopt;
        return timing_arithmetic::checked_multiply(rows / ratio.denominator, ratio.numerator);
    }

    auto map(std::uint64_t row) const -> std::optional<std::uint64_t> {
        if (row <= first) return row;
        const auto scaled_part = std::min(row, last) - first;
        const auto width = scale_rows(scaled_part);
        if (!width || *width > std::numeric_limits<std::uint64_t>::max() - (row - scaled_part))
            return std::nullopt;
        return first + *width + (row - std::min(row, last));
    }

    auto grid_factor(const chart_document_t& chart) const -> std::uint64_t {
        auto divisor = std::gcd(ratio.denominator, last - first);
        for_each_chart_position(chart, [&](const local_position_t& position, auto) {
            if (position.measure == measure && position.row > first)
                divisor = std::gcd(divisor, std::min(position.row, last) - first);
        });
        return ratio.denominator / divisor;
    }
};

auto scale_warp_pauses(chart_document_t& chart, const measure_index_t& index,
                        const bpm_range_warp_plan_t& plan) -> bool {
    for (auto& pause : chart.pauses) {
        if (pause.source_unit != pause_unit_t::bms_stop_ticks &&
            pause.source_unit != pause_unit_t::beats) continue;
        const auto row = index.row_of(pause.position);
        if (!row || *row < plan.first || *row >= plan.last) continue;
        const auto scaled = timing_arithmetic::multiply(pause.duration, plan.multiplier);
        if (!scaled) return false;
        pause.duration = *scaled;
    }
    return true;
}

// One entry per authored measure the range touches, in local rows of the original grid.
auto warp_segments(const measure_index_t& index, const bpm_range_warp_plan_t& plan,
                   rational_t ratio) -> std::vector<bpm_warp_geometry_t> {
    std::vector<bpm_warp_geometry_t> segments;
    for (const auto& entry : index.entries) {
        const auto begin = entry.absolute_begin_row;
        const auto end = begin + entry.row_capacity;
        if (end <= plan.first || begin >= plan.last) continue;
        segments.push_back({entry.id, std::max(plan.first, begin) - begin,
                            std::min(plan.last, end) - begin, ratio});
    }
    return segments;
}

auto deform_warp_measures(chart_document_t& chart, std::vector<bpm_warp_geometry_t> segments) -> bool {
    std::uint64_t factor = 1;
    for (const auto& segment : segments) {
        const auto next = std::lcm(factor, segment.grid_factor(chart));
        if (next < factor) return false;
        factor = next;
    }
    if (factor != 1) {
        const auto resolution = timing_arithmetic::checked_multiply(chart.divisions_per_standard_measure, factor);
        if (!resolution) return false;
        auto redivision = redivision_plan_t::from(chart, *resolution);
        if (!redivision.value.chart) return false;
        chart = std::move(*redivision.value.chart);
        for (auto& segment : segments) {
            segment.first *= factor;
            segment.last *= factor;
        }
    }
    for (const auto& segment : segments) {
        auto* measure = chart.find_measure(segment.measure);
        const auto before = validate_measure(chart.divisions_per_standard_measure, measure->definition);
        if (!before.row_capacity) return false;
        const auto capacity = segment.map(*before.row_capacity);
        if (!capacity) return false;
        measure->definition = {.length = rows_to_fraction(*capacity, chart.divisions_per_standard_measure)};
    }
    bool exact = true;
    for_each_chart_position(chart, [&](local_position_t& position, auto) {
        const auto segment = std::ranges::find(segments, position.measure, &bpm_warp_geometry_t::measure);
        if (segment == segments.end()) return;
        const auto row = segment->map(position.row);
        if (row) position.row = *row;
        else exact = false;
    });
    return exact;
}

auto stage_bpm_warp_geometry(chart_document_t& chart, const bpm_range_warp_plan_t& plan,
                             document_command_result_t& outcome, bool resize_span) -> attempt_t<document_edit_effect_t> {
    const auto old_timing = resize_span ? timing_index_t::from(chart).value : std::nullopt;
    const auto old_index = measure_index_t::from(chart);
    const bpm_edit_limits_t limits;
    bpm_operation_visitor_t operation{{outcome, limits}};
    for (const auto& edit : plan.edits) {
        std::visit([&](const auto& action) { operation(chart, action); }, edit);
        if (!outcome.diagnostics.empty()) return {};
    }
    if (!resize_span) {
        if (!timing_index_t::from(chart).value) {
            reject(outcome, diagnostic_code_t::invalid_rational, "The warped timing exceeds clock bounds.");
            return {};
        }
        return {.value = document_edit_effect_t{.changed = true, .command_label = "Warp selected timing range"}};
    }
    const auto ratio = reduce_fraction(
        rational_t{static_cast<std::uint64_t>(plan.multiplier.numerator), plan.multiplier.denominator});
    auto segments = old_index ? warp_segments(*old_index, plan, ratio) : std::vector<bpm_warp_geometry_t>{};
    if (!old_index || !old_timing || segments.empty() || !scale_warp_pauses(chart, *old_index, plan) ||
        !deform_warp_measures(chart, segments)) {
        reject(outcome, diagnostic_code_t::invalid_resolution, "The selected range cannot be warped on an exact chart grid.");
        return {};
    }
    const auto new_timing = timing_index_t::from(chart);
    const auto new_index = measure_index_t::from(chart);
    // Every touched measure must end at the same song time as before.
    const auto ends_in_place = [&](const bpm_warp_geometry_t& segment) {
        const auto* previous = old_index->entry_for(segment.measure);
        const auto* warped = new_index ? new_index->entry_for(segment.measure) : nullptr;
        return previous && warped &&
            old_timing->seconds_at(previous->absolute_begin_row + previous->row_capacity).seconds ==
            new_timing.value->seconds_at(warped->absolute_begin_row + warped->row_capacity).seconds;
    };
    if (!new_timing.value || !std::ranges::all_of(segments, ends_in_place)) {
        reject(outcome, diagnostic_code_t::invalid_measure, "The warp would move the end of a measure in audio time.");
        return {};
    }
    return {.value = document_edit_effect_t{.changed = true, .command_label = "Warp selected timing range"}};
}

auto valid_bpm_range_warp_multiplier(rational_t ratio) -> bool {
    return std::ranges::find(bpm_range_warp_factors, ratio.numerator) != bpm_range_warp_factors.end() &&
           std::ranges::find(bpm_range_warp_factors, ratio.denominator) != bpm_range_warp_factors.end();
}

} // namespace

auto apply_bpm_range_warp(document_history_t& history, chart_id_t chart, position_span_t range,
                          rational_t multiplier, bool resize_span) -> document_command_result_t {
    auto transaction = document_command_transaction_t(history);
    if (!valid_bpm_range_warp_multiplier(multiplier))
        return transaction.reject(diagnostic_code_t::invalid_rational, "Choose an available BPM multiplier.");
    if (multiplier.numerator == multiplier.denominator) return transaction.finish_without_commit();

    const auto* source = transaction.read_chart(chart);
    if (!source) return transaction.reject(diagnostic_code_t::invalid_id, "The chart is no longer available.");
    const auto measures = measure_index_t::from(*source);
    const auto first = measures ? measures->row_of(range.begin) : std::nullopt;
    const auto last = measures ? measures->row_of(range.end) : std::nullopt;
    if (!first || !last || *first >= *last)
        return transaction.reject(diagnostic_code_t::invalid_measure, "Select a nonempty timing range.");
    const auto timing = timing_index_t::from(*source);
    bpm_range_warp_plan_t plan{.chart = chart, .range = range,
        .multiplier = {static_cast<std::int64_t>(multiplier.numerator), multiplier.denominator},
        .first = *first, .last = *last, .edits = {}};
    if (!timing.value || timing.value->rows.empty() || !plan.collect(*source, *measures) ||
        !plan.add_boundaries(*timing.value))
        return transaction.reject(diagnostic_code_t::invalid_rational,
                                  "The selected timing cannot be scaled exactly.");
    if (!resize_span && transaction.has_bms_program(chart)) {
        for (const auto& event : plan.edits) {
            auto staged = stage_bpm_edit(transaction, event);
            if (!staged.value || !staged.ok()) return transaction.finish_execution(std::move(staged));
        }
        return transaction.commit("Warp selected timing range");
    }
    const auto edit = [&](chart_document_t& working) {
        return stage_bpm_warp_geometry(working, plan, transaction.outcome(), resize_span);
    };
    if (transaction.has_bms_program(chart))
        return transaction.finish_execution(transaction.stage_bms_structure_edit(chart, edit));
    return transaction.finish_execution(transaction.stage(chart, bpm_operation_visitor_t::unknown_chart_message,
        [&](chart_document_t& working, song_document_t&) { return edit(working); }));
}

// Native commands retain their immediate-publication boundary; compositions use staging.
auto apply_bpm_edit(document_command_transaction_t& transaction, const bpm_edit_action_t& action,
                    bpm_edit_limits_t limits, timing_anchoring_t anchoring) -> document_command_result_t {
    return transaction.finish_execution(stage_bpm_edit(transaction, action, limits, anchoring));
}

namespace {

// Placeholder id for a BPM event that a fit inserts; never present in a document.
constexpr auto beat_fit_probe_event = object_id_t{std::numeric_limits<std::uint64_t>::max()};

auto chart_with_probe_bpm(const chart_document_t& chart, local_position_t anchor,
                          signed_rational_t bpm) -> chart_document_t {
    auto candidate = chart;
    candidate.bpms.push_back({.id = beat_fit_probe_event,
                              .position = anchor,
                              .beats_per_minute = bpm,
                              .provenance = {}});
    return candidate;
}

// Chart copy whose anchor BPM is `bpm`: the existing event is set, or a probe
// event is added at the anchor row when the fit inserts one.
auto chart_with_anchor_bpm(const chart_document_t& chart, const beat_interval_drag_t& drag,
                           signed_rational_t bpm) -> std::optional<chart_document_t> {
    if (drag.insert_bpm) return chart_with_probe_bpm(chart, drag.original_bpm_position, bpm);
    auto candidate = chart;
    const auto event = std::ranges::find(candidate.bpms, drag.bpm_event, &bpm_event_t::id);
    if (event == candidate.bpms.end()) return std::nullopt;
    event->beats_per_minute = bpm;
    return candidate;
}

// BPM event sitting exactly at the anchor row, or nullptr when none does.
auto bpm_event_at_anchor(const chart_document_t& chart, local_position_t anchor)
    -> const bpm_event_t* {
    const auto measures = measure_index_t::from(chart);
    if (!measures) return nullptr;
    const auto& index = *measures;
    const auto anchor_row = index.row_of(anchor);
    if (!anchor_row) return nullptr;
    for (const auto& event : chart.bpms) {
        if (index.row_of(event.position) == anchor_row) return &event;
    }
    return nullptr;
}

} // namespace

auto beat_interval_drag_t::validate_initial_input(bool timing_locked, std::uint32_t sample_rate)
    -> std::optional<diagnostic_t> {
    if (auto locked = beat_interval_drag_t::revision_policy.validate_lock(
            timing_locked, proposal_lock_order_t::before_stamp)) return locked;
    if (sample_rate == 0) {
        return error(diagnostic_code_t::invalid_rational, "beat fitting requires a sample rate");
    }
    return std::nullopt;
}

// Builds the drag for a fit whose anchor is `existing` (update) or, when null,
// a BPM event to insert at `anchor`.
auto beat_interval_drag_t::from(const song_document_t& document, const chart_document_t& chart,
                             const bpm_event_t* existing, local_position_t anchor,
                             local_position_t beat, std::uint32_t sample_rate)
    -> beat_interval_drag_outcome_t {
    beat_interval_drag_outcome_t result;
    const auto timing = timing_index_t::from(chart);
    const auto measures = measure_index_t::from(chart);
    if (!timing.value || !measures) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_resolution, "beat fit requires valid chart timing"));
        return result;
    }
    const auto& index = *measures;
    const auto anchor_row = index.row_of(anchor);
    const auto beat_row = index.row_of(beat);
    if (!anchor_row || !beat_row || *beat_row <= *anchor_row) {
        result.diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
                                           "fitted beat must follow the selected BPM event"));
        return result;
    }
    const auto at = timing.value->seconds_at(*beat_row);
    const auto frame =
        at ? sample_frame_at(*at.seconds, sample_rate, sample_rounding_t::nearest_ties_to_even)
           : sample_frame_result_t{};
    if (!frame) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_rational,
                                           "fitted beat is outside the source frame range"));
        return result;
    }
    beat_interval_drag_t drag{.bpm_event = existing ? existing->id : beat_fit_probe_event,
                              .original_bpm_position = anchor,
                              .insert_bpm = existing == nullptr,
                              .beat = beat,
                              .sample_rate = sample_rate,
                              .original_sample_frame = *frame.frame,
                              .proposed_sample_frame = *frame.frame};
    if (existing) {
        drag.original_bpm = existing->beats_per_minute;
    } else {
        // The BPM in effect at the anchor is the one that lands the beat on its current frame.
        const auto probe = chart_with_probe_bpm(chart, anchor, {60, 1});
        const auto fitted = fit_beat_interval(probe, beat_fit_probe_event, beat, *frame.frame,
                                              sample_rate);
        if (!fitted) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_rational,
                      fitted.error ? fitted.error->message : "beat fit has no valid BPM"));
            return result;
        }
        drag.original_bpm = *fitted.beats_per_minute;
    }
    drag.proposed_bpm = drag.original_bpm;
    return {.value = proposal_t<beat_interval_drag_t>{
                .stamp = chart_stamp_t::from(document, chart.id), .payload = std::move(drag)},
            .diagnostics = {}};
}

auto beat_interval_drag_t::propose(const song_document_t& document, chart_id_t chart,
                              object_id_t bpm_event, local_position_t beat,
                              std::uint32_t sample_rate, bool timing_locked)
    -> beat_interval_drag_outcome_t {
    beat_interval_drag_outcome_t result;
    if (const auto precondition = validate_initial_input(timing_locked, sample_rate)) {
        result.diagnostics.push_back(*precondition);
        return result;
    }
    const auto selected = document.read_chart(chart);
    if (!chart || selected == nullptr) {
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
    return from(document, *selected, &*bpm, bpm->position, beat, sample_rate);
}

auto beat_interval_drag_t::propose(const song_document_t& document, chart_id_t chart,
                                     local_position_t anchor, local_position_t beat,
                                     std::uint32_t sample_rate, bool timing_locked)
    -> beat_interval_drag_outcome_t {
    beat_interval_drag_outcome_t result;
    const auto selected = document.read_chart(chart);
    if (!chart || selected == nullptr) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "beat fit refers to an unknown chart"));
        return result;
    }
    if (const auto* existing = bpm_event_at_anchor(*selected, anchor)) {
        return beat_interval_drag_t::propose(document, chart, existing->id, beat, sample_rate,
                                        timing_locked);
    }
    if (const auto precondition = validate_initial_input(timing_locked, sample_rate)) {
        result.diagnostics.push_back(*precondition);
        return result;
    }
    return from(document, *selected, nullptr, anchor, beat, sample_rate);
}

auto beat_interval_drag_t::revise(const proposal_read_scope_t<chart_stamp_t>& scope,
    std::uint64_t target_sample_frame) const -> beat_interval_drag_outcome_t {
    const auto& document = scope.source_document();
    const auto& stamp = scope.captured_stamp();
    const auto& drag = *this;
    beat_interval_drag_outcome_t result;
    const auto chart = document.read_chart(stamp.chart);
    if (chart == nullptr) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "beat fit chart is unavailable"));
        return result;
    }
    const auto bpm = std::ranges::find(chart->bpms, drag.bpm_event, &bpm_event_t::id);
    if (!drag.insert_bpm &&
        (bpm == chart->bpms.end() || bpm->beats_per_minute != drag.original_bpm ||
         bpm->position != drag.original_bpm_position)) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "beat fit BPM event changed during drag"));
        return result;
    }
    auto updated = drag;
    updated.proposed_sample_frame = target_sample_frame;
    if (target_sample_frame == drag.original_sample_frame) {
        updated.proposed_bpm = drag.original_bpm;
    } else {
        const auto fit_chart = chart_with_anchor_bpm(*chart, drag, drag.original_bpm);
        const auto fitted = fit_chart ? fit_beat_interval(*fit_chart, drag.bpm_event, drag.beat,
                                                          target_sample_frame, drag.sample_rate)
                                      : beat_interval_fit_result_t{};
        if (!fitted) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_rational,
                      fitted.error ? fitted.error->message : "beat fit has no valid BPM"));
            return result;
        }
        updated.proposed_bpm = *fitted.beats_per_minute;
    }
    return {.value = proposal_t<beat_interval_drag_t>{.stamp = stamp, .payload = std::move(updated)},
            .diagnostics = {}};
}

auto beat_interval_drag_t::prepare_snapped_revision(const proposal_read_scope_t<chart_stamp_t>& scope,
    signed_rational_t target_bpm, proposal_t<beat_interval_drag_t>&& updated) const
    -> beat_interval_drag_outcome_t {
    const auto& document = scope.source_document();
    const auto& stamp = scope.captured_stamp();
    const auto& drag = *this;
    auto chart = document.read_chart(stamp.chart);
    if (chart == nullptr) return {.value = std::move(updated), .diagnostics = {}};
    const auto candidate = chart_with_anchor_bpm(*chart, drag, target_bpm);
    if (!candidate) return {.value = std::move(updated), .diagnostics = {}};
    const auto measures = measure_index_t::from(*candidate);
    if (!measures)
        return {.value = std::nullopt, .diagnostics = measures.error()};
    const auto beat_row = measures->row_of(drag.beat);
    const auto timing = timing_index_t::from(*candidate);
    if (!beat_row || !timing.value) {
        beat_interval_drag_outcome_t rejected;
        rejected.diagnostics.push_back(error(
            diagnostic_code_t::invalid_rational,
            timing.diagnostics.empty() ? "Snapped beat timing is invalid"
                                       : timing.diagnostics.front().message));
        return rejected;
    }
    const auto at = timing.value->seconds_at(*beat_row);
    const auto frame = at ? sample_frame_at(*at.seconds, drag.sample_rate,
        sample_rounding_t::nearest_ties_to_even) : sample_frame_result_t{};
    if (!frame) return {.value = std::nullopt, .diagnostics = {error(diagnostic_code_t::invalid_rational,
        "Snapped beat target is outside the source frame range")}};
    const auto anchor_row =
        measures->row_of(drag.original_bpm_position);
    for (const auto& anchor : chart->timing_anchors) {
        if (!anchor.locked) continue;
        const auto row = measures->row_of(anchor.position);
        if (!row || !anchor_row || *row < *anchor_row) continue;
        const auto anchor_timing = timing.value->seconds_at(*row);
        const auto anchor_frame = anchor_timing
            ? sample_frame_at(*anchor_timing.seconds, drag.sample_rate,
                              sample_rounding_t::nearest_ties_to_even)
            : sample_frame_result_t{};
        if (!anchor_frame || *anchor_frame.frame != anchor.source_sample_frame) {
            return {.value = std::nullopt, .diagnostics = {error(diagnostic_code_t::invalid_rational,
                "Snapped beat fit would move a locked timing anchor")}};
        }
    }
    updated.payload.proposed_sample_frame = *frame.frame;
    updated.payload.proposed_bpm = target_bpm;
    return {.value = std::move(updated), .diagnostics = {}};
}

auto beat_interval_drag_t::validate_candidate(signed_rational_t target_bpm) -> std::optional<diagnostic_t> {
    if (target_bpm.numerator <= 0 || target_bpm.denominator == 0)
        return error(diagnostic_code_t::invalid_rational, "BPM snap must produce a positive exact value");
    return std::nullopt;
}

auto beat_interval_drag_t::revise(const proposal_read_scope_t<chart_stamp_t>& scope,
    signed_rational_t target_bpm) const -> beat_interval_drag_outcome_t {
    if (auto invalid = validate_candidate(target_bpm))
        return {.diagnostics = {std::move(*invalid)}};
    auto validated = revise(scope, proposed_sample_frame);
    if (!validated.value || !validated.diagnostics.empty()) return validated;
    return prepare_snapped_revision(scope, target_bpm, std::move(*validated.value));
}

auto quantize_bpm(signed_rational_t bpm, signed_rational_t step)
    -> std::optional<signed_rational_t> {
    if (bpm.numerator <= 0 || bpm.denominator == 0 || step.numerator <= 0 ||
        step.denominator == 0) return std::nullopt;
    const auto numerator = wide_math::multiply(static_cast<std::uint64_t>(bpm.numerator),
                                               step.denominator);
    const auto denominator = wide_math::multiply(bpm.denominator,
                                                 static_cast<std::uint64_t>(step.numerator));
    const auto division = wide_math::divide(numerator, denominator);
    auto multiples = division.quotient;
    if (!wide_math::u128_less(division.remainder,
                         wide_math::subtract(denominator, division.remainder)))
        multiples = wide_math::add(multiples, 1);
    const auto step_numerator = static_cast<std::uint64_t>(step.numerator);
    constexpr auto int64_max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (!wide_math::can_fit_u64(multiples) || multiples.low > int64_max / step_numerator ||
        multiples.low == 0)
        return std::nullopt;
    const auto result_numerator = multiples.low * step_numerator;
    const auto value = static_cast<std::int64_t>(result_numerator);
    const auto divisor = std::gcd(static_cast<std::uint64_t>(value), step.denominator);
    return signed_rational_t{value / static_cast<std::int64_t>(divisor),
                             step.denominator / divisor};
}

auto beat_interval_drag_t::validate_apply(const chart_stamp_t&, bool timing_locked) const -> diagnostic_list_t {
    if (snap_active) {
        if (auto invalid = validate_candidate(proposed_bpm)) return {std::move(*invalid)};
    }
    if (auto locked = revision_policy.validate_lock(timing_locked, proposal_lock_order_t::before_stamp))
        return {std::move(*locked)};
    return {};
}

auto beat_interval_drag_t::apply(proposal_edit_scope_t<chart_stamp_t>& scope,
                                bool) const -> document_command_result_t {
    const auto& stamp = scope.captured_stamp();
    const auto chart = stamp.chart;
    auto updated = snap_active ? revise(scope, proposed_bpm) : revise(scope, proposed_sample_frame);
    if (!(updated.value && updated.diagnostics.empty()))
        return document_command_transaction_t::rejection(scope.transaction().outcome().revision,
            std::move(updated.diagnostics));
    if (updated.value->payload.proposed_bpm != proposed_bpm ||
        updated.value->payload.proposed_sample_frame != proposed_sample_frame)
        return scope.transaction().reject(diagnostic_code_t::invalid_id,
            "beat fit proposal changed before commit");
    // Inserting the BPM already in effect would only add a no-op event.
    if (insert_bpm && proposed_bpm == original_bpm) return scope.transaction().finish_without_commit();
    const bpm_edit_action_t action = insert_bpm
        ? bpm_edit_action_t{wormedit::insert_edit_t<bpm_edit_value_t>{
              .chart = chart,
              .value = bpm_edit_value_t::from(original_bpm_position, timing_bpm_value_t{proposed_bpm})}}
        : bpm_edit_action_t{wormedit::update_edit_t<bpm_edit_value_t>{
              .chart = chart, .id = bpm_event,
              .value = bpm_edit_value_t::from(original_bpm_position, timing_bpm_value_t{proposed_bpm})}};
    return scope.transaction().execute<bpm_operation_visitor_t>(action, bpm_edit_limits_t{});
}

} // namespace wormedit
