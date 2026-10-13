#include "text_util.h"
#include <wormedit/core/rational.h>
#include "position_validation.h"
#include "chart_object_ids.h"
#include <wormedit/core/bpm_editor.h>
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

struct bpm_edit_outcome_t {
    bool valid{true};
    bool changed{};
    std::string label;
};

struct bpm_edit_context_t {
    chart_document_t& chart;
    bpm_edit_limits_t limits;
    document_command_result_t& result;
};

auto edit_context(song_document_t& document, chart_id_t id, bpm_edit_limits_t limits,
                  document_command_result_t& result) -> chart_document_t* {
    const auto found = std::ranges::find(document.charts, id, &chart_document_t::id);
    if (!id || found == document.charts.end()) {
        reject(result, diagnostic_code_t::invalid_id, "BPM edit refers to an unknown chart");
        return nullptr;
    }
    if (found->bpms.size() > limits.maximum_bpm_events) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "selected chart exceeds the BPM event cap");
        return nullptr;
    }
    return &*found;
}

auto apply_insert(bpm_edit_context_t context, const insert_bpm_event_action_t& edit)
    -> bpm_edit_outcome_t {
    if (context.chart.bpms.size() >= context.limits.maximum_bpm_events) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "BPM insertion would exceed the configured event cap");
        return {false, false, {}};
    }
    if (!validate_bpm_position(context.chart, edit.position, context.result) ||
        !validate_bpm_value(edit.beats_per_minute, context.result)) {
        return {false, false, {}};
    }
    const auto allocation =
        next_chart_object_id(context.chart, context.limits.maximum_chart_objects);
    if (!allocation.id) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               allocation.over_limit ? "selected chart exceeds the object ID allocation cap"
                                     : "chart object ID sequence is exhausted");
        return {false, false, {}};
    }
    const auto source_order = next_bpm_source_order(context.chart);
    if (!source_order) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "BPM source-order sequence is exhausted");
        return {false, false, {}};
    }
    context.chart.bpms.push_back({
        .id = *allocation.id,
        .position = edit.position,
        .beats_per_minute = edit.beats_per_minute,
        .provenance = {.source_order = *source_order},
    });
    return {true, true, "Insert BPM event"};
}

auto apply_update(bpm_edit_context_t context, const update_bpm_event_action_t& edit)
    -> bpm_edit_outcome_t {
    if (!edit.event) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "BPM update uses the reserved zero event ID");
        return {false, false, {}};
    }
    auto* event = unique_bpm_event(context.chart, edit.event, context.result);
    if (event == nullptr || !validate_bpm_position(context.chart, edit.position, context.result) ||
        !validate_bpm_value(edit.beats_per_minute, context.result)) {
        return {false, false, {}};
    }
    if (event->position == edit.position && event->beats_per_minute == edit.beats_per_minute) {
        return {true, false, {}};
    }
    event->position = edit.position;
    event->beats_per_minute = edit.beats_per_minute;
    return {true, true, "Update BPM event"};
}

auto apply_delete(bpm_edit_context_t context, const delete_bpm_event_action_t& edit)
    -> bpm_edit_outcome_t {
    if (!edit.event) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "BPM deletion uses the reserved zero event ID");
        return {false, false, {}};
    }
    auto* event = unique_bpm_event(context.chart, edit.event, context.result);
    if (event == nullptr) {
        return {false, false, {}};
    }
    const auto id = event->id;
    std::erase_if(context.chart.bms_bpm_projections,
                  [id](const auto& projection) { return projection.event == id; });
    std::erase_if(context.chart.bpms, [id](const auto& item) { return item.id == id; });
    return {true, true, "Delete BPM event"};
}

template <typename Edit>
auto apply_chart_edit(song_document_t& document, const Edit& edit, bpm_edit_limits_t limits,
                      document_command_result_t& result) -> bpm_edit_outcome_t {
    auto* chart = edit_context(document, edit.chart, limits, result);
    if (chart == nullptr) {
        return {false, false, {}};
    }
    auto context = bpm_edit_context_t{*chart, limits, result};
    if constexpr (std::is_same_v<Edit, insert_bpm_event_action_t>) {
        return apply_insert(context, edit);
    } else if constexpr (std::is_same_v<Edit, update_bpm_event_action_t>) {
        return apply_update(context, edit);
    } else {
        return apply_delete(context, edit);
    }
}

} // namespace

