#include "timing_event_internal.h"
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



constexpr target_query_policy_t bpm_target_policy{
    .messages = {"BPM event command refers to an unknown event",
                 "BPM event ID is ambiguous because it appears more than once"},
    .scope = target_uniqueness_scope_t::chart_objects};

struct bpm_edit_effect_t {
    bool changed{};
    std::string command_label;
    auto operator==(const bpm_edit_effect_t&) const -> bool = default;
};
using bpm_edit_outcome_t = result_t<bpm_edit_effect_t, bpm_edit_effect_t>;

struct bpm_operation_visitor_t : chart_edit_context_t<bpm_edit_limits_t> {
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
        -> bpm_edit_outcome_t {
        if (chart.bpms.size() >= limits.maximum_bpm_events) {
            reject(result, diagnostic_code_t::invalid_resolution,
                   "BPM insertion would exceed the configured event cap");
            return std::unexpected(bpm_edit_effect_t{});
        }
        if (!validate_bpm_position(chart, edit.value.position, result) ||
            !validate_bpm_value(edit.value.settings.beats_per_minute, result)) {
            return std::unexpected(bpm_edit_effect_t{});
        }
        const auto allocation = next_chart_object_id(chart, limits.maximum_chart_objects);
        if (!allocation.id) {
            reject(result, diagnostic_code_t::invalid_resolution,
                   allocation.over_limit ? "selected chart exceeds the object ID allocation cap"
                                         : "chart object ID sequence is exhausted");
            return std::unexpected(bpm_edit_effect_t{});
        }
        const auto source_order = next_source_order(chart.bpms);
        if (!source_order) {
            reject(result, diagnostic_code_t::invalid_resolution,
                   "BPM source-order sequence is exhausted");
            return std::unexpected(bpm_edit_effect_t{});
        }
        chart.bpms.push_back({
            .id = *allocation.id,
            .position = edit.value.position,
            .beats_per_minute = edit.value.settings.beats_per_minute,
            .provenance = {.source_order = *source_order},
        });
        return bpm_edit_effect_t{.changed = true, .command_label = "Insert BPM event"};
    }

    auto operator()(chart_document_t& chart, const update_edit_t<bpm_edit_value_t>& edit) const
        -> bpm_edit_outcome_t {
        if (!edit.id) {
            reject(result, diagnostic_code_t::invalid_id,
                   "BPM update uses the reserved zero event ID");
            return std::unexpected(bpm_edit_effect_t{});
        }
        auto* event = chart.unique_target(&chart_document_t::bpms, edit.id, result.diagnostics,
                                          bpm_target_policy);
        if (event == nullptr || !validate_bpm_position(chart, edit.value.position, result) ||
            !validate_bpm_value(edit.value.settings.beats_per_minute, result)) {
            return std::unexpected(bpm_edit_effect_t{});
        }
        if (event->position == edit.value.position &&
            event->beats_per_minute == edit.value.settings.beats_per_minute) {
            return bpm_edit_effect_t{};
        }
        event->position = edit.value.position;
        event->beats_per_minute = edit.value.settings.beats_per_minute;
        return bpm_edit_effect_t{.changed = true, .command_label = "Update BPM event"};
    }

    auto operator()(chart_document_t& chart, const delete_edit_t<bpm_edit_value_t>& edit) const
        -> bpm_edit_outcome_t {
        if (!edit.id) {
            reject(result, diagnostic_code_t::invalid_id,
                   "BPM deletion uses the reserved zero event ID");
            return std::unexpected(bpm_edit_effect_t{});
        }
        auto* event = chart.unique_target(&chart_document_t::bpms, edit.id, result.diagnostics,
                                          bpm_target_policy);
        if (event == nullptr) {
            return std::unexpected(bpm_edit_effect_t{});
        }
        const auto id = event->id;
        std::erase_if(chart.bms_bpm_projections,
                      [id](const auto& projection) { return projection.event == id; });
        std::erase_if(chart.bpms, [id](const auto& item) { return item.id == id; });
        return bpm_edit_effect_t{.changed = true, .command_label = "Delete BPM event"};
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
    const bpm_edit_action_t& action, bpm_edit_limits_t limits) -> document_command_result_t {
    return document_command_transaction_t::from(history).execute<bpm_operation_visitor_t>(action, limits);
}

// Temporary: removed by T-637; native_chart_edit_action.h still dispatches into existing transactions.
auto apply_bpm_edit(document_command_transaction_t& transaction,
                    const bpm_edit_action_t& action, bpm_edit_limits_t limits)
    -> document_command_result_t {
    return transaction.execute<bpm_operation_visitor_t>(action, limits);
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
    const auto selected = document.find_chart(chart);
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
    const auto selected = document.find_chart(chart);
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
    const auto chart = document.find_chart(stamp.chart);
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
    auto chart = document.find_chart(stamp.chart);
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
        return document_command_transaction_t::rejection(scope.transaction().outcome.revision,
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