auto parse_bpm_text(std::string_view text) -> std::optional<signed_rational_t> {
    text = text_util::trim(text, " \t\r\n");
    if (text.empty()) return std::nullopt;
    if (text.find('/') == std::string_view::npos) {
        const auto parsed = parse_exact_decimal(text);
        return parsed && parsed->numerator > 0 ? parsed : std::nullopt;
    }
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

auto apply_bpm_edit(document_history_t& history, std::uint64_t expected_revision,
                    const bpm_edit_action_t& action, bpm_edit_limits_t limits)
    -> document_command_result_t {
    auto transaction = document_command_transaction_t::from(history);
    auto& result = transaction.outcome;
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id, "BPM editor document revision is stale");
        return result;
    }
    if (limits.maximum_bpm_events == 0) {
        reject(result, diagnostic_code_t::invalid_resolution, "BPM event cap must be positive");
        return result;
    }

    auto& proposed = transaction.copy_document();
    auto outcome = std::visit(
        [&](const auto& edit) { return apply_chart_edit(proposed, edit, limits, result); }, action);
    if (!outcome.valid || !result.diagnostics.empty() || !outcome.changed) {
        return result;
    }
    if (!extend_native_charts_to_audio_end(proposed)) {
        reject(result, diagnostic_code_t::invalid_measure,
               "BPM edit would exceed the audio chart measure limits");
        return result;
    }
    return transaction.commit(std::move(outcome.label));
}

namespace {

// Placeholder id for a BPM event that a fit inserts; never present in a document.
constexpr auto beat_fit_probe_event = object_id_t{std::numeric_limits<std::uint64_t>::max()};

auto beat_fit_precondition_error(bool timing_locked, std::uint32_t sample_rate)
    -> std::optional<diagnostic_t> {
    if (timing_locked) {
        return error(diagnostic_code_t::unsupported_feature,
                     "beat fitting is disabled while timing is locked");
    }
    if (sample_rate == 0) {
        return error(diagnostic_code_t::invalid_rational, "beat fitting requires a sample rate");
    }
    return std::nullopt;
}

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
    const auto measures = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(measures)) return nullptr;
    const auto& index = std::get<measure_index_t>(measures);
    const auto anchor_row = absolute_row(index, anchor);
    if (!anchor_row) return nullptr;
    for (const auto& event : chart.bpms) {
        if (absolute_row(index, event.position) == anchor_row) return &event;
    }
    return nullptr;
}

// Builds the drag for a fit whose anchor is `existing` (update) or, when null,
// a BPM event to insert at `anchor`.
auto make_beat_interval_drag(const song_document_t& document, const chart_document_t& chart,
                             const bpm_event_t* existing, local_position_t anchor,
                             local_position_t beat, std::uint32_t sample_rate)
    -> beat_interval_drag_result_t {
    beat_interval_drag_result_t result;
    const auto timing = build_timing_index(chart);
    const auto measures = build_measure_index(chart);
    if (!timing || !std::holds_alternative<measure_index_t>(measures)) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_resolution, "beat fit requires valid chart timing"));
        return result;
    }
    const auto& index = std::get<measure_index_t>(measures);
    const auto anchor_row = absolute_row(index, anchor);
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
    beat_interval_drag_t drag{.chart = chart.id,
                              .document_revision = document.revision,
                              .bpm_event = existing ? existing->id : beat_fit_probe_event,
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
    result.drag = drag;
    return result;
}

} // namespace

auto begin_beat_interval_drag(const song_document_t& document, chart_id_t chart,
                              object_id_t bpm_event, local_position_t beat,
                              std::uint32_t sample_rate, bool timing_locked)
    -> beat_interval_drag_result_t {
    beat_interval_drag_result_t result;
    if (const auto precondition = beat_fit_precondition_error(timing_locked, sample_rate)) {
        result.diagnostics.push_back(*precondition);
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
    return make_beat_interval_drag(document, *selected, &*bpm, bpm->position, beat, sample_rate);
}

auto begin_beat_interval_anchor_drag(const song_document_t& document, chart_id_t chart,
                                     local_position_t anchor, local_position_t beat,
                                     std::uint32_t sample_rate, bool timing_locked)
    -> beat_interval_drag_result_t {
    beat_interval_drag_result_t result;
    const auto selected = std::ranges::find(document.charts, chart, &chart_document_t::id);
    if (!chart || selected == document.charts.end()) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "beat fit refers to an unknown chart"));
        return result;
    }
    if (const auto* existing = bpm_event_at_anchor(*selected, anchor)) {
        return begin_beat_interval_drag(document, chart, existing->id, beat, sample_rate,
                                        timing_locked);
    }
    if (const auto precondition = beat_fit_precondition_error(timing_locked, sample_rate)) {
        result.diagnostics.push_back(*precondition);
        return result;
    }
    return make_beat_interval_drag(document, *selected, nullptr, anchor, beat, sample_rate);
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
    result.drag = updated;
    return result;
}

auto update_beat_interval_drag_to_bpm(const song_document_t& document,
                                      const beat_interval_drag_t& drag,
                                      signed_rational_t target_bpm, bool timing_locked)
    -> beat_interval_drag_result_t {
    if (target_bpm.numerator <= 0 || target_bpm.denominator == 0) {
        return {.diagnostics = {error(diagnostic_code_t::invalid_rational,
                                      "BPM snap must produce a positive exact value")}};
    }
    const auto validated = update_beat_interval_drag(document, drag, drag.proposed_sample_frame,
                                                     timing_locked);
    if (!validated) return validated;
    auto updated = *validated.drag;
    auto chart = std::ranges::find(document.charts, drag.chart, &chart_document_t::id);
    if (chart == document.charts.end()) return validated;
    const auto candidate = chart_with_anchor_bpm(*chart, drag, target_bpm);
    if (!candidate) return validated;
    const auto measures = build_measure_index(*candidate);
    if (!std::holds_alternative<measure_index_t>(measures))
        return {.diagnostics = std::get<diagnostic_list_t>(measures)};
    const auto beat_row = absolute_row(std::get<measure_index_t>(measures), drag.beat);
    const auto timing = build_timing_index(*candidate);
    if (!beat_row || !timing) {
        beat_interval_drag_result_t rejected;
        rejected.diagnostics.push_back(error(
            diagnostic_code_t::invalid_rational,
            timing.diagnostics.empty() ? "Snapped beat timing is invalid"
                                       : timing.diagnostics.front().message));
        return rejected;
    }
    const auto at = timing_at(*timing.index, *beat_row);
    const auto frame = at ? sample_frame_at(*at.seconds, drag.sample_rate,
        sample_rounding_t::nearest_ties_to_even) : sample_frame_result_t{};
    if (!frame) return {.diagnostics = {error(diagnostic_code_t::invalid_rational,
        "Snapped beat target is outside the source frame range")}};
    const auto anchor_row =
        absolute_row(std::get<measure_index_t>(measures), drag.original_bpm_position);
    for (const auto& anchor : chart->timing_anchors) {
        if (!anchor.locked) continue;
        const auto row = absolute_row(std::get<measure_index_t>(measures), anchor.position);
        if (!row || !anchor_row || *row < *anchor_row) continue;
        const auto anchor_timing = timing_at(*timing.index, *row);
        const auto anchor_frame = anchor_timing
            ? sample_frame_at(*anchor_timing.seconds, drag.sample_rate,
                              sample_rounding_t::nearest_ties_to_even)
            : sample_frame_result_t{};
        if (!anchor_frame || *anchor_frame.frame != anchor.source_sample_frame) {
            return {.diagnostics = {error(diagnostic_code_t::invalid_rational,
                "Snapped beat fit would move a locked timing anchor")}};
        }
    }
    updated.proposed_sample_frame = *frame.frame;
    updated.proposed_bpm = target_bpm;
    return {.drag = updated};
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
    if (!wide_math::less(division.remainder,
                         wide_math::subtract(denominator, division.remainder)))
        multiples = wide_math::add(multiples, 1);
    const auto step_numerator = static_cast<std::uint64_t>(step.numerator);
    constexpr auto int64_max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (!wide_math::fits_u64(multiples) || multiples.low > int64_max / step_numerator ||
        multiples.low == 0)
        return std::nullopt;
    const auto result_numerator = multiples.low * step_numerator;
    const auto value = static_cast<std::int64_t>(result_numerator);
    const auto divisor = std::gcd(static_cast<std::uint64_t>(value), step.denominator);
    return signed_rational_t{value / static_cast<std::int64_t>(divisor),
                             step.denominator / divisor};
}

namespace {

// Inserting the BPM that is already in effect would only add a no-op event.
auto commit_inserted_beat_fit(document_history_t& history, const beat_interval_drag_t& drag)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (drag.proposed_bpm == drag.original_bpm) return result;
    return apply_bpm_edit(history, drag.document_revision,
                          insert_bpm_event_action_t{drag.chart, drag.original_bpm_position,
                                                    drag.proposed_bpm});
}

} // namespace

auto commit_beat_interval_drag(document_history_t& history, const beat_interval_drag_t& drag,
                               bool timing_locked) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    const auto updated = drag.snap_active
        ? update_beat_interval_drag_to_bpm(history.document, drag, drag.proposed_bpm,
                                           timing_locked)
        : update_beat_interval_drag(history.document, drag, drag.proposed_sample_frame,
                                    timing_locked);
    if (!updated) {
        result.diagnostics = updated.diagnostics;
        return result;
    }
    if (updated.drag->proposed_bpm != drag.proposed_bpm ||
        updated.drag->proposed_sample_frame != drag.proposed_sample_frame) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                          "beat fit proposal changed before commit"));
        return result;
    }
    if (drag.insert_bpm) return commit_inserted_beat_fit(history, drag);
    return apply_bpm_edit(history, drag.document_revision,
                          update_bpm_event_action_t{drag.chart, drag.bpm_event,
                                                    drag.original_bpm_position, drag.proposed_bpm});
}

} // namespace wormedit
